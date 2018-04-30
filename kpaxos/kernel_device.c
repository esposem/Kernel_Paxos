#include "kernel_device.h"
#include "eth.h"
#include "kernel_client.h"
#include "paxos.h"
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/vmalloc.h>

#define BUFFER_SIZE 10000
static struct mutex char_mutex;
static struct mutex buffer_mutex;

// The device-driver class struct pointer
static struct class* charClass = NULL;
// The device-driver device struct pointer
static struct device* charDevice = NULL;

// names
static char *de_name, *clas_name;

static int majorNumber, working, current_buf = 0, first_buf = 0;

static struct user_msg** msg_buf;
static size_t            value_size = 0;
static atomic_t          must_stop, used_buf;

static void
paxerr(char* err)
{
  paxos_log_error("Device Char: failed to %s", err);
  working = 0;
}

int
kdev_open(struct inode* inodep, struct file* filep)
{
  if (!mutex_trylock(&char_mutex) || working == 0) {
    paxos_log_error("Device char: Device used by another process");
    return -EBUSY;
    working = 0;
  }
  return 0;
}

void
kstop_device()
{
  atomic_set(&must_stop, 1);
}

void
kset_message(char* msg, size_t size, unsigned int iid)
{
  if (atomic_read(&used_buf) >= BUFFER_SIZE) {
    paxos_log_error("Buffer is full! Lost a value");
    return;
  }

  mutex_lock(&buffer_mutex);
  atomic_inc(&used_buf); // TODO useful atomic?
  msg_buf[current_buf]->iid = iid;
  memcpy(msg_buf[current_buf]->value, msg, size);
  current_buf = (current_buf + 1) % BUFFER_SIZE;
  mutex_unlock(&buffer_mutex);
  // paxos_log_debug("Set message %d, occupied %d/100, first one is %d",
  //                 (current_buf - 1) % BUFFER_SIZE, atomic_read(&used_buf),
  //                 first_buf);
}

ssize_t
kdev_read(struct file* filep, char* buffer, size_t len, loff_t* offset)
{
  if (working == 0) {
    return -1;
  }
  int error_count = -1;
  if (atomic_read(&must_stop) == 1) {
    return -2;
  }

  if (atomic_read(&used_buf) <= 0) {
    return 0;
  }

  mutex_lock(&buffer_mutex);
  error_count = copy_to_user(buffer, (char*)msg_buf[first_buf],
                             sizeof(struct user_msg) + value_size);
  if (error_count != 0) {
    working = 0;
    paxerr("send x characters to the user");
  } else {
    atomic_dec(&used_buf);
    first_buf = (first_buf + 1) % BUFFER_SIZE;
    paxos_log_debug("Read message %d, occupied %d/100, new first one is %d, "
                    "last one is %d",
                    (first_buf - 1) % BUFFER_SIZE, atomic_read(&used_buf),
                    first_buf, current_buf);
  }
  mutex_unlock(&buffer_mutex);

  return error_count;
}

ssize_t
kdev_write(struct file* filep, const char* buffer, size_t len, loff_t* offset)
{
  if (working == 0) {
    return -1;
  }

  if (value_size == 0) {
    memcpy(&value_size, buffer, sizeof(size_t));
    paxos_log_debug("Device: client value size is %zu", value_size);
  }

  return len;
}

int
kdev_release(struct inode* inodep, struct file* filep)
{
  if (working == 0)
    paxos_log_debug("Device Char: Device already closed");
  mutex_unlock(&char_mutex);
  mutex_unlock(&buffer_mutex);
  paxos_log_debug("Device Char: Device successfully closed");
  return 0;
}

static void
allocate_name(char** dest, char* name, int id)
{
  size_t len = strlen(name) + 1;
  *dest = pmalloc(len + 1);
  memcpy(*dest, name, len);
  (*dest)[len - 1] = id + '0';
  (*dest)[len] = '\0';
}

static void
allocate_name_folder(char** dest, char* name, int id)
{
  char*  folder = "chardevice";
  size_t f_len = strlen(folder);
  size_t len = strlen(name);
  *dest = pmalloc(f_len + len + 3);
  memcpy(*dest, folder, f_len);
  (*dest)[f_len] = '/';
  (*dest)[f_len + 1] = '\0';
  strcat(*dest, name);
  (*dest)[f_len + len + 1] = id + '0';
  (*dest)[f_len + len + 2] = '\0';
}

static int
major_number(int id, char* name)
{
  allocate_name_folder(&de_name, name, id);
  majorNumber = register_chrdev(0, de_name, &fops);
  if (majorNumber < 0) {
    paxerr("register major number");
    return majorNumber;
  }
  paxos_log_debug("Device Char: Device Char %s registered correctly with major"
                  " number %d",
                  de_name, majorNumber);
  return 0;
}

static int
reg_dev_class(int id, char* name)
{
  allocate_name(&clas_name, name, id);
  charClass = class_create(THIS_MODULE, clas_name);
  if (IS_ERR(charClass)) {
    unregister_chrdev(majorNumber, de_name);
    paxerr("register device class");
    return -1;
  }
  paxos_log_debug("Device Char: device class %s registered correctly",
                  clas_name);
  return 0;
}

static int
reg_char_class(void)
{
  charDevice =
    device_create(charClass, NULL, MKDEV(majorNumber, 0), NULL, de_name);
  if (IS_ERR(charDevice)) {
    class_destroy(charClass);
    unregister_chrdev(majorNumber, de_name);
    paxerr("create the device");
    return -1;
  }
  return 0;
}

int
kdevchar_init(int id, char* name)
{
  if (name == NULL)
    return 0;
  paxos_log_debug("Client: Initializing the Device Char");
  working = 1;

  // Register major number
  if (major_number(id, name) < 0)
    return -1;

  // Register the device class
  if (reg_dev_class(id, name) < 0)
    return -1;

  // Register the device driver
  if (reg_char_class() < 0)
    return -1;

  msg_buf = vmalloc(sizeof(struct user_msg*) * BUFFER_SIZE);
  for (size_t i = 0; i < BUFFER_SIZE; i++) {
    msg_buf[i] = vmalloc(sizeof(struct user_msg) + ETH_DATA_LEN);
  }

  mutex_init(&char_mutex);
  mutex_init(&buffer_mutex);
  atomic_set(&used_buf, 0);
  atomic_set(&must_stop, 0);
  paxos_log_debug("Device Char: device class created correctly");
  return 0;
}

void
kdevchar_exit(void)
{
  if (working == 0) {
    return;
  }
  working = 0;
  mutex_destroy(&char_mutex);
  mutex_destroy(&buffer_mutex);
  device_destroy(charClass, MKDEV(majorNumber, 0)); // remove the device
  class_unregister(charClass); // unregister the device class
  class_destroy(charClass);    // remove the device class

  for (int i = 0; i < BUFFER_SIZE; i++) {
    vfree(msg_buf[i]);
  }
  vfree(msg_buf);

  pfree(de_name);
  pfree(clas_name);
  unregister_chrdev(majorNumber, de_name); // unregister the major number
  paxos_log_debug(KERN_INFO "Device Char: Unloaded\n");
}

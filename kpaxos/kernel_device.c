#include "kernel_device.h"
#include "eth.h"
#include "kernel_client.h"
#include "paxos.h"
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#define BUFFER_SIZE 100000
static struct mutex char_mutex;
// static struct mutex buffer_mutex;

// The device-driver class struct pointer
static struct class* charClass = NULL;
// The device-driver device struct pointer
static struct device*    charDevice = NULL;
static wait_queue_head_t access_wait;

// names
static char *de_name, *clas_name;

static int majorNumber, working, current_buf = 0, first_buf = 0;

static struct user_msg** msg_buf;
static atomic_t          used_buf;

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
kset_message(char* msg, size_t size)
{
  if (atomic_read(&used_buf) == BUFFER_SIZE) {
    if (printk_ratelimit())
      paxos_log_error("Buffer is full! Lost a value");
    atomic_dec(&used_buf);
  }
  msg_buf[current_buf]->size = size;
  memcpy(msg_buf[current_buf]->value, msg, size);
  current_buf = (current_buf + 1) % BUFFER_SIZE;
  atomic_inc(&used_buf);
  wake_up_interruptible(&access_wait);
}

// returns 0 if it has to stop, >0 when it reads something, and <0 on error
ssize_t
kdev_read(struct file* filep, char* buffer, size_t len, loff_t* offset)
{
  int error_count;

  if (signal_pending(current) || !working) { // user called sigint
    return 0;
  }

  atomic_dec(&used_buf);
  size_t llen = sizeof(struct user_msg) + msg_buf[first_buf]->size;
  error_count = copy_to_user(buffer, (char*)msg_buf[first_buf], llen);

  if (error_count != 0) {
    atomic_inc(&used_buf);
    paxerr("send fewer characters to the user");
    return error_count;
  } else
    first_buf = (first_buf + 1) % BUFFER_SIZE;

  return llen;
}

ssize_t
kdev_write(struct file* filep, const char* buffer, size_t len, loff_t* offset)
{
  if (working == 0)
    return -1;

  return len;
}

unsigned int
kdev_poll(struct file* file, poll_table* wait)
{
  poll_wait(file, &access_wait, wait);
  if (atomic_read(&used_buf) > 0)
    return POLLIN | POLLRDNORM;
  return 0;
}

int
kdev_release(struct inode* inodep, struct file* filep)
{
  if (working == 0)
    paxos_log_debug("Device Char: Device already closed");
  mutex_unlock(&char_mutex);
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
  char*  folder = "paxos";
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
  init_waitqueue_head(&access_wait);
  atomic_set(&used_buf, 0);
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
  atomic_inc(&used_buf);
  wake_up_interruptible(&access_wait);
  mutex_destroy(&char_mutex);
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

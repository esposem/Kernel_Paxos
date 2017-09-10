#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include <asm/atomic.h>
#include "kernel_device.h"
#include "kernel_client.h"
#include "paxos.h"

#define BUFFER_SIZE 100
static DEFINE_MUTEX(char_mutex);
static DEFINE_MUTEX(buffer_mutex);
static DEFINE_MUTEX(read_mutex);
static struct class * charClass  = NULL;   //< The device-driver class struct pointer
static struct device * charDevice = NULL;  //< The device-driver device struct pointer
static char * de_name, * clas_name;
static int majorNumber, working, current_buf = 0, first_buf = 0;
static struct user_msg * msg_buf[BUFFER_SIZE];
static struct timer_list release_lock;
static struct timeval interval;
size_t value_size = 0;
static atomic_t must_stop, used_buf;

int kdev_open(struct inode *inodep, struct file *filep){
  if(!mutex_trylock(&char_mutex) || working == 0){
    paxos_log_error("Device char: Device used by another process");
    return -EBUSY;
    working = 0;
  }
	return 0;
}

void kstop_device(){
  atomic_set(&must_stop,1);
}

static void rel_lock(unsigned long arg){
  mutex_unlock(&read_mutex);
}

void kset_message(char * msg, size_t size, unsigned int iid){
  if(atomic_read(&used_buf) >= BUFFER_SIZE){
    paxos_log_error("Buffer is full! Lost a value");
    return;
  }

  if(value_size == 0){
    return;
  }
  mutex_lock(&buffer_mutex);
  atomic_inc(&used_buf);
  memcpy(&(msg_buf[current_buf]->iid), &iid, sizeof(unsigned int));
  memcpy(&(msg_buf[current_buf]->value), msg, size);

  current_buf = (current_buf + 1) % BUFFER_SIZE;
  mutex_unlock(&buffer_mutex);
  mutex_unlock(&read_mutex);
  paxos_log_debug("Set message %d, occupied %d/100, first one is %d", (current_buf -1) % BUFFER_SIZE, atomic_read(&used_buf), first_buf);
}

ssize_t kdev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
  if(working == 0){
    return -1;
  }
  int error_count = -1;
  // WARNING! assume that the buffer has size >= size_of_message
  if(atomic_read(&must_stop) == 1){
    return -2;
  }
  // mod_timer(&release_lock, jiffies + timeval_to_jiffies(&interval));
  // mutex_lock(&read_mutex);
  // del_timer(&release_lock);
  setup_timer( &release_lock,  rel_lock, 0);
  if(atomic_read(&used_buf) > 0){
    mutex_lock(&buffer_mutex);
    error_count = copy_to_user(buffer, (char *) msg_buf[first_buf], sizeof(struct user_msg) + value_size);
    if (error_count != 0){
      working = 0;
  		paxos_log_error("Device Char: Failed to send %d characters to the user\n", error_count);
  	}else{
      first_buf = (first_buf + 1) % BUFFER_SIZE;
      atomic_dec(&used_buf);
      paxos_log_debug("Read message %d, occupied %d/100, new first one is %d, last one is %d", (first_buf - 1) % BUFFER_SIZE, atomic_read(&used_buf), first_buf, current_buf);
    }
    mutex_unlock(&buffer_mutex);
  }

  if(atomic_read(&used_buf) != 0)
    mutex_unlock(&read_mutex);

  return error_count;
}

ssize_t kdev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
  if(working == 0){
    return -1;
  }
  paxos_log_info("Device: client value size is %zu", *((size_t *) buffer));
  if(value_size == 0){
    memcpy(&value_size,buffer, sizeof(size_t));
    for(int i = 0; i < BUFFER_SIZE; i++){
      msg_buf[i] = (struct user_msg *) kmalloc(sizeof(struct user_msg) + value_size, GFP_ATOMIC | __GFP_REPEAT);
    }
  }

  return len;
}

int kdev_release(struct inode *inodep, struct file *filep){
  if(working == 0)
    paxos_log_debug(KERN_INFO "Device Char: Device already closed\n");
  mutex_unlock(&char_mutex);
  mutex_unlock(&read_mutex);
  mutex_unlock(&buffer_mutex);
  paxos_log_debug(KERN_INFO "Device Char: Device successfully closed\n");
  return 0;
}

static void allocate_name(char ** dest, char * name, int id){
  size_t len = strlen(name) + 1;
  *dest = kmalloc(len + 1, GFP_ATOMIC | __GFP_REPEAT);
  memcpy(*dest, name, len);
  (*dest)[len-1] = id + '0';
  (*dest)[len] = '\0';
}

static void allocate_name_folder(char ** dest, char * name, int id){
  char * folder = "chardevice";
  size_t f_len = strlen(folder);
  size_t len = strlen(name);
  *dest = kmalloc(f_len + len + 3, GFP_ATOMIC | __GFP_REPEAT);
  memcpy(*dest, folder,f_len);
  (*dest)[f_len] = '/';
  (*dest)[f_len + 1] = '\0';
  strcat(*dest, name);
  (*dest)[f_len + len + 1] = id + '0';
  (*dest)[f_len + len + 2] = '\0';
}

int kdevchar_init(int id, char * name){

  paxos_log_debug(KERN_INFO "Client: Initializing the Device Char");
  working = 1;
  allocate_name_folder(&de_name, name, id);
  majorNumber = register_chrdev(0, de_name, &fops);
  if (majorNumber<0){
    paxos_log_debug(KERN_ALERT "Device Char: failed to register a major number\n");
    working = 0;
    return majorNumber;
  }
  paxos_log_debug(KERN_INFO "Device Char: Device Char %s registered correctly with major number %d\n",de_name, majorNumber);

  // Register the device class
  allocate_name(&clas_name, name, id);
  charClass = class_create(THIS_MODULE, clas_name);
  if (IS_ERR(charClass)){
    unregister_chrdev(majorNumber, de_name);
    paxos_log_debug(KERN_ALERT "Device Char: failed to register device class\n");
    working = 0;
    return PTR_ERR(charClass);
  }
   paxos_log_debug(KERN_INFO "Device Char: device class %s registered correctly", clas_name);

  // Register the device driver
  charDevice = device_create(charClass, NULL, MKDEV(majorNumber, 0), NULL, de_name);
  if (IS_ERR(charDevice)){
    class_destroy(charClass);
    unregister_chrdev(majorNumber, de_name);
    paxos_log_debug(KERN_ALERT "Device Char: failed to create the device\n");
    working = 0;
    return PTR_ERR(charDevice);
  }

  mutex_init(&char_mutex);
  mutex_init(&buffer_mutex);
  mutex_init(&read_mutex);
  mutex_lock(&read_mutex);
  atomic_set(&used_buf, 0);
  atomic_set(&must_stop, 0);
  setup_timer( &release_lock,  rel_lock, 0);
  interval = (struct timeval){0, 1};

   paxos_log_debug(KERN_INFO "Device Char: device class created correctly\n");
  return 0;
}

void kdevchar_exit(void){
  if(working == 0){
    return;
  }
  working = 0;
  del_timer(&release_lock);
  mutex_destroy(&char_mutex);
  mutex_destroy(&buffer_mutex);
  mutex_destroy(&read_mutex);
  device_destroy(charClass, MKDEV(majorNumber, 0));     // remove the device
  class_unregister(charClass);                          // unregister the device class
  class_destroy(charClass);                             // remove the device class
  for(int i = 0; i < BUFFER_SIZE; i++){
    kfree(msg_buf[i]);
  }

  kfree(de_name);
  kfree(clas_name);
  unregister_chrdev(majorNumber, de_name);             // unregister the major number
 	paxos_log_debug(KERN_INFO "Device Char: Unloaded\n");
}

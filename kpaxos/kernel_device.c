#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include <asm/atomic.h>
#include "kernel_device.h"
#include "paxos.h"

#define BUFFER_SIZE 100
static DEFINE_MUTEX(char_mutex);
static int majorNumber;                    //< Stores the device number -- determined automatically
static struct class * charClass  = NULL;   //< The device-driver class struct pointer
static struct device * charDevice = NULL;  //< The device-driver device struct pointer
static char * de_name;
static char * clas_name;
static int working;
int * clid;
static int must_stop = 0;

struct user_msg{
  struct timeval timenow;
  char msg[16]; //copy just the first 16 char of 64
  int iid;
};

struct user_msg msg_buf[BUFFER_SIZE];
int current_buf = 0;
int first_buf = 0;
atomic_t used_buf;

int kdev_open(struct inode *inodep, struct file *filep){
  if(!mutex_trylock(&char_mutex) || working == 0){
    printk(KERN_ALERT "Device char: Device used by another process");
    return -EBUSY;
    working = 0;
  }
	return 0;
}

void kstop_device(){
  must_stop = 1;
}

void kset_message(struct timeval timenow, char * msg, unsigned int iid){
  if(atomic_read(&used_buf) > BUFFER_SIZE){
    printk(KERN_ERR "Buffer is full! Lost a value");
    return;
  }
  atomic_inc(&used_buf);
  // memset((msg_buf[current_buf], 0, sizeof(struct user_msg)); //check
  memcpy(&msg_buf[current_buf].iid, &iid, sizeof(int));
  memcpy(&msg_buf[current_buf].timenow, &timenow, sizeof(struct timeval));
  memcpy(&msg_buf[current_buf].msg, msg, 15);
  msg_buf[current_buf].msg[15] = '\0';
  current_buf = (current_buf + 1) % BUFFER_SIZE;
  printk(KERN_INFO "Set message %d, occupied %d/100, first one is %d", current_buf, atomic_read(&used_buf), first_buf);
}

ssize_t kdev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
  if(working == 0){
    return -1;
  }
  int error_count = -1;
  // WARNING! assume that the buffer has size > size_of_message
  if(must_stop){
    struct user_msg mess;
    mess.iid = -1;
    mess.timenow.tv_sec = 0;
    mess.timenow.tv_usec = 0;
    mess.msg[0] = '\0';
    error_count = copy_to_user(buffer, (char *) &mess, sizeof(struct user_msg));
  }

  if(atomic_read(&used_buf) > 0){
    error_count = copy_to_user(buffer, (char *) &msg_buf[first_buf], sizeof(struct user_msg));
    if (error_count != 0){
      working = 0;
  		printk(KERN_INFO "Device Char: Failed to send %d characters to the user\n", error_count);
  		return -EFAULT;
  	}else{
      first_buf = (first_buf + 1) % BUFFER_SIZE;
      atomic_dec(&used_buf);
      printk(KERN_INFO "Read message %d, occupied %d/100, first one is %d, last one is %d", first_buf -1, atomic_read(&used_buf), first_buf, current_buf);
    }
  }


  return error_count;
}

ssize_t kdev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
  if(working == 0){
    return -1;
  }
  // printk(KERN_INFO "Device: Received id %d", *((int *) buffer));
  memcpy(clid,buffer, sizeof(int));
  return len;
}

int kdev_release(struct inode *inodep, struct file *filep){
  // if(working == 0)
    // printk(KERN_INFO "Device Char: Device already closed\n");
  mutex_unlock(&char_mutex);
   // printk(KERN_INFO "Device Char: Device successfully closed\n");
   return 0;
}

static void allocate_name(char ** dest, char * name, int id){
  size_t len = strlen(name) + 1;
  *dest = kmalloc(len + 1, GFP_KERNEL);
  memcpy(*dest, name, len);
  (*dest)[len-1] = id + '0';
  (*dest)[len] = '\0';
}

static void allocate_name_folder(char ** dest, char * name, int id){
  char * folder = "chardevice";
  size_t f_len = strlen(folder);
  size_t len = strlen(name);
  *dest = kmalloc(f_len + len + 3, GFP_KERNEL);
  memcpy(*dest, folder,f_len);
  (*dest)[f_len] = '/';
  (*dest)[f_len + 1] = '\0';
  strcat(*dest, name);
  (*dest)[f_len + len + 1] = id + '0';
  (*dest)[f_len + len + 2] = '\0';
}

int kdevchar_init(int id, char * name){
   // printk(KERN_INFO "Client: Initializing the Device Char");
   working = 1;
   allocate_name_folder(&de_name, name, id);
   majorNumber = register_chrdev(0, de_name, &fops);
   if (majorNumber<0){
      printk(KERN_ALERT "Device Char: failed to register a major number\n");
      working = 0;
      return majorNumber;
   }
  //  printk(KERN_INFO "Device Char: Device Char %s registered correctly with major number %d\n",de_name, majorNumber);

   // Register the device class
   allocate_name(&clas_name, name, id);
   charClass = class_create(THIS_MODULE, clas_name);
   if (IS_ERR(charClass)){
      unregister_chrdev(majorNumber, de_name);
      printk(KERN_ALERT "Device Char: failed to register device class\n");
      working = 0;
      return PTR_ERR(charClass);
   }
  //  printk(KERN_INFO "Device Char: device class %s registered correctly", clas_name);

   // Register the device driver
   charDevice = device_create(charClass, NULL, MKDEV(majorNumber, 0), NULL, de_name);
   if (IS_ERR(charDevice)){
      class_destroy(charClass);
      unregister_chrdev(majorNumber, de_name);
      printk(KERN_ALERT "Device Char: failed to create the device\n");
      working = 0;
      return PTR_ERR(charDevice);
   }

   mutex_init(&char_mutex);
   clid = kmalloc(sizeof(int), GFP_KERNEL);
   atomic_set(&used_buf, 0);
  //  printk(KERN_INFO "Device Char: device class created correctly\n");
   return 0;
}

void kdevchar_exit(void){
  printk(KERN_INFO "Called kdevchar_exit");
  if(working == 0){
    return;
  }
  working = 0;
  mutex_destroy(&char_mutex);
  device_destroy(charClass, MKDEV(majorNumber, 0));     // remove the device
  class_unregister(charClass);                          // unregister the device class
  class_destroy(charClass);                             // remove the device class
  kfree(de_name);
  kfree(clas_name);
  kfree(clid);
  unregister_chrdev(majorNumber, de_name);             // unregister the major number
 	// printk(KERN_INFO "Device Char: Unloaded\n");
}

#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "kernel_device.h"
#include "paxos.h"

static DEFINE_MUTEX(char_mutex);
static int    majorNumber;                  ///< Stores the device number -- determined automatically
static unsigned char *  message = NULL;              ///< Used to remember the size of the string stored
static unsigned long  size_of_message;              ///< Used to remember the size of the string stored
static struct class*  charClass  = NULL; ///< The device-driver class struct pointer
static struct device* charDevice = NULL; ///< The device-driver device struct pointer
static char * de_name;
static char * clas_name;
static int working;
int * clid;

int kdev_open(struct inode *inodep, struct file *filep){
  if(!mutex_trylock(&char_mutex) || working == 0){
    // printk(KERN_ALERT "Device char: Device used by another process");
    return -EBUSY;
    working = 0;
  }
	return 0;
}

void kset_message(struct timeval timenow, char * msg, unsigned int iid){
  if(message)
    kfree(message);
  size_t len = strlen(msg) + 1;
  size_of_message = sizeof(struct timeval) + len + sizeof(unsigned int);
  message = kmalloc(size_of_message, GFP_KERNEL);
  // message will be made of | timeval iid msg |
  memcpy(message, &timenow, sizeof(struct timeval));
  memcpy(message, &iid, sizeof(unsigned int));
  memcpy(message, msg, len);
}

ssize_t kdev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
  if(working == 0 || message == NULL){
    return -1;
  }
  int error_count = 0;
  // WARNING! assume that the buffer has size > size_of_message
  memset(buffer, '\0', size_of_message +1);
  error_count = copy_to_user(buffer, message, size_of_message);
  kfree(message);
  message = NULL;

	if (error_count==0){
		// printk(KERN_INFO "Device Char: Sent %lu characters to the user\n", size_of_message);
		return (size_of_message=0);
	}
	else {
    working = 0;
		// printk(KERN_INFO "Device Char: Failed to send %d characters to the user\n", error_count);
		return -EFAULT;
	}
}

ssize_t kdev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
  if(working == 0){
    return -1;
  }
  printk(KERN_INFO "Device: Received id %d", *((int *) buffer));
  // sprintf(message, "%s(%zu letters)", buffer, len);   // appending received string with its length
  // size_of_message = strlen(message);                 // store the length of the stored message
  // printk(KERN_INFO "Device Char: Received %zu characters from the user\n", len);
  return len;
}

int kdev_release(struct inode *inodep, struct file *filep){
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
      // printk(KERN_ALERT "Device Char: failed to register a major number\n");
      working = 0;
      return majorNumber;
   }
   // printk(KERN_INFO "Device Char: Device Char %s registered correctly with major number %d\n",de_name, majorNumber);

   // Register the device class
   allocate_name(&clas_name, name, id);
   charClass = class_create(THIS_MODULE, clas_name);
   if (IS_ERR(charClass)){
      unregister_chrdev(majorNumber, de_name);
      // printk(KERN_ALERT "Device Char: failed to register device class\n");
      working = 0;
      return PTR_ERR(charClass);
   }
   // printk(KERN_INFO "Device Char: device class %s registered correctly", clas_name);

   // Register the device driver
   charDevice = device_create(charClass, NULL, MKDEV(majorNumber, 0), NULL, de_name);
   if (IS_ERR(charDevice)){
      class_destroy(charClass);
      unregister_chrdev(majorNumber, de_name);
      // printk(KERN_ALERT "Device Char: failed to create the device\n");
      working = 0;
      return PTR_ERR(charDevice);
   }

   mutex_init(&char_mutex);
   clid = kmalloc(sizeof(int), GFP_KERNEL);
   // printk(KERN_INFO "Device Char: device class created correctly\n");
   return 0;
}

void kdevchar_exit(void){
  // printk(KERN_INFO "Called kdevchar_exit");
  if(working == 0){
    return;
  }
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

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
static struct user_msg msg_buf[BUFFER_SIZE];
static struct timer_list release_lock;
static struct timeval interval;
int * clid;
static atomic_t must_stop, used_buf;

int kdev_open(struct inode *inodep, struct file *filep){
  if(!mutex_trylock(&char_mutex) || working == 0){
    // printk(KERN_ALERT "Device char: Device used by another process");
    return -EBUSY;
    working = 0;
  }
	return 0;
}

void kstop_device(){
  atomic_set(&must_stop,1);
}

static void rel_lock(unsigned long arg){
  // printk(KERN_INFO "Too much time passed, released lock");
  mutex_unlock(&read_mutex);
}

void kset_message(struct timeval * timenow, char * msg, int client_id, unsigned int iid){
  if(atomic_read(&used_buf) > BUFFER_SIZE){
    printk(KERN_ERR "Buffer is full! Lost a value");
    return;
  }
  mutex_lock(&buffer_mutex);
  atomic_inc(&used_buf);
  // memset((msg_buf[current_buf], 0, sizeof(struct user_msg)); //check
  memcpy(&msg_buf[current_buf].iid, &iid, sizeof(int));
  memcpy(&msg_buf[current_buf].client_id, &client_id, sizeof(int));
  memcpy(&msg_buf[current_buf].timenow, timenow, sizeof(struct timeval));
  memcpy(&msg_buf[current_buf].msg, msg, 63);
  msg_buf[current_buf].msg[63] = '\0';
  current_buf = (current_buf + 1) % BUFFER_SIZE;
  mutex_unlock(&buffer_mutex);
  mutex_unlock(&read_mutex);
  // printk(KERN_INFO "Set message %d, occupied %d/100, first one is %d", current_buf, atomic_read(&used_buf), first_buf);
}

ssize_t kdev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
  if(working == 0){
    return -1;
  }
  int error_count = -1;
  // WARNING! assume that the buffer has size > size_of_message
  if(atomic_read(&must_stop) == 1){
    return -2;
  }
  // printk(KERN_INFO"Gaining lock...");
  mod_timer(&release_lock, jiffies + timeval_to_jiffies(&interval));
  mutex_lock(&read_mutex);
  // printk(KERN_INFO"Lock gained");
  del_timer(&release_lock);
  setup_timer( &release_lock,  rel_lock, 0);
  if(atomic_read(&used_buf) > 0){
    mutex_lock(&buffer_mutex);
    error_count = copy_to_user(buffer, (char *) &msg_buf[first_buf], sizeof(struct user_msg));
    if (error_count != 0){
      mutex_unlock(&buffer_mutex);
      working = 0;
  		printk(KERN_INFO "Device Char: Failed to send %d characters to the user\n", error_count);
  		return -EFAULT;
  	}else{
      first_buf = (first_buf + 1) % BUFFER_SIZE;
      atomic_dec(&used_buf);
      // printk(KERN_INFO "Read message %d, occupied %d/100, first one is %d, last one is %d", first_buf -1, atomic_read(&used_buf), first_buf, current_buf);
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
  // printk(KERN_INFO "Device: Received id %d", *((int *) buffer));
  memcpy(clid,buffer, sizeof(int));
  return len;
}

int kdev_release(struct inode *inodep, struct file *filep){
  // if(working == 0)
    // printk(KERN_INFO "Device Char: Device already closed\n");
  mutex_unlock(&char_mutex);
  mutex_unlock(&read_mutex);
  mutex_unlock(&buffer_mutex);
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
   mutex_init(&buffer_mutex);
   mutex_init(&read_mutex);
   clid = kmalloc(sizeof(int), GFP_KERNEL);
   atomic_set(&used_buf, 0);
   atomic_set(&must_stop, 0);
   setup_timer( &release_lock,  rel_lock, 0);
   interval = (struct timeval){0, 100};

  //  printk(KERN_INFO "Device Char: device class created correctly\n");
   return 0;
}

void kdevchar_exit(void){
  // printk(KERN_INFO "Called kdevchar_exit");
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
  kfree(de_name);
  kfree(clas_name);
  kfree(clid);
  unregister_chrdev(majorNumber, de_name);             // unregister the major number
 	// printk(KERN_INFO "Device Char: Unloaded\n");
}

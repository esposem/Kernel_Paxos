#ifndef DEVICE_INCLUDE
#define DEVICE_INCLUDE

#include <linux/fs.h>
#include <linux/time.h>

extern struct file_operations fops;
extern int * clid;
extern int     kdev_open(struct inode *, struct file *);
extern int     kdev_release(struct inode *, struct file *);
extern ssize_t kdev_read(struct file *, char *, size_t, loff_t *);
extern ssize_t kdev_write(struct file *, const char *, size_t, loff_t *);
extern int kdevchar_init(int id, char * name);
extern void kdevchar_exit(void);
extern void kset_message(struct timeval timenow, char * msg, unsigned int iid);

#endif

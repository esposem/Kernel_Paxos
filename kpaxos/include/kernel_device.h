#ifndef DEVICE_INCLUDE
#define DEVICE_INCLUDE

#include <asm/atomic.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/time.h>

extern int          kdev_open(struct inode*, struct file*);
extern int          kdev_release(struct inode*, struct file*);
extern ssize_t      kdev_read(struct file*, char*, size_t, loff_t*);
extern ssize_t      kdev_write(struct file*, const char*, size_t, loff_t*);
extern int          kdevchar_init(int id, char* name);
extern unsigned int kdev_poll(struct file*, poll_table* wait);
extern void         kdevchar_exit(void);
extern void         kset_message(char* msg, size_t size);
extern struct file_operations fops;

#endif

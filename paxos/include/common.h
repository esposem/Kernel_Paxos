#ifndef COMMON_PAX
#define COMMON_PAX

#include <linux/slab.h> //kmalloc

#define pmalloc(size) kmalloc(size, GFP_ATOMIC)
#define prealloc(ptr, size) krealloc(ptr, size, GFP_ATOMIC)
#define pfree(ptr) kfree(ptr)

extern const char* MOD_NAME;

typedef uint8_t eth_address;

#if 0
#define LOG_DEBUG(fmt, args...)                                                \
  printk(KERN_DEBUG "%s @ %s(): " fmt "\n", MOD_NAME, __func__, ##args)
#define LOG_INFO(fmt, args...)                                                 \
  printk(KERN_INFO "%s @ %s(): " fmt "\n", MOD_NAME, __func__, ##args)
#define LOG_ERROR(fmt, args...)                                                \
  printk(KERN_ERR "%s @ %s(): " fmt "\n", MOD_NAME, __func__, ##args)
#endif

#define LOG_DEBUG(fmt, args...)                                                \
  printk(KERN_DEBUG "%s: " fmt "\n", MOD_NAME, ##args)
#define LOG_INFO(fmt, args...)                                                 \
  printk(KERN_INFO "%s: " fmt "\n", MOD_NAME, ##args)
#define LOG_ERROR(fmt, args...)                                                \
  printk(KERN_ERR "%s: " fmt "\n", MOD_NAME, ##args)

#endif

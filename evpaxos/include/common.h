#ifndef COMMON_PAX
#define COMMON_PAX

#include <linux/slab.h> //kmalloc

#define pmalloc(size) kmalloc(size, GFP_ATOMIC)
#define prealloc(ptr, size) krealloc(ptr, size, GFP_ATOMIC)

typedef uint8_t eth_address;
#define eth_size ETH_ALEN * sizeof(uint8_t)

#endif

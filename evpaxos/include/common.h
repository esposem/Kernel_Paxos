#ifndef COMMON_PAX
#define COMMON_PAX

#include <linux/slab.h> //kmalloc

#define pmalloc(size) kmalloc(size, GFP_ATOMIC)
#define prealloc(ptr, size) kmalloc(size, GFP_ATOMIC)

typedef uint8_t eth_address;
#define eth_size ETH_ALEN * sizeof(uint8_t)

// all kmalloc in pmalloc
// check timeout
// hartbeat for learners
// check missing free
// check input
#endif

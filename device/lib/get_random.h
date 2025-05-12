#include <stdlib.h>

static inline size_t getrandom(void* buf, size_t buflen, unsigned int flags) {
    (void) flags;
    arc4random_buf(buf, buflen);
    return (size_t)buflen;
}
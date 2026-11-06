#include "defines.h"

#ifdef SE_RAND_GETRANDOM
#include "get_random.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef __APPLE__
#include <sys/random.h>
#endif

ssize_t se_getrandom(void *buf, size_t buflen, unsigned int flags)
{
    if (flags != 0 || buflen > (size_t)PTRDIFF_MAX) abort();
#if defined(__APPLE__)
    arc4random_buf(buf, buflen);
#else
    size_t filled = 0;
    while (filled < buflen)
    {
        ssize_t ret = getrandom((unsigned char *)buf + filled, buflen - filled, 0);
        if (ret < 0 && errno == EINTR) continue;
        if (ret <= 0) abort();
        filled += (size_t)ret;
    }
#endif
    return (ssize_t)buflen;
}

#endif

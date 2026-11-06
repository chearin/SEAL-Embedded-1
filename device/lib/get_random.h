#pragma once

#include <stddef.h>
#include <sys/types.h>

/* Fill the entire buffer with system CSPRNG bytes. Only flags == 0 is supported.
 * Success returns buflen; unrecoverable failure aborts, including Release builds.
 * Linux short reads and EINTR are handled internally. */
ssize_t se_getrandom(void *buf, size_t buflen, unsigned int flags);

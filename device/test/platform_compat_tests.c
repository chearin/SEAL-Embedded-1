/* Standalone Linux platform tests: intentionally do not invoke NTT/CKKS.
 * Link with -Wl,--wrap=getrandom; see device/LINUX_PORT.md. */
#include "get_random.h"
#include "m1cycles.h"
#include "timer.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int mode, calls;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expr); exit(1); \
} } while (0)

ssize_t __real_getrandom(void *, size_t, unsigned int);
ssize_t __wrap_getrandom(void *buf, size_t len, unsigned int flags)
{
    ++calls;
    if (mode == 0) return __real_getrandom(buf, len, flags);
    if (mode == 1)
    {
        if (calls == 1) { errno = EINTR; return -1; }
        size_t count = len > 3 ? 3 : len;
        memset(buf, 0xa5, count);
        return (ssize_t)count;
    }
    if (mode == 2) { errno = EIO; return -1; }
    return 0;
}

static void expect_abort(int failure_mode, unsigned int flags)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        struct rlimit limit = {0, 0};
        if (setrlimit(RLIMIT_CORE, &limit) != 0) _exit(2);
        mode = failure_mode;
        unsigned char buf[8];
        (void)se_getrandom(buf, sizeof(buf), flags);
        _exit(3);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

int main(void)
{
    unsigned char buf[257];
    CHECK(se_getrandom(buf, sizeof(buf), 0) == (ssize_t)sizeof(buf));
    CHECK(calls > 0); /* Real Linux system API was reached. */
    mode = 1;
    calls = 0;
    memset(buf, 0, sizeof(buf));
    CHECK(se_getrandom(buf, sizeof(buf), 0) == (ssize_t)sizeof(buf));
    CHECK(calls > 2);
    for (size_t i = 0; i < sizeof(buf); ++i) CHECK(buf[i] == 0xa5);
    CHECK(se_getrandom(NULL, 0, 0) == 0);
    expect_abort(2, 0); /* permanent error */
    expect_abort(3, 0); /* no progress */
    expect_abort(0, 1); /* unsupported flags */
#ifdef SE_ENABLE_TIMERS
    Timer timer;
    reset_start_timer(&timer);
    stop_timer(&timer);
    CHECK(read_timer(timer, NANO_SEC) >= 0);
#endif
    setup_rdtsc();
    unsigned long long previous = rdtsc();
    for (int i = 0; i < 1000; ++i)
    {
        unsigned long long current = rdtsc();
        CHECK(current >= previous);
        previous = current;
    }
    finish_rdtsc();
    puts("Linux randomness and monotonic timestamp tests passed (no NTT executed).");
    return 0;
}

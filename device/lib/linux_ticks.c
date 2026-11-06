/* Linux implementation of the historical m1cycles API.
 * PERF_CYCLES counts only this thread's user execution; no ns conversion.
 * Enable once outside the loop: paired read snapshots preserve call boundaries.
 */
#define _GNU_SOURCE
#include "m1cycles.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef SE_LINUX_PERF_CYCLES
#include <linux/perf_event.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
static _Thread_local int counter_fd = -1;
static _Thread_local struct se_pmu_snapshot previous;
static _Thread_local int have_previous;
static void fail(const char *operation)
{
    fprintf(stderr, "CPU cycles unavailable/invalid: %s (errno=%d). No ns fallback.\n", operation, errno);
    abort();
}
/* Exported for deterministic validation of stopped/multiplexed counters. */
int se_pmu_interval_valid(struct se_pmu_snapshot a, struct se_pmu_snapshot b)
{
    return b.value >= a.value && b.enabled > a.enabled && b.running > a.running &&
           b.enabled - a.enabled == b.running - a.running;
}
void finish_rdtsc(void)
{
    if (counter_fd < 0) return;
    if (ioctl(counter_fd, PERF_EVENT_IOC_DISABLE, 0) < 0) fail("disable");
    if (close(counter_fd) < 0) fail("close");
    counter_fd = -1;
    have_previous = 0;
}
void setup_rdtsc(void)
{
    finish_rdtsc();
    struct perf_event_attr attr = {0};
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.disabled = 1;
    attr.pinned = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    counter_fd = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (counter_fd < 0) fail("perf_event_open cycles:u");
    if (ioctl(counter_fd, PERF_EVENT_IOC_ENABLE, 0) < 0) fail("enable");
}
unsigned long long rdtsc(void)
{
    struct se_pmu_snapshot now;
    if (counter_fd < 0) fail("setup_rdtsc required");
    ssize_t size;
    do { size = read(counter_fd, &now, sizeof(now)); } while (size < 0 && errno == EINTR);
    if (size != sizeof(now)) fail("counter read (pinned event unavailable?)");
    if (!now.running || now.enabled != now.running ||
        (have_previous && !se_pmu_interval_valid(previous, now)))
        fail("stopped/multiplexed/nonmonotonic counter");
    previous = now;
    have_previous = 1;
    return now.value;
}
#else
void finish_rdtsc(void) {}
void setup_rdtsc(void) { (void)rdtsc(); }
unsigned long long rdtsc(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0) abort();
    return (unsigned long long)now.tv_sec * 1000000000ULL + (unsigned long long)now.tv_nsec;
}
#endif

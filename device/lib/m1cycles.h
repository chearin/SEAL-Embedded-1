#ifndef M1CYCLES_H
#define M1CYCLES_H
#if defined(__linux__)
#ifdef SE_LINUX_PERF_CYCLES
#include <stdint.h>
#define SE_RDTSC_UNIT "CPU cycles (perf_event_open, user-thread)"
struct se_pmu_snapshot { uint64_t value, enabled, running; };
int se_pmu_interval_valid(struct se_pmu_snapshot a, struct se_pmu_snapshot b);
#else
#define SE_RDTSC_UNIT "ns (CLOCK_MONOTONIC_RAW)"
#endif
void finish_rdtsc(void);
#else
#define SE_RDTSC_UNIT "cycles"
#endif
void setup_rdtsc(void);
extern unsigned long long int rdtsc(void);
#endif

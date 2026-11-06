/* Small PMU diagnostics only. No CKKS work or performance experiment. */
#define _GNU_SOURCE
#include "m1cycles.h"
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL: %s\n",#x); return 1; } } while(0)
static uint64_t ns(void) {
 struct timespec t; if(clock_gettime(CLOCK_MONOTONIC_RAW,&t)) abort();
 return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
static int cmp(const void *a,const void *b) {
 uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);
}
int main(void) {
#ifndef SE_LINUX_PERF_CYCLES
 puts("PMU smoke requires PERF_CYCLES"); return 77;
#else
 cpu_set_t original,one; CHECK(!sched_getaffinity(0,sizeof original,&original));
 int cpu=sched_getcpu(); CHECK(cpu>=0); CPU_ZERO(&one);CPU_SET(cpu,&one);
 CHECK(!sched_setaffinity(0,sizeof one,&one));
 CHECK(se_pmu_interval_valid((struct se_pmu_snapshot){1,2,2},(struct se_pmu_snapshot){2,3,3}));
 CHECK(!se_pmu_interval_valid((struct se_pmu_snapshot){1,2,2},(struct se_pmu_snapshot){2,4,3}));
 CHECK(!se_pmu_interval_valid((struct se_pmu_snapshot){1,2,2},(struct se_pmu_snapshot){2,2,2}));
 CHECK(!se_pmu_interval_valid((struct se_pmu_snapshot){3,2,2},(struct se_pmu_snapshot){2,3,3}));
 setup_rdtsc();
 uint64_t samples[101],t=ns();
 for(int i=0;i<101;i++){uint64_t a=rdtsc();samples[i]=rdtsc()-a;}
 uint64_t elapsed=ns()-t;
 qsort(samples,101,sizeof samples[0],cmp);
 printf("CPU=%d empty bracket cycles min=%llu median=%llu max=%llu; two-read wall ns mean=%llu\n",cpu,
 (unsigned long long)samples[0],(unsigned long long)samples[50],(unsigned long long)samples[100],(unsigned long long)(elapsed/101));
 volatile uint64_t x=1;
 for(unsigned n=10000;n<=1000000;n*=10){
 uint64_t a=rdtsc();for(unsigned i=0;i<n;i++)x=x*1664525+1013904223;
 uint64_t b=rdtsc();CHECK(b>a);printf("iterations=%u CPU cycles=%llu\n",n,(unsigned long long)(b-a));}
 uint64_t a=rdtsc(); usleep(10000); uint64_t b=rdtsc();
 printf("10ms sleep user-thread CPU cycles=%llu (not wall time)\n",(unsigned long long)(b-a));
 finish_rdtsc();
 t=ns();
 for(int i=0;i<20;i++){setup_rdtsc();finish_rdtsc();}
 printf("open/enable/disable/close wall ns mean=%llu (outside measurement)\n",(unsigned long long)((ns()-t)/20));
 CHECK(!sched_setaffinity(0,sizeof original,&original));
 puts("PMU snapshot validation PASS; no multiplexing; CKKS NOT executed");
 return x==0;
#endif
}

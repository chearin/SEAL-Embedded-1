/* Deterministic syscall fault injection; no real PMU needed. */
#include "m1cycles.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
static int mode,reads;
long __wrap_syscall(long number, ...) { (void)number; if(mode==1){errno=EACCES;return -1;}return 42; }
int __wrap_ioctl(int fd,unsigned long request,...) {(void)fd;(void)request;return 0;}
int __wrap_close(int fd) {(void)fd;return 0;}
ssize_t __wrap_read(int fd,void *buf,size_t size) {
 (void)fd; if(mode==2)return 0;
 if(mode==3 && !reads++){errno=EINTR;return -1;}
 struct se_pmu_snapshot *s=buf;
 s->value=100+reads;s->enabled=10+reads;s->running=s->enabled;
 if(mode==4)s->running--;
 if(mode==5)s->running=0;
 if(mode==6 && reads)s->value=0;
 reads++;return size;
}
int main(void) {
 for(int m=1;m<=6;m++){
 pid_t child=fork();if(child<0)return 1;
 if(!child){struct rlimit r={0,0};setrlimit(RLIMIT_CORE,&r);mode=m;setup_rdtsc();rdtsc();rdtsc();finish_rdtsc();_exit(0);}
 int status;if(waitpid(child,&status,0)!=child)return 1;
 if(m==3){if(!WIFEXITED(status)||WEXITSTATUS(status))return 1;}
 else if(!WIFSIGNALED(status)||WTERMSIG(status)!=SIGABRT)return 1;
 }
 puts("PMU permission, EOF, EINTR, multiplex, stopped and nonmonotonic fault tests PASS");return 0;
}

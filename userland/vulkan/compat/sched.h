/* SPDX-License-Identifier: MIT */
#ifndef BEGONIA_DARWIN_SCHED_H
#define BEGONIA_DARWIN_SCHED_H
#include_next <sched.h>
#if defined(__APPLE__) && !defined(HAKIOS_NATIVE_POSIX_H)
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>
static inline int begonia_sched_setscheduler(pid_t pid,int policy,const struct sched_param *parameters) {
 if(pid!=0) {errno=ENOTSUP;return -1;}
 int result=pthread_setschedparam(pthread_self(),policy,parameters);
 if(result) {errno=result;return -1;}return 0;
}
#define sched_setscheduler begonia_sched_setscheduler
#endif
#endif

/* SPDX-License-Identifier: MIT
 * No Linux performance-counter timerfd on Darwin. Core GPU job submission
 * uses the native synchronization transport instead.
 */
#ifndef BEGONIA_DARWIN_TIMERFD_H
#define BEGONIA_DARWIN_TIMERFD_H
#ifndef __APPLE__
#include_next <sys/timerfd.h>
#else
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#define TFD_CLOEXEC O_CLOEXEC
struct itimerspec {struct timespec it_interval,it_value;};
static inline int timerfd_create(int clock,int flags) {(void)clock;(void)flags;errno=ENOSYS;return -1;}
static inline int timerfd_settime(int fd,int flags,const struct itimerspec *value,struct itimerspec *old) {
 (void)fd;(void)flags;(void)value;(void)old;errno=ENOSYS;return -1;
}
#endif
#endif

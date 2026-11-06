/* SPDX-License-Identifier: MIT
 * Panfrost performance-counter sessions use Linux eventfd. This optional
 * facility is unavailable on Darwin; never report successful fake events.
 */
#ifndef BEGONIA_DARWIN_EVENTFD_H
#define BEGONIA_DARWIN_EVENTFD_H
#ifndef __APPLE__
#include_next <sys/eventfd.h>
#else
#include <stdint.h>
#include <errno.h>
/* The same optional performance worker uses sched_setscheduler(). */
#include <sched.h>
typedef uint64_t eventfd_t;
static inline int eventfd(unsigned value,int flags) {(void)value;(void)flags;errno=ENOSYS;return -1;}
static inline int eventfd_read(int fd,eventfd_t *value) {(void)fd;(void)value;errno=ENOSYS;return -1;}
static inline int eventfd_write(int fd,eventfd_t value) {(void)fd;(void)value;errno=ENOSYS;return -1;}
#endif
#endif

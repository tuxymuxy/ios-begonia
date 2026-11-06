/* SPDX-License-Identifier: MIT */
#ifndef BEGONIA_SYSMACROS_H
#define BEGONIA_SYSMACROS_H
#ifdef __APPLE__
/* Darwin's native dev_t encoding and major/minor live here. */
#include <sys/types.h>
#else
#include_next <sys/sysmacros.h>
#endif
#endif

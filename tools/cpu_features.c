/* SPDX-License-Identifier: MIT
 * Linux EL0 diagnostic. Linux emulates CPUID reads and may return sanitized
 * system-wide feature values. Never treats this as a per-core EL1 guarantee.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/auxv.h>
#include <unistd.h>
#define MRS(r) ({ uint64_t v; __asm__ volatile("mrs %0, " #r : "=r"(v)); v; })
int main(void) {
    if (!(getauxval(AT_HWCAP) & (1UL << 11))) {
        fputs("Linux HWCAP_CPUID unavailable; refusing MRS\n", stderr);
        return 1;
    }
    puts("Linux EL0 CPUID view; feature values may be system-wide sanitized.");
    for (int cpu=0; cpu<8; cpu++) {
        cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(cpu, &mask);
        if (sched_setaffinity(0, sizeof(mask), &mask)) { perror("affinity"); return 1; }
        uint64_t mmfr0=MRS(id_aa64mmfr0_el1);
        printf("cpu=%d midr=%016lx mmfr0=%016lx tgran16=%lx cntfrq=%lu\n", cpu,
               (unsigned long)MRS(midr_el1), (unsigned long)mmfr0,
               (unsigned long)((mmfr0>>20)&15), (unsigned long)MRS(cntfrq_el0));
    }
    return 0;
}

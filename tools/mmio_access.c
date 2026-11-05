/* Exact-width accesses for the Linux diagnostic harness. Python struct/memcpy
 * accesses must not be used on FIFO or interrupt registers. */
#include <stdint.h>
uint32_t probe_read32(void *base,uint32_t offset) {
 return *(volatile uint32_t *)((uintptr_t)base+offset);
}
void probe_write32(void *base,uint32_t offset,uint32_t value) {
 *(volatile uint32_t *)((uintptr_t)base+offset)=value;
 __asm__ volatile("dsb sy" ::: "memory");
}

/* SPDX-License-Identifier: MIT
 * Stage 0: architectural registers only. No MediaTek MMIO, storage, SMC,
 * clocks, interrupt enabling or CPU_ON. QEMU UART is an explicit build option.
 * Linux arm64 entry contract required: EL1/EL2, MMU off, valid RAM/stack.
 */
#include <stdint.h>
#define READ(r) ({ uint64_t v; __asm__ volatile("mrs %0, " #r : "=r"(v)); v; })
struct report {
    uint64_t magic, version, dtb, el, midr, mpidr, mmfr0, pfr0, cntfrq, sctlr;
    uint64_t complete;
};
volatile struct report begonia_report;
#ifdef QEMU_UART
static void putc_uart(char c) {
    volatile uint32_t *uart = (volatile uint32_t *)0x09000000;
    while (uart[6] & (1u << 5)) {}
    uart[0] = (uint32_t)c;
}
#else
static void putc_uart(char c) { (void)c; }
#endif
#ifdef RAMOOPS_STAGE0
static void putc_log(char c) {
    volatile uint32_t *h = (volatile uint32_t *)0x4d09f000;
    volatile unsigned char *data = (volatile unsigned char *)(h + 3);
    const uint32_t cap = 0x40000 - 12;
    if (h[0] != 0x43474244 || h[1] >= cap || h[2] > cap) {
        h[0] = 0x43474244; h[1] = 0; h[2] = 0;
    }
    uint32_t start=h[1]; data[start]=c;
    h[1]=(start+1)%cap;
    if (h[2]<cap) h[2]++;
    __asm__ volatile("dsb sy" ::: "memory");
}
#else
static void putc_log(char c) { (void)c; }
#endif
static void emit(char c) { putc_uart(c); putc_log(c); }
static void puts_uart(const char *s) { while (*s) emit(*s++); }
static void hex(const char *s, uint64_t n) {
    puts_uart(s);
    for (int i=60; i>=0; i-=4) emit("0123456789abcdef"[(n >> i)&15]);
    emit('\n');
}
void begonia_emit_hex(const char *s, uint64_t n) { hex(s,n); }
void begonia_emit_text(const char *s) { puts_uart(s); }
#ifdef XNU_BOOT
extern int xnu_main(void);
#endif
#ifdef BOARD_PROBE
extern void board_probe(void);
#endif
void probe_main(uint64_t dtb) {
    begonia_report.magic = 0x4245474f4e494130; /* BEGONIA0 */
    begonia_report.version = 1;
    begonia_report.dtb = dtb;
    begonia_report.el = (READ(CurrentEL) >> 2)&3;
    begonia_report.midr = READ(midr_el1);
    begonia_report.mpidr = READ(mpidr_el1);
    begonia_report.mmfr0 = READ(id_aa64mmfr0_el1);
    begonia_report.pfr0 = READ(id_aa64pfr0_el1);
    begonia_report.cntfrq = READ(cntfrq_el0);
    begonia_report.sctlr = begonia_report.el == 2 ? READ(sctlr_el2) : READ(sctlr_el1);
    begonia_report.complete = 1;
    __asm__ volatile("dsb sy" ::: "memory");
#ifdef XNU_BOOT
    puts_uart("BEGONIA stage0: experimental XNU boot loader\n");
#else
    puts_uart("BEGONIA stage0: architectural probe only; no XNU or iOS\n");
#endif
    hex("EL=", begonia_report.el);
    hex("DTB=", dtb);
    hex("MIDR=", begonia_report.midr);
    hex("MPIDR=", begonia_report.mpidr);
    hex("MMFR0=", begonia_report.mmfr0);
    hex("TGran16=", (begonia_report.mmfr0 >> 20)&15);
    hex("CNTFRQ=", begonia_report.cntfrq);
    hex("SCTLR=", begonia_report.sctlr);
    puts_uart("PROBE_COMPLETE\n");
#ifdef BOARD_PROBE
    board_probe();
#endif
#ifdef XNU_BOOT
    xnu_main();
#endif
#ifdef CHAIN_BOOT
    puts_uart("CHAINING_ORIGINAL_KERNEL\n");
#endif
}

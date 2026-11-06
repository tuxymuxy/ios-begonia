/* SPDX-License-Identifier: MIT */
.section .text.head,"ax"
.global _head
_head:
 b _start
 .long 0
 .quad 0x80000
 .quad _end
 .quad 0xa
 .quad 0,0,0
 .ascii "ARM\x64"
 .long 0
_start:
 msr daifset, #0xf
 mov x19, x0
 msr spsel, #1
 adr x1, _stack_top
 mov sp, x1
 adr x1, _bss_start
 adr x2, _bss_end
1: cmp x1, x2
 b.hs 2f
 str xzr, [x1], #8
 b 1b
2: mov x0, x19
#ifdef BOARD_PROBE
 adr x1, board_vectors
 msr vbar_el2, x1
 isb
#endif
 bl probe_main
.global after_probe
after_probe:
#ifdef CHAIN_BOOT
 /* Move copy stub outside source/destination. No peripheral reset. */
 adr x1, chain_stub
 adr x2, chain_stub_end
 mov x3, #0x42000000
 mov x4, x3
4: cmp x1, x2
 b.hs 5f
 ldr w5, [x1], #4
 str w5, [x4], #4
 b 4b
5: dsb sy
 ic iallu
 dsb sy
 isb
 mov x0, x19
 adr x1, _head
 ldr x2, [x1, #32]  /* exact original file bytes, padded to 8 */
 add x1, x1, #0x10, lsl #12
 mov x4, #0x40080000
 br x3
 .balign 4
chain_stub:
 mov x5, x4
6: cbz x2, 7f
 ldr x6, [x1], #8
 str x6, [x5], #8
 sub x2, x2, #8
 b 6b
7: dsb sy
 ic iallu
 dsb sy
 isb
 mov x1, xzr
 mov x2, xzr
 mov x3, xzr
 br x4
chain_stub_end:
#endif
3: wfe
 b 3b

#ifdef XNU_BOOT
.global xnu_jump
xnu_jump:
 msr daifset, #0xf
 mov x16, x0
 mov x0, x1
 mrs x2, CurrentEL
 cmp x2, #8
 b.ne 8f
 mov x2, #(1 << 31)
 msr hcr_el2, x2
 mov x2, #3
 msr cnthctl_el2, x2
 msr cntvoff_el2, xzr
 mov x2, #0x33ff
 msr cptr_el2, x2
 mrs x2, icc_sre_el2
 orr x2, x2, #1
 orr x2, x2, #8
 msr icc_sre_el2, x2
 isb
 mov x2, #0x0800
 movk x2, #0x30d0, lsl #16
 msr sctlr_el1, x2
 mov x2, sp
 msr sp_el1, x2
 mov x2, #0x3c5
 msr spsr_el2, x2
 msr elr_el2, x16
 eret
8: br x16
#endif

#ifdef BOARD_PROBE
.balign 2048
board_vectors:
.rept 16
 b board_fault
 .space 124
.endr
board_fault:
 mrs x0, esr_el2
 mrs x1, far_el2
 bl board_fault_report
 adr x1, _stack_top
 mov sp, x1
 adrp x1, begonia_report
 add x1, x1, :lo12:begonia_report
 ldr x19, [x1, #16]
 b after_probe
#endif
#ifdef XNU_BOOT
.global watcher_entry
watcher_entry:
 msr daifset, #0xf
 msr spsel, #1
 adrp x1, watcher_stack
 add x1,x1,:lo12:watcher_stack
 add x1,x1,#0x2000
 mov sp,x1
 bl watcher_main
9: wfe
 b 9b
#endif

#ifdef XNU_BOOT
/* PSCI64 context x0 carries the real high-RAM reset vector. Some firmware
 * entry paths only reliably reach low RAM; test this without moving XNU. */
.balign 64
.global begonia_secondary_trampoline
begonia_secondary_trampoline:
 msr daifset, #0xf
 br x0
#endif

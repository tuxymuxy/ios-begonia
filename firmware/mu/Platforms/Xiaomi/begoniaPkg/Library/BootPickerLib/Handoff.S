// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copied to physical 0x44000000. All references are PC-relative or constants.
#include <AsmMacroLib.h>
.text
.balign 8
.global BegoniaHandoffStart
.global BegoniaHandoffEnd
BegoniaHandoffStart:
  msr daifset, #0xf
  movz x12, #0x0200
  movk x12, #0x4d0e, lsl #16
  movz w13, #8
  movk w13, #0x4d55, lsl #16
  str w13, [x12]
  dsb sy
  mov x9, x1
  mov x10, x1
1:cbz x2, 2f
  ldr x11, [x0], #8
  str x11, [x10], #8
  sub x2, x2, #8
  b 1b
2:movz w13, #9
  movk w13, #0x4d55, lsl #16
  str w13, [x12]
  movz x11, #0x0100
  movk x11, #0x4d0e, lsl #16
  str w4, [x11]
  dsb sy
  ic iallu
  dsb sy
  isb
  mov x0, x3
  mov x1, xzr
  mov x2, xzr
  mov x3, xzr
  br x9
BegoniaHandoffEnd:

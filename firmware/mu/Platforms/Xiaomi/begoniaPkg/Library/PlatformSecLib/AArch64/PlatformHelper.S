// Copyright (c) 2012-2013, ARM Limited.
// SPDX-License-Identifier: BSD-2-Clause-Patent
// MT6785 LK entry is EL2, MMU off. Match the proven begonia XNU handoff.
#include <AsmMacroLib.h>
ASM_FUNC (PlatformAssemblyInitialize)
  ldr x9, =0x44001000
  str x0, [x9]
  msr daifset, #0xf
  EL1_OR_EL2 (x0)
1:msr spsel, #1
  ret
2:mov x0, #(1 << 31)
  msr hcr_el2, x0
  mov x0, #3
  msr cnthctl_el2, x0
  msr cntvoff_el2, xzr
  mov x0, #0x33ff
  msr cptr_el2, x0
  mrs x0, icc_sre_el2
  orr x0, x0, #1
  orr x0, x0, #0x100
  msr icc_sre_el2, x0
  ldr x0, =0x30d00800
  msr sctlr_el1, x0
  msr elr_el2, x30
  mov x0, #0x3c5
  msr spsr_el2, x0
  isb
  eret

// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/IoLib.h>
#include <Library/PlatformSecLib.h>
VOID PlatformInitialize (VOID) {
  // MTK keyed watchdog mode write; inherited display clocks stay untouched.
  MmioWrite32 (0x10007000, 0x22000000);
  // Actual scanout is OVL0 layer 1, not the reference platform's layer 0.
  MmioAnd32 (0x14008050, ~((UINT32)BIT8));
}

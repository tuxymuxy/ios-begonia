// SPDX-License-Identifier: BSD-2-Clause-Patent
// begonia live DT: row 0 col 0 = KEY_VOLUMEUP, col 1 = KEY_VOLUMEDOWN.
// MT6779-compatible KPD_MEM bits are active low. Read inherited setup only.
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/KeypadDeviceLib.h>

STATIC UINT32 mCandidate;
STATIC UINT32 mStable;
STATIC UINT64 mDebounce;
STATIC UINT64 mHeld;
STATIC BOOLEAN mChord;
STATIC BOOLEAN mRepeated;

RETURN_STATUS EFIAPI KeypadDeviceConstructor (VOID) { return RETURN_SUCCESS; }

EFI_STATUS EFIAPI KeypadDeviceReset (KEYPAD_DEVICE_PROTOCOL *This) {
  mCandidate = 0; mStable = 0; mDebounce = 0; mHeld = 0;
  mChord = FALSE; mRepeated = FALSE;
  return EFI_SUCCESS;
}

STATIC VOID Emit (KEYPAD_RETURN_API *Api, UINT32 Keys) {
  EFI_KEY_DATA Key;
  ZeroMem (&Key, sizeof(Key));
  if (Keys == 3) Key.Key.UnicodeChar = CHAR_CARRIAGE_RETURN;
  else Key.Key.ScanCode = (Keys == 1) ? SCAN_UP : SCAN_DOWN;
  Api->PushEfikeyBufTail (Api, &Key);
}

EFI_STATUS KeypadDeviceGetKeys (KEYPAD_DEVICE_PROTOCOL *This,
                               KEYPAD_RETURN_API *Api, UINT64 Delta) {
  UINT32 Keys = (~MmioRead32 (0x10010004)) & 3;
  if (Keys != mCandidate) { mCandidate = Keys; mDebounce = 0; }
  else if (mDebounce < 30000000ULL) mDebounce += MIN(Delta, 30000000ULL);
  if (mDebounce < 30000000ULL) return EFI_SUCCESS;
  if (Keys != mStable) {
    if (Keys == 3) { Emit(Api, 3); mChord = TRUE; }
    else if (Keys == 0) {
      if (!mChord && !mRepeated && mStable != 0) Emit(Api, mStable);
      mChord = FALSE; mRepeated = FALSE;
    }
    mStable = Keys; mHeld = 0;
  } else if (Keys != 0 && Keys != 3 && !mChord) {
    mHeld += MIN(Delta, 500000000ULL);
    if (mHeld >= (mRepeated ? 150000000ULL : 500000000ULL)) {
      Emit(Api, Keys); mRepeated = TRUE; mHeld = 0;
    }
  }
  return EFI_SUCCESS;
}

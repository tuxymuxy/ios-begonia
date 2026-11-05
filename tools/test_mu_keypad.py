#!/usr/bin/env python3
"""Exercise actual Mu keypad C with simulated MMIO and poll timing."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=root/'firmware/mu/Platforms/Xiaomi/begoniaPkg/Library/KeypadDeviceLib/KeypadDeviceLib.c'
with tempfile.TemporaryDirectory(prefix='mu-keypad-') as temp:
    d=Path(temp);(d/'Library').mkdir()
    (d/'Uefi.h').write_text('''#pragma once
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT32; typedef uint16_t UINT16; typedef uint64_t UINT64;
typedef int BOOLEAN; typedef void VOID; typedef int EFI_STATUS; typedef int RETURN_STATUS;
#define STATIC static
#define EFIAPI
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define RETURN_SUCCESS 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define CHAR_CARRIAGE_RETURN 13
#define SCAN_UP 1
#define SCAN_DOWN 2
typedef struct {struct {UINT16 ScanCode, UnicodeChar;} Key;} EFI_KEY_DATA;
typedef struct {int unused;} KEYPAD_DEVICE_PROTOCOL;
typedef struct Api {void (*PushEfikeyBufTail)(struct Api *,EFI_KEY_DATA *);} KEYPAD_RETURN_API;
''')
    (d/'Library/IoLib.h').write_text('UINT32 MmioRead32(UINT64 address);\n')
    (d/'Library/BaseMemoryLib.h').write_text('#define ZeroMem(p,n) memset(p,0,n)\n')
    (d/'Library/KeypadDeviceLib.h').write_text('')
    harness='''#include "Uefi.h"
static UINT32 raw=3; static int events[32], count;
UINT32 MmioRead32(UINT64 address) {assert(address==0x10010004);return raw;}
static void push(KEYPAD_RETURN_API *a,EFI_KEY_DATA *key) {
  assert(count<32); events[count++]=key->Key.UnicodeChar?13:key->Key.ScanCode;
}
#include "SOURCE"
static KEYPAD_RETURN_API api={push};
static void poll(int keys,int n) {raw=(~keys)&3;while(n--)KeypadDeviceGetKeys(0,&api,10000000);}
static void reset(void) {KeypadDeviceReset(0);count=0;poll(0,5);}
int main(void) {
 reset();poll(1,10);poll(0,5);assert(count==1&&events[0]==1);
 reset();poll(2,10);poll(0,5);assert(count==1&&events[0]==2);
 reset();poll(1,8);poll(3,8);poll(2,8);poll(0,8);assert(count==1&&events[0]==13);
 reset();poll(1,1);poll(0,8);assert(count==0);
 reset();poll(2,100);assert(count>=3);int old=count;poll(0,8);assert(count==old);
 reset();poll(0,100);assert(count==0);
 return 0;
}
'''.replace('SOURCE',str(source))
    (d/'test.c').write_text(harness)
    subprocess.run(['cc','-Werror','-I',str(d),str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('PASS: up/down, chord Enter, bounce rejection, repeat, release suppression, idle')

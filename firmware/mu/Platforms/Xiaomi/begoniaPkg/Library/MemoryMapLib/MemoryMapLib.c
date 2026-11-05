// SPDX-License-Identifier: BSD-2-Clause-Patent
// Reserve the low chain source; allocate DXE only in the measured upper DDR window.
#include <Library/MemoryMapLib.h>
STATIC EFI_MEMORY_REGION_DESCRIPTOR mRegions[] = {
  {"UEFI Stack", 0x40000000, 0x40000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI FD", 0x40080000, 0x200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"Chain payload", 0x40280000, 0x3C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"DXE Heap", 0x180000000ULL, 0x4000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK},
  {"Handoff scratch", 0x44000000, 0x2000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Request word", 0x4d0e0000, 0x1000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"LK device tree", 0x4bc80000, 0x200000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Display Reserved", 0x7e384000, 0x9a4000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"GIC Distributor", 0x0c000000, 0x40000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"GIC Redistributors", 0x0c040000, 0x200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"WatchDog Timer", 0x10007000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Keypad", 0x10010000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Display OVL", 0x14008000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE}
};
VOID GetMemoryMap (OUT EFI_MEMORY_REGION_DESCRIPTOR **Map, OUT UINT8 *Count) {
  *Map = mRegions; *Count = sizeof(mRegions) / sizeof(mRegions[0]);
}

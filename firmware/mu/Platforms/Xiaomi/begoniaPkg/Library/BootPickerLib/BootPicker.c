// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ArmLib.h>
#include <Library/CacheMaintenanceLib.h>

extern UINT8 BegoniaHandoffStart[], BegoniaHandoffEnd[];
typedef VOID (*HANDOFF)(UINT64,UINT64,UINT64,UINT64,UINT64);
#define CHAIN ((UINT8 *)0x40280000)

STATIC VOID Mark (UINT32 Step) {
  volatile UINT32 *P=(volatile UINT32 *)0x4d0e0200;
  *P=0x4d550000|Step;
  __asm__ volatile("dc cvac, %0; dsb sy" :: "r"(P) : "memory");
}

STATIC EFI_STATUS BootChoice (BOOLEAN Ios) {
  UINT64 Size=*(UINT64 *)(CHAIN+16), Offset=*(UINT64 *)(CHAIN+40);
  UINT64 PayloadSize=*(UINT64 *)(CHAIN+48);
  UINT64 Dtb=*(volatile UINT64 *)0x44001000;
  UINT32 Request=0;
  EFI_STATUS Status;
  UINTN MapSize=0, MapKey, DescriptorSize;
  UINT32 Version;
  EFI_MEMORY_DESCRIPTOR *Map;
  Mark(1);
  Print(L"\r\nStarting %s...\r\n", Ios?L"experimental iOS":L"postmarketOS");
  if (*(UINT32 *)(CHAIN+56)!=0x644d5241 || Size<0x10000 || Size>0x3c00000 ||
      Dtb<0x4bc80000 || Dtb>0x4be7f000 || *(UINT32 *)(UINTN)Dtb!=0xedfe0dd0)
    return EFI_COMPROMISED_DATA;
  if (Ios) {
    UINT8 *Payload;
    UINT64 AdtOffset, AdtSize;
    UINT32 Hash=0x811c9dc5;
    if (Offset>Size || PayloadSize>Size-Offset || PayloadSize<0x2a8) return EFI_COMPROMISED_DATA;
    Payload=CHAIN+Offset;
    if (CompareMem(Payload,"SWEETXNU",8)!=0) return EFI_COMPROMISED_DATA;
    AdtOffset=*(UINT64 *)(Payload+32); AdtSize=*(UINT64 *)(Payload+40);
    if (AdtOffset>PayloadSize || AdtSize>PayloadSize-AdtOffset || AdtSize>0x100000)
      return EFI_COMPROMISED_DATA;
    for (UINTN I=0; I<608; I++) Hash=(Hash^Payload[72+I])*0x01000193;
    for (UINTN I=0; I<AdtSize; I++) Hash=(Hash^Payload[AdtOffset+I])*0x01000193;
    Request=Hash^0xb00610de; if (!Request) Request=1;
  }
  Mark(2);
  Print(L"Boot image validated. Preparing handoff...\r\n");
  CopyMem((VOID *)0x44000000,BegoniaHandoffStart,BegoniaHandoffEnd-BegoniaHandoffStart);
  Status=gBS->GetMemoryMap(&MapSize,NULL,&MapKey,&DescriptorSize,&Version);
  if (Status!=EFI_BUFFER_TOO_SMALL) return Status;
  MapSize+=4096;
  Map=AllocatePool(MapSize); if (!Map) return EFI_OUT_OF_RESOURCES;
  for (UINTN Retry=0; Retry<2; Retry++) {
    UINTN Capacity=MapSize;
    Status=gBS->GetMemoryMap(&Capacity,Map,&MapKey,&DescriptorSize,&Version);
    if (EFI_ERROR(Status)) break;
    Mark(3);
    Status=gBS->ExitBootServices(gImageHandle,MapKey);
    if (!EFI_ERROR(Status)) {
      ArmDisableInterrupts();
      Mark(4);
      WriteBackInvalidateDataCache();
      Mark(5);
      ArmDisableCachesAndMmu();
      Mark(6);
      ArmInvalidateInstructionCache();
      ArmInvalidateTlb();
      Mark(7);
      ((HANDOFF)(UINTN)0x44000000)((UINT64)(UINTN)CHAIN,0x40080000,(Size+7)&~7ULL,Dtb,Request);
      for (;;) { __asm__ volatile("wfe"); }
    }
    if (Status!=EFI_INVALID_PARAMETER) break;
  }
  // After an ExitBootServices attempt, avoid allocating or returning to BDS.
  if (Map) FreePool(Map);
  return Status;
}

STATIC VOID Draw (UINTN Selected, BOOLEAN Timed) {
  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"Mu-Silicium / Redmi Note 8 Pro\r\n\r\n");
  Print(L"%s postmarketOS\r\n",Selected==0?L">":L" ");
  Print(L"%s iOS 16 (experimental, verbose)\r\n\r\n",Selected==1?L">":L" ");
  Print(L"Volume Up / Down: move\r\nBoth volume buttons: select\r\n");
  if (Timed) Print(L"\r\npostmarketOS starts after 10 seconds.\r\n");
}

EFI_DEVICE_PATH_PROTOCOL ** EFIAPI DeviceBootManagerAfterConsole (VOID) {
  UINTN Selected=0, Polls=0;
  BOOLEAN Timed=TRUE;
  EFI_INPUT_KEY Key;
  Draw(Selected,Timed);
  for (;;) {
    EFI_STATUS Status=gST->ConIn->ReadKeyStroke(gST->ConIn,&Key);
    if (!EFI_ERROR(Status)) {
      Timed=FALSE;
      if (Key.ScanCode==SCAN_UP || Key.ScanCode==SCAN_DOWN) Selected^=1;
      else if (Key.UnicodeChar==CHAR_CARRIAGE_RETURN) {
        Status=BootChoice(Selected==1);
        Print(L"\r\nBoot validation/handoff failed: %r\r\n",Status);
        for (;;) gBS->Stall(1000000);
      }
      Draw(Selected,Timed);
    }
    if (Timed && (Polls%100)==0) {
      gST->ConOut->SetCursorPosition(gST->ConOut,0,10);
      Print(L"Starting postmarketOS in %2u seconds... ",10-Polls/100);
    }
    if (Timed && ++Polls>=1000) {
      Status=BootChoice(FALSE);
      Print(L"\r\nDefault boot failed: %r\r\n",Status);
      for (;;) gBS->Stall(1000000);
    }
    gBS->Stall(10000);
  }
}

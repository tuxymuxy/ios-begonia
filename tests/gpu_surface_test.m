/* Authored IOSurface ownership/cache test. No Apple shaders or rendered assets. */
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceRef.h>
#define main private_buffer_test
#include "gpu_service_test.c"
#undef main
#include <signal.h>
struct surface_request {uint32_t surface,handle;uint64_t size;uint32_t offset,row,width,height,format,planes;};
struct offset_request {uint32_t handle,pad;uint64_t offset;};
struct wait_buffer {uint32_t handle,pad;int64_t timeout;};
_Static_assert(sizeof(struct surface_request)==40,"surface transport ABI");
int main(void) {@autoreleasepool {
 setvbuf(stdout,NULL,_IOLBF,0);alarm(20);
 io_service_t service=IOServiceGetMatchingService(0,IOServiceMatching("HakiosMali"));if(!service)return 1;
 io_connect_t client=0;kern_return_t error=IOServiceOpen(service,mach_task_self(),0,&client);IOObjectRelease(service);if(error)return 1;
 NSDictionary *properties=@{(id)kIOSurfaceWidth:@64,(id)kIOSurfaceHeight:@64,(id)kIOSurfaceBytesPerElement:@4,
 (id)kIOSurfaceBytesPerRow:@256,(id)kIOSurfaceAllocSize:@16384,(id)kIOSurfacePixelFormat:@(0x42475241)};
 IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)properties);
 if(!surface || IOSurfaceGetAllocSize(surface)!=16384) {puts("BEGONIA SURFACE: allocation failed");return 1;}
 struct surface_request imported={.surface=IOSurfaceGetID(surface)};
 if(call(client,0x7e,&imported,sizeof imported) || !imported.handle)return 1;
 struct offset_request offset={.handle=imported.handle};if(call(client,0x45,&offset,sizeof offset))return 1;
 struct create buffer={.size=16384};if(call(client,0x42,&buffer,sizeof buffer))return 1;
 mach_vm_address_t address=0;mach_vm_size_t length=16384;
 if(IOConnectMapMemory64(client,buffer.handle,mach_task_self(),&address,&length,kIOMapAnywhere) || length<16384)return 1;
 volatile uint32_t *job=(volatile uint32_t *)(uintptr_t)address,*pixels=IOSurfaceGetBaseAddress(surface);
 unsigned passed=0;
 for(unsigned round=0;round<3;round++) {
  if(IOSurfaceLock(surface,0,NULL))return 1;
  for(unsigned i=0;i<4096;i++)pixels[i]=0xcafe0000u+round;
  if(IOSurfaceUnlock(surface,0,NULL))return 1;
  memset((void *)(uintptr_t)address,0,16384);job[4]=0x10005;
  uint64_t target=offset.offset+imported.offset+0x800;
  job[8]=(uint32_t)target;job[9]=target>>32;job[10]=6;job[12]=0xbec07610u+round;
  uint32_t handles[2]={buffer.handle,imported.handle};
  struct submit submit={.jc=buffer.offset,.handles=(uint64_t)(uintptr_t)handles,.count=2};
  if(call(client,0x40,&submit,sizeof submit))return 1;
  /* WAIT_BO invalidates the original cached surface through its kernel alias. */
  struct wait_buffer wait={imported.handle,0,2000000000};if(call(client,0x41,&wait,sizeof wait))return 1;
  if(IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL))return 1;
  uint32_t value=pixels[0x800/4],guard=pixels[0];
  if(IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL))return 1;
  int good=value==0xbec07610u+round && guard==0xcafe0000u+round;passed+=good;
  printf("BEGONIA SURFACE: round%u %s gpu=%08x guard=%08x original pages\n",round,good?"PASS":"FAIL",value,guard);
 }
 IOConnectUnmapMemory64(client,buffer.handle,mach_task_self(),address);
 for(unsigned i=0;i<2;i++){struct close_bo close={i?imported.handle:buffer.handle,0};if(call(client,9,&close,sizeof close))return 1;}
 CFRelease(surface);IOServiceClose(client);alarm(0);return passed==3?0:1;
}}

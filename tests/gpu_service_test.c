/* Own-source IPC test: private BO -> mapped GPU write -> CPU readback. */
#include "iokit_compat.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
struct parameter {uint32_t which,pad;uint64_t value;};
struct create {uint32_t size,flags,handle,pad;uint64_t offset;};
struct submit {uint64_t jc,inputs;uint32_t input_count,output;uint64_t handles;uint32_t count,requirements;};
struct wait_job {uint64_t seq;int64_t timeout;};
struct close_bo {uint32_t handle,pad;};
_Static_assert(sizeof(struct submit)==40,"S9 transport submit ABI");
static int call(io_connect_t c,unsigned sel,void *arg,size_t size) {
 size_t out=size;kern_return_t r=IOConnectCallStructMethod(c,sel,arg,size,arg,&out);
 if(r || out!=size) {printf("BEGONIA GPU USER: selector%x failed %x bytes%zu\n",sel,r,out);return -1;}return 0;
}
int main(void) {
 setvbuf(stdout,NULL,_IOLBF,0);io_service_t service=0;
 puts("BEGONIA GPU USER: matching service");
 for(unsigned i=0;i<20 && !service;i++) {
  service=IOServiceGetMatchingService(0,IOServiceMatching("HakiosMali"));if(!service)sleep(1);
 }
 if(!service) {puts("BEGONIA GPU USER: service absent");return 1;}
 puts("BEGONIA GPU USER: opening service");
 io_connect_t client=0;kern_return_t result=IOServiceOpen(service,mach_task_self(),0,&client);IOObjectRelease(service);
 if(result) {printf("BEGONIA GPU USER: open failed %x\n",result);return 1;}
 struct parameter parameter={0};
 puts("BEGONIA GPU USER: querying product");
 if(call(client,0x44,&parameter,sizeof parameter) || parameter.value!=0x7211)return 1;
 struct create buffer={.size=16384};if(call(client,0x42,&buffer,sizeof buffer))return 1;
 puts("BEGONIA GPU USER: private BO created");
 mach_vm_address_t address=0;mach_vm_size_t length=16384;
 result=IOConnectMapMemory64(client,buffer.handle,mach_task_self(),&address,&length,kIOMapAnywhere);
 if(result || length<16384 || !address) {printf("BEGONIA GPU USER: map failed%x\n",result);return 1;}
 volatile uint32_t *words=(volatile uint32_t *)(uintptr_t)address;
 memset((void *)(uintptr_t)address,0,16384);words[4]=0x10005;
 words[8]=(uint32_t)(buffer.offset+0x800);words[9]=(buffer.offset+0x800)>>32;
 words[10]=6;words[12]=0xbec07601;
 __asm__ volatile("dsb sy":::"memory");uint32_t handle=buffer.handle;
 struct submit job={.jc=buffer.offset,.handles=(uint64_t)(uintptr_t)&handle,.count=1};
 if(call(client,0x40,&job,sizeof job) || !job.jc)return 1;
 struct wait_job wait={job.jc,2000000000};if(call(client,0x7d,&wait,sizeof wait))return 1;
 __asm__ volatile("dsb sy":::"memory");uint32_t value=words[0x800/4];
 printf("BEGONIA GPU USER: private mapped WRITE_VALUE %s value=%08x seq=%llu\n",value==0xbec07601?"PASS":"FAIL",value,(unsigned long long)job.jc);
 IOConnectUnmapMemory64(client,buffer.handle,mach_task_self(),address);
 struct close_bo close={buffer.handle,0};if(call(client,9,&close,sizeof close))return 1;
 IOServiceClose(client);return value==0xbec07601?0:1;
}

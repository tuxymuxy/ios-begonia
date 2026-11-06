/* Own-source Metal-shaped API -> metal2vulkan -> native PanVK -> G76 test. */
#include "NativeMetalObjects.h"
#include <mach/mach.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static atomic_bool finished;
static void *monitor(void *unused) {
 (void)unused;
 while(!atomic_load(&finished)) {
  mach_task_basic_info_data_t info;mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
  if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&info,&count)!=KERN_SUCCESS)_exit(125);
  if(info.resident_size>500ull*1024*1024) {fputs("BEGONIA M2V USER: memory limit exceeded\n",stderr);_exit(137);}
  struct timespec pause={.tv_nsec=20000000};nanosleep(&pause,NULL);
 }
 return NULL;
}
static int fail(const char *stage) {fprintf(stderr,"BEGONIA M2V USER: %s failed: %s\n",stage,nmb_last_error());return 1;}
#define CHECK(value,stage) do {if(!(value))return fail(stage);}while(0)
int main(int argc,char **argv) {
 setvbuf(stdout,NULL,_IOLBF,0);alarm(60);pthread_t thread;
 if(pthread_create(&thread,NULL,monitor,NULL))return 125;
 setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER","1",1);
 setenv("MESA_LOG","file",1);setenv("MESA_LOG_LEVEL","debug",1);
 setenv("PANVK_DEBUG","startup",1);
 const char *air=argc>1?argv[1]:"/redwood/fw/begonia-own-compute.air";
 FILE *file=fopen(air,"rb");if(!file) {perror("own AIR");return 1;}
 if(fseek(file,0,SEEK_END))return 1;long length=ftell(file);
 if(length<=0 || length>1024*1024 || fseek(file,0,SEEK_SET))return 1;
 void *bytes=malloc((size_t)length);if(!bytes || fread(bytes,1,(size_t)length,file)!=(size_t)length)return 1;
 fclose(file);mkdir("/private/var/tmp/begonia-vk-cache",0700);
 puts("BEGONIA M2V USER: opening native Vulkan device");
 puts("BEGONIA M2V USER: ICD dlopen begin");
 void *icd=dlopen("/redwood/lib/libvulkan_panfrost.dylib",RTLD_NOW|RTLD_LOCAL);
 if(!icd) {fprintf(stderr,"BEGONIA M2V USER: ICD dlopen failed: %s\n",dlerror());return 1;}
 puts("BEGONIA M2V USER: ICD dlopen complete; bridge device begin");
 NmbDevice *device=nmb_device_open("/redwood/lib/libvulkan_panfrost.dylib","/private/var/tmp/begonia-vk-cache");CHECK(device,"device");
 char name[256]={0};nmb_device_name(device,name,sizeof name);printf("BEGONIA M2V USER: device=%s\n",name);
 CHECK(strstr(name,"G76"),"native G76 identity");
 alarm(20); /* Translation itself retains its strict twenty-second bound. */
 NmbLibrary *library=nmb_library_new(device,bytes,(size_t)length,true);CHECK(library,"own AIR library");free(bytes);
 NmbFunction *function=nmb_function_new(library,"begonia_compute");CHECK(function,"own compute function");
 puts("BEGONIA M2V USER: compiling own compute pipeline");
 NmbComputePipeline *pipeline=nmb_compute_pipeline_new(device,function);CHECK(pipeline,"pipeline");
 alarm(20);
 uint32_t initial[64];for(unsigned i=0;i<64;i++)initial[i]=0xdeadbeef;
 NmbBuffer *buffer=nmb_buffer_new(device,sizeof initial,0,initial);CHECK(buffer,"buffer");
 NmbQueue *queue=nmb_queue_new(device);CHECK(queue,"queue");
 NmbCommandBuffer *command=nmb_command_buffer_new(queue);CHECK(command,"command buffer");
 NmbEncoder *encoder=nmb_compute_encoder_new(command);CHECK(encoder,"compute encoder");
 CHECK(nmb_compute_set_pipeline(encoder,pipeline),"bind pipeline");
 CHECK(nmb_encoder_set_buffer(encoder,0,buffer,0,0),"bind buffer");
 const uint64_t grid[3]={64,1,1},group[3]={64,1,1};
 CHECK(nmb_compute_dispatch(encoder,grid,group,true),"dispatch");CHECK(nmb_encoder_end(encoder),"end encoder");
 puts("BEGONIA M2V USER: submitting native Vulkan compute");
 CHECK(nmb_command_buffer_commit(command),"commit");CHECK(nmb_command_buffer_wait(command,5000)==0,"completion wait");
 if(nmb_command_buffer_status(command)!=4) {
  char error[512]={0};nmb_command_buffer_error(command,error,sizeof error);fprintf(stderr,"BEGONIA M2V USER: command failed: %s\n",error);return 1;
 }
 const uint32_t *result=nmb_buffer_contents(buffer);CHECK(result,"readback");unsigned matches=0;
 for(unsigned i=0;i<64;i++)if(result[i]==i*3+7)matches++;
 printf("BEGONIA M2V USER: native compute matches=%u/64 first=%x last=%x\n",matches,result[0],result[63]);
 nmb_command_buffer_release(command);nmb_queue_release(queue);nmb_buffer_release(buffer);
 nmb_compute_pipeline_release(pipeline);nmb_function_release(function);nmb_library_release(library);nmb_device_release(device);
 atomic_store(&finished,true);pthread_join(thread,NULL);alarm(0);return matches==64?0:1;
}

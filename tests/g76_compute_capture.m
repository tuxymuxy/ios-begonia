// Produce an own-source compute job; the mock captures descriptors, never executes it.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "vmg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) { @autoreleasepool {
    const char *which=argc>1?argv[1]:"panfrost";
    bool software=!strcmp(which,"llvmpipe");
    char error[256]={0};
    vmg_device *dev=vmg_device_create(which,error,sizeof error);
    if(!dev) {fprintf(stderr,"%s\n",error);return 1;}
    id<MTLDevice> system=MTLCreateSystemDefaultDevice();
    NSError *err=nil;
    NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\nkernel void begonia_compute(device uint *output [[buffer(0)]], uint index [[thread_position_in_grid]]) { output[index]=index*3u+7u; }";
    id<MTLLibrary> lib=[system newLibraryWithSource:source options:nil error:&err];
    if(!lib) {fprintf(stderr,"%s\n",err.description.UTF8String);return 1;}
    id<MTLFunction> fn=[lib newFunctionWithName:@"begonia_compute"];
    NSData *bc=[(id)fn respondsToSelector:@selector(bitcodeData)]?[(id)fn performSelector:@selector(bitcodeData)]:nil;
    if(!bc.length)return 1;
    vmg_air_input in={.bitcode=bc.bytes,.bitcode_size=bc.length};
    char *message=NULL;vmg_reflection refl;
    vmg_shader *sh=vmg_shader_create(dev,&in,&refl,&message);
    if(!sh) {fprintf(stderr,"shader: %s\n",message);return 1;}
    vmg_compute_pipeline *pipe=vmg_compute_pipeline_create(dev,sh,&message);
    if(!pipe) {fprintf(stderr,"pipeline: %s\n",message);return 1;}
    void *contents=NULL;
    vmg_resource *output=vmg_buffer_create(dev,256,NULL,&contents);
    if(!output || !contents)return 1;
    for(unsigned i=0;i<64;i++)((uint32_t *)contents)[i]=0xdeadbeef;
    printf("output_gpu_va=0x%llx words=64 expected=i*3+7\n",(unsigned long long)vmg_buffer_gpu_address(dev,output));
    vmg_cmd_set_buffer(dev,VMG_COMPUTE,0,output,0,0);
    vmg_dispatch_info dispatch={.pipeline=pipe,.groups={1,1,1},.threads={64,1,1}};
    vmg_cmd_dispatch(dev,&dispatch);
    vmg_fence *fence=NULL;vmg_flush(dev,&fence);
    if(fence) {if(!vmg_fence_wait(dev,fence,1000000000))return 1;vmg_fence_release(dev,fence);}
    // Software execution checks the translation. The mock must leave output untouched.
    for(unsigned i=0;i<64;i++)if(((uint32_t *)contents)[i]!=(software?i*3+7:0xdeadbeef))return 1;
    puts(software?"Software compute matches64/64 (not native GPU)":"Own compute descriptors captured; mock did not execute shader");
    vmg_resource_release(dev,output);vmg_compute_pipeline_release(dev,pipe);
    vmg_shader_release(dev,sh);vmg_device_destroy(dev);return 0;
} }

#!/usr/bin/env python3
"""Build the pinned Metal Objective-C frontend separately; never install/enable it."""
from pathlib import Path
import argparse,shutil,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--output-dir',type=Path,default=root/'out/metal-frontend-ios')
p.add_argument('--experimental-mps-stubs',action='store_true',help='reproduce v59 diagnostics that silently skip unsupported compute; never for correctness qualification')
a=p.parse_args()
ref=root/'upstream/ios-s9plus-exynos';out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
assert subprocess.check_output(['git','-C',str(ref),'rev-parse','HEAD'],text=True).strip()=='2d092398040f98685b0c5a07776df5e9f199b9dc'
source=out/'metalvk';source.mkdir(exist_ok=True)
for f in (ref/'metal-wip/frontend').iterdir():
 if f.is_file():shutil.copy2(f,source/f.name)
for name in ('hakios_metal_shown.h','hakios_metal_shown.c'):
 shutil.copy2(ref/'core/userland/src'/name,out/name)
f=source/'HakiosMetalVK.mm';text=f.read_text();old='#define DEFAULT_ICD "/redwood/lib/libhakios.vk.dylib"'
assert text.count(old)==1;text=text.replace(old,'#define DEFAULT_ICD "/redwood/lib/libvulkan_panfrost.dylib"')
text=text.replace('tried = true;','tried = true;\n\t\tsetenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);')
f.write_text(text)

mf=source/'NativeBridgeMetalFrontend.mm';original_mtext=mf.read_text();mtext=original_mtext
mtext=mtext.replace('- (void)dealloc {nmb_library_release(_handle);}', '- (void)dealloc {if(_handle)nmb_library_release(_handle);}')
mtext=mtext.replace('if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {',
                    'if(!_handle){if(error)*error=nbError(@"no library handle");return nil;}\n if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {')
mtext=mtext.replace('- (NSArray<NSString *> *)functionNames {\n NSMutableArray *names=[NSMutableArray array];',
                    '- (NSArray<NSString *> *)functionNames {\n NSMutableArray *names=[NSMutableArray array];\n if(!_handle)return names;')
mtext=mtext.replace('NmbFunction *handle=name?nmb_function_new(_handle,name.UTF8String):nullptr;',
                    'NmbFunction *handle=(name && _handle)?nmb_function_new(_handle,name.UTF8String):nullptr;')
mtext=mtext.replace('NmbFunction *handle=name?nmb_function_new_zero_constants(_handle,name.UTF8String):nullptr;',
                    'NmbFunction *handle=(name && _handle)?nmb_function_new_zero_constants(_handle,name.UTF8String):nullptr;')
old_default = '''- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error {
 NSURL *url=[bundle URLForResource:@"default" withExtension:@"metallib"];
 if(!url){if(error)*error=nbError(@"the bundle has no default.metallib");return nil;}
 return [(id<MTLDevice>)self newLibraryWithURL:url error:error];
}'''
new_default = '''- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error {
 NSURL *url=[bundle URLForResource:@"default" withExtension:@"metallib"];
 if(!url){
  // iOS 16 bundles (e.g. MPSImage) ship pipelinelibs or have no standalone default.metallib.
  // Returning a stub library avoids fatal assertions in MPSDeviceComputePipelineState while
  // letting callers query functions gracefully.
  NBLibrary *stub=[NBLibrary new];stub.handle=nullptr;if(error)*error=nil;return (id<MTLLibrary>)stub;
 }
 return [(id<MTLDevice>)self newLibraryWithURL:url error:error];
}'''
assert mtext.count(old_default) == 1
mtext = mtext.replace(old_default, new_default)

mtext = mtext.replace('- (void)dealloc {nmb_function_release(_handle);}', '- (void)dealloc {if(_handle)nmb_function_release(_handle);}')
mtext = mtext.replace(
    '- (void)dealloc {nmb_compute_pipeline_release(_handle);}\n- (NSUInteger)maxTotalThreadsPerThreadgroup {return nmb_compute_pipeline_max_threads(_handle);}\n- (NSUInteger)threadExecutionWidth {return nmb_compute_pipeline_execution_width(_handle);}',
    '- (void)dealloc {if(_handle)nmb_compute_pipeline_release(_handle);}\n- (NSUInteger)maxTotalThreadsPerThreadgroup {return _handle?nmb_compute_pipeline_max_threads(_handle):256;}\n- (NSUInteger)threadExecutionWidth {return _handle?nmb_compute_pipeline_execution_width(_handle):16;}',
)

old_spec_fail = ''' if(!handle){
  noteProblem([NSString stringWithFormat:@"specialized function %@: %@",name,lastError()]);
  if(error)*error=nbError([NSString stringWithFormat:@"specialized function: %@",lastError()]);return nil;
 }'''
new_spec_fail = ''' if(!handle){
  noteProblem([NSString stringWithFormat:@"specialized function %@: %@",name,lastError()]);
  NBFunction *function=[NBFunction new];function.handle=nullptr;function.name=name;function.functionType=MTLFunctionTypeKernel;
  function.constantsText=text;if(error)*error=nil;return (id<MTLFunction>)function;
 }'''
assert mtext.count(old_spec_fail) == 1
mtext = mtext.replace(old_spec_fail, new_spec_fail)

old_comp_pipe = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(MTLPipelineOption)options
                                                         reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(![(id)function isKindOfClass:[NBFunction class]]){if(error)*error=nbError(@"foreign function");return nil;}
 const NmbPipelineOptions creation={(uint64_t)options,nullptr,0};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_options(gDevice,[(NBFunction *)function handle],&creation);gTimePipeline.add(pipelineBegan);
 if(!handle)return failWith<id<MTLComputePipelineState>>(error,@"compute pipeline",nil);
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}'''
new_comp_pipe = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(MTLPipelineOption)options
                                                         reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(![(id)function isKindOfClass:[NBFunction class]]){if(error)*error=nbError(@"foreign function");return nil;}
 if(![(NBFunction *)function handle]){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 const NmbPipelineOptions creation={(uint64_t)options,nullptr,0};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_options(gDevice,[(NBFunction *)function handle],&creation);gTimePipeline.add(pipelineBegan);
 if(!handle){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}'''
assert mtext.count(old_comp_pipe) == 1
mtext = mtext.replace(old_comp_pipe, new_comp_pipe)

old_encoder = '''@interface NBComputeEncoder : NBEncoder
@end
@implementation NBComputeEncoder
- (MTLDispatchType)dispatchType {return MTLDispatchTypeSerial;}
- (void)setComputePipelineState:(id<MTLComputePipelineState>)state {
 if(![(id)state isKindOfClass:[NBComputePipeline class]]){[self.commandBuffer refuse:@"foreign compute pipeline"];return;}
 [self check:nmb_compute_set_pipeline(self.handle,[(NBComputePipeline *)state handle]) what:"setComputePipelineState"];
}'''
new_encoder = '''@interface NBComputeEncoder : NBEncoder
@property(nonatomic) bool stub;
@end
@implementation NBComputeEncoder
- (MTLDispatchType)dispatchType {return MTLDispatchTypeSerial;}
- (void)setComputePipelineState:(id<MTLComputePipelineState>)state {
 if(![(id)state isKindOfClass:[NBComputePipeline class]]){[self.commandBuffer refuse:@"foreign compute pipeline"];return;}
 if(![(NBComputePipeline *)state handle]){
  self.stub=true;
  return;
 }
 self.stub=false;
 [self check:nmb_compute_set_pipeline(self.handle,[(NBComputePipeline *)state handle]) what:"setComputePipelineState"];
}'''
assert mtext.count(old_encoder) == 1
mtext = mtext.replace(old_encoder, new_encoder)

old_dispatch = '''- (void)dispatch:(MTLSize)size group:(MTLSize)group threads:(bool)threads {
 gDispatches++;self.commandBuffer->encodedWork.dispatches++;
 const uint64_t s[3]={size.width,size.height,size.depth},g[3]={group.width,group.height,group.depth};
 [self check:nmb_compute_dispatch(self.handle,s,g,threads) what:"dispatch"];
}'''
new_dispatch = '''- (void)dispatch:(MTLSize)size group:(MTLSize)group threads:(bool)threads {
 if(self.stub)return;
 gDispatches++;self.commandBuffer->encodedWork.dispatches++;
 const uint64_t s[3]={size.width,size.height,size.depth},g[3]={group.width,group.height,group.depth};
 [self check:nmb_compute_dispatch(self.handle,s,g,threads) what:"dispatch"];
}'''
assert mtext.count(old_dispatch) == 1
mtext = mtext.replace(old_dispatch, new_dispatch)

old_comp_desc = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options
                                                       reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 NmbComputePipelineDescriptor p={};std::vector<NmbFunction *> linked,privateLinked;
 if(!computeDescriptor(d,&p,linked,privateLinked,error))return nil;
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error))return nil;
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_descriptor(gDevice,&p,&creation);gTimePipeline.add(pipelineBegan);
 if(!handle)return failWith<id<MTLComputePipelineState>>(error,@"compute pipeline",nil);
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;state.label=d.label;gPipelines++;
 if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}'''
new_comp_desc = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options
                                                       reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(!d || ![(NBFunction *)d.computeFunction handle]){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d?d.label:nil;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 NmbComputePipelineDescriptor p={};std::vector<NmbFunction *> linked,privateLinked;
 if(!computeDescriptor(d,&p,linked,privateLinked,error)){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error)){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_descriptor(gDevice,&p,&creation);gTimePipeline.add(pipelineBegan);
 if(!handle){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;state.label=d.label;gPipelines++;
 if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}
- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d error:(NSError **)error {
 return [(id)self newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionNone reflection:nil error:error];
}
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d completionHandler:(MTLNewComputePipelineStateCompletionHandler)handler {
 MTLComputePipelineDescriptor *snapshot=[d copy];
 compileAsync(^{
  NSError *error=nil;id<MTLComputePipelineState> state=[(id)self newComputePipelineStateWithDescriptor:snapshot options:MTLPipelineOptionNone reflection:nil error:&error];
  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,error);});
 });
}
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options
                           completionHandler:(MTLNewComputePipelineStateWithReflectionCompletionHandler)handler {
 MTLComputePipelineDescriptor *snapshot=[d copy];
 compileAsync(^{
  NSError *error=nil;id<MTLComputePipelineState> state=[(id)self newComputePipelineStateWithDescriptor:snapshot options:options reflection:nil error:&error];
  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,nil,error);});
 });
}'''
assert mtext.count(old_comp_desc) == 1
mtext = mtext.replace(old_comp_desc, new_comp_desc)
mf.write_text(mtext if a.experimental_mps_stubs else original_mtext)
common=['-target','arm64-apple-ios16.5','-isysroot',str(a.sdk.resolve()),'-O1','-Wall','-Wextra',
 '-I'+str(root/'out/metal2vulkan-bridge').replace('/metal2vulkan-bridge',''),'-I'+str(source)]
# The source includes bridge/NativeMetalObjects.h. Keep a staged header only;
# implementation remains dynamically linked and replaceable.
bridge=out/'bridge';bridge.mkdir(exist_ok=True)
shutil.copy2(root/'out/metal2vulkan-bridge/NativeMetalObjects.h',bridge/'NativeMetalObjects.h')
common+=['-I'+str(out)]
objects=[]
with (out/'build.log').open('w') as log:
 for f in (source/'HakiosMetalVK.mm',source/'NativeBridgeMetalFrontend.mm',out/'hakios_metal_shown.c'):
  obj=out/(f.stem+'.o');objects.append(obj)
  cmd=['xcrun','clang++' if f.suffix=='.mm' else 'clang',*common]
  if f.suffix=='.mm':cmd+=['-std=c++17','-fobjc-arc']
  subprocess.run([*cmd,'-c',str(f),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
 binary=out/'HakiosMetal'
 subprocess.run(['xcrun','clang++',*common,'-bundle','-o',str(binary),*map(str,objects),
  str(root/'out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib'),
  '-Wl,-rpath,/redwood/lib','-Wl,-rpath,@loader_path/..',
  '-framework','Foundation','-framework','Metal','-framework','IOSurface','-framework','IOKit','-framework','CoreFoundation','-lobjc'],stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run(['codesign','-s','-','-f',str(binary)],check=True)
subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
print('Built signed Metal frontend; '+('experimental MPS stubs enabled' if a.experimental_mps_stubs else 'unsupported compute reports failure')+'; not installed')

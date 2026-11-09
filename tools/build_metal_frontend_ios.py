#!/usr/bin/env python3
"""Build the pinned Metal Objective-C frontend separately; never install/enable it."""
from pathlib import Path
import argparse,shutil,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--output-dir',type=Path,default=root/'out/metal-frontend-ios')
p.add_argument('--quartzcore',action='store_true',help='enable the iOS 16 QuartzCore pipeline library adapter')
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
                    'if(!_handle){NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=name?:@"stub";stub.functionType=[name containsString:@"vert"]?MTLFunctionTypeVertex:MTLFunctionTypeFragment;if(error)*error=nil;return (id<MTLFunction>)stub;}\n if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {')
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
  NBFunction *function=[NBFunction new];function.handle=nullptr;function.name=name;function.functionType=[name containsString:@"vert"]?MTLFunctionTypeVertex:MTLFunctionTypeFragment;
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

old_note_problem = '''static void noteProblem(NSString *what) {
 gProblems++;'''
new_note_problem = '''static void noteProblem(NSString *what) {
 NSLog(@"[BegoniaMetal] noteProblem: %@", what);
 gProblems++;'''
assert mtext.count(old_note_problem) == 1
mtext = mtext.replace(old_note_problem, new_note_problem)

old_fail_with = '''template <typename T> static T failWith(NSError **error,NSString *what,T value) {
 noteProblem([NSString stringWithFormat:@"%@: %@",what,lastError()]);'''
new_fail_with = '''template <typename T> static T failWith(NSError **error,NSString *what,T value) {
 NSLog(@"[BegoniaMetal] failWith: %@: %@", what, lastError());
 noteProblem([NSString stringWithFormat:@"%@: %@",what,lastError()]);'''
assert mtext.count(old_fail_with) == 1
mtext = mtext.replace(old_fail_with, new_fail_with)

old_render_pipeline = '''@interface NBRenderPipeline : NBObject
@property(nonatomic) NmbRenderPipeline *handle;
@end
@implementation NBRenderPipeline
- (void)dealloc {nmb_render_pipeline_release(_handle);}'''
new_render_pipeline = '''@interface NBRenderPipeline : NBObject
@property(nonatomic) NmbRenderPipeline *handle;
@end
@implementation NBRenderPipeline
- (void)dealloc {if(_handle)nmb_render_pipeline_release(_handle);}'''
assert mtext.count(old_render_pipeline) == 1
mtext = mtext.replace(old_render_pipeline, new_render_pipeline)

old_render_encoder = '''@interface NBRenderEncoder : NBEncoder
@end
@implementation NBRenderEncoder
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)state {
 if(![(id)state isKindOfClass:[NBRenderPipeline class]]){[self.commandBuffer refuse:@"foreign render pipeline"];return;}
 [self check:nmb_render_set_pipeline(self.handle,[(NBRenderPipeline *)state handle]) what:"setRenderPipelineState"];
}'''
new_render_encoder = '''@interface NBRenderEncoder : NBEncoder
@property(nonatomic) bool stub;
@end
@implementation NBRenderEncoder
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)state {
 if(![(id)state isKindOfClass:[NBRenderPipeline class]]){[self.commandBuffer refuse:@"foreign render pipeline"];return;}
 if(![(NBRenderPipeline *)state handle]){
  self.stub=true;
  return;
 }
 self.stub=false;
 [self check:nmb_render_set_pipeline(self.handle,[(NBRenderPipeline *)state handle]) what:"setRenderPipelineState"];
}'''
assert mtext.count(old_render_encoder) == 1
mtext = mtext.replace(old_render_encoder, new_render_encoder)

old_render_draw = '''- (void)drawPrimitives:(MTLPrimitiveType)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n instanceCount:(NSUInteger)i baseInstance:(NSUInteger)b {
 gDraws++;self.commandBuffer->encodedWork.draws++;
 [self check:nmb_render_draw(self.handle,t,(uint32_t)s,(uint32_t)n,(uint32_t)i,(uint32_t)b) what:"drawPrimitives"];
}'''
new_render_draw = '''- (void)drawPrimitives:(MTLPrimitiveType)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n instanceCount:(NSUInteger)i baseInstance:(NSUInteger)b {
 if(self.stub)return;
 gDraws++;self.commandBuffer->encodedWork.draws++;
 [self check:nmb_render_draw(self.handle,t,(uint32_t)s,(uint32_t)n,(uint32_t)i,(uint32_t)b) what:"drawPrimitives"];
}'''
assert mtext.count(old_render_draw) == 1
mtext = mtext.replace(old_render_draw, new_render_draw)

old_render_draw_indexed = '''- (void)drawIndexedPrimitives:(MTLPrimitiveType)t indexCount:(NSUInteger)n indexType:(MTLIndexType)k indexBuffer:(id<MTLBuffer>)b indexBufferOffset:(NSUInteger)o
                instanceCount:(NSUInteger)i baseVertex:(NSInteger)v baseInstance:(NSUInteger)bi {
 if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign index buffer"];return;}'''
new_render_draw_indexed = '''- (void)drawIndexedPrimitives:(MTLPrimitiveType)t indexCount:(NSUInteger)n indexType:(MTLIndexType)k indexBuffer:(id<MTLBuffer>)b indexBufferOffset:(NSUInteger)o
                instanceCount:(NSUInteger)i baseVertex:(NSInteger)v baseInstance:(NSUInteger)bi {
 if(self.stub)return;
 if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign index buffer"];return;}'''
assert mtext.count(old_render_draw_indexed) == 1
mtext = mtext.replace(old_render_draw_indexed, new_render_draw_indexed)

old_render_desc = '''static bool renderDescriptor(MTLRenderPipelineDescriptor *d,NmbRenderPipelineDescriptor *p,NSError **error) {
 if(![(id)d.vertexFunction isKindOfClass:[NBFunction class]] || (d.fragmentFunction && ![(id)d.fragmentFunction isKindOfClass:[NBFunction class]])) {
  if(error)*error=nbError(@"foreign function");return false;
 }'''
new_render_desc = '''static bool renderDescriptor(MTLRenderPipelineDescriptor *d,NmbRenderPipelineDescriptor *p,NSError **error) {
 NSLog(@"[BegoniaMetal] renderDescriptor: label='%@' vf=%@ (class=%s) ff=%@ (class=%s)",
       d.label,
       d.vertexFunction,
       d.vertexFunction ? class_getName(object_getClass(d.vertexFunction)) : "nil",
       d.fragmentFunction,
       d.fragmentFunction ? class_getName(object_getClass(d.fragmentFunction)) : "nil");
 if(!d.vertexFunction || ![(id)d.vertexFunction isKindOfClass:[NBFunction class]] || (d.fragmentFunction && ![(id)d.fragmentFunction isKindOfClass:[NBFunction class]])) {
  NSLog(@"[BegoniaMetal] renderDescriptor foreign/missing function: vf class=%s ff class=%s",
        d.vertexFunction ? class_getName(object_getClass(d.vertexFunction)) : "nil",
        d.fragmentFunction ? class_getName(object_getClass(d.fragmentFunction)) : "nil");
  if(error)*error=nbError(@"foreign function");return false;
 }'''
assert mtext.count(old_render_desc) == 1
mtext = mtext.replace(old_render_desc, new_render_desc)

old_new_render_pipe = '''- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d options:(MTLPipelineOption)options
                                                        reflection:(MTLAutoreleasedRenderPipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 auto *p=new NmbRenderPipelineDescriptor();
 if(!renderDescriptor(d,p,error)){delete p;return nil;}
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error)){delete p;return nil;}
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();const NBCost costBegan=NBCost::now();
 NmbRenderPipeline *handle=nmb_render_pipeline_new_with_options(gDevice,p,&creation);delete p;gTimePipeline.add(pipelineBegan);
 journalRenderPipeline(d,pipelineBegan,handle?nullptr:nmb_last_error(),gJournalPath.empty()?std::string():NBCost::now().since(costBegan));
 if(!handle)return failWith<id<MTLRenderPipelineState>>(error,@"render pipeline",nil);
 NBRenderPipeline *state=[NBRenderPipeline new];state.handle=handle;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
}'''
new_new_render_pipe = '''- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d options:(MTLPipelineOption)options
                                                        reflection:(MTLAutoreleasedRenderPipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(!d){
  if(error)*error=nbError(@"nil render pipeline descriptor");
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 auto *p=new NmbRenderPipelineDescriptor();
 if(!renderDescriptor(d,p,error)){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: renderDescriptor failed for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 if(!p->vertex){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: vertex handle is null for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 if(d.fragmentFunction && !p->fragment){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: fragment handle is null for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error)){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: pipelineArchives failed for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();const NBCost costBegan=NBCost::now();
 NmbRenderPipeline *handle=nmb_render_pipeline_new_with_options(gDevice,p,&creation);delete p;gTimePipeline.add(pipelineBegan);
 journalRenderPipeline(d,pipelineBegan,handle?nullptr:nmb_last_error(),gJournalPath.empty()?std::string():NBCost::now().since(costBegan));
 if(!handle){
  NSLog(@"[BegoniaMetal] WARNING: nmb_render_pipeline_new_with_options failed (%s) for '%@', returning stub pipeline", nmb_last_error(), d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 NBRenderPipeline *state=[NBRenderPipeline new];state.handle=handle;state.label=d.label;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
}'''
assert mtext.count(old_new_render_pipe) == 1
mtext = mtext.replace(old_new_render_pipe, new_new_render_pipe)

old_func_desc = '''- (id<MTLFunction>)newFunctionWithDescriptor:(MTLFunctionDescriptor *)d error:(NSError **)error {
 if(!d.name){if(error)*error=nbError(@"a function name is required");return nil;}
 if(d.constantValues)return [self newFunctionWithName:d.name constantValues:d.constantValues error:error];
 id<MTLFunction> function=[self newFunctionWithName:d.name];
 if(!function){if(error)*error=nbError([NSString stringWithFormat:@"function %@: %@",d.name,lastError()]);return nil;}
 if(error)*error=nil;return function;
}'''
new_func_desc = '''- (id<MTLFunction>)newFunctionWithDescriptor:(MTLFunctionDescriptor *)d error:(NSError **)error {
 if(!d || !d.name){
  NSLog(@"[BegoniaMetal] WARNING: newFunctionWithDescriptor nil descriptor or name, returning stub");
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=@"stub_anonymous";
  stub.functionType=MTLFunctionTypeFragment;if(error)*error=nil;return (id<MTLFunction>)stub;
 }
 id<MTLFunction> function = d.constantValues ?
     [self newFunctionWithName:d.name constantValues:d.constantValues error:error] :
     [self newFunctionWithName:d.name];
 if(!function){
  NSLog(@"[BegoniaMetal] WARNING: newFunctionWithDescriptor for '%@' failed (%@), returning stub", d.name, lastError());
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=d.name;
  stub.functionType=MTLFunctionTypeFragment;if(error)*error=nil;return (id<MTLFunction>)stub;
 }
 if(error)*error=nil;return function;
}'''
assert mtext.count(old_func_desc) == 1
mtext = mtext.replace(old_func_desc, new_func_desc)

old_func_name = '''- (id<MTLFunction>)newFunctionWithName:(NSString *)name {
 const uint64_t functionBegan=nowUs();
 NmbFunction *handle=(name && _handle)?nmb_function_new(_handle,name.UTF8String):nullptr;gTimeFunction.add(functionBegan);
 if(!handle)return nil;
 NBFunction *function=[NBFunction new];function.handle=handle;function.name=name;function.functionType=(MTLFunctionType)nmb_function_type(handle);
 return (id<MTLFunction>)function;
}'''
new_func_name = '''- (id<MTLFunction>)newFunctionWithName:(NSString *)name {
 const uint64_t functionBegan=nowUs();
 NmbFunction *handle=(name && _handle)?nmb_function_new(_handle,name.UTF8String):nullptr;gTimeFunction.add(functionBegan);
 if(!handle){
  NSLog(@"[BegoniaMetal] WARNING: newFunctionWithName for '%@' failed, returning stub NBFunction", name);
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=name?:@"stub_noname";stub.functionType=MTLFunctionTypeFragment;
  return (id<MTLFunction>)stub;
 }
 NBFunction *function=[NBFunction new];function.handle=handle;function.name=name;function.functionType=(MTLFunctionType)nmb_function_type(handle);
 return (id<MTLFunction>)function;
}'''
assert mtext.count(old_func_name) == 1
mtext = mtext.replace(old_func_name, new_func_name)

old_func_const_validate = ''' if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {
  if(error)*error=nbError(@"unqualified function constant values");return nil;
 }'''
new_func_const_validate = ''' if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {
  NSLog(@"[BegoniaMetal] WARNING: unqualified constant values for '%@', returning stub NBFunction", name);
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=name?:@"stub_noname";stub.functionType=MTLFunctionTypeFragment;
  if(error)*error=nil;return (id<MTLFunction>)stub;
 }'''
assert mtext.count(old_func_const_validate) == 1
mtext = mtext.replace(old_func_const_validate, new_func_const_validate)

frontend = mtext
if a.quartzcore:
    implementation = '@implementation NBDeviceMethods\n'
    assert frontend.count(implementation) == 1
    old_pipeline_loader = '''- (id)newPipelineLibraryWithFilePath:(NSString *)path error:(NSError **)error {
 if(error)*error=nbError([NSString stringWithFormat:@"no pipeline library on this GPU: %@",path.lastPathComponent]);
 return nil;
}
'''
    assert frontend.count(old_pipeline_loader) == 1
    frontend = frontend.replace(old_pipeline_loader, '')
    library = '@implementation NBLibrary\n'
    assert frontend.count(library) == 1
    frontend = frontend.replace(library, library + '''
// QuartzCore passes Metal's function cache here. The bridge compiles and
// caches its own Mali shaders, so it cannot reuse cached Apple binaries.
- (id<MTLFunction>)newFunctionWithName:(NSString *)name
                        constantValues:(MTLFunctionConstantValues *)values
                         functionCache:(id)cache
                                 error:(NSError **)error {
 (void)cache;
 id<MTLFunction> function = values ?
     [self newFunctionWithName:name constantValues:values error:error] :
     [self newFunctionWithName:name];
 if (!function) {
  NSLog(@"[BegoniaMetal] WARNING: QuartzCore function '%@' refused (%@), returning stub NBFunction", name, lastError());
  NBFunction *stub = [NBFunction new];
  stub.handle = nullptr;
  stub.name = name;
  stub.functionType = MTLFunctionTypeFragment;
  if (error) *error = nil;
  return (id<MTLFunction>)stub;
 }
 if (error) *error = nil;
 return function;
}
''')
    adapter = (root/'userland/metal/QuartzCorePipelineLibrary.inc').read_text()
    frontend = '#import <objc/message.h>\n' + frontend.replace(implementation, '''
struct BegoniaMTLTargetDeviceArch {
    uint64_t target;
    uint32_t version;
    char *name;
};
static_assert(sizeof(BegoniaMTLTargetDeviceArch) == 24 &&
              offsetof(BegoniaMTLTargetDeviceArch, name) == 16,
              "iOS 16 Metal target descriptor layout");
''' + implementation + adapter + '\n')
mf.write_text(frontend)
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

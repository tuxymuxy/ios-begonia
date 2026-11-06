#!/usr/bin/env python3
"""Stage pinned S9+ native DRM/POSIX/sync transport for begonia's HakiosMali."""
from pathlib import Path
import shutil,subprocess
root=Path(__file__).resolve().parents[1];ref=root/'upstream/ios-s9plus-exynos'
assert subprocess.check_output(['git','-C',str(ref),'rev-parse','HEAD'],text=True).strip()=='2d092398040f98685b0c5a07776df5e9f199b9dc'
out=root/'out/native-vulkan-drm';out.mkdir(exist_ok=True)
native=ref/'metal-wip/native-vulkan';core=ref/'core/tools/mesa/hakios-drm'
for f in native.iterdir():
    if f.suffix in ['.c','.h','.inc']:shutil.copy2(f,out/f.name)
shutil.copy2(ref/'metal-wip/host-sync/hakios.mali_host_sync.h',out/'hakios.mali_host_sync.h')
# Full libdrm ABI types for native device enumeration, plus S9 transport extras.
header=(root/'upstream/viva-gpu/libdrm-xnu/include/xf86drm.h').read_text()
extra=(core/'xf86drm.h').read_text();at=extra.index('void *hakios_drm_map(')
header+='\n#ifndef BEGONIA_DRM_EXTRAS_H\n#define BEGONIA_DRM_EXTRAS_H\n'+extra[at:extra.rfind('#endif')]+'\n#endif\n'
(out/'xf86drm.h').write_text(header)
io=(root/'upstream/viva-gpu/vgpu/iokit_compat.h').read_text()
io=io.replace('#if __has_include(<IOKit/IOKitLib.h>)','#if 0')
io=io.replace('#define iokit_common_err(r)    (0xe0000000 | (r))',
              '#define iokit_common_err(r)    ((kern_return_t)(0xe0000000u | (r)))')
(out/'IOKit').mkdir(exist_ok=True);(out/'IOKit/IOKitLib.h').write_text(io)
text=(core/'hakios.drm.c').read_text()
text=text.replace('drmCommandWrite(int fd, unsigned long index, const void *data, unsigned long size)',
                  'drmCommandWrite(int fd, unsigned long index, void *data, unsigned long size)')
text=text[:text.index('/* ---- sync objects')]
text=text.replace('static uint64_t *syncobj(uint32_t handle);','')
text=text.replace('static uint64_t submitted;              /* the last job number the kernel gave (to this process) */','')
text=text.replace('#include "xf86drm.h"','#include "xf86drm.h"\n#include "hakios-native-sync.h"\n#include "hakios-panfrost-submit-wire.h"\n#include "hakios-native-surface-fd.h"',1)
start=text.index('static int\nsubmit(struct drm_panfrost_submit *request)');end=text.index('\n/*',start+10)
text=text[:start]+'''static int
submit(struct drm_panfrost_submit *request)
{
 struct hakios_panfrost_submit_v1 wire;
 int error=hakios_submit_pack(request,&wire);
 if(error)return error;
 void *cookie=NULL;
 error=hakios_native_sync_prepare((const uint32_t *)(uintptr_t)request->in_syncs,
     request->in_sync_count,request->out_sync,&cookie);
 if(error)return error;
 wire.in_syncs=0;wire.in_sync_count=0;wire.out_sync=0;
 error=call(DRM_COMMAND_BASE+DRM_PANFROST_SUBMIT,&wire,sizeof wire);
 if(!error && !wire.jc)error=EIO;
 hakios_native_sync_publish(cookie,error,error?0:wire.jc);
 return error;
}
'''+text[end:]
text=text.replace('static int wait_buffer(struct drm_panfrost_wait_bo *request);','''static int wait_buffer(struct drm_panfrost_wait_bo *request);
static int wait_job(uint64_t seq,int64_t deadline);''',1)
marker='drmIoctl(int fd, unsigned long request, void *arg)\n{';assert text.count(marker)==1
text=text.replace(marker,marker+'''\n int handled=0;
 hakios_native_sync_set_wait(wait_job);
 int native=hakios_native_sync_ioctl(fd,request,arg,&handled);
 if(handled)return native;''',1)
start=text.index('int\ndrmPrimeFDToHandle(');end=text.index('\n/*',start+5)
text=text[:start]+'''int
drmPrimeFDToHandle(int fd,int prime_fd,uint32_t *handle)
{
 (void)fd;uint32_t surface=0;uint64_t size=0;
 int error=hakios_native_surface_fd_lookup(prime_fd,&surface,&size);
 if(error){errno=error;return -1;}
 struct hakios_drm_surface request={.surface=surface};
 error=call(HAKIOS_GPU_SURFACE,&request,sizeof request);
 if(error){errno=error;return -1;}
 if(request.size<size){errno=EINVAL;return -1;}
 error=hakios_native_surface_fd_bind(prime_fd,request.handle);
 if(error){errno=error;return -1;}
 *handle=request.handle;return 0;
}
'''+text[end:]
needle='\tstruct drm_gem_close request = { .handle = handle };';assert text.count(needle)==1
text=text.replace(needle,needle+'\n\thakios_native_surface_fd_forget(handle);',1)
text+='\n#include "hakios-native-map.inc"\n#include "hakios-native-surface-cache.inc"\n'
text+='''
int drmPrimeHandleToFD(int fd,uint32_t handle,uint32_t flags,int *prime_fd) {
 (void)fd;(void)handle;(void)flags;(void)prime_fd;errno=ENOSYS;return -1;
}
'''
(out/'hakios.drm.c').write_text(text)
print('Staged native HakiosMali transport, real binary fences and checked16KiB mappings')

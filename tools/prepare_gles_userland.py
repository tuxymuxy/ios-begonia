#!/usr/bin/env python3
"""Prepare pinned hardware GLES source separately from native PanVK; no install."""
from pathlib import Path
import hashlib,json,shutil,subprocess,tarfile
root=Path(__file__).resolve().parents[1];ref=root/'upstream/ios-s9plus-exynos';mesa=root/'upstream/mesa-gles-25/mesa-25.0.7'
assert subprocess.check_output(['git','-C',str(ref),'rev-parse','HEAD'],text=True).strip()=='2d092398040f98685b0c5a07776df5e9f199b9dc'
archive=root/'out/mesa-25.0.7.tar.xz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6'
if not mesa.exists():
 mesa.parent.mkdir(exist_ok=True)
 with tarfile.open(archive) as source:source.extractall(mesa.parent,filter='data')
names=('freedreno-darwin.patch','osmesa-es-profile.patch','panfrost-darwin.patch')
checksums={name:hashlib.sha256((ref/'core/tools/mesa'/name).read_bytes()).hexdigest() for name in names}
marker=mesa/'.begonia-gles-patches.json'
if marker.exists() and json.loads(marker.read_text())!=checksums:raise SystemExit('GLES reference patches changed; use a pristine extracted tree')
for name in (() if marker.exists() else names):
 patch=ref/'core/tools/mesa'/name
 if subprocess.run(['patch','--dry-run','--reverse','--force','-p1','-i',str(patch)],cwd=mesa,capture_output=True).returncode==0:continue
 subprocess.run(['patch','--dry-run','--force','-p1','-i',str(patch)],cwd=mesa,check=True,capture_output=True)
 subprocess.run(['patch','--force','-p1','-i',str(patch)],cwd=mesa,check=True,capture_output=True)
marker.write_text(json.dumps(checksums,indent=2)+'\n')
shim=mesa/'src/hakios-drm';shim.mkdir(exist_ok=True)
# This pinned Mesa uses the original forty-byte submit ABI. Keep it separate
# from the new forty-eight-byte PanVK transport rather than interpreting one as another.
text=(ref/'core/tools/mesa/hakios-drm/hakios.drm.c').read_text()
text=text[:text.index('/* ---- sync objects')]
text=text.replace('static uint64_t *syncobj(uint32_t handle);','').replace('static uint64_t submitted;              /* the last job number the kernel gave (to this process) */','')
text=text.replace('#include "xf86drm.h"','#include "xf86drm.h"\n#include "hakios-native-sync.h"',1)
text=text.replace('static int wait_buffer(struct drm_panfrost_wait_bo *request);','static int wait_buffer(struct drm_panfrost_wait_bo *request);\nstatic int wait_job(uint64_t seq,int64_t deadline);',1)
marker='drmIoctl(int fd, unsigned long request, void *arg)\n{';assert text.count(marker)==1
text=text.replace(marker,marker+'''\n int handled=0;hakios_native_sync_set_wait(wait_job);
 int native=hakios_native_sync_ioctl(fd,request,arg,&handled);if(handled)return native;''',1)
start=text.index('static int\nsubmit(struct drm_panfrost_submit *request)');end=text.index('\n/*',start+10)
text=text[:start]+'''static int
submit(struct drm_panfrost_submit *request)
{
 _Static_assert(sizeof(*request)==40,"legacy Panfrost submit ABI");
 struct drm_panfrost_submit wire=*request;void *cookie=NULL;
 int error=hakios_native_sync_prepare((const uint32_t *)(uintptr_t)request->in_syncs,
     request->in_sync_count,request->out_sync,&cookie);if(error)return error;
 wire.in_syncs=0;wire.in_sync_count=0;wire.out_sync=0;
 error=call(DRM_COMMAND_BASE+DRM_PANFROST_SUBMIT,&wire,sizeof wire);
 if(!error && !wire.jc)error=EIO;
 hakios_native_sync_publish(cookie,error,error?0:wire.jc);return error;
}
'''+text[end:]
text+='''
int drmPrimeHandleToFD(int fd,uint32_t handle,uint32_t flags,int *prime_fd)
{ (void)fd;(void)handle;(void)flags;(void)prime_fd;errno=ENOSYS;return -1; }
'''
(shim/'hakios.drm.c').write_text(text)
# Keep the reference's IOSurface-ID import ABI for the existing EAGL adapter.
# Native PanVK's real descriptor registry is intentionally not substituted here.
header=(ref/'core/tools/mesa/hakios-drm/xf86drm.h').read_text()
header=header[:header.rfind('#endif')]+'''
int drmSyncobjSignal(int fd,const uint32_t *handles,uint32_t count);
int drmSyncobjTimelineSignal(int fd,const uint32_t *handles,uint64_t *points,uint32_t count);
int drmSyncobjQuery(int fd,uint32_t *handles,uint64_t *points,uint32_t count);
int drmSyncobjQuery2(int fd,uint32_t *handles,uint64_t *points,uint32_t count,uint32_t flags);
#endif
'''
(shim/'xf86drm.h').write_text(header)
for name in ('hakios-native-sync.c','hakios-native-sync.h'):
 shutil.copy2(ref/'metal-wip/native-vulkan'/name,shim/name)
f=mesa/'src/panfrost/lib/kmod/meson.build';s=f.read_text();old="files('../../../hakios-drm/hakios.drm.c')";new="files('../../../hakios-drm/hakios.drm.c', '../../../hakios-drm/hakios-native-sync.c')"
if old in s:s=s.replace(old,new);f.write_text(s)
print('Prepared separate Mesa25 hardware GLES with native binary fences; not built or installed')

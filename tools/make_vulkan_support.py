#!/usr/bin/env python3
"""Stage the native Metal/Vulkan diagnostic without enabling the UI frontend."""
from pathlib import Path
import argparse,plistlib,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base-root',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--test',type=Path,required=True);p.add_argument('--service-test',type=Path);p.add_argument('--surface-test',type=Path);p.add_argument('--vulkan-test',type=Path);p.add_argument('--isolated',action='store_true');a=p.parse_args()
base=a.base_root.resolve();out=a.output_dir.resolve();dest=out/'root'
if base==dest or base in dest.parents or dest in base.parents:p.error('base/output trees overlap')
if dest.exists():p.error('output tree already exists; use a fresh version directory')
shutil.copytree(base,dest,symlinks=True)
for source,name in [(root/'out/panvk-ios-build/src/panfrost/vulkan/libvulkan_panfrost.dylib','libvulkan_panfrost.dylib'),(root/'out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib','libmetal2vulkan.dylib')]:
 target=dest/'lib'/name;shutil.copy2(source,target)
 subprocess.run(['xcrun','strip','-S','-x',str(target)],check=True)
 subprocess.run(['codesign','-s','-','-f',str(target)],check=True)
 subprocess.run(['codesign','--verify','--strict',str(target)],check=True)
shutil.copy2(a.test,dest/'bin/metal_vulkan_compute_test')
if a.service_test:shutil.copy2(a.service_test,dest/'bin/gpu_service_test')
for binary,name in ((a.surface_test,'gpu_surface_test'),(a.vulkan_test,'vulkan_device_test')):
 if binary:shutil.copy2(binary,dest/'bin'/name)
(dest/'fw').mkdir(exist_ok=True);shutil.copy2(root/'out/begonia-own-compute.air',dest/'fw/begonia-own-compute.air')
keep={'gpu_service_test','gpu_surface_test','vulkan_device_test','metal_vulkan_compute_test','vol_ls','var_store','hello_dyn','svc_test','surf_test','rwtouch'}
for f in (dest/'bin').iterdir():
 if f.name not in keep:f.unlink()
jobs=dest/'etc/rc.jobs';lines=[l for l in jobs.read_text().splitlines(True) if not any(name in l for name in ('/redwood/bin/metal_vulkan_compute_test','/redwood/bin/gpu_service_test'))]
checks=['gpu_service_test']
if a.surface_test:checks+=['gpu_surface_test']
if a.vulkan_test:checks+=['vulkan_device_test']
checks+=['metal_vulkan_compute_test']
jobs.write_text(''.join('/redwood/bin/'+name+'\n' for name in checks)+('' if a.isolated else ''.join(lines)))
if a.isolated:
 # Dedicated hardware qualification, not a GUI image. Avoid competing startup
 # work and preserve the full test output in the bounded kernel capture.
 keep_jobs={'com.hakios.demo.plist','com.apple.notifyd.plist','com.apple.cfprefsd.xpc.daemon.plist'}
 for f in (dest/'LaunchDaemons').glob('*.plist'):
  if f.name not in keep_jobs:f.unlink()
  else:
   job=plistlib.loads(f.read_bytes());job.get('EnvironmentVariables',{}).pop('OS_ACTIVITY_DT_MODE',None)
   f.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))
subprocess.run([sys.executable,str(root/'upstream/hakios-xnu-kernel/tools/apfs/mkapfs.py'),'-z','--slack','128','--plain','fw','-o',str(out/'root.apfs'),'redwood='+str(dest)],check=True)
print('Diagnostic support built. Payload builder must check loader memory bounds before installation.')

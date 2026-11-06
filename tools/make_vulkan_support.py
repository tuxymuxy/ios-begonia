#!/usr/bin/env python3
"""Stage the native Metal/Vulkan diagnostic without enabling the UI frontend."""
from pathlib import Path
import argparse,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base-root',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--test',type=Path,required=True);a=p.parse_args()
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
(dest/'fw').mkdir(exist_ok=True);shutil.copy2(root/'out/begonia-own-compute.air',dest/'fw/begonia-own-compute.air')
keep={'gpu_service_test','metal_vulkan_compute_test','vol_ls','var_store','hello_dyn','svc_test','surf_test','rwtouch'}
for f in (dest/'bin').iterdir():
 if f.name not in keep:f.unlink()
jobs=dest/'etc/rc.jobs';lines=[l for l in jobs.read_text().splitlines(True) if '/redwood/bin/metal_vulkan_compute_test' not in l]
jobs.write_text('/redwood/bin/metal_vulkan_compute_test\n'+''.join(lines))
subprocess.run([sys.executable,str(root/'upstream/hakios-xnu-kernel/tools/apfs/mkapfs.py'),'-z','--slack','128','--plain','fw','-o',str(out/'root.apfs'),'redwood='+str(dest)],check=True)
print('Diagnostic support built. Payload builder must check loader memory bounds before installation.')

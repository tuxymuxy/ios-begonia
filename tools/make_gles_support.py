#!/usr/bin/env python3
"""Stage a private hardware GLES render test or opt-in EAGL compositor image."""
from pathlib import Path
import argparse,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base-root',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--ui',action='store_true');a=p.parse_args()
base=a.base_root.resolve();out=a.output_dir.resolve();dest=out/'root'
if base==dest or base in dest.parents or dest in base.parents or dest.exists():p.error('use a fresh separate output directory')
shutil.copytree(base,dest,symlinks=True)
stage=root/'out/gles-ios'
for name in ('libvulkan_panfrost.dylib','libmetal2vulkan.dylib'):
 (dest/'lib'/name).unlink(missing_ok=True)
shutil.copy2(stage/'libOSMesa.8.dylib',dest/'lib/libOSMesa.8.dylib')
shutil.copy2(stage/'gles_render_test',dest/'bin/gles_render_test')
if a.ui:
 shutil.copy2(stage/'libhakios.gl.dylib',dest/'lib/libhakios.gl.dylib')
 jobs='/redwood/bin/gles_render_test\nsleep 60\n/bin/ps -A\n'
else:
 keep={'com.hakios.demo.plist','com.apple.notifyd.plist','com.apple.cfprefsd.xpc.daemon.plist'}
 for f in (dest/'LaunchDaemons').glob('*.plist'):
  if f.name not in keep:f.unlink()
 jobs='/redwood/bin/gles_render_test\n'
(dest/'etc/rc.jobs').write_text(jobs)
subprocess.run([sys.executable,str(root/'upstream/hakios-xnu-kernel/tools/apfs/mkapfs.py'),'-z','--plain','fw','-o',str(out/'root.apfs'),'redwood='+str(dest)],check=True)
print('Built private GLES support; no system image or phone writes')

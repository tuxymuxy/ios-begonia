#!/usr/bin/env python3
"""Build the pinned Metal Objective-C frontend separately; never install/enable it."""
from pathlib import Path
import argparse,shutil,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);a=p.parse_args()
ref=root/'upstream/ios-s9plus-exynos';out=root/'out/metal-frontend-ios';out.mkdir(exist_ok=True)
assert subprocess.check_output(['git','-C',str(ref),'rev-parse','HEAD'],text=True).strip()=='2d092398040f98685b0c5a07776df5e9f199b9dc'
source=out/'metalvk';source.mkdir(exist_ok=True)
for f in (ref/'metal-wip/frontend').iterdir():
 if f.is_file():shutil.copy2(f,source/f.name)
for name in ('hakios_metal_shown.h','hakios_metal_shown.c'):
 shutil.copy2(ref/'core/userland/src'/name,out/name)
f=source/'HakiosMetalVK.mm';text=f.read_text();old='#define DEFAULT_ICD "/redwood/lib/libhakios.vk.dylib"'
assert text.count(old)==1;f.write_text(text.replace(old,'#define DEFAULT_ICD "/redwood/lib/libvulkan_panfrost.dylib"'))
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
  '-framework','Foundation','-framework','Metal','-framework','IOSurface','-framework','IOKit','-framework','CoreFoundation','-lobjc'],stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run(['codesign','-s','-','-f',str(binary)],check=True)
subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
print('Built signed Metal frontend; default process gate retained; not installed')

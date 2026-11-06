#!/usr/bin/env python3
"""Build/sign authored native GPU/Vulkan diagnostics; no phone access."""
from pathlib import Path
import argparse,subprocess
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output-dir',type=Path,default=root/'out/native-gpu-tests');a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
base=['xcrun','clang','-target','arm64-apple-ios16.5','-isysroot',str(a.sdk.resolve()),'-O1','-Wall','-Wextra','-Werror']
cases=[('gpu_service_test.c',['-I',str(root/'upstream/viva-gpu/vgpu'),'-framework','IOKit','-framework','CoreFoundation']),
 ('gpu_surface_test.m',['-fobjc-arc','-I',str(root/'upstream/viva-gpu/vgpu'),'-framework','Foundation','-framework','IOSurface','-framework','IOKit']),
 ('vulkan_device_test.c',['-I',str(root/'upstream/mesa-vulkan/include')]),
 ('metal_vulkan_compute_test.c',['-I',str(root/'out/metal2vulkan-bridge'),str(root/'out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib'),'-Wl,-rpath,@executable_path/../lib'])]
for name,flags in cases:
 target=out/Path(name).stem
 subprocess.run([*base,str(root/'tests'/name),*flags,'-o',str(target)],check=True)
 subprocess.run(['codesign','-s','-','-f',str(target)],check=True)
 subprocess.run(['codesign','--verify','--strict',str(target)],check=True)
 build=subprocess.check_output(['xcrun','vtool','-show-build',str(target)],text=True)
 if 'platform IOS' not in build:raise SystemExit('Wrong platform: '+str(target))
print('Built/signed four native iOS diagnostics; no hardware result implied')

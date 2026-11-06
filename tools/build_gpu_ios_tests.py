#!/usr/bin/env python3
"""Cross-build GPU connection/Metal discovery tools; never install or launch."""
from pathlib import Path
import argparse,subprocess
root=Path(__file__).resolve().parents[1];repo=root/'upstream/viva-gpu'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk',type=Path,required=True);a=p.parse_args()
sdk=a.sdk.resolve();assert (sdk/'System/Library/Frameworks/Metal.framework').is_dir()
out=root/'out/gpu-ios-tests';out.mkdir(exist_ok=True)
clang=subprocess.check_output(['xcrun','-f','clang'],text=True).strip()
base=[clang,'-target','arm64-apple-ios16.5','-isysroot',str(sdk),'-O2','-g']
sources=[('test_vgpu',[repo/'vgpu/test_vgpu.c',repo/'vgpu/vgpu.c',repo/'vgpu/vgpu_mock.c',repo/'vgpu/vgpu_iokit.c'],
          ['-framework','IOKit','-framework','IOSurface','-framework','CoreFoundation']),
         ('vmprobe',[repo/'ios/vmprobe.m'],['-fobjc-arc','-framework','Foundation','-framework','Metal'])]
for name,files,flags in sources:
    binary=out/name
    r=subprocess.run([*base,*map(str,files),*flags,'-o',str(binary)],capture_output=True,text=True)
    (out/(name+'-build.log')).write_text(r.stdout+r.stderr);r.check_returncode()
    subprocess.run(['codesign','-s','-','-f',str(binary)],check=True)
    subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
    print('Built/signed iOS16.5 '+name)

#!/usr/bin/env python3
"""Build native iOS PanVK with the pinned, checked HakiosMali transport."""
from pathlib import Path
import argparse,os,subprocess,sys
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk',type=Path,required=True);a=p.parse_args();sdk=a.sdk.resolve()
mesa=root/'upstream/mesa-vulkan';viva=root/'upstream/viva-gpu';out=root/'out/panvk-ios-build'
head=subprocess.check_output(['git','-C',str(mesa),'rev-parse','HEAD'],text=True).strip()
if head!='8e87a503ebd91408c88596d1b9eeb5f4c0b2088d':raise SystemExit('Mesa revision mismatch')
for patch in sorted((root/'userland/vulkan/patches').glob('*.patch')):
    cmd=['git','-C',str(mesa),'apply']
    if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode==0:continue
    subprocess.run([*cmd,'--check',str(patch)],check=True);subprocess.run([*cmd,str(patch)],check=True)
subprocess.run([sys.executable,str(root/'tools/build_native_vulkan_drm.py'),'--sdk',str(sdk)],check=True)
native=root/'out/native-vulkan-drm'
base=['-arch','arm64','-isysroot',str(sdk),'-mios-version-min=16.5','-target','arm64-apple-ios16.5']
flags=[*base,'-g0','-I'+str(root/'userland/vulkan/compat'),'-include',str(native/'hakios-native-posix.h')]
paths=[viva/'.venv/bin',viva/'mesa/build-macos/src/compiler/clc',viva/'mesa/build-macos/src/compiler/spirv',viva/'mesa/build-macos/src/panfrost/clc']
env=dict(os.environ,PATH=':'.join(map(str,paths))+':'+os.environ['PATH'])
cmd=[str(viva/'.venv/bin/meson'),'setup']
if (out/'meson-private/coredata.dat').exists():cmd+=['--reconfigure','--clearcache']
cmd += [str(out),str(mesa),'--cross-file',str(viva/'ios/out/ios-arm64.ini'),
        '-Dc_args='+repr(flags),'-Dcpp_args='+repr(flags),'-Dc_link_args='+repr(base),'-Dcpp_link_args='+repr(base),
        '-Dpkg_config_path='+str(native/'pkgconfig'),'-Dgallium-drivers=','-Dvulkan-drivers=panfrost',
        '-Dllvm=disabled','-Dplatforms=','-Dopengl=false','-Dgles1=disabled','-Dgles2=disabled',
        '-Degl=disabled','-Dglx=disabled','-Dgbm=disabled','-Dmesa-clc=system','-Dprecomp-compiler=system',
        '-Dshader-cache=disabled','-Dzstd=disabled','-Dexpat=disabled']
with (root/'out/panvk-ios-configure.log').open('w') as log:
    subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
with (root/'out/panvk-ios-build.log').open('w') as log:
    subprocess.run([str(viva/'.venv/bin/ninja'),'-C',str(out),
                    'src/panfrost/vulkan/libvulkan_panfrost.dylib'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
binary=out/'src/panfrost/vulkan/libvulkan_panfrost.dylib'
symbols=subprocess.check_output(['nm','-m','-u',str(binary)],text=True)
(root/'out/panvk-ios-undefined-symbol-audit.txt').write_text(symbols)
required=[line for line in symbols.splitlines() if 'dynamically looked up' in line and 'weak external' not in line]
if required:raise SystemExit('Required unresolved runtime symbols:\n'+'\n'.join(required))
build=subprocess.check_output(['xcrun','vtool','-show-build',str(binary)],text=True)
if 'platform IOS' not in build:raise SystemExit('PanVK is not an iOS binary')
subprocess.run(['codesign','-s','-','-f',str(binary)],check=True)
subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
print('Built/signed native iOS PanVK; hardware execution still requires a device test')

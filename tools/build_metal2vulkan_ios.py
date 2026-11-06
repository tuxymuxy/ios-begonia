#!/usr/bin/env python3
"""Build a separate iOS Metal-to-Vulkan runtime from pinned references; no install."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);a=p.parse_args()
subprocess.run([sys.executable,str(root/'tools/prepare_vulkan_userland.py')],check=True)
sdk=a.sdk.resolve();assert (sdk/'SDKSettings.plist').exists()
bridge=root/'out/metal2vulkan-bridge';bridge.mkdir(exist_ok=True)
source=root/'upstream/ios-s9plus-exynos/core/tools/metal/bridge'
for f in source.iterdir():
    if f.is_file():shutil.copy2(f,bridge/f.name)
manifest=bridge/'Cargo.toml'
manifest.write_text(manifest.read_text().replace('../src/crates/metal2vulkan','../../upstream/metal2vulkan/crates/metal2vulkan'))
# SDKROOT is needed by Rust's iOS target. Build scripts/proc macros are host
# executables; prevent that setting from linking them against iOS libSystem.
macsdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
clang=subprocess.check_output(['xcrun','-f','clang'],text=True).strip()
linker=root/'out/metal2vulkan-host-linker.py'
linker.write_text('#!'+sys.executable+'\n'+
    'import os,sys\nargs=[];it=iter(sys.argv[1:])\n'+
    'for arg in it:\n'+
    ' if arg=="-isysroot":next(it);continue\n'+
    ' if arg.startswith("--sysroot="):continue\n'+
    ' args.append(arg)\n'+
    f'env=dict(os.environ,SDKROOT={macsdk!r})\n'+
    f'os.execve({clang!r},[{clang!r},"-isysroot",{macsdk!r},*args],env)\n')
linker.chmod(0o755)
env=dict(os.environ,RUSTUP_HOME=str(root/'.tools/rust/rustup'),CARGO_HOME=str(root/'.tools/rust/cargo'),
         CARGO_TARGET_DIR=str(root/'out/metal2vulkan-target-ios'),SDKROOT=str(sdk),
         IPHONEOS_DEPLOYMENT_TARGET='16.5',CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER=str(linker),
         RUSTFLAGS='-Dwarnings',M2V_INSTALL_NAME='@rpath/libmetal2vulkan.dylib',
         M2V_RUNTIME_ID='begonia-720ef1ce-s9-2d092398-dba02deb')
command=[str(root/'.tools/rust/cargo/bin/cargo'),'build','--manifest-path',str(manifest),
         '--target','aarch64-apple-ios','--release']
with (root/'out/metal2vulkan-ios-bridge-build.log').open('w') as log:
    subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
binary=root/'out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib'
subprocess.run(['codesign','-s','-','-f',str(binary)],check=True)
subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
build=subprocess.check_output(['xcrun','vtool','-show-build',str(binary)],text=True)
if 'platform IOS' not in build:raise SystemExit('Runtime is not an iOS binary')
print('Built/signed separate iOS Metal-to-Vulkan runtime; native ICD integration still required')

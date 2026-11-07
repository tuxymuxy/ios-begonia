#!/usr/bin/env python3
"""Build the pinned Panfrost GLES compositor library separately; never install."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--frontend',action='store_true');a=p.parse_args()
subprocess.run([sys.executable,str(root/'tools/prepare_gles_userland.py')],check=True)
mesa=root/'upstream/mesa-gles-25/mesa-25.0.7';out=root/'out/gles-ios-build';viva=root/'upstream/viva-gpu'
config=root/'out/gles-ios-arm64.ini';sdk=str(a.sdk.resolve())
config.write_text(f'''[binaries]
c = ['xcrun', 'clang']
cpp = ['xcrun', 'clang++']
objc = ['xcrun', 'clang']
ar = ['xcrun', 'ar']
strip = ['xcrun', 'strip']
pkg-config = 'false'
cmake = 'false'
[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
[properties]
needs_exe_wrapper = true
''')
flags=['-target','arm64-apple-ios16.5','-isysroot',sdk,'-I'+str(root/'userland/vulkan/compat')]
env=dict(os.environ,PATH=':'.join(map(str,[viva/'.venv/bin',viva/'mesa/build-macos/src/compiler/clc',viva/'mesa/build-macos/src/compiler/spirv',viva/'mesa/build-macos/src/panfrost/clc']))+':'+os.environ['PATH'])
cmd=[str(viva/'.venv/bin/meson'),'setup']
if (out/'meson-private/coredata.dat').exists():cmd+=['--reconfigure','--clearcache']
cmd += [str(out),str(mesa),'--cross-file',str(config),'--wrap-mode=nofallback','--buildtype=release','-Db_ndebug=true',
 '-Dc_args='+repr(flags),'-Dcpp_args='+repr([*flags,'-fuse-cxa-atexit']),'-Dobjc_args='+repr(flags),'-Dc_link_args='+repr(flags),'-Dcpp_link_args='+repr(flags),
 '-Dplatforms=','-Dgallium-drivers=panfrost,softpipe','-Dvulkan-drivers=','-Dglx=disabled','-Degl=disabled','-Dgbm=disabled',
 '-Dosmesa=true','-Dllvm=disabled','-Dshared-glapi=enabled','-Dgles1=disabled','-Dgles2=enabled','-Dopengl=true',
 '-Dzstd=disabled','-Dxmlconfig=disabled','-Dexpat=disabled','-Dshader-cache=disabled','-Dvalgrind=disabled',
 '-Dlibunwind=disabled','-Dglvnd=disabled','-Dvideo-codecs=','-Dtools=','-Dgallium-vdpau=disabled','-Dgallium-va=disabled',
 '-Dgallium-xa=disabled','-Dlmsensors=disabled','-Dbuild-tests=false','-Dmesa-clc=auto','-Dprecomp-compiler=system']
with (root/'out/gles-ios-configure.log').open('w') as log:subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
with (root/'out/gles-ios-build.log').open('w') as log:subprocess.run([str(viva/'.venv/bin/ninja'),'-j8','-C',str(out),'src/gallium/targets/osmesa/libOSMesa.8.dylib'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
stage=root/'out/gles-ios';stage.mkdir(exist_ok=True)
lib=stage/'libOSMesa.8.dylib';shutil.copy2(out/'src/gallium/targets/osmesa/libOSMesa.8.dylib',lib)
subprocess.run(['xcrun','strip','-S','-x',str(lib)],check=True)
subprocess.run(['install_name_tool','-id','/redwood/lib/libOSMesa.8.dylib',str(lib)],check=True)
subprocess.run(['codesign','-s','-','-f',str(lib)],check=True)
subprocess.run(['codesign','--verify','--strict',str(lib)],check=True)
build=subprocess.check_output(['xcrun','vtool','-show-build',str(lib)],text=True)
if 'platform IOS' not in build:raise SystemExit('OSMesa is not an iOS binary')
test=stage/'gles_render_test'
subprocess.run(['xcrun','clang',*flags,'-O1','-Wall','-Wextra','-Werror','-I'+str(mesa/'include'),str(root/'tests/gles_render_test.c'),'-o',str(test)],check=True)
subprocess.run(['codesign','-s','-','-f',str(test)],check=True)
subprocess.run(['codesign','--verify','--strict',str(test)],check=True)
if a.frontend:
 userland=root/'upstream/hakios-xnu-kernel/userland';src=userland/'src'
 symbols=subprocess.check_output(['nm','-gjU',str(lib)],text=True)
 exports=stage/'exported-gl.txt';exports.write_text(''.join(s[1:]+'\n' for s in symbols.splitlines() if s.startswith('_gl')))
 with (stage/'eagl_gen.h').open('w') as generated:
  subprocess.run([sys.executable,str(userland/'mkeagl.py'),str(src/'eagl_slots.txt'),str(mesa/'src/mapi/glapi/registry/gl.xml'),str(exports),str(src/'eagl.c')],stdout=generated,check=True)
 frontend=stage/'libhakios.gl.dylib'
 subprocess.run(['xcrun','clang',*flags,'-O2','-dynamiclib','-install_name','/redwood/lib/libhakios.gl.dylib','-I'+str(stage),
  '-framework','CoreFoundation','-framework','IOKit','-framework','IOSurface','-lobjc',str(lib),'-o',str(frontend),str(src/'eagl.c'),str(src/'gl_osmesa.c')],check=True)
 subprocess.run(['codesign','-s','-','-f',str(frontend)],check=True)
 subprocess.run(['codesign','--verify','--strict',str(frontend)],check=True)
print('Built/signed GLES library and authored render test; hardware execution not yet qualified')

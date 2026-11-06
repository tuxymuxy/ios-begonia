#!/usr/bin/env python3
"""Build the native PanVK transport for iOS, never using a macOS archive."""
from pathlib import Path
import argparse,subprocess,sys
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);a=p.parse_args()
subprocess.run([sys.executable,str(root/'tools/prepare_native_vulkan_drm.py')],check=True)
out=root/'out/native-vulkan-drm';mesa=root/'upstream/mesa-vulkan'
flags=['clang','-target','arm64-apple-ios16.5','-isysroot',str(a.sdk.resolve()),'-O2','-Wall','-Wextra','-Werror',
       '-I'+str(out),'-I'+str(mesa/'include'),'-I'+str(root/'upstream/viva-gpu/libdrm-xnu/include'),
       '-I'+str(root/'upstream/viva-gpu/libdrm-xnu/include/libdrm')]
objects=[]
for name in ['hakios.drm','hakios-native-posix','hakios-native-sync','hakios-native-surface-fd']:
    obj=out/(name+'.o')
    r=subprocess.run([*flags,'-c',str(out/(name+'.c')),'-o',str(obj)],capture_output=True,text=True)
    (out/(name+'-build.log')).write_text(r.stdout+r.stderr)
    if r.returncode:raise SystemExit(r.stderr)
    objects.append(str(obj))
subprocess.run(['ar','rcs',str(out/'libdrm-begonia-ios.a'),*objects],check=True)
pc=out/'pkgconfig';pc.mkdir(exist_ok=True)
(pc/'libdrm.pc').write_text(f'''prefix={out}
Name: libdrm
Description: Begonia native HakiosMali transport
Version: 2.4.134
Libs: {out}/libdrm-begonia-ios.a -framework IOKit -framework CoreFoundation
Cflags: -I{out} -I{root}/upstream/viva-gpu/libdrm-xnu/include -I{root}/upstream/viva-gpu/libdrm-xnu/include/libdrm
''')
print('Built native iOS DRM/POSIX/sync/surface transport')

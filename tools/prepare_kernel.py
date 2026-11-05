#!/usr/bin/env python3
"""Apply the begonia additions to the pinned, already-prepared HakiOS XNU tree.
Run xnu/apply.sh and xnu-deps/setup.sh in the common checkout first.
"""
import pathlib,subprocess,shutil
root=pathlib.Path(__file__).resolve().parents[1];repo=root/'upstream/hakios-xnu-kernel';tree=repo/'xnu-src/xnu-8796.121.2'
if not(tree/'bsd/conf/files').exists():raise SystemExit('Prepare the pinned common XNU source first (docs/PROCESS.md).')
for patch in sorted((root/'patches').glob('*.patch')):
    cmd=['git','-C',str(tree),'apply']
    if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode==0:
        print('Already applied:',patch.name);continue
    subprocess.run([*cmd,'--check',str(patch)],check=True)
    subprocess.run([*cmd,str(patch)],check=True);print('Applied:',patch.name)
# Keep the imported driver's canonical copy in sync; upstream build may recopy it.
for name in ['0008-framebuffer-release-text-console.patch']:
    patch=root/'patches'/name
    cmd=['git','-C',str(repo),'apply','--directory=xnu/files']
    if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode!=0:
        subprocess.run([*cmd,'--check',str(patch)],check=True)
        subprocess.run([*cmd,str(patch)],check=True)
for name in ['begonia_ufs.c','begonia_touch.c','begonia_touch_report.h']:
    shutil.copyfile(root/'kernel/bsd/dev'/name,tree/'bsd/dev'/name)
shutil.copyfile(root/'devices/begonia/configs/begonia_defconfig',repo/'configs/begonia_defconfig')
print('Begonia root driver and build profile copied. Build with make begonia_defconfig, then make kernel.')

shutil.copyfile(tree/'bsd/dev/hakios.input.c',repo/'drivers/input/hakios.input.c')

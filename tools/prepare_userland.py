#!/usr/bin/env python3
"""Apply the recorded support-init fixes and build init without rebuilding Apple assets."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
repo=root/'upstream/hakios-xnu-kernel'
for patch in sorted((root/'userland/patches').glob('*.patch')):
 cmd=['git','-C',str(repo),'apply']
 if subprocess.run([*cmd,'--reverse','--check',str(patch)],capture_output=True).returncode==0:continue
 subprocess.run([*cmd,'--check',str(patch)],check=True)
 subprocess.run([*cmd,str(patch)],check=True)
script='''set -e
source "$1/xnu-env.sh"
ccflags=(-arch arm64 -Os -static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra)
xnu-clang "${ccflags[@]}" -Ilibmini -c libmini/mini.c -o obj/mini.o
xnu-clang "${ccflags[@]}" -c libmini/start.S -o obj/start.o
xnu-clang "${ccflags[@]}" -Ilibmini -Wl,-e,_start -Wl,-pagezero_size,0x100000000 -o obj/init src/init.c obj/mini.o obj/start.o
cp obj/init root/sbin/init
'''
subprocess.run(['bash','-c',script,'build-init',str(repo)],cwd=repo/'userland',check=True)
print('Built support init with recorded HFS defaults; persistent system groups are preserved.')

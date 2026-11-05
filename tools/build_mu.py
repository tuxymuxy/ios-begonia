#!/usr/bin/env python3
"""Build begonia Mu with source-built BaseTools on this Mac; never flash."""
from pathlib import Path
import os,shutil,subprocess,sys,shlex
root=Path(__file__).resolve().parents[1]; repo=root/'upstream/Mu-Silicium'
subprocess.run([sys.executable,str(root/'tools/prepare_mu.py')],check=True)
bt=repo/'Mu_Basecore/BaseTools'; conf=root/'out/mu-conf'; conf.mkdir(parents=True,exist_ok=True)
for name in ['target','tools_def','build_rule']:
    shutil.copy2(bt/f'Conf/{name}.template',conf/f'{name}.txt')
zig=root/'.tools/python/ziglang/zig'; bindir=root/'.tools/mu-bin';bindir.mkdir(parents=True,exist_ok=True)
for name,cmd in [('clang','cc'),('llvm-lib','lib'),('lld-link','lld-link'),('llvm-rc','rc')]:
    p=bindir/name
    if name == 'clang':
        p.write_text('#!'+str(root/'.tools/mu-venv/bin/python')+'\nimport os,sys\na=["aarch64-windows-msvc" if v=="aarch64-unknown-windows-msvc" else v for v in sys.argv[1:]]\nos.execv('+repr(str(zig))+', ['+repr(str(zig))+', "cc"]+a)\n')
    else:
        p.write_text('#!/bin/sh\nexec '+shlex.quote(str(zig))+' '+cmd+' "$@"\n')
    p.chmod(0o755)
env=os.environ.copy()
env.update(ZIG_GLOBAL_CACHE_DIR=str(root/'out/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(root/'out/zig-mu-cache'),WORKSPACE=str(repo),EDK_TOOLS_PATH=str(bt),CONF_PATH=str(conf),
           PYTHON_COMMAND=str(root/'.tools/mu-venv/bin/python'),CLANG_BIN=str(bindir)+'/',
           PACKAGES_PATH=os.pathsep.join(str(repo/p) for p in ['Platforms/Xiaomi','Common/Mu','Common/Mu_OEM_Sample','Mu_Basecore','Silicon/MediaTek','Silicon/Silicium']),
           PATH=os.pathsep.join([str(bindir),str(bt/'Source/C/bin'),str(bt/'BinWrappers/PosixLike'),env['PATH']]))
cmd=[str(bt/'BinWrappers/PosixLike/build'),'-p','begoniaPkg/begonia.dsc','-a','AARCH64','-t','CLANGPDB','-b','RELEASE','-n','8']
for value in ['FD_BASE=0x40080000','FD_SIZE=0x200000','FD_BLOCKS=0x200','ENABLE_SECUREBOOT=0','DEVICE_MODEL=0']:
    cmd+=['-D',value]
raise SystemExit(subprocess.call(cmd,cwd=repo,env=env))

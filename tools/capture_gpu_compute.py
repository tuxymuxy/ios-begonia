#!/usr/bin/env python3
"""Generate own G76 compute descriptors on host; does not execute GPU work."""
from pathlib import Path
import os,shlex,subprocess,sys
root=Path(__file__).resolve().parents[1];repo=root/'upstream/viva-gpu';out=root/'out'
subprocess.run([sys.executable,str(root/'tools/prepare_gpu_userland.py')],check=True)
r=subprocess.run(['bash',str(repo/'mesa/build.sh'),'fn','build_libdrm_xnu'],capture_output=True,text=True)
(out/'gpu-capture-transport-build.log').write_text(r.stdout+r.stderr);r.check_returncode()
package=repo/'mesa/out/macos'
cflags=[x for x in shlex.split((package/'cflags.txt').read_text()) if not x.startswith('-std=')]
command=['clang','-fobjc-arc','-O1',*cflags,'-I'+str(repo/'driver'),
         str(root/'tests/g76_compute_capture.m'),str(repo/'driver/out/macos/libvmg.a'),
         *shlex.split((package/'ldflags.txt').read_text()),'-framework','Foundation',
         '-framework','Metal','-o',str(out/'g76-compute-capture')]
r=subprocess.run(command,capture_output=True,text=True)
(out/'gpu-capture-build.log').write_text(r.stdout+r.stderr);r.check_returncode()
capture=out/'g76-compute-capture-data';capture.mkdir(exist_ok=False)
env=dict(os.environ,VGPU_BACKEND='mock',VGPU_MOCK_PROFILE='begonia',VMG_DRIVER='panfrost',
         VGPU_MOCK_CAPTURE_DIR=str(capture),MESA_SHADER_CACHE_DIR=str(out/'mesa-cache'))
r=subprocess.run([str(out/'g76-compute-capture')],env=env,capture_output=True,text=True,timeout=60)
(out/'gpu-compute-capture.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout)

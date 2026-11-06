#!/usr/bin/env python3
"""G76 frontend mock/resource and sample AIR compilation checks; no GPU execution."""
from pathlib import Path
import os,subprocess,sys
root=Path(__file__).resolve().parents[1];repo=root/'upstream/viva-gpu';out=root/'out'
subprocess.run([sys.executable,str(root/'tools/prepare_gpu_userland.py')],check=True)
r=subprocess.run(['bash',str(repo/'mesa/build.sh'),'fn','build_libdrm_xnu'],capture_output=True,text=True)
(out/'gpu-libdrm-build.log').write_text(r.stdout+r.stderr);r.check_returncode()
for executable in ['test_vmg','airnir']:(repo/'driver/out/macos'/executable).unlink(missing_ok=True)
r=subprocess.run(['make','-C',str(repo/'driver'),'all'],capture_output=True,text=True)
(out/'gpu-userland-build.log').write_text(r.stdout+r.stderr);r.check_returncode()
env=dict(os.environ,MESA_SHADER_CACHE_DIR=str(out/'mesa-cache'),VGPU_BACKEND='mock',
         VGPU_MOCK_GPU_ID='7211',VGPU_MOCK_PROFILE='begonia',VMG_DRIVER='panfrost',VMG_ISA='1',VMG_ISA_STRICT='1')
for binary,args,log,expected in [
 ('test_vmg',['panfrost'],'g76-frontend-test.log','4/4 checks passed'),
 ('airnir',[str(root/'tests/g76_sample.metal')],'g76-air-test.log','3/3 translated and compiled')]:
    r=subprocess.run([str(repo/'driver/out/macos'/binary),*args],env=env,capture_output=True,text=True)
    (out/log).write_text(r.stdout+r.stderr);r.check_returncode()
    if 'Mali-G76 MC4' not in r.stdout or expected not in r.stdout:
        raise SystemExit('Unexpected test result; inspect out/'+log)
    print(expected+' (host/mock only)')

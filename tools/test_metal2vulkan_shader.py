#!/usr/bin/env python3
"""Translate our own compute AIR under strict bounds; no native execution claim."""
from pathlib import Path
import json,os,signal,subprocess,time
root=Path(__file__).resolve().parents[1];out=root/'out'
air=out/'begonia-own-compute.air'
if not air.exists():raise SystemExit('Export own AIR with BEGONIA_OWN_AIR_OUTPUT and capture_gpu_compute.py first')
profile=root/'upstream/metal2vulkan/crates/metal2vulkan-validation/profiles/vk12-bare.json'
command=[str(out/'metal2vulkan-target/release/metal2vulkan'),str(air),str(out/'begonia-own-compute.spv'),
         '--profile',str(profile),'--emit-meta',str(out/'begonia-own-compute-vulkan.json')]
env=dict(os.environ,METAL2VULKAN_REPRO_DIR=str(out/'metal2vulkan-repros'))
began=time.monotonic();peak=0;failure=None
logfile=out/'metal2vulkan-own-translation.log'
with logfile.open('wb') as log:
    process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
    try:
        while process.poll() is None:
            rows=subprocess.check_output(['ps','-axo','pgid=,rss='],text=True)
            rss=sum(int(row.split()[1])*1024 for row in rows.splitlines()
                    if len(row.split())==2 and int(row.split()[0])==process.pid)
            peak=max(peak,rss)
            if rss>500*1024*1024 or time.monotonic()-began>=20:
                failure='memory ceiling' if rss>500*1024*1024 else 'time ceiling'
                os.killpg(process.pid,signal.SIGKILL);break
            time.sleep(.02)
    finally:
        if process.poll() is None:os.killpg(process.pid,signal.SIGKILL)
        process.wait()
report={'returncode':process.returncode,'elapsed_seconds':round(time.monotonic()-began,4),
        'peak_group_rss_bytes':peak,'limit_failure':failure,
        'profile':'synthetic Vulkan1.2 minimum, not a measured begonia profile',
        'scope':'one own-source compute shader, translation only'}
(out/'metal2vulkan-own-translation-result.json').write_text(json.dumps(report,indent=2)+'\n')
if process.returncode or failure:raise SystemExit('Translation failed; inspect '+str(logfile))
if 'spirv-val vulkan1.2: PASS' not in logfile.read_text():raise SystemExit('SPIR-V validation did not run')
print('Own AIR -> SPIR-V validates for synthetic Vulkan1.2 profile; no native Vulkan execution implied')

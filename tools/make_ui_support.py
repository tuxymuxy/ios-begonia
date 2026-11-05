#!/usr/bin/env python3
"""Build a private begonia support image with SpringBoard logging and test swipes."""
from pathlib import Path
import plistlib,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1];repo=root/'upstream/hakios-xnu-kernel';out=root/'out/begonia-ui-support';dest=out/'root'
if dest.exists():shutil.rmtree(dest)
shutil.copytree(repo/'userland/root',dest,symlinks=True)
for label in ['com.apple.SpringBoard','com.apple.lsd']:
    p=dest/'LaunchDaemons'/(label+'.plist');job=plistlib.loads(p.read_bytes())
    job.setdefault('EnvironmentVariables',{})['OS_ACTIVITY_DT_MODE']='YES'
    p.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))
p=dest/'LaunchDaemons/com.apple.backboardd.plist';job=plistlib.loads(p.read_bytes())
job.get('EnvironmentVariables',{}).pop('OS_ACTIVITY_DT_MODE',None)
p.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))
shutil.copy2(root/'devices/begonia/userland/rc.jobs.ui-diagnostic',dest/'etc/rc.jobs')
subprocess.run([sys.executable,str(repo/'tools/apfs/mkapfs.py'),'-z','--plain','fw','-o',str(out/'root.apfs'),'redwood='+str(dest)],check=True)
print('Built private support image; the staged system APFS file is unchanged.')

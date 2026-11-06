#!/usr/bin/env python3
"""Build a private begonia support image with SpringBoard logging and test swipes."""
from pathlib import Path
import argparse,plistlib,shutil,subprocess,sys
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--physical-touch',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parents[1];repo=root/'upstream/hakios-xnu-kernel';out=root/'out/begonia-ui-support';dest=out/'root'
if dest.exists():shutil.rmtree(dest)
shutil.copytree(repo/'userland/root',dest,symlinks=True)
# Build init from the recorded patch, rather than trusting a stale private binary.
subprocess.run([sys.executable,str(root/'tools/prepare_userland.py')],check=True)
shutil.copy2(repo/'userland/obj/init',dest/'sbin/init')
shutil.copy2(repo/'userland/obj/libhakios.dylib',dest/'lib/libhakios.dylib')
for label in ['com.apple.SpringBoard','com.apple.lsd']:
    p=dest/'LaunchDaemons'/(label+'.plist');job=plistlib.loads(p.read_bytes())
    job.setdefault('EnvironmentVariables',{})['OS_ACTIVITY_DT_MODE']='YES'
    p.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))
p=dest/'LaunchDaemons/com.apple.backboardd.plist';job=plistlib.loads(p.read_bytes())
job.get('EnvironmentVariables',{}).pop('OS_ACTIVITY_DT_MODE',None)
p.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))
shutil.copy2(root/'devices/begonia/userland/rc.jobs.ui-diagnostic',dest/'etc/rc.jobs')
if args.physical_touch:
    jobs=dest/'etc/rc.jobs'
    jobs.write_text(''.join(line for line in jobs.read_text().splitlines(True) if '/bin/rwtouch ' not in line))
subprocess.run([sys.executable,str(repo/'tools/apfs/mkapfs.py'),'-z','--plain','fw','-o',str(out/'root.apfs'),'redwood='+str(dest)],check=True)
print('Built private support image; the staged system APFS file is unchanged.')

#!/usr/bin/env python3
"""Run on postmarketOS: reserve and format a NEW 16 GiB regular iOS data file.
Never accepts a block device, never repartitions, never overwrites an existing file.
Fully writes zeros (not sparse/unwritten extents) before HFS+ formatting, because
XNU will access the mapped physical blocks with Linux shut down.
"""
import argparse,os,pathlib,shutil,stat,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('path',type=pathlib.Path)
a=p.parse_args();dest=a.path
assert os.geteuid()==0,'Run with sudo'
assert b'xiaomi,begonia' in pathlib.Path('/proc/device-tree/compatible').read_bytes()
assert dest.is_absolute() and dest.parent.resolve()==pathlib.Path('/home/user/ios-begonia')
assert not dest.exists() and not dest.is_symlink(),'Refusing existing destination'
formatter=shutil.which('mkfs.hfsplus');assert formatter,'Install hfsprogs first'
size=16<<30
assert shutil.disk_usage(dest.parent).free>size+(4<<30),'Leave 4 GiB free for postmarketOS'
fd=os.open(dest,os.O_CREAT|os.O_EXCL|os.O_WRONLY|os.O_NOFOLLOW,0o600)
try:
 assert stat.S_ISREG(os.fstat(fd).st_mode)
 chunk=bytes(4<<20)
 for done in range(0,size,len(chunk)):
  view=memoryview(chunk)
  while view:
   n=os.write(fd,view);assert n>0;view=view[n:]
  if (done+len(chunk))%(1<<30)==0:print(f'{(done+len(chunk))>>30}/16 GiB reserved',flush=True)
 os.fsync(fd)
finally:os.close(fd)
subprocess.run([formatter,'-J','-v','iOSData',str(dest)],check=True)
with dest.open('rb') as f:
 f.seek(1024);assert f.read(2)==b'H+'
os.sync()
print('16 GiB HFS+ data image prepared. Not yet enabled in iOS.',flush=True)

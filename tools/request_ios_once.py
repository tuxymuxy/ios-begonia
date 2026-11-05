#!/usr/bin/env python3
"""Arm one iOS test from postmarketOS. Requires an installed opt-in loader image.
The installed image is verified before the reserved-RAM request is written.
No partition is written by this tool. The request is consumed before XNU starts.
"""
import argparse,getpass,hashlib,pathlib,shlex,struct,subprocess

def request_id(payload):
    off,size=struct.unpack_from('<QQ',payload,0x20)
    assert off<=len(payload) and size<=len(payload)-off
    value=0x811c9dc5
    for byte in payload[0x48:0x2a8]+payload[off:off+size]:value=((value^byte)*0x01000193)&0xffffffff
    return (value^0xb00610de) or 1

def arm(image,payload,password,host=None,reboot=True):
    if not host: raise ValueError('An explicit phone host is required')
    image=pathlib.Path(image).read_bytes();request=request_id(pathlib.Path(payload).read_bytes())
    expected=hashlib.sha256(image).hexdigest()
    base=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','-o','HostKeyAlias='+host,'-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile=/private/tmp/begonia_known_hosts','user@'+host]
    code="""import hashlib,mmap,os,struct
assert b'xiaomi,begonia' in open('/proc/device-tree/compatible','rb').read()
with open('/dev/disk/by-partlabel/boot','rb') as f: assert hashlib.sha256(f.read(%d)).hexdigest()==%r, 'Installed boot image mismatch'
f=os.open('/dev/mem',os.O_RDWR|os.O_SYNC)
m=mmap.mmap(f,4096,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ|mmap.PROT_WRITE,offset=0x4d0e0000)
data=struct.pack('<I',%d);m[256:260]=data;assert m[256:260]==data
m.close();os.close(f)
print('One-use iOS request armed; the default remains postmarketOS')
"""%(len(image),expected,request)
    p=subprocess.run([*base,"sudo -S -p '' python3 -c "+shlex.quote(code)],input=(password+'\n').encode(),capture_output=True,timeout=30)
    if p.returncode:raise RuntimeError(p.stderr.decode())
    print(p.stdout.decode().strip())
    if reboot:
        p=subprocess.run([*base,"sudo -S -p '' systemctl reboot"],input=(password+'\n').encode(),capture_output=True,timeout=20)
        if p.returncode:raise RuntimeError(p.stderr.decode())
        print('Requested one iOS boot')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('image');p.add_argument('payload');p.add_argument('--host',required=True);a=p.parse_args()
    arm(a.image,a.payload,getpass.getpass('Phone sudo password: '),a.host)

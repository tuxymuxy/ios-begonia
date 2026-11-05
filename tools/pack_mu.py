#!/usr/bin/env python3
"""Package Mu + existing chain image; preserve backed-up ramdisk and DTB. No device writes."""
from pathlib import Path
import argparse,gzip,hashlib,json,struct
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
backup=(root/'private/device/backups/boot.img').read_bytes()
expected='115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7'
if hashlib.sha256(backup).hexdigest()!=expected:p.error('original backup hash mismatch')
chain=(root/'out/begonia-chain-xnu/ios16-optin-supervised.img').read_bytes()
if hashlib.sha256(chain).hexdigest()!='05f5cf90204638c99d1b2db30db8b17d96d11524473b73b03991b616d0f3a266':p.error('chain test image changed; review required')
fd=(root/'upstream/Mu-Silicium/Build/begoniaPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd').read_bytes()
if len(fd)!=0x200000 or fd[40:44]!=b'_FVH':p.error('unexpected Mu firmware volume')
raw=gzip.decompress(chain[2048:2048+struct.unpack_from('<I',chain,8)[0]])
if raw[56:60]!=b'ARM\x64' or len(raw)>0x3c00000:p.error('unexpected chain image')
raw+=bytes((-len(raw))%8)
kernel=gzip.compress(fd+raw,mtime=0)
header=bytearray(backup[:2048]);ps=2048;align=lambda n:(n+ps-1)//ps*ps
ks,ka,rs,ra,ss,sa,ta,page,version=struct.unpack_from('<9I',backup,8)
if (ka,ra,page,version)!=(0x40080000,0x47c80000,2048,2):p.error('boot layout changed')
rds=struct.unpack_from('<I',backup,1632)[0];ds=struct.unpack_from('<I',backup,1648)[0]
parts=[];off=ps
for size in [ks,rs,ss,rds,ds]:parts.append(backup[off:off+size]);off+=align(size)
parts[0]=kernel
struct.pack_into('<I',header,8,len(kernel));struct.pack_into('<Q',header,1652,0x47000000)
struct.pack_into('<Q',header,1636,ps+align(len(kernel))+align(rs)+align(ss) if rds else 0)
digest=hashlib.sha1()
for index,part in enumerate(parts):
 if index==3 and not rds:continue
 digest.update(part);digest.update(struct.pack('<I',len(part)))
header[576:608]=digest.digest()+bytes(12)
image=header
for part in parts:image+=part+bytes(align(len(part))-len(part))
if len(image)>len(backup):p.error('image does not fit boot partition')
if a.output.exists():p.error('output exists; choose a new output')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(image)
manifest={'stage':'experimental Mu volume-button boot picker; not phone-tested','size':len(image),'sha256':hashlib.sha256(image).hexdigest(),'firmware_sha256':hashlib.sha256(fd).hexdigest(),'original_boot_sha256':expected,'chain_sha256':hashlib.sha256(chain).hexdigest(),'raw_chain_bytes':len(raw),'menu_default':'postmarketOS after 10 seconds','phone_tested':False}
a.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))

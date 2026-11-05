#!/usr/bin/env python3
"""Build original PMOS with one extra reserved-RAM range for collecting XNU logs."""
from pathlib import Path
import hashlib,struct,subprocess,json
root=Path(__file__).resolve().parents[1];out=root/'out/log-recovery';out.mkdir(parents=True,exist_ok=True)
boot=(root/'private/device/backups/boot.img').read_bytes()
assert hashlib.sha256(boot).hexdigest()=='115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7'
ps=2048;align=lambda n:(n+ps-1)//ps*ps
sizes=[struct.unpack_from('<I',boot,k)[0] for k in [8,16,24,1632,1648]]
parts=[];off=ps
for n in sizes:parts.append(boot[off:off+n]);off+=align(n)
# A DTB memreserve entry is independent of /reserved-memory address-cell conventions.
header=list(struct.unpack_from('>10I',parts[4]));assert header[0]==0xd00dfeed
mem=header[4];end=mem
while struct.unpack_from('>QQ',parts[4],end)!=(0,0):end+=16
insert=struct.pack('>QQ',0x1b7e00000,0x200000)
dtb=bytearray(parts[4][:end]+insert+parts[4][end:])
for i in [2,3]:
 if header[i]>=end:header[i]+=16
header[1]+=16
struct.pack_into('>10I',dtb,0,*header)
dtbfile=out/'recovery.dtb';dtbfile.write_bytes(dtb)
for prop in ['#address-cells','#size-cells']:
 assert subprocess.check_output(['fdtget','-t','x',str(dtbfile),'/reserved-memory',prop],text=True).strip()=='2'
node='/reserved-memory/begonia-log-mirror@1b7e00000'
subprocess.run(['fdtput','-p','-t','x',str(dtbfile),node,'reg','1','b7e00000','0','200000'],check=True)
subprocess.run(['fdtput',str(dtbfile),node,'no-map'],check=True)
dtb=dtbfile.read_bytes();parts[4]=dtb
subprocess.run(['dtc','-q','-I','dtb','-O','dts',str(dtbfile),'-o',str(out/'recovery.dts')],check=True)
h=bytearray(boot[:ps]);struct.pack_into('<I',h,1648,len(dtb))
sha=hashlib.sha1()
for i,data in enumerate(parts):
 if i==3 and not data:continue
 sha.update(data);sha.update(struct.pack('<I',len(data)))
h[576:608]=sha.digest()+bytes(12)
image=h
for part in parts:image+=part+bytes(align(len(part))-len(part))
assert len(image)<=len(boot)
p=out/'pmos-log-recovery.img';p.write_bytes(image)
(p.with_suffix('.json')).write_text(json.dumps({'purpose':'original PMOS plus reserved XNU log mirror; no other hardware changes','size':len(image),'sha256':hashlib.sha256(image).hexdigest(),'reserved_base':'0x1b7e00000','reserved_bytes':0x200000},indent=2)+'\n')
(out/'pmos-log-recovery-full.img').write_bytes(image+boot[len(image):])
print('Prepared recovery boot image:',len(image),'bytes')

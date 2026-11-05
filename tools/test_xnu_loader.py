#!/usr/bin/env python3
"""Smoke-test the begonia raw XNU loader in QEMU and retrieve reserved-RAM logs."""
import pathlib,struct,subprocess,time,sys
from pack_chain import chain_image
root=pathlib.Path(__file__).resolve().parents[1];out=root/'out/qemu-chain-xnu'
probe=(out/'Image').read_bytes();sentinel=(root/'out/qemu-chain/sentinel.Image').read_bytes()
body=chain_image(probe,sentinel);body.extend(bytes((-len(body))%16))
payload=(root/'out/begonia-open-qemu.payload').read_bytes()
struct.pack_into('<Q',body,40,len(body));struct.pack_into('<Q',body,48,len(payload));body.extend(payload);struct.pack_into('<Q',body,16,len(body))
(out/'test.Image').write_bytes(body)
adt_off,adt_size=struct.unpack_from('<QQ',payload,0x20)
request=0x811c9dc5
for value in payload[0x48:0x2a8]+payload[adt_off:adt_off+adt_size]:request=((request^value)*0x01000193)&0xffffffff
request=(request^0xb00610de) or 1

cmd=['qemu-system-aarch64','-M','virt,gic-version=3'+(',virtualization=on' if '--el2' in sys.argv else ''),'-cpu','cortex-a55','-smp','2','-m','6G','-display','none','-serial','file:'+str(out/'serial.log'),'-monitor','stdio','-no-reboot','-kernel',str(out/'test.Image')]
if '--default-linux' not in sys.argv:cmd+=['-device','loader,addr=0x4d0e0100,data='+hex(request)+',data-len=4']
proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
time.sleep(3 if '--default-linux' in sys.argv else 30)
commands='pmemsave 0x4d0e0100 4 \"'+str(out/'request.bin')+'\"\ninfo registers\npmemsave 0x101b109c0 0x180000 \"'+str(out/'loader-log.bin')+'\"\npmemsave 0x101cf7018 8 \"'+str(out/'console-pointer.bin')+'\"\npmemsave 0x4d09f000 0x40000 "'+str(out/'ramoops.bin')+'"\nquit\n'
if '--mirror' in sys.argv:commands=commands.replace('quit\n','pmemsave 0x1b7e00000 0x200000 \"'+str(out/'mirror.bin')+'\"\nquit\n')
try:data,_=proc.communicate(commands.encode(),timeout=10)
except subprocess.TimeoutExpired:proc.terminate();data,_=proc.communicate(timeout=5)
(out/'monitor.log').write_bytes(data)
print((out/'serial.log').read_text(errors='replace'))
if '--default-linux' in sys.argv:
    serial=(out/'serial.log').read_text(errors='replace')
    assert 'CHAIN_PASS' in serial and 'no explicit request' in serial
    assert 'entering open XNU' not in serial
    print('PASS: without an explicit request, Linux is always the default')
    raise SystemExit(0)
if not (out/'ramoops.bin').exists():raise SystemExit('QEMU failed to save console RAM; inspect monitor.log')
buf=(out/'ramoops.bin').read_bytes();sig,start,size=struct.unpack_from('<III',buf);assert sig==0x43474244
cap=len(buf)-12;assert start<cap and size<=cap
at=(start-size)%cap;raw=buf[12:];log=(raw[at:]+raw[:at])[:size].decode(errors='replace')
(out/'kernel.log').write_text(log)
for line in log.splitlines():
 if any(x in line for x in ['BEGONIA','Darwin Kernel','gic:','Hello','panic(']):print(line[:250])
assert struct.unpack('<I',(out/'request.bin').read_bytes())[0]==0, 'one-shot request was not consumed'
assert 'BEGONIA WATCHER: emulated reset verified' in log
assert 'BEGONIA XNU: persistent console mapped' in log
assert 'Hello from user space on Darwin!' in log
print('PASS: begonia raw loader -> XNU -> user-space hello in QEMU')

if '--mirror' in sys.argv:
    mirror=(out/'mirror.bin').read_bytes()
    values=struct.unpack_from('<9Q',mirror)
    assert values[0]==0x3230474f4c474542 and values[1]==0x40000 and values[2]<=1536*1024
    assert values[8]==request
    def checksum(data):
        h=0x811c9dc5
        for v in data:h=((h^v)*0x01000193)&0xffffffff
        return h
    assert checksum(mirror[128:128+values[1]])==values[5]
    assert checksum(mirror[128+values[1]:128+values[1]+values[2]])==values[6]
    print('PASS: upper-RAM captured log header, request and both checksums')

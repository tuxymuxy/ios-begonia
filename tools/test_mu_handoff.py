#!/usr/bin/env python3
"""QEMU-test actual Mu handoff assembly: overlapping copy, DTB, registers, token."""
from pathlib import Path
import os,subprocess,struct
root=Path(__file__).resolve().parents[1];out=root/'out/mu-handoff-test';out.mkdir(parents=True,exist_ok=True)
zig=root/'.tools/python/ziglang/zig';env=os.environ.copy();env['ZIG_GLOBAL_CACHE_DIR']=str(root/'out/zig-global-cache')
inc=root/'upstream/Mu-Silicium/Mu_Basecore/MdePkg/Include'
def binary(source,name,address):
    s=out/(name+'.S');s.write_text(source);o=out/(name+'.o');b=out/(name+'.bin')
    subprocess.run(['clang','--target=aarch64-none-elf','-I',str(inc),'-I',str(inc/'AArch64'),'-c',str(s),'-o',str(o)],check=True)
    subprocess.run([str(zig),'ld.lld','-Ttext='+hex(address),'--oformat=binary',str(o),'-o',str(b)],check=True,env=env)
    return b
stub=binary((root/'firmware/mu/Platforms/Xiaomi/begoniaPkg/Library/BootPickerLib/Handoff.S').read_text(),'stub',0x44000000)
for request in [0,0x0966e324]:
    wrapper=binary(f'''.text
.global _start
_start:
 ldr x0, =0x40280000
 ldr x1, =0x40080000
 ldr x2, =0x400000
 ldr x3, =0x4bc80000
 ldr x4, ={request}
 ldr x9, =0x44000000
 br x9
''','wrapper',0x44002000)
    sentinel=binary(f'''.text
.global _start
_start:
 ldr x5, =0x4bc80000
 cmp x0,x5
 b.ne fail
 orr x5,x1,x2
 orr x5,x5,x3
 cbnz x5,fail
 ldr x5,=0x4d0e0100
 ldr w5,[x5]
 ldr w6,={request}
 cmp w5,w6
 b.ne fail
 ldr x5,=0x4047fff8
 ldr x5,[x5]
 ldr x6,=0xabcdef0123456789
 cmp x5,x6
 b.ne fail
 adr x2,pass
 b print
fail: adr x2,bad
print:
 mov x1,#0x09000000
1:ldrb w3,[x2],#1
 cbz w3,2f
 str w3,[x1]
 b 1b
2:wfe
 b 2b
pass:.asciz "MU_HANDOFF_PASS\\n"
bad:.asciz "MU_HANDOFF_FAIL\\n"
''','sentinel',0x40080000)
    data=bytearray(sentinel.read_bytes());data.extend(bytes(0x400000-len(data)));struct.pack_into('<Q',data,len(data)-8,0xabcdef0123456789);sentinel.write_bytes(data)
    cmd=['qemu-system-aarch64','-M','virt','-cpu','cortex-a55','-m','512M','-nographic','-monitor','none']
    for p,addr,cpu in [(stub,0x44000000,False),(sentinel,0x40280000,False),(wrapper,0x44002000,True)]:
        cmd+=['-device',f'loader,file={p},addr={addr},force-raw=on'+(',cpu-num=0' if cpu else '')]
    proc=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try: log=proc.communicate(timeout=2)[0]
    except subprocess.TimeoutExpired: proc.terminate();log=proc.communicate(timeout=3)[0]
    (out/f'request-{request:x}.log').write_bytes(log)
    assert b'MU_HANDOFF_PASS' in log and b'MU_HANDOFF_FAIL' not in log,log
print('PASS: both choices preserve DTB, clear argument registers, copy overlapping image and set request')

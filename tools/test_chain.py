#!/usr/bin/env python3
"""Exercise relocated overlapping kernel copy and handoff under QEMU."""
import pathlib,subprocess,sys
from pack_chain import chain_image
root=pathlib.Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/build_probe.py'),'--qemu','--chain'],check=True)
out=root/'out/qemu-chain'
# Synthetic original kernel prints only after the relocated copy and branch.
source='''
.section .text.head,"ax"
.global _head
_head:
 b entry
 .long 0
 .quad 0
 .quad 4096
 .quad 10
 .quad 0,0,0
 .ascii "ARM\\x64"
 .long 0
entry:
 mov x1, #0x09000000
 adr x2, msg
1: ldrb w3, [x2], #1
 cbz w3, 2f
 str w3, [x1]
 b 1b
2: wfe
 b 2b
msg: .asciz "CHAIN_PASS\\n"
'''
(out/'sentinel.S').write_text(source)
subprocess.run(['clang','--target=aarch64-none-elf','-c',str(out/'sentinel.S'),'-o',str(out/'sentinel.o')],check=True)
subprocess.run([str(root/'.tools/python/ziglang/zig'),'ld.lld','-T',str(root/'boot/link.ld'),'--oformat=binary',str(out/'sentinel.o'),'-o',str(out/'sentinel.Image')],check=True)
(out/'test.Image').write_bytes(chain_image((out/'Image').read_bytes(),(out/'sentinel.Image').read_bytes()))
proc=subprocess.Popen(['qemu-system-aarch64','-M','virt','-cpu','cortex-a55','-m','256M','-nographic','-monitor','none','-kernel',str(out/'test.Image')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
try:data,_=proc.communicate(timeout=5)
except subprocess.TimeoutExpired:
 proc.terminate();data,_=proc.communicate(timeout=5)
log=data.decode(errors='replace');(out/'serial.log').write_text(log);print(log)
assert 'PROBE_COMPLETE' in log and 'CHAIN_PASS' in log
print('PASS: diagnostic -> relocated overlapping copy -> original kernel entry')

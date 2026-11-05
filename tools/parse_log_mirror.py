#!/usr/bin/env python3
"""Validate a reserved XNU log capture and decode the ramoops ring plus early log."""
from pathlib import Path
import argparse,struct,json
p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
b=a.input.read_bytes()
if len(b)<128: p.error('truncated header')
h=struct.unpack_from('<9Q',b)
if h[0]!=0x3230474f4c474542 or h[1]!=0x40000 or h[2]>1536*1024 or len(b)<128+h[1]+h[2]:p.error('invalid mirror header')
def checksum(data):
 value=0x811c9dc5
 for v in data:value=((value^v)*0x01000193)&0xffffffff
 return value
console=b[128:128+h[1]];early=b[128+h[1]:128+h[1]+h[2]]
if checksum(console)!=h[5] or checksum(early)!=h[6]:p.error('capture checksum mismatch')
sig,start,size=struct.unpack_from('<III',console);cap=len(console)-12
if sig!=0x43474244 or start>=cap or size>cap:p.error('invalid ramoops ring')
raw=console[12:];at=(start-size)%cap;log=(raw[at:]+raw[:at])[:size]
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text('=== KERNEL LOG BUFFER (tail may be circular) ===\n'+early.decode(errors='replace')+'\n=== RESERVED CONSOLE ===\n'+log.decode(errors='replace'))
a.output.with_suffix('.json').write_text(json.dumps({'counter_hz':h[3],'counter_at_capture':h[4],'request':hex(h[8]),'console_bytes':size,'early_bytes':h[2],'console_pointer':hex(h[7]),'checksums_valid':True},indent=2)+'\n')
print('Validated capture:',size,'console bytes,',h[2],'early bytes; output:',a.output)

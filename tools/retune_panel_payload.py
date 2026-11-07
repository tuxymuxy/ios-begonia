#!/usr/bin/env python3
"""Repackage a known native payload with panel-sized canvas and a fresh request ID."""
import argparse,json,re,struct,time
from pathlib import Path
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('input',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
b=bytearray(a.input.read_bytes());start,end=0x48,0x2a8
if len(b)<end or b[:8]!=b'SWEETXNU' or struct.unpack_from('<I',b,8)[0]!=1:p.error('invalid native payload')
args=b[start:end].split(b'\0',1)[0].decode('ascii').split()
if 'begonia_root=1' not in args:p.error('expected native file root')
epoch=[arg for arg in args if re.fullmatch(r'epoch=\d+',arg)]
if len(epoch)!=1:p.error('expected exactly one existing epoch')
scan=json.loads((root/'devices/begonia/board.json').read_text())['native_ios_test']['scanout']
# Keep the current display scale and device subtype. Only change canvas pixels.
args=[arg for arg in args if arg.split('=',1)[0] not in ('fbwidth','fbheight','epoch')]
args.extend((f'fbwidth={scan["width"]}',f'fbheight={scan["height"]}',f'epoch={max(int(time.time()),int(epoch[0][6:])+1)}'))
line=' '.join(args).encode('ascii')
if len(line)>=end-start:p.error('boot arguments do not fit the loader ABI')
b[start:end]=line.ljust(end-start,b'\0')
with a.output.open('xb') as f:f.write(b)
print(f'Native canvas {scan["width"]}x{scan["height"]}; only command-line region changed')

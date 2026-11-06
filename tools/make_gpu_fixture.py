#!/usr/bin/env python3
"""Validate an own compute capture and emit a private kernel test fixture."""
from pathlib import Path
import argparse,json,struct,hashlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('capture',type=Path);p.add_argument('--output-va',type=lambda x:int(x,0),required=True)
p.add_argument('--output',type=Path,required=True);a=p.parse_args()
m=json.loads((a.capture/'submit-1.json').read_text());bos=m['bos']
assert m['requirements']==0 and 0<len(bos)<=16
assert len(list(a.capture.glob('submit-*.json')))==1,'Expected exactly one compute submission'
base=m['jc']&~((1<<21)-1);seen=set();total=0;job=None;output=False
chunks=[];digest=hashlib.sha256()
for i,b in enumerate(bos):
    raw=(a.capture/f"submit-1-bo-{b['handle']}.bin").read_bytes()
    assert len(raw)==b['size'] and b['size']>0 and b['size']%16384==0 and b['va']%16384==0
    assert base<=b['va'] and b['va']+b['size']<=base+(1<<21)
    for va in range(b['va'],b['va']+b['size'],4096):assert va not in seen;seen.add(va)
    total+=b['size'];assert total<=32*16384
    if b['va']<=m['jc'] and m['jc']+256<=b['va']+b['size']:
        job=raw[m['jc']-b['va']:m['jc']-b['va']+256]
    if b['va']<=a.output_va and a.output_va+256<=b['va']+b['size']:
        off=a.output_va-b['va'];assert raw[off:off+256]==struct.pack('<64I',*([0xdeadbeef]*64));output=True
    digest.update(raw)
    entries=','.join(f'[{j}]=0x{v:02x}' for j,v in enumerate(raw) if v)
    chunks.append(f'static const unsigned char beg_gpu_blob_{i}[{len(raw)}]={{'+entries+'};\n')
assert job is not None and output and not(m['jc']&63)
assert (struct.unpack_from('<I',job,16)[0]>>1)&127==4,'Only compute jobs supported'
assert struct.unpack_from('<Q',job,24)[0]==0,'Only one-job chains supported'
text='/* Generated from our own Metal shader; not an Apple asset. */\n#define BEG_GPU_FIXTURE_PRESENT 1\n'
text+=''.join(chunks)
text+='struct beg_gpu_blob {uint64_t va;unsigned size;const unsigned char *bytes;};\n'
text+='static const struct beg_gpu_blob beg_gpu_blobs[]={\n'
text+=''.join(f'{{0x{b["va"]:x}ull,{b["size"]},beg_gpu_blob_{i}}},\n' for i,b in enumerate(bos))+'};\n'
text+=f'#define BEG_GPU_FIXTURE_JC 0x{m["jc"]:x}ull\n#define BEG_GPU_FIXTURE_OUTPUT 0x{a.output_va:x}ull\n'
text+=f'#define BEG_GPU_FIXTURE_PAGES {total//16384}\n'
a.output.write_text(text)
print(f'Validated single compute job: {len(bos)} BOs, {total//16384} kernel pages, SHA256 {digest.hexdigest()}')

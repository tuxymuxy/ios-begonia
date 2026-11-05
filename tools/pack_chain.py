#!/usr/bin/env python3
"""Build a temporary begonia diagnostic boot image from its verified boot backup.
No device access. Preserves original ramdisk, DTB, cmdline and load addresses.
"""
import argparse, gzip, hashlib, pathlib, struct
CHAIN_OFFSET=0x10000

def chain_image(probe, original):
    if probe[56:60] != b'ARM\x64' or original[56:60] != b'ARM\x64':
        raise ValueError('both inputs must be uncompressed arm64 Images')
    if len(probe)>CHAIN_OFFSET: raise ValueError('probe overlaps appended kernel')
    body=bytearray(probe); body.extend(bytes(CHAIN_OFFSET-len(body)))
    body.extend(original); body.extend(bytes((-len(body))%8))
    struct.pack_into('<Q', body, 8, struct.unpack_from('<Q',original,8)[0])
    struct.pack_into('<Q', body, 16, len(body))
    struct.pack_into('<Q', body, 32, len(body)-CHAIN_OFFSET)
    return body

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('boot',type=pathlib.Path);p.add_argument('probe',type=pathlib.Path)
    p.add_argument('--payload',type=pathlib.Path,help='append an XNU payload; moves DTB staging address out of the enlarged kernel')
    p.add_argument('--sha256',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
    a=p.parse_args();src=a.boot.read_bytes()
    if hashlib.sha256(src).hexdigest()!=a.sha256: p.error('boot backup checksum mismatch')
    if src[:8]!=b'ANDROID!': p.error('not an Android boot image')
    ks,ka,rs,ra,ss,sa,ta,ps,ver,osv=struct.unpack_from('<10I',src,8)
    if (ka,ra,ps,ver)!=(0x40080000,0x47c80000,2048,2): p.error('unexpected begonia boot layout')
    align=lambda n:(n+ps-1)//ps*ps
    off=ps;parts=[]
    rds,rdo,hs=struct.unpack_from('<IQI',src,1632)
    ds,da=struct.unpack_from('<IQ',src,1648)
    for size in (ks,rs,ss,rds,ds):
        if off+size>len(src): p.error('truncated section')
        parts.append(src[off:off+size]);off+=align(size)
    was_gzip=parts[0][:2]==b'\x1f\x8b'
    original=gzip.decompress(parts[0]) if was_gzip else parts[0]
    kernel=chain_image(a.probe.read_bytes(),original)
    if a.payload:
        payload=a.payload.read_bytes()
        kernel.extend(bytes((-len(kernel))%16))
        struct.pack_into('<Q',kernel,40,len(kernel));struct.pack_into('<Q',kernel,48,len(payload))
        kernel.extend(payload)
        struct.pack_into('<Q',kernel,16,len(kernel))
        da=0x47000000
    # Neither loader, full original kernel allocation nor copy stub may hit DTB/ramdisk.
    end=ka+len(kernel)
    original_end=ka+struct.unpack_from('<Q',original,16)[0]
    if max(end,original_end)>da or not(original_end<0x42000000 and da+ds<=ra): p.error('memory placement collision')
    parts[0]=gzip.compress(kernel,mtime=0) if was_gzip else kernel
    header=bytearray(src[:ps]);struct.pack_into('<Q',header,1652,da)
    struct.pack_into('<I',header,8,len(parts[0]))
    recovery_off=ps+align(len(parts[0]))+align(rs)+align(ss)
    struct.pack_into('<Q',header,1636,recovery_off if rds else 0)
    digest=hashlib.sha1()
    for index,data in enumerate(parts):
        if index==3 and not rds: continue
        digest.update(data);digest.update(struct.pack('<I',len(data)))
    header[576:608]=digest.digest()+bytes(12)
    out=header
    for data in parts: out+=data+bytes(align(len(data))-len(data))
    if len(out)>len(src):p.error('output exceeds backed-up boot partition size')
    if a.output.exists():p.error('output exists')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(out)
    print('Temporary diagnostic boot image:',len(out),'bytes')
    print('SHA256:',hashlib.sha256(out).hexdigest())
    print('No partition writes performed. '+('Experimental XNU payload; userland depends on the supplied payload and root filesystem.' if a.payload else 'Image chainloads postmarketOS; no XNU or iOS.'))
if __name__=='__main__':main()

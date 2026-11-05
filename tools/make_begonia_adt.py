#!/usr/bin/env python3
"""Minimal open-kernel ADT for a single begonia CPU. No iPhone identity or hardware drivers."""
import argparse,pathlib,struct,os,zlib,hashlib,sys
u32=lambda *v:struct.pack('<'+'I'*len(v),*v)
u64=lambda *v:struct.pack('<'+'Q'*len(v),*v)
s=lambda v:v.encode()+b'\0'
def node(name,props=(),children=()):
    props=[('name',s(name)),*props]
    data=u32(len(props),len(children))
    for name,value in props:
        data+=name.encode().ljust(32,b'\0')+u32(len(value))+value+bytes((-len(value))%4)
    return data+b''.join(children)
def nvram():
    # Empty CHRP bank, following HakiOS xnuloader/loader.c (MIT; licenses/HakiOS-MIT.txt).
    image=bytearray(0x2000)
    for offset,signature,length,name in [(0,0x5a,0x20,b'nvram'),(0x20,0x70,0x1fe0,b'common')]:
        header=bytearray(16);header[0]=signature;struct.pack_into('<H',header,2,length//16);header[4:4+len(name)]=name
        checksum=sum(header)
        while checksum>255:checksum=(checksum&255)+(checksum>>8)
        header[1]=checksum;image[offset:offset+16]=header
    struct.pack_into('<I',image,20,1);struct.pack_into('<I',image,16,zlib.adler32(image[20:]))
    return bytes(image)

def build(qemu=False,extents=None,identity=None,touch_firmware=None,data_extents=None):
    frequency=1000000000 if qemu else 13000000
    base,size=0x100000000,0xb7e00000
    chosen=node('chosen',[('dram-base',u64(base)),('dram-size',u64(size)),('random-seed',os.urandom(64)),('debug-enabled',u32(1)),('embedded-panic-log-size',u32(0x100000)),('firmware-version',s('begonia-bringup')),('display-scale',u32(3)),('boot-manifest-hash',hashlib.sha384(b'begonia manifest').digest()),('crypto-hash-method',s('sha2-384')),('unique-chip-id',u64(0x4245474f4e494101)),('chip-id',u32(0x8015)),('certificate-production-status',u32(0)),('effective-production-status-ap',u32(0)),('certificate-security-mode',u32(1)),('effective-security-mode-sep',u32(1)),('nvram-bank-size',u32(0x2000)),('nvram-bank-count',u32(1)),('nvram-current-bank',u32(0)),('nvram-proxy-data',nvram())],[node('memory-map')])
    cpu=node('cpu0',[('device_type',s('cpu')),('compatible',s('arm,cortex-a55')+s('ARM,v8')),('reg',u32(0)),('cpu-id',u32(0)),('cluster-id',u32(0)),('cluster-type',u32(0)),('die-cluster-id',u32(0)),('cluster-core-id',u32(0)),('state',s('running')),('timebase-frequency',u32(frequency)),('fixed-frequency',u32(frequency)),('peripheral-frequency',u32(frequency))])
    cpus=node('cpus',[('#address-cells',u32(1)),('#size-cells',u32(0))],[cpu])
    gic=node('gic',[('compatible',s('arm,gic-v3')),('interrupt-controller',b''),('reg',u64(0x07000000 if qemu else 0x0b000000,0x40000,0x070a0000 if qemu else 0x0b040000,0x200000))])
    armio=node('arm-io',[('device_type',s('arm-io')),('compatible',s('arm-io')),('#address-cells',u32(2)),('#size-cells',u32(2)),('ranges',u64(0,0x01000000,0xff000000))],[gic])
    extra=[]
    if data_extents is not None:
        extra.append(node("begonia-data",[("file-extents",data_extents)]))
    if touch_firmware is not None:
        if len(touch_firmware)!=118784: raise ValueError("Unexpected touch firmware size")
        extra.append(node("begonia-touch",[("firmware",touch_firmware)]))
    if extents is not None:
        extra.append(node('begonia-ufs',[('reg',u64(0x11270000,0x2300)),('file-extents',extents)]))
    if identity is not None:
        sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'upstream/ios-sweet/tools'))
        import adt
        tree,_=adt.parse(identity)
        product=next(c for c in tree.children if c.name=='product')
        extra.append(node('product',[(k,v) for k,f,v in product.props if not f and k not in ('name','AAPL,phandle')]))
    compatible=(s('D22AP')+s('iPhone10,3') if identity else b'')+s('xiaomi,begonia')+s('AppleARM')
    return node('device-tree',[('compatible',compatible),('model',s('iPhone10,3' if identity else 'BegoniaOpenKernel')),('target-type',s('D22')),('target-sub-type',s('D22AP')),('#address-cells',u32(2)),('#size-cells',u32(2))],[chosen,node('defaults'),node('options'),cpus,armio,node('pram',[('reg',u64(base+size,0x100000))])]+extra)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=pathlib.Path);p.add_argument('--qemu',action='store_true');p.add_argument('--extents',type=pathlib.Path);p.add_argument('--identity',type=pathlib.Path);p.add_argument('--touch-firmware',type=pathlib.Path);p.add_argument('--data-extents',type=pathlib.Path);a=p.parse_args()
    a.output.write_bytes(build(qemu=a.qemu,extents=a.extents.read_bytes() if a.extents else None,identity=a.identity.read_bytes() if a.identity else None,touch_firmware=a.touch_firmware.read_bytes() if a.touch_firmware else None,data_extents=a.data_extents.read_bytes() if a.data_extents else None));print(a.output)

#!/usr/bin/env python3
"""Build private native-file-root payload; small support image is md1, dummy md0 keeps iOS boot conventions."""
import argparse,pathlib,subprocess,sys,struct,time,copy
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'upstream/ios-sweet/tools'));import adt
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=pathlib.Path);p.add_argument('--support-root',type=pathlib.Path,default=root/'upstream/hakios-xnu-kernel/userland/root.apfs');p.add_argument('--smp-trampoline',type=lambda x:int(x,0),default=0);p.add_argument('--cpus',type=int,choices=[1,2],default=1);p.add_argument('--kernel',type=pathlib.Path,default=root/'out/kernel.begonia-native-ufs');p.add_argument('--gpu-service',action='store_true');p.add_argument('--metal',type=int,choices=[0,1,2,3],default=0);p.add_argument('--gpu-shader-selftest',action='store_true');p.add_argument('--gpu-mapped-selftest',action='store_true');p.add_argument('--gpu-selftest',action='store_true');p.add_argument('--gpu-probe',action='store_true');p.add_argument('--cpu-boost',action='store_true');p.add_argument('--perf-probe',action='store_true');p.add_argument('--touch-direct',action='store_true');p.add_argument('--touch-debug',action='store_true');p.add_argument('--disable-cs',action='store_true');p.add_argument('--data',action='store_true');p.add_argument('--touch',action='store_true');p.add_argument('--keep-console',action='store_true');p.add_argument('--watch-seconds',type=int,default=240);a=p.parse_args()
if not 30<=a.watch_seconds<=7200:p.error('--watch-seconds must be 30..7200')
if a.gpu_service and not a.gpu_shader_selftest:p.error('--gpu-service requires the native shader selftest')
if a.gpu_shader_selftest and not a.gpu_mapped_selftest:p.error('--gpu-shader-selftest requires --gpu-mapped-selftest')
if a.gpu_mapped_selftest and not a.gpu_selftest:p.error('--gpu-mapped-selftest requires --gpu-selftest')
if a.gpu_selftest and not a.gpu_probe:p.error('--gpu-selftest requires --gpu-probe')
if a.gpu_probe and (not a.perf_probe or a.cpus!=1 or a.watch_seconds>180):p.error('--gpu-probe requires single-core --perf-probe and a maximum 180-second capture')
if a.cpu_boost and (not a.perf_probe or a.cpus!=1):p.error('--cpu-boost requires single-core --perf-probe')
if a.smp_trampoline and (a.cpus!=2 or not 0x40080000<=a.smp_trampoline<0x40180000 or a.smp_trampoline%4):p.error('trampoline requires two CPUs and aligned loader entry in low RAM')
tree,_=adt.parse((root/'out/begonia-native.adt').read_bytes())
if a.touch and not any(c.name=='begonia-touch' for c in tree.children):p.error('--touch requires private touch firmware in the ADT')
if a.data and not any(c.name=='begonia-data' for c in tree.children):p.error('--data requires the verified data extent map in the ADT')
symbols={}
present=set()
for line in subprocess.check_output(['nm','-n',str(a.kernel)],text=True).splitlines():
    parts=line.split()
    if len(parts)==3:present.add(parts[2])
    if len(parts)==3 and parts[2] in ('_loader_log','_loader_log_len','_begonia_console'):symbols[parts[2]]=int(parts[0],16)
assert len(symbols)==3
if a.perf_probe and '_begonia_perf_init' not in present:p.error('selected kernel lacks the performance probe')
if a.gpu_mapped_selftest and not a.gpu_selftest:p.error('--gpu-mapped-selftest requires --gpu-selftest')
if a.gpu_selftest and not a.gpu_probe:p.error('--gpu-selftest requires --gpu-probe')
if a.gpu_probe and '_begonia_mali_probe' not in present:p.error('selected kernel lacks the GPU probe')
physical=lambda name:0x100000000+symbols[name]-0xfffffff006000000
args=('metal='+str(a.metal)+' ca_metallib=/redwood/lib/default.metallib ' if a.metal else '')+('begonia_gpu_service=1 ' if a.gpu_service else '')+('begonia_gpu_shader=1 ' if a.gpu_shader_selftest else '')+('begonia_gpu_mapped=1 ' if a.gpu_mapped_selftest else '')+('begonia_gpu_selftest=1 ' if a.gpu_selftest else '')+('begonia_gpu_probe=1 ' if a.gpu_probe else '')+('begonia_cpu_boost=1 ' if a.cpu_boost else '')+('begonia_smp_trampoline='+hex(a.smp_trampoline)+' ' if a.smp_trampoline else '')+('begonia_perf_probe=1 ' if a.perf_probe else '')+('begonia_touch_direct=1 ' if a.touch_direct else '')+('touch_lat=1 ' if a.touch_debug else '')+('cs_enforcement_disable=1 amfi_get_out_of_my_way=1 amfi_allow_any_signature=1 amfi=0xff ' if a.disable_cs else '')+('begonia_data=1 var_hfs=begonia-data ' if a.data else '')+('begonia_touch=1 ' if a.touch else '')+('begonia_keep_console=1 ' if a.keep_console else '')+'begonia_watch='+str(a.watch_seconds)+' beg_log='+hex(physical('_loader_log'))+' beg_len='+hex(physical('_loader_log_len'))+' beg_con='+hex(physical('_begonia_console'))+' cpus='+str(a.cpus)+' rd=md0 begonia_root=1 begonia_log=1 begonia_test=0 begonia_cpumon_log_limit=16 debug=0x8 -v epoch='+str(int(time.time()))
subprocess.run([sys.executable,str(root/'upstream/ios-sweet/tools/mkpayload.py'),str(a.kernel),str(root/'out/begonia-native.adt'),str(a.output),'--ramdisk',str(a.support_root),'--cmdline',args],check=True)
b=bytearray(a.output.read_bytes());off,length=struct.unpack_from('<QQ',b,0x20);tree,_=adt.parse(b[off:off+length])
chosen=next(c for c in tree.children if c.name=='chosen');memory=next(c for c in chosen.children if c.name=='memory-map')
if a.cpus==2:
    # CPU1/MPIDR0x100 is the independent recovery supervisor: never give it to XNU.
    cpus=next(c for c in tree.children if c.name=='cpus')
    assert len(cpus.children)==1 and cpus.children[0].prop('reg')==struct.pack('<I',0)
    secondary=copy.deepcopy(cpus.children[0])
    values={'name':b'cpu1\0','reg':struct.pack('<I',0x200),
            'cpu-id':struct.pack('<I',1),'cluster-core-id':struct.pack('<I',2),
            'state':b'halted\0'}
    secondary.props=[(k,f,values.get(k,v)) for k,f,v in secondary.props]
    cpus.children.append(secondary)
    chosen.props=[(k,f,v) for k,f,v in chosen.props if k!='psci-method']+[('psci-method',0,b'smc\0')]

old=memory.prop('RAMDisk');assert old and len(old)==16
memory.props=[(k,f,v) for k,f,v in memory.props if k!='RAMDisk']+[('RAMDisk',0,struct.pack('<QQ',0x103000000,0x4000)),('RAMDisk1',0,old)]
blob=adt.serialize(tree);struct.pack_into('<Q',b,0x28,len(blob));a.output.write_bytes(b[:off]+blob)
virt,_,_,_,end,phys=struct.unpack_from('<6Q',b,0x10)
align=lambda value:(value+0x3fff)&~0x3fff
boot_args=align(align(phys+end-virt)+len(blob))
if align(boot_args+736)>phys+0x03000000:
    a.output.unlink()
    p.error('kernel/support/ADT/boot arguments overlap the loader dummy RAM disk')
print('Native file-root payload built, mini support volume mapped as md1.')

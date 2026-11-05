#!/usr/bin/env python3
"""Build private native-file-root payload; small support image is md1, dummy md0 keeps iOS boot conventions."""
import argparse,pathlib,subprocess,sys,struct,time
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'upstream/ios-sweet/tools'));import adt
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=pathlib.Path);p.add_argument('--support-root',type=pathlib.Path,default=root/'upstream/hakios-xnu-kernel/userland/root.apfs');p.add_argument('--data',action='store_true');p.add_argument('--touch',action='store_true');p.add_argument('--keep-console',action='store_true');p.add_argument('--watch-seconds',type=int,default=240);a=p.parse_args()
if not 30<=a.watch_seconds<=7200:p.error('--watch-seconds must be 30..7200')
tree,_=adt.parse((root/'out/begonia-native.adt').read_bytes())
if a.touch and not any(c.name=='begonia-touch' for c in tree.children):p.error('--touch requires private touch firmware in the ADT')
if a.data and not any(c.name=='begonia-data' for c in tree.children):p.error('--data requires the verified data extent map in the ADT')
symbols={}
for line in subprocess.check_output(['nm','-n',str(root/'out/kernel.begonia-native-ufs')],text=True).splitlines():
    parts=line.split()
    if len(parts)==3 and parts[2] in ('_loader_log','_loader_log_len','_begonia_console'):symbols[parts[2]]=int(parts[0],16)
assert len(symbols)==3
physical=lambda name:0x100000000+symbols[name]-0xfffffff006000000
args=('begonia_data=1 var_hfs=begonia-data ' if a.data else '')+('begonia_touch=1 ' if a.touch else '')+('begonia_keep_console=1 ' if a.keep_console else '')+'begonia_watch='+str(a.watch_seconds)+' beg_log='+hex(physical('_loader_log'))+' beg_len='+hex(physical('_loader_log_len'))+' beg_con='+hex(physical('_begonia_console'))+' cpus=1 rd=md0 begonia_root=1 begonia_log=1 begonia_test=0 begonia_cpumon_log_limit=16 debug=0x8 cs_enforcement_disable=1 amfi_get_out_of_my_way=1 amfi_allow_any_signature=1 amfi=0xff -v epoch='+str(int(time.time()))
subprocess.run([sys.executable,str(root/'upstream/ios-sweet/tools/mkpayload.py'),str(root/'out/kernel.begonia-native-ufs'),str(root/'out/begonia-native.adt'),str(a.output),'--ramdisk',str(a.support_root),'--cmdline',args],check=True)
b=bytearray(a.output.read_bytes());off,length=struct.unpack_from('<QQ',b,0x20);tree,_=adt.parse(b[off:off+length])
chosen=next(c for c in tree.children if c.name=='chosen');memory=next(c for c in chosen.children if c.name=='memory-map')
old=memory.prop('RAMDisk');assert old and len(old)==16
memory.props=[(k,f,v) for k,f,v in memory.props if k!='RAMDisk']+[('RAMDisk',0,struct.pack('<QQ',0x103000000,0x4000)),('RAMDisk1',0,old)]
blob=adt.serialize(tree);struct.pack_into('<Q',b,0x28,len(blob));a.output.write_bytes(b[:off]+blob)
print('Native file-root payload built, mini support volume mapped as md1.')

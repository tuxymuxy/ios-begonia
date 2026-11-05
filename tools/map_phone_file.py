#!/usr/bin/env python3
"""Run on postmarketOS to map an unchanged regular file to read-only UFS LUN2 blocks.
This only reads metadata. The supported layout is this phone's loop0p2 on /dev/sdc46.
"""
import fcntl,json,os,pathlib,struct,sys
path=pathlib.Path(sys.argv[1]);fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
size=os.fstat(fd).st_size
assert size>0 and size%4096==0
read=lambda p:int(pathlib.Path(p).read_text().strip())
assert pathlib.Path('/sys/class/block/loop0/loop/backing_file').read_text().strip()=='/dev/sdc46'
outer=read('/sys/class/block/sdc46/start')*512
outer_size=read('/sys/class/block/sdc46/size')*512
inner=read('/sys/class/block/loop0p2/start')*512+read('/sys/class/block/loop0/loop/offset')
assert outer%4096==0 and inner%4096==0 and outer_size%4096==0
count=4096;buf=bytearray(32+count*56);struct.pack_into('<QQIIII',buf,0,0,size,1,0,count,0)
fcntl.ioctl(fd,0xc020660b,buf,True)
_,_,flags,mapped,_,_=struct.unpack_from('<QQIIII',buf)
assert 0<mapped<=count
extents=[];next_byte=0
for i in range(mapped):
 logical,physical,length,_,_,eflags,_,_,_=struct.unpack_from('<QQQQQIIII',buf,32+56*i)
 assert not(eflags&~0x1001),hex(eflags)
 assert logical==next_byte and logical%4096==0 and physical%4096==0
 length=min(length,size-logical);assert length>0 and length%4096==0
 at=outer+inner+physical
 assert at>=outer and at+length<=outer+outer_size
 extents.append([logical//4096,at//4096,length//4096]);next_byte+=length
assert next_byte==size and eflags&1
print(json.dumps({'file_bytes':size,'userdata_first_lba':outer//4096,'userdata_end_lba':(outer+outer_size)//4096,'extents':extents},indent=2))

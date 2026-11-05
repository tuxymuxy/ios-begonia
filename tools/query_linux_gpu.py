#!/usr/bin/env python3
"""Read Panfrost GPU parameters on Linux; no GPU jobs or register writes."""
import fcntl,json,os,struct
fd=os.open('/dev/dri/renderD128',os.O_RDWR|os.O_CLOEXEC)
fields=['product_id','revision','shader_present','tiler_present','l2_present','stack_present','address_spaces','job_slots','l2_features','core_features','tiler_features','memory_features','mmu_features']
result={}
try:
 for number,name in enumerate(fields):
  request=bytearray(struct.pack('<IIQ',number,0,0));fcntl.ioctl(fd,0xc0106444,request,True)
  result[name]=hex(struct.unpack_from('<Q',request,8)[0])
finally:os.close(fd)
print(json.dumps(result,indent=2))

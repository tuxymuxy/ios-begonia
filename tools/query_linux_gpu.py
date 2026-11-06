#!/usr/bin/env python3
"""Read Panfrost GPU parameters on Linux; no GPU jobs or register writes."""
import argparse,errno,fcntl,json,os,struct
p=argparse.ArgumentParser(description=__doc__);p.add_argument("--all",action="store_true",help="include extended Panfrost capability parameters");a=p.parse_args()
fd=os.open('/dev/dri/renderD128',os.O_RDWR|os.O_CLOEXEC)
fields=['product_id','revision','shader_present','tiler_present','l2_present','stack_present','address_spaces','job_slots','l2_features','core_features','tiler_features','memory_features','mmu_features']
if a.all:
 fields += ['thread_features','max_threads','thread_max_workgroup_size','thread_max_barrier_size',
 'coherency_features',*[f'texture_features{i}' for i in range(4)],
 *[f'job_slot_features{i}' for i in range(16)],'core_groups','thread_tls_alloc','afbc_features',
 'system_timestamp','system_timestamp_frequency','allowed_jm_context_priorities','selected_coherency']
result={}
try:
 for number,name in enumerate(fields):
  request=bytearray(struct.pack('<IIQ',number,0,0))
  try:fcntl.ioctl(fd,0xc0106444,request,True)
  except OSError as error:
   if a.all and error.errno==errno.EINVAL:
    result[name]=None;continue
   raise
  result[name]=hex(struct.unpack_from('<Q',request,8)[0])
finally:os.close(fd)
print(json.dumps(result,indent=2))

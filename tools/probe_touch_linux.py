#!/usr/bin/env python3
"""Temporarily use spidev for NT36672A SRAM diagnostics.
Requires /tmp/mmio-access.so built from tools/mmio_access.c (aarch64 Linux).
No flash programming. Run as root on the owner's Begonia with private firmware.
Attempts driver reattachment; this kernel may require a PMOS reboot to restore
its IRQ mapping. --leave-unbound is for a controlled test series ending in reboot.
"""
import argparse,ctypes,fcntl,hashlib,mmap,os,pathlib,struct,subprocess,time
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('firmware',type=pathlib.Path);p.add_argument('--chunk',type=int,default=1024)
p.add_argument('--touch-seconds',type=int,default=0);p.add_argument('--idle-poll',action='store_true');p.add_argument('--irq-latches',action='store_true');p.add_argument('--linux-cs',action='store_true');p.add_argument('--already-unbound',action='store_true');p.add_argument('--native-fifo',action='store_true');p.add_argument('--leave-unbound',action='store_true');p.add_argument('--crc-bytes',type=int,choices=[2,3],default=3)
a=p.parse_args();lib=ctypes.CDLL('/tmp/mmio-access.so');lib.probe_read32.argtypes=[ctypes.c_void_p,ctypes.c_uint32];lib.probe_read32.restype=ctypes.c_uint32;lib.probe_write32.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32]
def address(m):return ctypes.addressof(ctypes.c_char.from_buffer(m))
def load32(m,o):return lib.probe_read32(address(m),o)
def store32(m,o,v):lib.probe_write32(address(m),o,v)
fw=a.firmware.read_bytes()
assert os.geteuid()==0 and b'xiaomi,begonia' in pathlib.Path('/proc/device-tree/compatible').read_bytes()
assert hashlib.sha256(fw).hexdigest()=='0b57dd62863f0f20ea285263a667af3fdd86f48473e23ac638efeba5c8f6ad03'
assert 1<=a.chunk<=2048
base=pathlib.Path('/sys/bus/spi');dev=base/'devices/spi1.0';original=base/'drivers/novatek-nvt-ts-spi'
assert ((dev/'driver').exists() and (dev/'driver').resolve()==original) or (a.already_unbound and not (dev/'driver').exists())
assert '11019000.spi' in str((dev/'of_node').resolve()) or 'spi@11019000' in str((dev/'of_node').resolve())
subprocess.run(['modprobe','spidev'],check=True)
fd=None;mem=None;gpio=None;spi=None;gic=None;irq_enabled=False;bound=False;power=pathlib.Path('/sys/bus/platform/devices/11019000.spi/power/control');old_power=power.read_text()
try:
 if (dev/'driver').exists():(original/'unbind').write_text('spi1.0')
 (dev/'driver_override').write_text('spidev')
 (base/'drivers/spidev/bind').write_text('spi1.0');bound=True
 mem=os.open('/dev/mem',os.O_RDWR|os.O_SYNC)
 gpio=mmap.mmap(mem,4096,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ|mmap.PROT_WRITE,offset=0x10005000)
 # Captured working Linux configuration: GPIO2 is an output, driven low.
 assert load32(gpio,0)&4
 store32(gpio,0x108,4);time.sleep(.02)
 fd=os.open('/dev/spidev1.0',os.O_RDWR)
 fcntl.ioctl(fd,0x40016b01,bytes([0]));fcntl.ioctl(fd,0x40016b03,bytes([8]));fcntl.ioctl(fd,0x40046b04,struct.pack('<I',1000000))
 def xfer(data):
  tx=ctypes.create_string_buffer(bytes(data));rx=ctypes.create_string_buffer(len(data))
  msg=struct.pack('<QQIIHBBBBBB',ctypes.addressof(tx),ctypes.addressof(rx),len(data),1000000,0,8,0,0,0,0,0)
  fcntl.ioctl(fd,0x40206b00,msg);return rx.raw
 if a.native_fifo:
  power.write_text('on')
  xfer(bytes([255,4,56]));print('KERNEL_SPI_INFO',xfer(bytes([0x78])+bytes(17)).hex(),flush=True);time.sleep(.1)
  spi=mmap.mmap(mem,4096,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ|mmap.PROT_WRITE,offset=0x11019000)
  def rd(off):return load32(spi,off)
  def wr(off,val):store32(spi,off,val)
  if a.irq_latches:
   assert 'MT_SYSIRQ 157 Level' in pathlib.Path('/proc/interrupts').read_text()
   gic=mmap.mmap(mem,4096,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ|mmap.PROT_WRITE,offset=0x0c000000)
   irq_enabled=bool(load32(gic,0x114)&(1<<29))
   store32(gic,0x194,1<<29);time.sleep(.05);assert not load32(gic,0x114)&(1<<29) # mask only SPI5 GIC interrupt 189
   print('GIC SPI5 masked; IRQ count before:',next(l for l in pathlib.Path('/proc/interrupts').read_text().splitlines() if '11019000.spi' in l),flush=True)
  print('LINUX_SPI_REGS',*[hex(rd(o)) for o in [0,4,0x18,0x24,0x28]],flush=True)
  # Leave the shared Linux SPI parent alone. Conservative 128-tick halves.
  wr(0x18,4);wr(0x18,0);wr(0x28,127|(127<<16));wr(0,5|(5<<16));wr(0x24,0)
  def xfer(data):
   data=bytes(data);rx=bytearray();done=0;paused=False;base=0x33000 if a.irq_latches else 0x3000
   wr(0x18,base|4);wr(0x18,base);rd(0x1c)
   while done<len(data):
    n=min(32,len(data)-done)
    wr(4,((n-1)<<16)|5)
    for off in range(0,n,4):wr(0x10,int.from_bytes(data[done+off:done+min(off+4,n)].ljust(4,b'\0'),'little'))
    wr(0x18,base|(16 if a.linux_cs or done+n<len(data) else 0)|(2 if paused else 1))
    deadline=time.monotonic()+.1;status=0
    if a.idle_poll:time.sleep(n*.000025+.00001)
    while time.monotonic()<deadline:
     if a.idle_poll:
      if rd(0x20)&1:
       status=rd(0x1c) or (2 if a.linux_cs or done+n<len(data) else 1);break
     else:
      status=rd(0x1c)
      if status&3:break
    if not status&3:
     print('IRQ after:',next(l for l in pathlib.Path('/proc/interrupts').read_text().splitlines() if '11019000.spi' in l),'GIC enabled',hex(load32(gic,0x114)) if gic else 'unmasked',flush=True)
     print('FIFO_TIMEOUT tx',data.hex(),'CMD',hex(rd(0x18)),'CFG1',hex(rd(4)),'STATUS1',hex(rd(0x20)),'runtime', (power.parent/'runtime_status').read_text().strip(),flush=True)
     raise RuntimeError('native FIFO timeout')
    paused=bool(status&2)
    for off in range(0,n,4):rx.extend(rd(0x14).to_bytes(4,'little')[:min(4,n-off)])
    done+=n
   if a.linux_cs:
    wr(0x18,rd(0x18)&~16);wr(0x18,rd(0x18)|4);wr(0x18,rd(0x18)&~4)
   elif paused:raise RuntimeError('native FIFO unexpectedly paused at end')
   return bytes(rx)
 def page(addr):xfer(bytes([255,(addr>>15)&255,(addr>>7)&255]))
 def read(addr,n):
  page(addr);return xfer(bytes([addr&127])+bytes(n+1))[2:]
 def write(addr,data):
  i=0
  while i<len(data):
   n=min(a.chunk,len(data)-i,128-((addr+i)&127)) if a.native_fifo else min(a.chunk,len(data)-i)
   page(addr+i);xfer(bytes([((addr+i)&127)|128])+data[i:i+n]);i+=n
 def number(addr,n,size):write(addr,n.to_bytes(size,'little'))
 def u32(off):return struct.unpack_from('<I',fw,off)[0]
 print('BEFORE reset',read(0x21c60,1).hex(),'fwinfo',read(0x21c78,16).hex(),flush=True)
 parts=[];header=u32(0);infos=(header-0x30)//16;overlays=(fw[0x28]&15) if fw[0x28]&16 else 0
 assert header>=0x30 and header<=len(fw) and (header-0x30)%16==0 and infos+overlays+2<=32
 for i in range(2+infos+overlays):
  if i<2:src,dst,n=struct.unpack_from('<III',fw,i*12)
  else:
   off=0x30+(i-2)*16 if i<2+infos else u32(12)+(i-2-infos)*16
   dst,n,src,_=struct.unpack_from('<IIII',fw,off)
  if n:
   assert src<len(fw) and n<len(fw)-src and dst+n<0x40000
   parts.append((src,dst,n+1))
 number(0x3f0fe,0x69,1);time.sleep(.02);number(0x3f310,0,1)
 for src,dst,n in parts:
  write(dst,fw[src:src+n]);got=read(dst,16)
  print('SECTION',hex(dst),n,'readback',got.hex(),'expected',fw[src:src+16].hex(),flush=True)
 for i in range(2):
  number(0x3f12c if i else 0x3f128,u32(4+i*12),3)
  number(0x3f130 if i else 0x3f118,u32(8+i*12),a.crc_bytes)
  number(0x3f104 if i else 0x3f100,u32(24+i*4),4)
 number(0x3f30e,read(0x3f30e,1)[0]|128,1)
 number(0x21c60,0,1);number(0x21c50,0xae,1);number(0x3f10d,1,1)
 status=0
 for i in range(100):
  time.sleep(.01);status=read(0x21c60,1)[0]
  if 0xa0<=status<=0xaf:break
 time.sleep(.1)
 print('AFTER reset',hex(status),'fwinfo',read(0x21c78,16).hex(),'crc',read(0x3f120,8).hex(),'flags',read(0x3f133,1).hex(),flush=True)
 if not 0xa0<=status<=0xaf:raise RuntimeError('Firmware did not start')
 print('FIRMWARE_BOOT_PASS',flush=True)
 if a.touch_seconds:
  assert 0<a.touch_seconds<=120
  last=None;end=time.monotonic()+a.touch_seconds
  print('PHYSICAL_TOUCH_CAPTURE_READY',flush=True)
  while time.monotonic()<end:
   b=read(0x21c00,65);points=[]
   for off in range(0,60,6):
    ident=b[off]>>3;status=b[off]&7
    if 1<=ident<=10 and status in (1,2):
     points.append((ident,(b[off+1]<<4)|(b[off+3]>>4),(b[off+2]<<4)|(b[off+3]&15)))
   now=tuple(points)
   if now!=last:print('TOUCH_SAMPLE',now,flush=True);last=now
   time.sleep(.02)

finally:
 if fd is not None:os.close(fd)
 if spi is not None:
  store32(spi,0x18,4);store32(spi,0x18,0);spi.close()
 if gic is not None:
  store32(gic,0x294,1<<29)
  if irq_enabled:store32(gic,0x114,1<<29)
  gic.close()
 if gpio is not None:gpio.close()
 if mem is not None:os.close(mem)
 if bound:(base/'drivers/spidev/unbind').write_text('spi1.0')
 (dev/'driver_override').write_text('\n')
 power.write_text(old_power)
 if not a.leave_unbound and not (dev/'driver').exists():
  try:(original/'bind').write_text('spi1.0')
  except OSError as e:print('Linux IRQ remapping prevents reattach; normal PMOS reboot required:',e,flush=True)
 print('Linux touchscreen driver restored' if (dev/'driver').exists() else 'Linux touch temporarily unbound; reboot PMOS to restore',flush=True)

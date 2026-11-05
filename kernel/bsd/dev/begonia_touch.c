/* Begonia NT36672A/SPI5 bring-up. Opt-in: begonia_touch=1.
 * Addresses/pins from the captured working Linux DT; clock fields from
 * Xiaomi clk-mt6785.c. FIFO transaction segmentation follows spi-mt65xx.c.
 * Private firmware is uploaded to controller SRAM. Missing/failed firmware
 * disables polling without stopping boot; no controller flash is written.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <sys/systm.h>
#include <sys/hakios.input.h>
#include <vm/vm_kern.h>
#include "begonia_touch_report.h"
extern kern_return_t thread_terminate(thread_t thread);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
static vm_offset_t spi, gpio, top, infra;
static struct begonia_touch_state contacts;
static uint16_t xmax, ymax;
static uint32_t events;
#define CMD 0x18
#define STATUS 0x1c
#define CMD_BASE ((1u<<12)|(1u<<13))
static uint32_t rd(vm_offset_t b,unsigned o) { return *(volatile uint32_t *)(b+o); }
static void wr(vm_offset_t b,unsigned o,uint32_t v) {
 *(volatile uint32_t *)(b+o)=v;__asm__ volatile("dsb sy" ::: "memory");
}
/* Pause keeps CS asserted across FIFO segments; the final segment finishes
 * and releases CS. Every segment has a bounded 20ms timeout. */
static int transfer(const uint8_t *tx,uint8_t *rx,unsigned len) {
 unsigned done=0;int paused=0;
 if (!len || len>256) return -1;
 wr(spi,CMD,CMD_BASE|4);wr(spi,CMD,CMD_BASE);
 (void)rd(spi,STATUS);
 while(done<len) {
  unsigned count=MIN(32u,len-done),i;uint32_t cmd=CMD_BASE;
  for(i=0;i<count;i+=4) {
   uint32_t word=0;unsigned j;for(j=0;j<4 && i+j<count;j++)word|=(uint32_t)tx[done+i+j]<<(8*j);
   wr(spi,0x10,word);
  }
  wr(spi,0x04,((count-1)<<16)|5);
  if(done+count<len)cmd|=1u<<4;
  wr(spi,CMD,cmd|(paused?2u:1u));
  unsigned timeout;uint32_t status=0;
  for(timeout=0;timeout<2000;timeout++) {
   status=rd(spi,STATUS);if(status&3)break;delay(10);
  }
  if(!(status&3)) { wr(spi,CMD,CMD_BASE|4);wr(spi,CMD,CMD_BASE);return -1; }
  paused=(status&2)!=0;
  for(i=0;i<count;i+=4) {
   uint32_t word=rd(spi,0x14);unsigned j;
   for(j=0;j<4 && i+j<count;j++)if(rx)rx[done+i+j]=word>>(8*j);
  }
  done+=count;
 }
 return paused?-1:0;
}
static int set_page(uint32_t address) {
 uint8_t tx[3]={0xff,address>>15,address>>7};return transfer(tx,NULL,3);
}
static int read_address(uint32_t address,uint8_t *dst,unsigned size) {
 uint8_t tx[128]={0},rx[128];
 if(size>126 || set_page(address))return -1;
 tx[0]=address&127;
 if(transfer(tx,rx,size+2))return -1;
 memcpy(dst,rx+2,size);return 0;
}
/* Controller SRAM upload, never flash programming. Firmware is supplied privately
 * as an ADT property; every section is validated before the first reset/write. */
static uint32_t le32(const uint8_t *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int write_address(uint32_t addr,const uint8_t *data,unsigned size) {
 while(size) {
  uint8_t tx[32];unsigned n=MIN(28u,MIN(size,128u-(addr&127)));
  if(set_page(addr))return -1;
  tx[0]=(addr&127)|128;memcpy(tx+1,data,n);
  if(transfer(tx,NULL,n+1))return -1;
  addr+=n;data+=n;size-=n;
 }
 return 0;
}
static int write_number(uint32_t addr,uint32_t number,unsigned bytes) {
 uint8_t b[4];for(unsigned i=0;i<4;i++)b[i]=number>>(8*i);
 return write_address(addr,b,bytes);
}
struct fw_section { uint32_t src,dst,size; };
static int firmware_upload(void) {
 DTEntry node;const uint8_t *fw;unsigned bytes;
 struct fw_section parts[32];unsigned count=0,infos,overlays,readback_bad=0;
 uint8_t crc=0,status=0;
 if(SecureDTLookupEntry(NULL,"/begonia-touch",&node)!=kSuccess ||
    SecureDTGetProperty(node,"firmware",(const void **)&fw,&bytes)!=kSuccess || bytes!=118784) {
  printf("BEGONIA TOUCH: no private firmware supplied\n");return -1;
 }
 uint32_t header=le32(fw),dlm=le32(fw+12);
 if(header<0x30 || header>bytes || (header-0x30)%16)return -1;
 infos=(header-0x30)/16;overlays=(fw[0x28]&16)?fw[0x28]&15:0;
 if(2+infos+overlays>32 || dlm>bytes || overlays*16>bytes-dlm)return -1;
 for(unsigned i=0;i<2+infos+overlays;i++) {
  uint32_t src,dst,len;
  if(i<2) {const uint8_t *p=fw+i*12;src=le32(p);dst=le32(p+4);len=le32(p+8);}
  else {const uint8_t *p=fw+(i<2+infos?0x30+(i-2)*16:dlm+(i-2-infos)*16);
   dst=le32(p);len=le32(p+4);src=le32(p+8);}
  if(!len)continue;
  if(src>=bytes || len>=bytes-src || dst>=0x40000 || len>=0x40000-dst)return -1;
  parts[count++]=(struct fw_section){src,dst,len+1};
 }
 printf("BEGONIA TOUCH: uploading %u firmware sections to controller RAM\n",count);
 if(write_number(0x3f0fe,0x69,1))return -1;
 delay_for_interval(20,NSEC_PER_MSEC);
 if(write_number(0x3f310,0,1))return -1;
 for(unsigned i=0;i<count;i++) {
  uint8_t check[16]={0};
  if(write_address(parts[i].dst,fw+parts[i].src,parts[i].size))return -1;
  if(read_address(parts[i].dst,check,sizeof(check)) || memcmp(check,fw+parts[i].src,sizeof(check))) {
   unsigned at=0;while(at<sizeof(check) && check[at]==fw[parts[i].src+at])at++;
   printf("BEGONIA TOUCH: firmware section %u readback mismatch at %x +%u\n",i,parts[i].dst,at);
   readback_bad++;
  }
 }
 printf("BEGONIA TOUCH: %u of %u section readbacks differ; hardware CRC will validate boot\n",readback_bad,count);
 /* Hardware CRC metadata uses this chip's map, not Apollo's NT36675 map. */
 for(unsigned i=0;i<2;i++) {
  if(write_number(i?0x3f12c:0x3f128,le32(fw+4+i*12),3) ||
     write_number(i?0x3f130:0x3f118,le32(fw+8+i*12),2) ||
     write_number(i?0x3f104:0x3f100,le32(fw+24+i*4),4))return -1;
 }
 if(read_address(0x3f30e,&crc,1) || write_number(0x3f30e,crc|128,1) ||
    write_number(0x21c60,0,1) || write_number(0x21c50,0xae,1) || write_number(0x3f10d,1,1))return -1;
 for(unsigned i=0;i<100;i++) {
  delay_for_interval(10,NSEC_PER_MSEC);
  if(!read_address(0x21c60,&status,1) && status>=0xa0 && status<=0xaf) {
   printf("BEGONIA TOUCH: controller firmware booted (status %02x)\n",status);return 0;
  }
 }
 uint8_t actual[8]={0},flags=0;
 (void)read_address(0x3f120,actual,sizeof(actual));(void)read_address(0x3f133,&flags,1);
 printf("BEGONIA TOUCH: controller firmware did not start (status %02x, CRC flags %02x, ILM %08x/%08x DLM %08x/%08x)\n",status,flags,le32(actual),le32(fw+24),le32(actual+4),le32(fw+28));return -1;
}
static void emit(__unused void *ctx,uint32_t type,uint32_t finger,uint32_t x,uint32_t y) {
 hakios_touch_post(type,finger,x,y);events++;
 if(events<=20)printf("BEGONIA TOUCH: type %u finger %u x %u y %u\n",type,finger,x,y);
}
static void touch_thread(__unused void *arg,__unused wait_result_t result) {
 uint8_t info[16],reset=0,report[65];unsigned polls=0,bad=0,errors=0;
 spi=ml_io_map(0x11019000,0x1000);gpio=ml_io_map(0x10005000,0x1000);
 top=ml_io_map(0x10000000,0x1000);infra=ml_io_map(0x10001000,0x1000);
 if(!spi || !gpio || !top || !infra)goto stop;
 /* GPIO35..38 function5; GPIO1 IRQ, GPIO2 reset.
  * MediaTek MT6785 GPIO: 0x004 is DIR_SET, 0x008 is DIR_CLR; 0x104 is DOUT_SET, 0x108 is DOUT_CLR.
  * Novatek TP_RESX is active-low (ts_reset_active is output-high in DT). Pulse low then drive high. */
 uint32_t mode=rd(gpio,0x340);
 for(unsigned pin=35;pin<=38;pin++) { unsigned shift=(pin%8)*4;mode=(mode&~(15u<<shift))|(5u<<shift); }
 wr(gpio,0x340,mode);
 wr(gpio,0x300,rd(gpio,0x300)&~((15u<<4)|(15u<<8)));
 wr(gpio,0x004,1u<<2); /* GPIO2 is output (DIR_SET) */
 wr(gpio,0x008,1u<<1); /* GPIO1 IRQ is input (DIR_CLR) */
 wr(gpio,0x108,1u<<2); /* DOUT_CLR: pulse reset low */
 delay_for_interval(10,NSEC_PER_MSEC);
 wr(gpio,0x104,1u<<2); /* DOUT_SET: deassert reset high (TP_RESX high) */
 delay_for_interval(20,NSEC_PER_MSEC);
 wr(top,0x78,(3u<<8)|(1u<<15));wr(top,0x04,1u<<21); /* 26MHz crystal */
 wr(infra,0xa8,1u<<26); /* SPI5 clock gate clear */
 wr(spi,CMD,4);wr(spi,CMD,0);
 wr(spi,0x28,12u|(12u<<16)); /* 26MHz / 26 = 1MHz during bring-up */
 wr(spi,0,5u|(5u<<16));wr(spi,0x24,0);
 printf("BEGONIA TOUCH: SPI5 configured, GPIO1 IRQ level %u\n",(rd(gpio,0x200)>>1)&1);
 if(firmware_upload()) {printf("BEGONIA TOUCH: firmware initialization failed; boot continues\n");goto stop;}
 delay_for_interval(100,NSEC_PER_MSEC);
 if(read_address(0x21c60,&reset,1) || read_address(0x21c78,info,sizeof(info))) {
  printf("BEGONIA TOUCH: SPI read timed out; controller disabled\n");goto stop;
 }
 xmax=((uint16_t)info[4]<<8)|info[5];ymax=((uint16_t)info[6]<<8)|info[7];
 printf("BEGONIA TOUCH: reset %02x firmware %02x/%02x bounds %u x %u\n",reset,info[0],info[1],xmax,ymax);
 if(reset<0xa0 || reset>0xaf || (unsigned)info[0]+info[1]!=255 || xmax!=1080 || ymax!=2340) {
  printf("BEGONIA TOUCH: resident firmware not ready/recognized; host firmware download required\n");goto stop;
 }
 printf("BEGONIA TOUCH: polling enabled (8ms); feeding hakios-touch\n");
 for(;;) {
  if(read_address(0x21c00,report,sizeof(report))) {
   if(++errors>=5) {printf("BEGONIA TOUCH: repeated SPI failures; polling stopped\n");goto stop;}
  } else {errors=0;if(begonia_touch_decode(&contacts,report,sizeof(report),xmax,ymax,emit,NULL))bad++;}
  if(++polls%7500==0)printf("BEGONIA TOUCH: polls %u bad %u events %u\n",polls,bad,events);
  delay_for_interval(8,NSEC_PER_MSEC);
 }
stop:
 {uint8_t empty[65]={0};if(xmax && ymax)(void)begonia_touch_decode(&contacts,empty,sizeof(empty),xmax,ymax,emit,NULL);}
 thread_terminate(current_thread());
}
void begonia_touch_init(void) {
 uint32_t on=0;thread_t thread;
 if(!PE_parse_boot_argn("begonia_touch",&on,sizeof(on)) || on!=1)return;
 if(kernel_thread_start(touch_thread,NULL,&thread)==KERN_SUCCESS)thread_deallocate(thread);
}

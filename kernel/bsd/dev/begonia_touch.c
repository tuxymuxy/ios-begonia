/* Begonia NT36672A/SPI5 bring-up. Opt-in: begonia_touch=1.
 * Addresses/pins from the captured working Linux DT; clock fields from
 * Xiaomi clk-mt6785.c. FIFO transaction segmentation follows spi-mt65xx.c.
 * This first stage requires resident controller firmware; it does not reset
 * the controller or write its firmware. Missing firmware is reported and
 * disables polling without stopping boot.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
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
 if(size>126 || (address&127)+size>128 || set_page(address))return -1;
 tx[0]=address&127;
 if(transfer(tx,rx,size+2))return -1;
 memcpy(dst,rx+2,size);return 0;
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
 /* GPIO35..38, function5, from spi5-pins. Leave reset GPIO2 high as LK
  * left it: toggling it would erase resident touch-controller firmware. */
 uint32_t mode=rd(gpio,0x340);
 for(unsigned pin=35;pin<=38;pin++) { unsigned shift=(pin%8)*4;mode=(mode&~(15u<<shift))|(5u<<shift); }
 wr(gpio,0x340,mode);
 wr(top,0x78,(3u<<8)|(1u<<15));wr(top,0x04,1u<<21); /* 26MHz crystal */
 wr(infra,0xa8,1u<<26); /* SPI5 clock gate clear */
 wr(spi,CMD,4);wr(spi,CMD,0);
 wr(spi,0x28,2u|(2u<<16)); /* 26MHz / 6 = 4.33MHz (< DT 5MHz limit) */
 wr(spi,0,5u|(5u<<16));wr(spi,0x24,0);
 printf("BEGONIA TOUCH: SPI5 configured, GPIO1 IRQ level %u\n",(rd(gpio,0x200)>>1)&1);
 if(read_address(0x21c60,&reset,1) || read_address(0x21c78,info,sizeof(info))) {
  printf("BEGONIA TOUCH: SPI read timed out; controller disabled\n");goto stop;
 }
 xmax=((uint16_t)info[4]<<8)|info[5];ymax=((uint16_t)info[6]<<8)|info[7];
 printf("BEGONIA TOUCH: reset %02x firmware %02x/%02x bounds %u x %u\n",reset,info[0],info[1],xmax,ymax);
 if(reset<0xa1 || reset>0xaf || (unsigned)info[0]+info[1]!=255 || xmax!=1080 || ymax!=2340) {
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
 thread_terminate(current_thread());
}
void begonia_touch_init(void) {
 uint32_t on=0;thread_t thread;
 if(!PE_parse_boot_argn("begonia_touch",&on,sizeof(on)) || on!=1)return;
 if(kernel_thread_start(touch_thread,NULL,&thread)==KERN_SUCCESS)thread_deallocate(thread);
}

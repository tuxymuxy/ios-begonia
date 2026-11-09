/* SPDX-License-Identifier: MIT
 * An unused CPU supervises the single-core XNU test. No storage writes.
 * Primary CPU is the only CPU advertised to XNU. The watcher retains the
 * bootloader's low RAM code/stack until the whole SoC resets.
 */
#include <stdint.h>
extern char watcher_entry[];
extern void begonia_emit_text(const char *);
extern void begonia_emit_hex(const char *,uint64_t);
char watcher_stack[8192] __attribute__((aligned(16)));
struct context { uint64_t log, length, console; uint32_t seconds, request; };
static struct context ctx;
static uint64_t ticks(void){uint64_t n;__asm__ volatile("isb; mrs %0,cntpct_el0":"=r"(n));return n;}
static uint64_t freq(void){uint64_t n;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(n));return n;}
static uint64_t smc(uint64_t fn,uint64_t a,uint64_t b,uint64_t c){
 register uint64_t x0 __asm__("x0")=fn,x1 __asm__("x1")=a,x2 __asm__("x2")=b,x3 __asm__("x3")=c;
 __asm__ volatile("smc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3)::"x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");return x0;
}
/* Mirror above 4 GiB: FASTBOOT download buffers may overwrite low ramoops RAM.
 * The paired PMOS recovery DTB reserves this same 2 MiB range. */
__attribute__((unused)) static void mirror_logs(struct context *c){
 _Static_assert(128+0x40000+1536*1024<=0x200000,"mirror fits reserved range");
 volatile uint64_t *h=(volatile uint64_t *)0x1b7e00000ULL;
 volatile unsigned char *dst=(volatile unsigned char *)(h+16);
 volatile unsigned char *src=(volatile unsigned char *)0x4d09f000;
 uint32_t n=c->length?*(volatile uint32_t *)c->length:0;
 if(!c->log || n>1536*1024)n=0;

 h[0]=0;
 uint64_t ch=0x811c9dc5,eh=0x811c9dc5;
 for(uint32_t i=0;i<0x40000;i++){unsigned char v=src[i];dst[i]=v;ch=((ch^v)*0x01000193)&0xffffffff;}
 src=(volatile unsigned char *)c->log;
 for(uint32_t i=0;i<n;i++){unsigned char v=src[i];dst[0x40000+i]=v;eh=((eh^v)*0x01000193)&0xffffffff;}
 h[1]=0x40000;h[2]=n;h[3]=freq();h[4]=ticks();h[5]=ch;h[6]=eh;h[7]=c->console?*(volatile uint64_t *)c->console:0;h[8]=c->request;
 __asm__ volatile("dsb sy":::"memory");
 h[0]=0x3230474f4c474542ULL; /* BEGLOG02, written last */
 __asm__ volatile("dsb sy":::"memory");
}
void watcher_main(struct context *c){
 uint64_t hz=freq(),end=ticks()+hz*c->seconds,next=0;
 begonia_emit_text("BEGONIA WATCHER: independent supervisor running\n");
 while(c->seconds == 0 || ticks()<end){
#ifndef QEMU_UART
  if(ticks()>=next){
   volatile uint32_t *wdt=(volatile uint32_t *)0x10007000;
   if(wdt[0]&1){wdt[2]=0x1971;__asm__ volatile("dsb sy":::"memory");}
   next=ticks()+hz;
  }
#else
  (void)next;
#endif
 }
 begonia_emit_text("BEGONIA WATCHER: deadline reached\n");
#if !defined(QEMU_UART) || defined(TEST_LOG_MIRROR)
 mirror_logs(c);
#endif
 if(c->log && c->length && (!c->console || !*(volatile uint64_t *)c->console)){
  uint32_t n=*(volatile uint32_t *)c->length;
  if(n && n<=1536*1024){
   if(n>256*1024)n=256*1024;
   begonia_emit_text("BEGONIA WATCHER: early kernel log follows\n");
   volatile unsigned char *log=(volatile unsigned char *)c->log;
   for(uint32_t i=0;i<n;i++){
    char text[2]={(char)log[i],0};
    if(text[0]=='\n'||text[0]=='\r'||text[0]=='\t'||(text[0]>=32&&text[0]<=126))begonia_emit_text(text);
   }
  }
 }
 __asm__ volatile("dsb sy":::"memory");
#ifdef QEMU_UART
 begonia_emit_text("BEGONIA WATCHER: emulated reset verified\n");
 (void)smc(0x84000002,0,0,0);
#else
#ifdef XNU_FORCE_TEST
 begonia_emit_text("BEGONIA WATCHER: reset after log capture; recover through postmarketOS\n");
#else
 begonia_emit_text("BEGONIA WATCHER: warm reset to postmarketOS\n");
#endif
 (void)smc(0x84000008,0,0,0);
 *(volatile uint32_t *)0x10007014=0x1209;
#endif
 for(;;)__asm__ volatile("wfe");
}
int watcher_start(uint64_t log,uint64_t length,uint64_t console,uint32_t seconds,uint32_t request){
 ctx.log=log;ctx.length=length;ctx.console=console;ctx.seconds=seconds;ctx.request=request;
 __asm__ volatile("dsb sy":::"memory");
#ifdef QEMU_UART
 uint64_t core=1;
#else
 uint64_t core=0x100; /* live MT6785 cpu1 reg, not used by single-core XNU */
#endif
 uint64_t rc=smc(0xc4000003,core,(uint64_t)watcher_entry,(uint64_t)&ctx);
 begonia_emit_hex("BEGONIA_WATCHER_CPU_ON=",rc);
 return rc==0;
}

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../kernel/bsd/dev/begonia_touch_report.h"
static unsigned n, typ[32], finger[32], xx[32], yy[32];
static void event(void *ctx,uint32_t t,uint32_t f,uint32_t x,uint32_t y) {
 (void)ctx;assert(n<32);typ[n]=t;finger[n]=f;xx[n]=x;yy[n++]=y;
}
static void contact(uint8_t *p,unsigned index,unsigned id,unsigned x,unsigned y) {
 uint8_t *r=p+index*6;r[0]=(id<<3)|1;r[1]=x>>4;r[2]=y>>4;r[3]=(x<<4)|(y&15);
}
int main(void) {
 struct begonia_touch_state s={0}, before;uint8_t p[65]={0};
 contact(p,0,1,540,1170);contact(p,1,10,1080,2340);
 assert(!begonia_touch_decode(&s,p,65,1080,2340,event,0));
 assert(n==2 && typ[0]==1 && finger[1]==9 && xx[0]==32767 && yy[1]==65535);
 assert(!begonia_touch_decode(&s,p,65,1080,2340,event,0) && n==2);
 p[0]=(1<<3)|2;p[1]=0;p[3]&=15;
 assert(!begonia_touch_decode(&s,p,65,1080,2340,event,0));assert(n==3 && typ[2]==2 && xx[2]==0);
 before=s;memset(p,0xfd,65);assert(begonia_touch_decode(&s,p,65,1080,2340,event,0)==BEG_TOUCH_WATCHDOG);assert(!memcmp(&s,&before,sizeof s));
 memset(p,0x77,6);assert(begonia_touch_decode(&s,p,65,1080,2340,event,0)==BEG_TOUCH_RECOVERY);
 memset(p,0,65);contact(p,0,1,1081,1);assert(begonia_touch_decode(&s,p,65,1080,2340,event,0)==BEG_TOUCH_BOUNDS);
 memset(p,0,65);contact(p,0,1,10,10);contact(p,1,1,20,20);assert(begonia_touch_decode(&s,p,65,1080,2340,event,0)==BEG_TOUCH_DUPLICATE);
 assert(begonia_touch_decode(&s,p,64,1080,2340,event,0)==-1);
 memset(p,0xff,65);assert(!begonia_touch_decode(&s,p,65,1080,2340,event,0));assert(n==5 && typ[3]==3 && typ[4]==3);
 puts("Touch report tests passed: multitouch, scaling, motion, lift, malformed/recovery frames");
}

/* Read-only Linux snapshot. Never reads FIFO or clear-on-read status registers. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
static volatile uint32_t *map(int fd,uint64_t at) {
 void *p=mmap(0,4096,PROT_READ,MAP_SHARED,fd,at);
 if(p==MAP_FAILED){perror("mmap");return 0;}return p;
}
int main(void) {
 int fd=open("/dev/mem",O_RDONLY|O_SYNC);if(fd<0){perror("/dev/mem");return 1;}
 volatile uint32_t *g=map(fd,0x10005000),*t=map(fd,0x10000000),*i=map(fd,0x10001000),*s=0;
 char state[32]={0};FILE *power=fopen("/sys/bus/platform/devices/11019000.spi/power/runtime_status","r");
 if(power){fgets(state,sizeof(state),power);fclose(power);}
 if(!strncmp(state,"active",6))s=map(fd,0x11019000);
 if(!g||!t||!i)return 2;
 printf("GPIO DIR0=%08x OUT0=%08x IN0=%08x MODE0=%08x MODE4=%08x\n",g[0],g[0x100/4],g[0x200/4],g[0x300/4],g[0x340/4]);
 printf("TOP CLK_CFG5=%08x INFRA_STA2=%08x\n",t[0x70/4],i[0xac/4]);
 if(s)printf("SPI5 CFG0=%08x CFG1=%08x CMD=%08x PAD=%08x CFG2=%08x\n",s[0],s[1],s[0x18/4],s[0x24/4],s[0x28/4]);
 if(!s)puts("SPI5 register snapshot skipped: controller is runtime-suspended");
 return 0;
}

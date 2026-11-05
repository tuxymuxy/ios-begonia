/* SPDX-License-Identifier: MIT
 * Read only the Mu progress marker from reserved pmsg memory; no media access.
 */
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
 int fd=open("/dev/mem",O_RDWR|O_SYNC);
 if(fd<0){perror("open");return 1;}
 void *map=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x4d0e0000);
 if(map==MAP_FAILED){perror("mmap");close(fd);return 1;}
 volatile uint32_t *p=(volatile uint32_t *)((char *)map+512);
 __asm__ volatile("dc civac,%0; dsb sy"::"r"(p):"memory");
 uint32_t value=*p;
 printf("Mu progress marker: 0x%08x\n",value);
 if((value&0xffff0000)==0x4d550000)printf("Mu handoff step: %u\n",value&0xffff);
 munmap(map,4096);close(fd);return 0;
}

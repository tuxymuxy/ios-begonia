/* SPDX-License-Identifier: MIT
 * Update only the reserved one-shot request word; clean/invalidate its cache
 * line to avoid an old alias overwriting the request during shutdown.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;
 char *end;unsigned long n=strtoul(argv[1],&end,0);if(*end || n>0xffffffffUL)return 2;
 int fd=open("/dev/mem",O_RDWR|O_SYNC);if(fd<0){perror("open");return 1;}
 void *map=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x4d0e0000);
 if(map==MAP_FAILED){perror("mmap");return 1;}
 volatile uint32_t *p=(volatile uint32_t *)((char *)map+256);
 __asm__ volatile("dc civac,%0; dsb sy"::"r"(p):"memory");
 *p=(uint32_t)n;
 __asm__ volatile("dsb sy; dc civac,%0; dsb sy"::"r"(p):"memory");
 uint32_t actual=*p;
 printf("Reserved request word readback: 0x%08x\n",actual);
 munmap(map,4096);close(fd);return actual!=(uint32_t)n;
}

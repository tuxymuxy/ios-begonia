/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void){
 int fd=open("/dev/mem",O_RDONLY|O_SYNC);if(fd<0){perror("open");return 1;}
 void *p=mmap(0,0x200000,PROT_READ,MAP_SHARED,fd,0x1b7e00000ULL);
 if(p==MAP_FAILED){perror("mmap");return 1;}
 volatile uint64_t *h=p;
 if(h[0]!=0x3230474f4c474542ULL||h[1]!=0x40000||h[2]>1536*1024){fprintf(stderr,"No valid captured log header\n");return 2;}
 size_t n=128+h[1]+h[2],done=0;
 while(done<n){ssize_t r=write(1,(char *)p+done,n-done);if(r<=0){perror("write");return 1;}done+=r;}
 munmap(p,0x200000);close(fd);return 0;
}

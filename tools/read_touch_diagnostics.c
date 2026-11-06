/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../kernel/bsd/dev/begonia_touch_diagnostics.h"
int main(int argc,char **argv) {
 if(argc>2)return 2;
 int fd=open("/dev/mem",O_RDONLY|O_SYNC);if(fd<0){perror("open");return 1;}
 void *mapping=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,BEG_TOUCH_DIAG_PHYS);
 if(mapping==MAP_FAILED){perror("mmap");close(fd);return 1;}
 volatile uint32_t *input=mapping;struct begonia_touch_diagnostics d={0};int good=0;
 for(unsigned attempt=0;attempt<100;attempt++) {
  uint32_t before=input[5],words[sizeof(d)/4];if(before&1)continue;
  for(unsigned i=0;i<sizeof(d)/4;i++)words[i]=input[i];
  __asm__ volatile("dsb sy":::"memory");
  if(before!=input[5])continue;memcpy(&d,words,sizeof(d));
  if(begonia_touch_diag_valid(&d)){good=1;break;}
 }
 munmap(mapping,4096);close(fd);
 if(!good){fprintf(stderr,"No valid touch record/checksum\n");return 1;}
 if(argc==2 && d.epoch!=strtoull(argv[1],NULL,0)){fprintf(stderr,"Stale touch record epoch\n");return 1;}
 printf("{\"epoch\":%"PRIu64",\"checksums_valid\":true,\"stage\":%u,\"polls\":%u,\"bad\":%u,\"events\":%u,\"spi_errors\":%u,\"invalid\":%u,\"recovery\":%u,\"watchdog\":%u,\"invalid_id\":%u,\"duplicate_id\":%u,\"bounds\":%u,\"irq\":%u,\"max_read_us\":%.3f,\"last_time\":%"PRIu64",\"last\":\"",
 d.epoch,d.stage,d.polls,d.bad,d.events,d.reasons[0],d.reasons[1],d.reasons[2],d.reasons[3],d.reasons[4],d.reasons[5],d.reasons[6],d.irq,d.longest_read_ns/1000.0,d.last_time);
 for(unsigned i=0;i<8;i++)printf("%02x",d.last[i]);puts("\"}");
 return 0;
}

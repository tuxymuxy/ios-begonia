#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }
    void *p = mmap(0, 0x200000, PROT_READ, MAP_SHARED, fd, 0x1b7e00000ULL);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    volatile uint64_t *h = p;
    printf("h[0]=0x%llx (expected 0x3230474f4c474542)\n", (unsigned long long)h[0]);
    printf("h[1]=0x%llx (expected 0x40000)\n", (unsigned long long)h[1]);
    printf("h[2]=0x%llx\n", (unsigned long long)h[2]);
    printf("h[3]=0x%llx\n", (unsigned long long)h[3]);
    printf("raw: ");
    for (int i = 0; i < 32; i++) printf("%02x ", ((uint8_t*)p)[i]);
    printf("\n");
    return 0;
}

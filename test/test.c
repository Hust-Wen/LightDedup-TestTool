#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<malloc.h>
#include<fcntl.h>
#include<time.h>

struct IO_info {
	long long int time_t;
	int device;
	int lpn;
	int size;
	int ope;
};

static inline unsigned long read_tsc(void) {
    unsigned long var;
    unsigned int hi, lo;

    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    var = ((unsigned long long int) hi << 32) | lo;

    return var;
}

int main(void)
{
    unsigned char* buf;
    unsigned long t;
    FILE *fp1, *fp2, *fp3;
    int fd;
    size_t len;
    int ret, n, lbn_random;
    struct IO_info io_information;

    srand( (unsigned)time( NULL ) );
    ret = posix_memalign((void **)&buf, 4096, 4096);
    fd = open("/dev/mapper/mydedup", O_RDWR | O_DIRECT);
    fp1 = fopen("/home/femu/test/newinput", "rb");
    fp2 = fopen("/home/femu/test/rand.trace","r");
    fp3 = fopen("/home/femu/test/output", "wb");

    t = read_tsc();

    n = 5;
    for(int i = 0; i < n; i++) {
	fscanf(fp2, "%llu %d %d %d %d\n", &(io_information.time_t), &(io_information.device),
            &(io_information.lpn), &(io_information.size), &(io_information.ope));
	if(io_information.ope == 0) {
	    lbn_random = io_information.lpn;
	    len = 4096ll * lbn_random;

	    fread(buf, 4096, 1, fp1);
	    pwrite(fd, buf, 4096, len);
	    printf("pwrite\n");
	}
	else if(io_information.ope == 1) {
	    lbn_random = io_information.lpn;
            len = 4096ll * lbn_random;

            pread(fd, buf, 4096, len);
            fwrite(buf, 4096, 1, fp3);
            printf("pread\n");
	}
    }

    t = read_tsc() - t;
    t /= 2200;
    printf("t = %lu\n", t);

    fclose(fp1);
    fclose(fp2);
    fclose(fp3);
    close(fd);
    return 0;
}

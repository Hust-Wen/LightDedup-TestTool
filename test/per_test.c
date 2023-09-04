#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<malloc.h>
#include<fcntl.h>
#include<time.h>

struct IO_info {
	long long int time_t;
	unsigned int device;
	unsigned int lpn;
	unsigned int size;
	unsigned int ope;
};

unsigned int fingerprint[64];

static inline unsigned long read_tsc(void) {
    unsigned long var;
    unsigned int hi, lo;

    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    var = ((unsigned long long int) hi << 32) | lo;

    return var;
}

static inline void set_unique_data(int* data_p, struct IO_info IO)
{
    for(int i=0; i<IO.size; i++) {
        *data_p = *(data_p+333) = *(data_p+666) = *(data_p+999) = fingerprint[i];
        data_p += 1024;
    }
}

int main(int argc , char* argv[])
{
    unsigned int* data;
    unsigned long t, second_begin_t, second_end_t;
    FILE *fp_trace;
    int fd;
    size_t offset, size;
    int ret, n;
    struct IO_info io_information;

    char trace_path[100];
	if(argc == 1) {
        sprintf(trace_path, "/home/femu/test/randwrite-4G.trace");
	}
	else if(argc == 2) {
        sprintf(trace_path, "/home/femu/test/%s", argv[1]);
	}
	else {
		printf("wrong argc\n");
		return 0;
	}
    
    fp_trace = fopen(trace_path,"r");

    srand( (unsigned)time( NULL ) );
    ret = posix_memalign((void **)&data, 4096, 1048576);  //1M
    //fd = open("/dev/mapper/mydedup", O_RDWR | O_DIRECT);
    fd = open("/dev/md0", O_RDWR | O_DIRECT);

    t = read_tsc();

    //n = 8;
    //for(int i = 0; i < n; i++) 
    while(!feof(fp_trace))
    {
        fread(&io_information, sizeof(struct IO_info), 1, fp_trace);
        fread(&fingerprint, sizeof(unsigned int), io_information.size, fp_trace);

        offset = 4096ll * io_information.lpn;
        size = 4096 * io_information.size;

        // printf("%2d: pwrite, lpn=%d, size=%d, op=%d, fingerprint=", i, io_information.lpn, io_information.size, io_information.ope);
        // for(int j=0; j<io_information.size; j++)
        //     printf(" %u", io_information.fingerprint[j]);

        if(io_information.ope == 0) {
            set_unique_data(data, io_information);
            //printf(",\n");
            pwrite(fd, data, size, offset);
        }
        else if(io_information.ope == 1) {
            pread(fd, data, size, offset);
        }
    }

    t = read_tsc() - t;
    second_begin_t = read_tsc();
    sleep(1);
    second_end_t = read_tsc();
    t /= (second_end_t - second_begin_t);
    printf("running time: %lus, tsc per second:%lu\n", t, second_end_t - second_begin_t);

    fclose(fp_trace);
    close(fd);
    return 0;
}

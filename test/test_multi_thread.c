#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<malloc.h>
#include<fcntl.h>
#include<time.h>
#include<math.h>
#include<pthread.h>
#include<string.h>

struct IO_info {
	long long int time_t;
	unsigned int device;
	unsigned int lpn;
	unsigned int size;
	unsigned int ope;
};

struct Thread_info {
    int thread_id;
    char trace_name[100];
    char latency_name[100];
    int thread_N;
};

unsigned long tsc_per_second = 0;
unsigned long tsc_per_us = 0;
unsigned int fingerprint[64];
char device_name[100];
unsigned int IOPS[100][2];
int running_flag = 1;
FILE *fp_IOPS;
char fp_IOPS_file_path[100] = "/home/femu/running_res.log";

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
        // *data_p = *(data_p+333) = *(data_p+666) = *(data_p+999) = fingerprint[i];
        *data_p = fingerprint[i];
        data_p += 1024;
    }
}

void output_per_second_thread(void *thread_info)
{
    unsigned int total_IOPS[2], last_IOPS[2]={0,0};
    int thread_N = ((struct Thread_info*)thread_info)->thread_N;
    fp_IOPS = fopen(fp_IOPS_file_path,"w");
    while(running_flag == 1) {
        sleep(1);
        total_IOPS[0] = total_IOPS[1] = 0;
        for(int i=0; i<thread_N; i++) {
            total_IOPS[0] += IOPS[i][0];
            total_IOPS[1] += IOPS[i][1];
        }
        fprintf(fp_IOPS, "total_IOPS:(%d)(%d), last_second_IOPS:(%d)(%d)\n", total_IOPS[0], total_IOPS[1], total_IOPS[0]-last_IOPS[0], total_IOPS[1]-last_IOPS[1]);
        fflush(fp_IOPS);
        last_IOPS[0] = total_IOPS[0];
        last_IOPS[1] = total_IOPS[1];
    }
}

void run_thread(void *thread_info)
{
    int thread_id = ((struct Thread_info*)thread_info)->thread_id;

    unsigned int* data;
    FILE *fp_trace;
    FILE *fp_latency;
    int fd;
    size_t offset, size;
    struct IO_info io_information;
    
    fp_trace = fopen(((struct Thread_info*)thread_info)->trace_name,"r");
    fp_latency = fopen(((struct Thread_info*)thread_info)->latency_name,"w");

    srand( (unsigned)time( NULL ) );
    posix_memalign((void **)&data, 4096, 1048576);  //1M
    //fd = open("/dev/mapper/mydedup", O_RDWR | O_DIRECT);
    fd = open(device_name, O_RDWR | O_DIRECT);

    while(!feof(fp_trace))
    {
        fread(&io_information, sizeof(struct IO_info), 1, fp_trace);
        fread(&fingerprint, sizeof(unsigned int), io_information.size, fp_trace);

        offset = 4096ll * io_information.lpn;
        size = 4096 * io_information.size;

        // printf("%2d: pwrite, lpn=%d, size=%d, op=%d, fingerprint=", i, io_information.lpn, io_information.size, io_information.ope);
        // for(int j=0; j<io_information.size; j++)
        //     printf(" %u", io_information.fingerprint[j]);

        fprintf(fp_latency, "submit:%d\t", io_information.lpn);
        fflush(fp_latency);
        unsigned long IO_submit_time = read_tsc();
        if(io_information.ope == 0) {
            set_unique_data(data, io_information);
            //printf(",\n");
            pwrite(fd, data, size, offset);
            IOPS[thread_id][0]+=io_information.size;
        }
        else if(io_information.ope == 1) {
            pread(fd, data, size, offset);
            IOPS[thread_id][1]+=io_information.size;
        }
        unsigned long IO_finish_time = read_tsc();
        fprintf(fp_latency, "finish:%d\tlatency:%ldus\n", io_information.lpn, (IO_finish_time - IO_submit_time) / tsc_per_us);
        fflush(fp_latency);
    }

    fclose(fp_trace);
    fclose(fp_latency);
    close(fd);
}

int main(int argc , char* argv[])
{
    tsc_per_second = read_tsc();
    sleep(1);
    tsc_per_second = read_tsc() - tsc_per_second;
    tsc_per_us = tsc_per_second / 1e6;

    unsigned long running_time = read_tsc();

	if(argc != 8) {
        printf("wrong argc\n");
		return 0;
	}
	else {
        char trace_path[100];
        pthread_t tid[100], output_tid;
        strcpy(device_name, argv[1]);
        int trace_size = strtol(argv[3], NULL, 10);
        int user_LPN_range = strtol(argv[4], NULL, 10);
        int dedup_ratio = strtol(argv[5], NULL, 10);
        int threads = strtol(argv[6], NULL, 10);
        int IO_size = strtol(argv[7], NULL, 10);

        for (int i=0; i<threads; i++)
            IOPS[i][0] = IOPS[i][1] = 0;
        struct Thread_info* output_info = malloc(sizeof(struct Thread_info));
        output_info->thread_N = threads;
        if (pthread_create(&output_tid, NULL, (void*)output_per_second_thread, (void *)output_info) != 0) {
            printf("pthread_create error.");
            exit(EXIT_FAILURE);
        }
        // else {
        //     printf("running output_per_second_thread success\n");
        // }

        for (int i=0; i<threads; i++) {
            struct Thread_info* thread_info = malloc(sizeof(struct Thread_info));
            thread_info->thread_id = i;
            sprintf(thread_info->trace_name, "%s-%dG-%dG-%d%%-%dKB-%d-b", argv[2], trace_size, user_LPN_range, dedup_ratio, IO_size, i);
            sprintf(thread_info->latency_name, "/home/femu/latency-%d.log", i);
            if (pthread_create(&tid[i], NULL, (void*)run_thread, (void *)thread_info) != 0) {
                printf("pthread_create error.");
                exit(EXIT_FAILURE);
            }
            // else {
            //     printf("running %s success\n", trace_path);
            // }
        }
        for (int i=0; i<threads; i++) {
            void* rev = NULL;
            pthread_join(tid[i], &rev);
        }
        running_flag = 0;
        void* rev = NULL;
        pthread_join(output_tid, &rev);
	}

    running_time = (read_tsc() - running_time) / tsc_per_second;
    fprintf(fp_IOPS, "running time: %lus, tsc per second:%lu\n", running_time, tsc_per_second);
    fclose(fp_IOPS);
    return 0;
}

#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<malloc.h>
#include<fcntl.h>
#include<time.h>
#include<pthread.h>
#include<string.h>
#include<errno.h>
#include<sys/queue.h>
#include<stdatomic.h> 
#include<ctype.h>
#include <stdbool.h> 
#include "voidQueue.h"


#define ffprintf(fmt, args...) {fprintf(out_fp, fmt, ## args); fflush(out_fp);}
#define assert_fopen(fp, file_name, open_type, tips, return_value) { \
    if ((fp = fopen(file_name, open_type)) == NULL) { \
        ffprintf("%s: open %s error!\n", tips, file_name); \
        return return_value; \
    }else{ \
        ffprintf("%s: open %s success!\n", tips, file_name); \
    }}
#define isdigitstr(str) (strspn(str, "0123456789")==strlen(str))

#define MINUTE 60ul
#define HOUR MINUTE*60ul
#define MAX_RUNNING_TIME HOUR*100ul

#define TRACE_N 1

bool trace_finish;
atomic_uint workload_finish;
size_t device_size;
int thread_count;
voidQueue IO_queue;
pthread_mutex_t queue_mutex_lock;
FILE *out_fp;
FILE *IOPS_fp;
time_t begin_time;

struct Statisic_Info {
    int IOPS[2];    //[0]Write;[1]Read
    size_t sum_IOPS[2];    //[0]Write;[1]Read
}statisic;

struct IO_info {
    //[ts in ns] [pid] [process] [lba] [size in 512 Bytes blocks] [Write or Read] [major device number] [minor device number] [MD5 per 4096 Bytes]
	size_t time; 
    int pid;
    char process[64];
	size_t lba;
	int size;
	char ope;
    int major_device;
    int minor_device;
    unsigned char data[4096];
};

struct workload_fp_info {
    FILE* workload_fp;
    long int workload_end_offset;
    int workload_id;
    size_t min_lba;
    size_t lba_num;
};

struct Print_Thread_Info {
    pthread_t thread_pid;
    char outfile_name[128];
};

struct Reading_Thread_Info {
    pthread_t thread_pid;
    char trace_path[128];
    int trace_id;
};

struct Running_Thread_Info {
    int id;
    pthread_t thread_pid;
    char device_name[128];
};

static inline unsigned long read_tsc(void) {
    unsigned long var;
    unsigned int hi, lo;

    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    var = ((unsigned long long int) hi << 32) | lo;

    return var;
}

void *workload_read(void *arg) {
    struct Reading_Thread_Info* thread_info = (struct Reading_Thread_Info*)arg;
    char trace_name[1024];
    char input_line[40960];

    int split_num = 21;
    voidQueue active_workload_queue;
    initQueue(&active_workload_queue, sizeof(struct workload_fp_info), split_num);
    
    if(thread_info->trace_id == 0) {
        for(int i=0; i<split_num; i++) {
            sprintf(trace_name, "%s%d%s", thread_info->trace_path, i+1, ".blkparse");

            struct workload_fp_info cur_workload;
            assert_fopen(cur_workload.workload_fp, trace_name, "r", "workload_read thread", NULL);
            fseek(cur_workload.workload_fp, 0, SEEK_END);
            cur_workload.workload_end_offset = ftell(cur_workload.workload_fp);
            cur_workload.workload_id = i;
            cur_workload.lba_num = device_size / split_num / 8 * 8;     //4KB page align
            cur_workload.min_lba = cur_workload.lba_num * i;
            fseek(cur_workload.workload_fp, 0, SEEK_SET);

            if(enQueue(&active_workload_queue, &cur_workload) == FALSE) {
                ffprintf("%s: enQueue error!\n", "workload_read");
            }
            else {
                ffprintf("thread-%d: begin:%ld, end:%ld, lba_num:%ld, min_lba:%ld!\n", 
                        cur_workload.workload_id, ftell(cur_workload.workload_fp), cur_workload.workload_end_offset,
                        cur_workload.lba_num, cur_workload.min_lba);
            }
        }
    }
    else {
        sprintf(trace_name, "%s%d%s", thread_info->trace_path, thread_info->trace_id, ".blkparse");

        struct workload_fp_info cur_workload;
        assert_fopen(cur_workload.workload_fp, trace_name, "r", "workload_read thread", NULL);
        fseek(cur_workload.workload_fp, 0, SEEK_END);
        long int avg_workload_size = ftell(cur_workload.workload_fp) / split_num;
        fclose(cur_workload.workload_fp);

        for(int i=0; i<split_num; i++) {
            assert_fopen(cur_workload.workload_fp, trace_name, "r", "workload_read thread", NULL);
            cur_workload.workload_end_offset = avg_workload_size * (i+1);
            cur_workload.workload_id = i;
            cur_workload.lba_num = device_size / split_num / 8 * 8;     //4KB page align
            cur_workload.min_lba = cur_workload.lba_num * i;
            fseek(cur_workload.workload_fp, avg_workload_size * i, SEEK_SET);

            if(enQueue(&active_workload_queue, &cur_workload) == FALSE) {
                ffprintf("%s: enQueue error!\n", "workload_read");
            }
            else {
                ffprintf("thread-%d: begin:%ld, end:%ld, avg_size:%ld, lba_num:%ld, min_lba:%ld!\n", 
                        cur_workload.workload_id, ftell(cur_workload.workload_fp), cur_workload.workload_end_offset, avg_workload_size,
                        cur_workload.lba_num, cur_workload.min_lba);
            }
        }
    }

    struct workload_fp_info next_workload;
    struct IO_info io_information;
    while(1) {
        if(delQueue(&active_workload_queue, &next_workload) == FALSE) {
            ffprintf("%s: delQueue error!\n", "workload_read");
            usleep(1000);
            continue;
        }
        
        fgets(input_line, 40960, next_workload.workload_fp);
        int space_n = 0;
        for(char* c=input_line; (*c)!='\0'; c++)
            if((*c) == ' ') space_n++;
        if(space_n == 8) {
            sscanf(input_line, "%lu %d %s %lu %d %c %d %d %s\n", &(io_information.time), &(io_information.pid), (io_information.process), &(io_information.lba), 
                &(io_information.size), &(io_information.ope), &(io_information.major_device), &(io_information.minor_device), (io_information.data));
            
            if(io_information.lba % 8 != 0 || (io_information.ope != 'R' && io_information.ope != 'W')) {
                ffprintf("%s: lba error: %lu %d %s %lu %d %c %d %d %s\n", "workload_read thread", (io_information.time), (io_information.pid), (io_information.process), (io_information.lba), 
                        (io_information.size), (io_information.ope), (io_information.major_device), (io_information.minor_device), (io_information.data));
            } else {
                // *((int *)(io_information.data+4092)) = next_workload.workload_id;
                io_information.lba = ((io_information.lba % next_workload.lba_num + next_workload.min_lba) >> 3) << 12;     //lba (sector, 512B) -> offset (B)

                pthread_mutex_lock(&queue_mutex_lock);
                while(enQueue(&IO_queue, &io_information) == FALSE) {   //max 10240 entries
                    pthread_mutex_unlock(&queue_mutex_lock);
                    usleep(200000);  //200ms support 1s/200ms*(10240*4KB)=200MB/s bandwidth
                    pthread_mutex_lock(&queue_mutex_lock);
                }
                pthread_mutex_unlock(&queue_mutex_lock);
            }
        }
        if(feof(next_workload.workload_fp) || ftell(next_workload.workload_fp) >= next_workload.workload_end_offset) {
            ffprintf("workload %d finish! end_fp:%ld\n", next_workload.workload_id, ftell(next_workload.workload_fp));
            fclose(next_workload.workload_fp);
        }
        else
            enQueue(&active_workload_queue, &next_workload);

        if(isEmptyQueue(&active_workload_queue))
            break;
    }
    trace_finish = true;
}

void *print_info_per_second(void *arg) {
    struct Print_Thread_Info* print_thread_info = (struct Print_Thread_Info*)arg;
    assert_fopen(IOPS_fp, print_thread_info->outfile_name, "w", "print_info_per_second thread", NULL);

    static int running_time = 0;
    while(workload_finish < thread_count) {
        sleep(1);
        fprintf(IOPS_fp, "%d TotalIOPS:%d ReadIOPS:%d WriteIOPS:%d SumReadIOPS:%ld(%.2fGB) SumWriteIOPS:%ld(%.2fGB), total:%.2fGB\n", ++running_time, 
            statisic.IOPS[0]+statisic.IOPS[1], statisic.IOPS[1], statisic.IOPS[0], 
            statisic.sum_IOPS[1], (float)statisic.sum_IOPS[1]*4/1024/1024,
            statisic.sum_IOPS[0], (float)statisic.sum_IOPS[0]*4/1024/1024,
            (float)(statisic.sum_IOPS[0]+statisic.sum_IOPS[1])*4/1024/1024);
        fflush(IOPS_fp);
        statisic.IOPS[0] = statisic.IOPS[1] = 0;
    }
}

void *running_workload(void *arg) {
    struct Running_Thread_Info* running_thread_info = (struct Running_Thread_Info*)arg;

    char* buffer;
    posix_memalign((void **)&buffer, 4096, 4096);

    int fd = -1;
    if ((fd = open(running_thread_info->device_name, O_RDWR | O_DIRECT)) == -1) {
        ffprintf("%d: open %s error!\n", running_thread_info->id, running_thread_info->device_name);
        return NULL;
    }
    else {
        ffprintf("%d: open %s success!\n", running_thread_info->id, running_thread_info->device_name);
    }

    struct IO_info io_information;
    while(1) {
        pthread_mutex_lock(&queue_mutex_lock);
        while(delQueue(&IO_queue, &io_information) == FALSE) {
            if (trace_finish)   break;
            pthread_mutex_unlock(&queue_mutex_lock);
            usleep(1000);
            pthread_mutex_lock(&queue_mutex_lock);
        }
        pthread_mutex_unlock(&queue_mutex_lock);
        if (trace_finish)   break;

        // // *((int *)(io_information->data+4092)) = running_thread_info->id;
        // io_information->lba = ((io_information->lba % running_thread_info->lba_num + running_thread_info->min_lba) >> 3) << 12;     //lba (sector, 512B) -> offset (B)

        if(io_information.ope == 'W') {
            for(int i=0; i<io_information.size; i+=8) {
                memcpy(buffer, io_information.data, 4096);
                int write_bytes = 0;
                write_bytes = pwrite(fd, buffer, 4096, (off_t)io_information.lba + ((off_t)i<<9));
                if(write_bytes == -1) {
                    ffprintf("%d: write %lu (%lu + %d*512) error!\n", running_thread_info->id, io_information.lba + (i<<9), io_information.lba, i);
                    perror("error write:");
                    // return NULL;
                }
                statisic.IOPS[0]++;
                statisic.sum_IOPS[0]++;
            }
        }
        else if(io_information.ope == 'R') {
            for(int i=0; i<io_information.size; i+=8) {
                int read_bytes = 0;
                read_bytes = pread(fd, buffer, 4096, (off_t)io_information.lba + ((off_t)i<<9));
                if(read_bytes == -1) {
                    ffprintf("%d: read %lu error!\n", running_thread_info->id, io_information.lba + (i<<9));
                    perror("error read:");
                    // return NULL;
                }
                statisic.IOPS[1]++;
                statisic.sum_IOPS[1]++;
            }
        }
        else {
            ffprintf("%d: error type: %c!\n", running_thread_info->id, io_information.ope);
            // return NULL;
        }
    }
    atomic_fetch_add(&workload_finish, 1);
    close(fd);
    ffprintf("thread-%d running finish!\n", running_thread_info->id);
}

int main(int argc, char **argv) // 1[target_device] 2[total_lbas_of_device] 3[trace_name] 4[IOPS_file]
{
    out_fp = NULL;
    if ((out_fp = fopen("replay_trace.log", "w")) == NULL) {
        printf("%s: open %s error!\n", "main", "replay_trace.log");
        return 0;
    }else{
        ffprintf("%s: open %s success!\n", "main", "replay_trace.log");
    }

    if(argc!=6)
    {
        ffprintf("Error parameters!\n");
        return 0;
    }

    ffprintf("%s %s %s %s %s %d %d %d\n", argv[1], argv[2], argv[3], argv[4], argv[5], 
            strcmp(argv[3], "webvm"), strcmp(argv[3], "homes"), strcmp(argv[3], "mail"));
    device_size = atoi(argv[2]);
    ffprintf("Device size: %.1f GB!\n", (float)device_size/2/1024/1024);
    thread_count = atoi(argv[5]);


    //configure trace_read thread---------------------------------------------------------------------------------   
    struct Reading_Thread_Info* reading_thread_info = malloc(sizeof(struct Reading_Thread_Info));
    memset(reading_thread_info, 0, sizeof(struct Reading_Thread_Info));

    char *split_left, *split_right;
    split_left = strtok_r(argv[3], "-", &split_right);
    if(strcmp(split_left, "webvm") == 0)
        sprintf(reading_thread_info->trace_path, "/home/femu/traces/web-vm/webmail+online.cs.fiu.edu-110108-113008.");
    else if(strcmp(split_left, "homes") == 0)
        sprintf(reading_thread_info->trace_path, "/home/femu/traces/homes/homes-110108-112108.");
    else if(strcmp(split_left, "mail") == 0)
        sprintf(reading_thread_info->trace_path, "/home/femu/traces/mail/cheetah.cs.fiu.edu-110108-113008.");
    else {
        ffprintf("Unknow Trace: %s!\n", split_left);
        return 0;
    }

    if (split_right == NULL)           reading_thread_info->trace_id = 0;
    else if (isdigitstr(split_right))  reading_thread_info->trace_id = atoi(split_right);
    else{
        ffprintf("Unknow Trace: %s-%s!\n", split_left, split_right);
        return 0;
    }
    
    initQueue(&IO_queue, sizeof(struct IO_info), 10240);
    // struct IO_info* queue_data = IO_queue.data;
    // for(int i=0; i<10240, i++)  posix_memalign((void **)&(queue_data[i].data), 4096, 40960);

    trace_finish = false;
    if (pthread_mutex_init(&queue_mutex_lock, NULL) != 0) {
        ffprintf("mutex init failed\n");
        return 0;
    }
    pthread_create(&reading_thread_info->thread_pid, NULL, workload_read, reading_thread_info);


    //configure IOPS_statistic thread-----------------------------------------------------------------------------
    struct Print_Thread_Info* print_thread_info = malloc(sizeof(struct Print_Thread_Info));
    memset(print_thread_info, 0, sizeof(struct Print_Thread_Info));

    atomic_store(&workload_finish, 0);
    strcpy(print_thread_info->outfile_name, argv[4]);
    pthread_create(&print_thread_info->thread_pid, NULL, print_info_per_second, print_thread_info);


    //configure workload_running thread-----------------------------------------------------------------------------
    struct Running_Thread_Info* running_thread_info = malloc(sizeof(struct Running_Thread_Info)*thread_count);
    memset(running_thread_info, 0, sizeof(struct Running_Thread_Info)*thread_count);
    for(int thread_id=0; thread_id<thread_count; thread_id++)
    {
        running_thread_info[thread_id].id = thread_id+1;
        // running_thread_info[thread_id].lba_num = device_size / thread_count / 8 * 8;     //4KB page align
        // running_thread_info[thread_id].min_lba = running_thread_info[thread_id].lba_num * thread_id;
        // running_thread_info[thread_id].lba_num = device_size / 8 * 8;     //4KB page align
        // running_thread_info[thread_id].min_lba = 0;
        strcpy(running_thread_info[thread_id].device_name, argv[1]);
        
        pthread_create(&running_thread_info[thread_id].thread_pid, NULL, running_workload, &running_thread_info[thread_id]);
    }
    
    //waiting threads finish---------------------------------------------------------------------------------------
    pthread_join(reading_thread_info->thread_pid, NULL);
    for(int thread_id=0; thread_id<thread_count; thread_id++)
    {
        pthread_join(running_thread_info[thread_id].thread_pid, NULL);
    }
    pthread_join(print_thread_info->thread_pid, NULL);

    //printf final IOPS
    time_t current_time = time(NULL);
    float tt_running_time = difftime(current_time, begin_time);
    fprintf(IOPS_fp, "result: SumReadIOPS:%ld SumWriteIOPS:%ld AvgReadIOPS:%.0f AvgWriteIOPS:%.0f\n",
            statisic.sum_IOPS[1], statisic.sum_IOPS[0], statisic.sum_IOPS[1]/tt_running_time, statisic.sum_IOPS[0]/tt_running_time);
    fflush(IOPS_fp);
    fclose(IOPS_fp);
    
    fclose(out_fp);
    return 0;
}

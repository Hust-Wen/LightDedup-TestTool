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
long running_time;
int thread_count;
struct Queue IO_queue;
pthread_mutex_t queue_mutex_lock;
FILE *out_fp;

#define QUEUE_ZISE 10240    //队列长度

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
    unsigned char *data;
};

struct Queue
{
	int qFront;//队首
	int qRear;//队尾
	struct IO_info IO_array[QUEUE_ZISE];//队列数据
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
    size_t min_lba;
    size_t lba_num;
};

//初始化队列
void InitQueue(struct Queue* queue)
{
    memset(queue, 0, sizeof(struct Queue));
    for(int i=0; i<QUEUE_ZISE; i++) {
        posix_memalign((void **)&(queue->IO_array[i].data), 4096, 40960);
    }
}

//队列是否为空
_Bool IsEmptyQueue(struct Queue* queue)
{
    return queue->qFront == queue->qRear;
}

//队列是否为满
_Bool IsFullQueue(struct Queue* queue)
{
	return (((queue->qRear + 1) % QUEUE_ZISE) == queue->qFront);
}

//入队
struct IO_info* EnterQueue(struct Queue* queue)
{
	if (IsFullQueue(queue))
	{
		return NULL;
	}

	//从队尾入队
	struct IO_info* res = &(queue->IO_array[queue->qRear]);//入队数据
	queue->qRear = (queue->qRear + 1) % QUEUE_ZISE;//队尾移向下个位置
    return res;
}

//出队
struct IO_info* OutQueue(struct Queue* queue)
{
	if (IsEmptyQueue(queue))
	{
		return NULL;
	}
    struct IO_info* res = &(queue->IO_array[queue->qFront]);//出队值
	queue->qFront = (queue->qFront + 1) % QUEUE_ZISE;//指向下一个出队值
    return res;
}

//队列使用空间
int CountQueue(struct Queue* queue)
{
	int cur = 0;
	int len = 0;

	cur = queue->qFront;
	while (cur != queue->qRear)
	{
		len++;
		cur = (cur + 1) % QUEUE_ZISE;
	}

	return len;
}

//队列剩余空间
int ResidueQueue(struct Queue* queue)
{
	int len = 0;
	int cur = 0;
	int res = 0;

	cur = queue->qFront;
	while (cur != queue->qRear)
	{
		len++;
		cur = (cur + 1) % QUEUE_ZISE;
	}

	res = QUEUE_ZISE - 1 - len;
	return res;
}



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
    struct IO_info *io_information = NULL;

    FILE *workload = NULL;
    int current_trace_id = 1;
    while(current_trace_id <= thread_count) {
        sprintf(trace_name, "%s%d%s", thread_info->trace_path, thread_info->trace_id == 0 ? current_trace_id : thread_info->trace_id, ".blkparse");
        assert_fopen(workload, trace_name, "r", "workload_read thread", NULL);

        while(!feof(workload) && running_time < MAX_RUNNING_TIME) {
            if(IsFullQueue(&IO_queue)) {
                usleep(1000);
                continue;
            }

            fgets(input_line, 40960, workload);
            int space_n = 0;
            for(char* c=input_line; (*c)!='\0'; c++)
                if((*c) == ' ') space_n++;
            if(space_n == 8) {
                io_information = NULL;
                while(io_information == NULL) {
                    pthread_mutex_lock(&queue_mutex_lock);
                    io_information = EnterQueue(&IO_queue);
                    pthread_mutex_unlock(&queue_mutex_lock);
                    if(io_information == NULL)  usleep(1000);
                    else break;
                }

                sscanf(input_line, "%lu %d %s %lu %d %c %d %d %s\n", &(io_information->time), &(io_information->pid), (io_information->process), &(io_information->lba), 
                    &(io_information->size), &(io_information->ope), &(io_information->major_device), &(io_information->minor_device), (io_information->data));
                
                if(io_information->lba % 8 != 0 || (io_information->ope != 'R' && io_information->ope != 'W')) {
                    ffprintf("%s: lba error: %lu %d %s %lu %d %c %d %d %s\n", "workload_read thread", (io_information->time), (io_information->pid), (io_information->process), (io_information->lba), 
                            (io_information->size), (io_information->ope), (io_information->major_device), (io_information->minor_device), (io_information->data));
                }
            }
        }
        fclose(workload);
        ffprintf("workload %s read finish!\n", trace_name);

        if(thread_info->trace_id != 0) 
            break;
        else
            current_trace_id++;
    }
    trace_finish = true;
}

void *running_workload(void *arg) {
    struct Running_Thread_Info* running_thread_info = (struct Running_Thread_Info*)arg;
    struct IO_info* io_information;

    int fd = -1;
    if ((fd = open(running_thread_info->device_name, O_RDWR | O_DIRECT)) == -1) {
        ffprintf("%d: open %s error!\n", running_thread_info->id, running_thread_info->device_name);
        return NULL;
    }
    else {
        ffprintf("%d: open %s success!\n", running_thread_info->id, running_thread_info->device_name);
    }

    while(running_time < MAX_RUNNING_TIME) {
        io_information = NULL;
        pthread_mutex_lock(&queue_mutex_lock);
        io_information = OutQueue(&IO_queue);
        pthread_mutex_unlock(&queue_mutex_lock);
        if(io_information == NULL) {
            if(trace_finish)    break;
            usleep(1000);
            continue;
        }

        // *((int *)(io_information->data+4092)) = running_thread_info->id;
        io_information->lba = ((io_information->lba % running_thread_info->lba_num + running_thread_info->min_lba) >> 3) << 12;     //lba (sector, 512B) -> offset (B)

        if(io_information->ope == 'W') {
            for(int i=0; i<io_information->size; i+=8) {
                int write_bytes = 0;
                write_bytes = pwrite(fd, io_information->data, 4096, (off_t)io_information->lba + ((off_t)i<<9));
                if(write_bytes == -1) {
                    ffprintf("%d: write %lu (%lu + %d*512) error!\n", running_thread_info->id, io_information->lba + (i<<9), io_information->lba, i);
                    perror("error write:");
                    // return NULL;
                }
                statisic.IOPS[0]++;
            }
        }
        else if(io_information->ope == 'R') {
            for(int i=0; i<io_information->size; i+=8) {
                int read_bytes = 0;
                read_bytes = pread(fd, io_information->data, 4096, (off_t)io_information->lba + ((off_t)i<<9));
                if(read_bytes == -1) {
                    ffprintf("%d: read %lu error!\n", running_thread_info->id, io_information->lba + (i<<9));
                    perror("error read:");
                    // return NULL;
                }
                statisic.IOPS[1]++;
            }
        }
        else {
            ffprintf("%d: error type: %c!\n", running_thread_info->id, io_information->ope);
            // return NULL;
        }
    }
    atomic_fetch_add(&workload_finish, 1);
    close(fd);
    ffprintf("thread-%d running finish!\n", running_thread_info->id);
}

void *print_info_per_second(void *arg) {
    struct Print_Thread_Info* print_thread_info = (struct Print_Thread_Info*)arg;
    FILE* IOPS_fp = NULL;
    assert_fopen(IOPS_fp, print_thread_info->outfile_name, "w", "print_info_per_second thread", NULL);

    while(workload_finish < thread_count) {
        sleep(1);
        fprintf(IOPS_fp, "%ld TotalIOPS:%d ReadIOPS:%d WriteIOPS:%d\n", running_time++, 
            statisic.IOPS[0]+statisic.IOPS[1], statisic.IOPS[1], statisic.IOPS[0]);
        fflush(IOPS_fp);
        statisic.sum_IOPS[0] += statisic.IOPS[0];
        statisic.sum_IOPS[1] += statisic.IOPS[1];
        statisic.IOPS[0] = statisic.IOPS[1] = 0;
    }
    fprintf(IOPS_fp, "result: SumReadIOPS:%ld SumWriteIOPS:%ld AvgReadIOPS:%ld AvgWriteIOPS:%ld\n",
            statisic.sum_IOPS[1], statisic.sum_IOPS[0], statisic.sum_IOPS[1]/running_time, statisic.sum_IOPS[0]/running_time);
    fflush(IOPS_fp);
    fclose(IOPS_fp);
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
    size_t device_size = atoi(argv[2]);
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
    
    trace_finish = false;
    InitQueue(&IO_queue);
    if (pthread_mutex_init(&queue_mutex_lock, NULL) != 0) {
        ffprintf("mutex init failed\n");
        return 0;
    }
    pthread_create(&reading_thread_info->thread_pid, NULL, workload_read, reading_thread_info);


    //configure IOPS_statistic thread-----------------------------------------------------------------------------
    struct Print_Thread_Info* print_thread_info = malloc(sizeof(struct Print_Thread_Info));
    memset(print_thread_info, 0, sizeof(struct Print_Thread_Info));

    atomic_store(&workload_finish, 0);
    running_time = 0;
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
        running_thread_info[thread_id].lba_num = device_size / 8 * 8;     //4KB page align
        running_thread_info[thread_id].min_lba = 0;
        strcpy(running_thread_info[thread_id].device_name, argv[1]);
        
        pthread_create(&running_thread_info[thread_id].thread_pid, NULL, running_workload, &running_thread_info[thread_id]);
    }
    
    //waiting threads finish---------------------------------------------------------------------------------------
    pthread_join(reading_thread_info->thread_pid, NULL);
    pthread_join(print_thread_info->thread_pid, NULL);
    for(int thread_id=0; thread_id<thread_count; thread_id++)
    {
        pthread_join(running_thread_info[thread_id].thread_pid, NULL);
    }
    
    fclose(out_fp);
    return 0;
}

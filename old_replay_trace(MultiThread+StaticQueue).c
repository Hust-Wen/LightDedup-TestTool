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

#define ffprintf(fmt, args...) {printf(fmt, ## args); fflush(stdout);}

#define MINUTE 60ul
#define HOUR MINUTE*60ul
#define MAX_RUNNING_TIME HOUR*100ul

#define TRACE_N 1

atomic_uint workload_finish;
long running_time;

#define QUEUE_ZISE 1024    //队列长度

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

struct Thread_Info {
    int id;
    pthread_t thread_pid[4];    //workload_read, running_workload[]
    char device_name[100];
    char trace_name[100];
    char IOPS_outfile_name[100];
    size_t min_lba;
    size_t lba_num;
    int trace_finish;
    struct Queue IO_queue;
    pthread_mutex_t queue_mutex_lock;
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
		ffprintf("fail enterqueue, the queue is full\n");
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
		ffprintf("fail outqueue, the queue is null\n");
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
    FILE *workload = NULL;
    if ((workload = fopen(((struct Thread_Info*)arg)->trace_name, "r")) == NULL) {
        ffprintf("%d: open %s error!\n", ((struct Thread_Info*)arg)->id, ((struct Thread_Info*)arg)->trace_name);
        return NULL;
    }
    else {
        ffprintf("%d: open %s success!\n", ((struct Thread_Info*)arg)->id, ((struct Thread_Info*)arg)->trace_name);
    }

    size_t min_lba = ((struct Thread_Info*)arg)->min_lba;
    size_t lba_num = ((struct Thread_Info*)arg)->lba_num;
    ffprintf("%d: min_lba:%ld, lba_num:%ld\n", ((struct Thread_Info*)arg)->id, min_lba, lba_num);

    struct Queue* pqueue = &((struct Thread_Info*)arg)->IO_queue;
    pthread_mutex_t* queue_mutex_lock_p = &(((struct Thread_Info*)arg)->queue_mutex_lock);
    char input_line[40960];
    while(!feof(workload) && running_time < MAX_RUNNING_TIME) {
        if(IsFullQueue(pqueue)) {
            usleep(1000);
            continue;
        }

        fgets(input_line, 40960, workload);
        int space_n = 0;
        for(char* c=input_line; (*c)!='\0'; c++)
            if((*c) == ' ') space_n++;
        if(space_n == 8) {
            pthread_mutex_lock(queue_mutex_lock_p);
            struct IO_info *io_information = EnterQueue(pqueue);
            pthread_mutex_unlock(queue_mutex_lock_p);
            sscanf(input_line, "%lu %d %s %lu %d %c %d %d %s\n", &(io_information->time), &(io_information->pid), (io_information->process), &(io_information->lba), 
                &(io_information->size), &(io_information->ope), &(io_information->major_device), &(io_information->minor_device), (io_information->data));
            
            if(io_information->lba % 8 != 0) {
                ffprintf("%d: lba error: %lu %d %s %lu %d %c %d %d %s\n", ((struct Thread_Info*)arg)->id, (io_information->time), (io_information->pid), (io_information->process), (io_information->lba), 
                        (io_information->size), (io_information->ope), (io_information->major_device), (io_information->minor_device), (io_information->data));
            }

            *((int *)(io_information->data+4092)) = ((struct Thread_Info*)arg)->id;
            io_information->lba = ((io_information->lba % lba_num + min_lba) >> 3) << 12;     //lba (sector, 512B) -> offset (B)
        }
    }
    ((struct Thread_Info*)arg)->trace_finish = 1;
    fclose(workload);
    ffprintf("workload %s read finish!\n", ((struct Thread_Info*)arg)->trace_name);
}

void *running_workload(void *arg) {
    int fd = -1;
    if ((fd = open(((struct Thread_Info*)arg)->device_name, O_RDWR | O_DIRECT)) == -1) {
        ffprintf("%d: open %s error!\n", ((struct Thread_Info*)arg)->id, ((struct Thread_Info*)arg)->device_name);
        return NULL;
    }
    else {
        ffprintf("%d: open %s success!\n", ((struct Thread_Info*)arg)->id, ((struct Thread_Info*)arg)->device_name);
    }

    struct Queue* pqueue = &((struct Thread_Info*)arg)->IO_queue;
    pthread_mutex_t* queue_mutex_lock_p = &(((struct Thread_Info*)arg)->queue_mutex_lock);
    while(((struct Thread_Info*)arg)->trace_finish == 0 && running_time < MAX_RUNNING_TIME) {
        if(IsEmptyQueue(pqueue)) {
            usleep(1000);
            continue;
        }

        pthread_mutex_lock(queue_mutex_lock_p);
        struct IO_info* io_information = OutQueue(pqueue);
        pthread_mutex_unlock(queue_mutex_lock_p);
        if(io_information->ope == 'W') {
            for(int i=0; i<io_information->size; i+=8) {
                int write_bytes = 0;
                write_bytes = pwrite(fd, io_information->data, 4096, (off_t)io_information->lba + ((off_t)i<<9));
                if(write_bytes == -1) {
                    ffprintf("%d: write %lu (%lu + %d*512) error!\n", ((struct Thread_Info*)arg)->id, io_information->lba + (i<<9), io_information->lba, i);
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
                    ffprintf("%d: read %lu error!\n", ((struct Thread_Info*)arg)->id, io_information->lba + (i<<9));
                    perror("error read:");
                    // return NULL;
                }
                statisic.IOPS[1]++;
            }
        }
        else {
            ffprintf("%d: error type: %c!\n", ((struct Thread_Info*)arg)->id, io_information->ope);
            return NULL;
        }
    }
    atomic_fetch_add(&workload_finish, 1);
    close(fd);
    ffprintf("workload %s running finish!\n", ((struct Thread_Info*)arg)->trace_name);
}

void *print_info_per_second(void *arg) {
    FILE* IOPS_fp = NULL;
    if((IOPS_fp = fopen(((struct Thread_Info*)arg)->IOPS_outfile_name, "w")) == NULL) {
        ffprintf("open %s fail\n", ((struct Thread_Info*)arg)->IOPS_outfile_name);
        return NULL;
    }
    else {
        ffprintf("open %s success\n", ((struct Thread_Info*)arg)->IOPS_outfile_name);
    }

    while(workload_finish < TRACE_N*3) {
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
    fflush(stdout);
	setvbuf(stdout, NULL, _IONBF, 0);
	freopen("replay_trace.log", "w", stdout);

    if(argc!=5)
    {
        ffprintf("Error parameters!\n");
        return 0;
    }

    ffprintf("%s %s %s %s %d %d %d\n", argv[1], argv[2], argv[3], argv[4], strcmp(argv[3], "webvm"), strcmp(argv[3], "homes"), strcmp(argv[3], "mail"));

    char *dir_name = (char *)malloc(sizeof(char)*100);
    if(strcmp(argv[3], "0") == 0 || strcmp(argv[3], "webvm") == 0)
        sprintf(dir_name, "/home/femu/traces/web-vm/webmail+online.cs.fiu.edu-110108-113008.");
    else if(strcmp(argv[3], "1") == 0 || strcmp(argv[3], "homes") == 0)
        sprintf(dir_name, "/home/femu/traces/homes/homes-110108-112108.");
    else if(strcmp(argv[3], "2") == 0 || strcmp(argv[3], "mail") == 0)
        sprintf(dir_name, "/home/femu/traces/mail/cheetah.cs.fiu.edu-110108-113008.");
    else {
        ffprintf("Unknow Trace: %s!\n", argv[3]);
        return 0;
    }

    size_t device_size = atoi(argv[2]);
    ffprintf("Device size: %.1f GB!\n", (float)device_size/2/1024/1024);

    struct Thread_Info* thread_info = malloc(sizeof(struct Thread_Info)*(TRACE_N+1));
    memset(thread_info, 0, sizeof(struct Thread_Info)*(TRACE_N+1));

    atomic_store(&workload_finish, 0);
    running_time = 0;
    thread_info[0].id = 0;
    strcpy(thread_info[0].IOPS_outfile_name, argv[4]);
    pthread_create(&thread_info[0].thread_pid[0], NULL, print_info_per_second, &thread_info[0]);
    for(int thread_id=1; thread_id<=TRACE_N; thread_id++)
    {
        thread_info[thread_id].id = thread_id;
        if (TRACE_N > 1)
            sprintf(thread_info[thread_id].trace_name, "%s%d%s", dir_name, thread_id, ".blkparse");
        else
            sprintf(thread_info[thread_id].trace_name, "%s%d%s", dir_name, 1, ".blkparse");
        strcpy(thread_info[thread_id].device_name, argv[1]);

        thread_info[thread_id].lba_num = device_size / TRACE_N / 8 * 8;     //4KB page align
        thread_info[thread_id].min_lba = thread_info[thread_id].lba_num * (thread_id-1);
        
        InitQueue(&(thread_info[thread_id].IO_queue));
        if (pthread_mutex_init(&(thread_info[thread_id].queue_mutex_lock), NULL) != 0) {
            ffprintf("mutex(%d) init failed\n", thread_id);
            return 0;
        }
        pthread_create(&thread_info[thread_id].thread_pid[0], NULL, workload_read, &thread_info[thread_id]);
        pthread_create(&thread_info[thread_id].thread_pid[1], NULL, running_workload, &thread_info[thread_id]);
        pthread_create(&thread_info[thread_id].thread_pid[2], NULL, running_workload, &thread_info[thread_id]);
        pthread_create(&thread_info[thread_id].thread_pid[3], NULL, running_workload, &thread_info[thread_id]);
    }
    
    pthread_join(thread_info[0].thread_pid[0], NULL);
    for(int thread_id=1; thread_id<=TRACE_N; thread_id++)
    {
        pthread_join(thread_info[thread_id].thread_pid[0], NULL);
        pthread_join(thread_info[thread_id].thread_pid[1], NULL);
        pthread_join(thread_info[thread_id].thread_pid[2], NULL);
        pthread_join(thread_info[thread_id].thread_pid[3], NULL);
    }
    
    return 0;
}

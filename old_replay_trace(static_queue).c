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

#define MINUTE 60ul
#define HOUR MINUTE*60ul
#define MAX_RUNNING_TIME HOUR*100ul

#define TRACE_N 21

int running_finish = 0;
long running_time = 0;

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
    pthread_t thread_pid[2];    //workload_read, running_workload_pid
    char device_name[100];
    char trace_name[100];
    char output_name[100];
    size_t max_lbas;
    int trace_finish;
    struct Queue IO_queue;
};

//初始化队列
void InitQueue(struct Queue* queue)
{
    memset(queue, 0, sizeof(struct Queue));
    for(int i=0; i<QUEUE_ZISE; i++) {
        posix_memalign((void **)&(queue->IO_array[i].data), 4096, 4096);
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
		printf("fail enterqueue, the queue is full\n");
		return NULL;
	}

	//从队尾入队
	struct IO_info* res = &(queue->IO_array[queue->qRear]);//入队数据
	queue->qRear = (queue->qRear + 1) % QUEUE_ZISE;//队尾移向下个位置
    return res;
}

//出队
struct IO_info* GetQueueHead(struct Queue* queue)
{
	struct IO_info* res = &(queue->IO_array[queue->qFront]);//出队值
    return res;
}

//出队
void OutQueue(struct Queue* queue)
{
	if (IsEmptyQueue(queue))
	{
		printf("fail outqueue, the queue is null\n");
		return;
	}
	queue->qFront = (queue->qFront + 1) % QUEUE_ZISE;//指向下一个出队值
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
        printf("open %s error!\n", ((struct Thread_Info*)arg)->trace_name);
        return NULL;
    }
    else {
        printf("open %s success!\n", ((struct Thread_Info*)arg)->trace_name);
    }

    struct Queue* pqueue = &((struct Thread_Info*)arg)->IO_queue;
    size_t max_lbas = ((struct Thread_Info*)arg)->max_lbas+8ll;
    while(!feof(workload) && running_time < MAX_RUNNING_TIME) {
        if(IsFullQueue(pqueue)) {
            usleep(1000);
            continue;
        }

        struct IO_info *io_information = EnterQueue(pqueue);
        fscanf(workload, "%lu %d %s %lu %d %c %d %d %s\n", &(io_information->time), &(io_information->pid), (io_information->process), &(io_information->lba), 
                &(io_information->size), &(io_information->ope), &(io_information->major_device), &(io_information->minor_device), (io_information->data));
    }
    ((struct Thread_Info*)arg)->trace_finish = 1;
    fclose(workload);
    printf("workload %s read finish!\n", ((struct Thread_Info*)arg)->trace_name);
}

void *running_workload(void *arg) {
    int fd = -1;
    if ((fd = open(((struct Thread_Info*)arg)->device_name, O_RDWR | O_DIRECT)) == -1) {
        printf("open %s error!\n", ((struct Thread_Info*)arg)->device_name);
        return NULL;
    }
    else {
        printf("open %s success!\n", ((struct Thread_Info*)arg)->device_name);
    }

    struct Queue* pqueue = &((struct Thread_Info*)arg)->IO_queue;
    size_t max_lbas = ((struct Thread_Info*)arg)->max_lbas+8ll;
    int count=0;
    while(((struct Thread_Info*)arg)->trace_finish == 0 && running_time < MAX_RUNNING_TIME) {
        if(IsEmptyQueue(pqueue)) {
            usleep(1000);
            continue;
        }

        struct IO_info* io_information = GetQueueHead(pqueue);
        if(io_information->ope == 'W') {
            for(int i=0; i<io_information->size; i+=8) {
                int write_bytes = 0;
                write_bytes = pwrite(fd, io_information->data, 4096, ((io_information->lba+i)%max_lbas)<<9);
                if(write_bytes == -1) {
                    printf("write %lu error!\n", ((io_information->lba+i)%max_lbas)<<9);
                    printf("%d: %lu %d %s %lu %d %c %d %d %s\n", count, (io_information->time), (io_information->pid), (io_information->process), (io_information->lba), 
                        (io_information->size), (io_information->ope), (io_information->major_device), (io_information->minor_device), (io_information->data));
                    perror("error write:");
                    return NULL;
                }
                statisic.IOPS[0]++;
            }
        }
        else if(io_information->ope == 'R') {
            for(int i=0; i<io_information->size; i+=8) {
                int read_bytes = 0;
                read_bytes = pread(fd, io_information->data, 4096, ((io_information->lba+i)%max_lbas)<<9);
                if(read_bytes == -1) {
                    printf("read %lu error!\n", ((io_information->lba+i)%max_lbas)<<9);
                    return NULL;
                }
                statisic.IOPS[1]++;
            }
        }
        OutQueue(pqueue);
    }
    running_finish++;
    close(fd);
    printf("workload %s running finish!\n", ((struct Thread_Info*)arg)->trace_name);
}

void *print_info_per_second(void *arg) {
    FILE *outputfile = NULL;
    if ((outputfile = fopen(((struct Thread_Info*)arg)->output_name, "w")) == NULL) {
        printf("open %s error!\n", ((struct Thread_Info*)arg)->output_name);
        return NULL;
    }
    else {
        printf("open %s success!\n", ((struct Thread_Info*)arg)->output_name);
    }

    while(running_finish < TRACE_N) {
        sleep(1);
        fprintf(outputfile, "%ld TotalIOPS:%d ReadIOPS:%d WriteIOPS:%d\n", running_time++, 
            statisic.IOPS[0]+statisic.IOPS[1], statisic.IOPS[1], statisic.IOPS[0]);
        statisic.sum_IOPS[0] += statisic.IOPS[0];
        statisic.sum_IOPS[1] += statisic.IOPS[1];
        statisic.IOPS[0] = statisic.IOPS[1] = 0;
        fflush(outputfile);
    }
    fprintf(outputfile, "result: SumReadIOPS:%ld SumWriteIOPS:%ld AvgReadIOPS:%ld AvgWriteIOPS:%ld\n",
            statisic.sum_IOPS[1], statisic.sum_IOPS[0], statisic.sum_IOPS[1]/running_time, statisic.sum_IOPS[0]/running_time);
    fflush(outputfile);
    fclose(outputfile);
}

int main(int argc, char **argv) // 1[target_device] 2[total_lbas_of_device] 3[trace_name] 4[output_file]
{
    if(argc!=5)
    {
        printf("Error parameters!\n");
        return 0;
    }

    printf("%s %s %s %s %d %d %d\n", argv[1], argv[2], argv[3], argv[4], strcmp(argv[3], "webvm"), strcmp(argv[3], "homes"), strcmp(argv[3], "mail"));

    char *dir_name = (char *)malloc(sizeof(char)*100);
    if(strcmp(argv[3], "0") == 0 || strcmp(argv[3], "webvm") == 0)
        sprintf(dir_name, "/home/femu/traces/web-vm/webmail+online.cs.fiu.edu-110108-113008.");
    else if(strcmp(argv[3], "1") == 0 || strcmp(argv[3], "homes") == 0)
        sprintf(dir_name, "/home/femu/traces/homes/homes-110108-112108.");
    else if(strcmp(argv[3], "2") == 0 || strcmp(argv[3], "mail") == 0)
        sprintf(dir_name, "/home/femu/traces/mail/cheetah.cs.fiu.edu-110108-113008.");
    else {
        printf("Unknow Trace: %s!\n", argv[3]);
        return 0;
    }

    size_t max_lbas = atoi(argv[2]);
    printf("Device size: %.1f GB!\n", (float)max_lbas/2/1024/1024);

    struct Thread_Info* thread_info = malloc(sizeof(struct Thread_Info)*(TRACE_N+1));
    memset(thread_info, 0, sizeof(struct Thread_Info)*(TRACE_N+1));

    thread_info[0].id = 0;
    strcpy(thread_info[0].output_name, argv[4]);
    pthread_create(&thread_info[0].thread_pid[0], NULL, print_info_per_second, &thread_info[0]);
    for(int thread_id=1; thread_id<=TRACE_N; thread_id++)
    {
        thread_info[thread_id].id = thread_id;
        sprintf(thread_info[thread_id].trace_name, "%s%d%s", dir_name, thread_id, ".blkparse");
        // sprintf(thread_info[thread_id].trace_name, "%s%d%s", dir_name, 6, ".blkparse");
        strcpy(thread_info[thread_id].device_name, argv[1]);
        thread_info[thread_id].max_lbas = max_lbas;
        InitQueue(&(thread_info[thread_id].IO_queue));
        pthread_create(&thread_info[thread_id].thread_pid[0], NULL, workload_read, &thread_info[thread_id]);
        pthread_create(&thread_info[thread_id].thread_pid[1], NULL, running_workload, &thread_info[thread_id]);
    }
    
    pthread_join(thread_info[0].thread_pid[0], NULL);
    for(int thread_id=1; thread_id<=TRACE_N; thread_id++)
    {
        pthread_join(thread_info[thread_id].thread_pid[0], NULL);
        pthread_join(thread_info[thread_id].thread_pid[1], NULL);
    }
    
    return 0;
}

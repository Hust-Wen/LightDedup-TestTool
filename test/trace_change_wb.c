#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<string.h>
#include<math.h>

#define WRITE 0
#define READ 1

struct IO_info {
	long long int time;
	unsigned int device;
	unsigned int lpn;
	unsigned int size;
	unsigned int ope;
};

unsigned int fingerprint[255];

int main(int argc , char* argv[])
{
	srand((unsigned)time(NULL));
	int trace_size;		//GB
	int dedup_ratio;	//percentage
	int threads;
	int user_LPN_range;
	int IO_size;
	if(argc == 1) {
		trace_size = 4;	//GB
		dedup_ratio = 0;
		threads = 1;
		user_LPN_range = trace_size;
		IO_size = 64;
	}
	else if(argc == 2) {
		trace_size = strtol(argv[1], NULL, 10);
		dedup_ratio = 0;
		threads = 1;
		user_LPN_range = trace_size;
		IO_size = 64;
	}
	else if(argc == 3) {
		trace_size = strtol(argv[1], NULL, 10);
		dedup_ratio = strtol(argv[2], NULL, 10);
		threads = 1;
		user_LPN_range = trace_size;
		IO_size = 64;
	}
	else if(argc == 4) {
		trace_size = strtol(argv[1], NULL, 10);
		dedup_ratio = strtol(argv[2], NULL, 10);
		threads = strtol(argv[3], NULL, 10);
		user_LPN_range = trace_size;
		IO_size = 64;
	}
	else if(argc == 5) {
		trace_size = strtol(argv[1], NULL, 10);
		dedup_ratio = strtol(argv[2], NULL, 10);
		threads = strtol(argv[3], NULL, 10);
		user_LPN_range = strtol(argv[4], NULL, 10);
		IO_size = 64;
	}
	else if(argc == 6) {
		trace_size = strtol(argv[1], NULL, 10);
		dedup_ratio = strtol(argv[2], NULL, 10);
		threads = strtol(argv[3], NULL, 10);
		user_LPN_range = strtol(argv[4], NULL, 10);
		IO_size = strtol(argv[5], NULL, 10);
	}
	else {
		printf("wrong argc\n");
		return 0;
	}

	char input_trace_name[255];
	sprintf(input_trace_name, "randwrite-%dG-%dG-%d%%-%dKB-%d", trace_size, user_LPN_range, dedup_ratio, IO_size, threads);
	char output_trace_name[255];
	sprintf(output_trace_name, "randwrite-%dG-%dG-%d%%-%dKB-%d-b", trace_size, user_LPN_range, dedup_ratio, IO_size, threads);

	FILE* fp_in = fopen(input_trace_name, "r");
	FILE* fp_out = fopen(output_trace_name, "w");
	struct IO_info io;
	//total_lpns = 32;
	int count = 0, print_lpns=0;
	while(!feof(fp_in))
    {
		fscanf(fp_in, "%llu %d %d %d %d", &io.time, &io.device, &io.lpn, &io.size, &io.ope);
		for(int i=0; i<io.size; i++)	fscanf(fp_in, " %d", &fingerprint[i]);

        if (++count % 100000 == 0)
			printf("%.2fGB\n", (float)print_lpns*4/1024/1024);

		fwrite(&io, sizeof(struct IO_info), 1, fp_out);
		fwrite(&fingerprint, sizeof(unsigned int), io.size, fp_out);
		print_lpns+=io.size;
    }
	fclose(fp_in);
	fclose(fp_out);
	return 0;
}
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<string.h>
#include<math.h>

#define WRITE 0
#define READ 1

struct IO_info {
	long long int time_t;
	unsigned int device;
	unsigned int lpn;
	unsigned int size;
	unsigned int ope;
};

unsigned int fingerprint[32];

int main(int argc , char* argv[])
{
	srand((unsigned)time(NULL));
	int trace_size;	//GB
	if(argc == 1) {
		trace_size = 4;	//GB
	}
	else if(argc == 2) {
		trace_size = strtol(argv[1], NULL, 10);
	}
	else {
		printf("wrong argc\n");
		return 0;
	}

	char trace_name[100];
	sprintf(trace_name, "randwrite-%dG", trace_size);

	FILE* fp = fopen(trace_name, "w");

	unsigned int lpn_range = 1024 * 1024 / 4 * 4;
	unsigned int total_lpns = 1024 * 1024 / 4 * trace_size;
	
	int count = 0;
	struct IO_info io;
	unsigned int print_lpns = 0;
	
	//total_lpns = 32;
	while (print_lpns < total_lpns)
	{
		io.time_t = 0;
		io.device = 0;
		io.lpn = ((rand() << 15) + rand()) % lpn_range;
		io.size = 16;	//64KB
		io.ope = WRITE;

		for(int i=0; i<io.size; i++) {
			//char tmp = 'a'+print_lpns;
			//io_information.fingerprint[i].fp_int = tmp + (tmp << 8) + (tmp << 16) + (tmp << 24);
			fingerprint[i] = io.lpn+i;
			print_lpns++;
		}

		if (++count % 1000000 == 0)
			printf("%.2fGB\n", (float)print_lpns*4/1024/1024);

		fwrite(&io, sizeof(struct IO_info), 1, fp);
		fwrite(&fingerprint, sizeof(unsigned int), io.size, fp);
		//fprintf(fp, "\n");
	}
	fclose(fp);
	return 0;
}
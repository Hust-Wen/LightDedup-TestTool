#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<malloc.h>
#include<fcntl.h>
#include<time.h>

static inline void set_unique_data(char* data, int fingerprint)
{
    int* end_data_p = (int*)(data + 4096);
    for(int* data_p=(int*)data; data_p<end_data_p; data_p++) {
        *data_p = fingerprint;
    }
}

int main(void)
{
    int data_size = 1; //GB
    unsigned char data_file_name[100];
    sprintf(data_file_name, "/home/femu/test/%dGB_unique_data", data_size);
    FILE* fp_unique_data = fopen(data_file_name, "w");

    int total_pages = ((data_size << 20) / 4);
    printf("%d\n", total_pages);

    unsigned char data[4097];
    int temp = ('a'<<24) + ('b' << 16) + ('c' << 8) + 'd';
    for(int i=temp; i<temp+1; i++) {
        set_unique_data(data, i);
        data[4096] = '\0';
        
        fwrite(data, 4096, 1, fp_unique_data);
    }    
    return 0;
}

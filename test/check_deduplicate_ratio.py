# coding=utf-8
import sys
import os
import random
import numpy as np
import queue

WRITE = 0
READ = 1

def GB_to_pages(x):
    return int(x*1024*1024/4)

def pages_to_GB(x):
    return x*4/1024/1024

def sum_array(x):
    res = 0
    for i in range(len(x)):
        res += x[i]
    return res

def distrupt_array(x):
    for i in range(len(x)):
        index1 = random.randint(0, len(x)-1)
        index2 = random.randint(0, len(x)-1)
        temp = x[index1]
        x[index1] = x[index2]
        x[index2] = temp
    return x

trace_size = int(0)
dedup_ratio = int(0)
threads = int(1)
user_LPN_range = int(4)
trace_IO_size = int(64)

if(len(sys.argv) == 1):
    trace_size = int(4)
    dedup_ratio = int(0)
    threads = int(1)
    user_LPN_range = trace_size
    trace_IO_size = int(64)
elif(len(sys.argv) == 2):
    trace_size = int(sys.argv[1])
    dedup_ratio = int(0)
    threads = int(1)
    user_LPN_range = trace_size
    trace_IO_size = int(64)
elif(len(sys.argv) == 3):
    trace_size = int(sys.argv[1])
    dedup_ratio = int(sys.argv[2])
    threads = int(1)
    user_LPN_range = trace_size
    trace_IO_size = int(64)
elif(len(sys.argv) == 4):
    trace_size = int(sys.argv[1])
    dedup_ratio = int(sys.argv[2])
    threads = int(sys.argv[3])
    user_LPN_range = trace_size
    trace_IO_size = int(64)
elif(len(sys.argv) == 5):
    trace_size = int(sys.argv[1])
    dedup_ratio = int(sys.argv[2])
    threads = int(sys.argv[3])
    user_LPN_range = int(sys.argv[4])
    trace_IO_size = int(64)
elif(len(sys.argv) == 6):
    trace_size = int(sys.argv[1])
    dedup_ratio = int(sys.argv[2])
    threads = int(sys.argv[3])
    user_LPN_range = int(sys.argv[4])
    trace_IO_size = int(sys.argv[5])
else:
    print("Error argv")
    sys.exit()

lpn_range = GB_to_pages(user_LPN_range)
FP_range = GB_to_pages(trace_size)
FP_of_lpn = [-1 for i in range(lpn_range)]
refcount_of_FP = [0 for i in range(FP_range)]
deduplicate_count = 0
IOs = 0
for thread_id in range(threads):
    trace_name = "randwrite-%dG-%dG-%d%%-%dKB-%d" % (trace_size, user_LPN_range, dedup_ratio, trace_IO_size, thread_id)
    fp = open(dir_name+trace_name, "r")

    for line in fp:
        data = line.split(' ')
        if(len(data) < 5):
            break
        
        IO_lpn = int(data[2])
        IO_size = int(data[3])
        IO_ope = int(data[4])
        for i in range(IO_size):
            FP = int(data[5+i])
            if refcount_of_FP[FP] > 0:
                deduplicate_count += 1
            if FP_of_lpn[IO_lpn+i] != -1:
                refcount_of_FP[FP_of_lpn[IO_lpn+i]] -= 1
            if refcount_of_FP[FP_of_lpn[IO_lpn+i]] < 0:
                print("error, refcount < 0")
                sys.exit()
            FP_of_lpn[IO_lpn+i] = FP
            refcount_of_FP[FP] += 1

            IOs += 1
            if(IOs % 2621440) == 0:
                print("%dGB(%dGB)" % (pages_to_GB(IOs), pages_to_GB(deduplicate_count)))
            
print("Dedup_ratio: %.2f" % (deduplicate_count/FP_range))

# coding=utf-8
import sys
import os
import random
import numpy as np
import queue
from collections import deque

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

storLPN_range = GB_to_pages(56)
lpn_range = GB_to_pages(user_LPN_range)
lpns_per_thread = lpn_range // threads
total_lpns_per_thread = GB_to_pages(trace_size) // threads
is_print_lpn = [0 for i in range(lpn_range)]
thread_FP_offset = 0
thread_lpn_offset = 0
IO_size = trace_IO_size // 4
for thread_id in range(threads):
    trace_name = "randwrite-%dG-%dG-%d%%-%dKB-%d" % (trace_size, user_LPN_range, dedup_ratio, trace_IO_size, thread_id)
    fp = open(trace_name, "w")

    min_lpn = lpns_per_thread * thread_id
    max_lpn = lpns_per_thread * (thread_id + 1) - 1

    FP_of_lpn = [-1 for i in range(lpns_per_thread)]
    distance_lpn = [0 for i in range(lpns_per_thread)]
    remap_of_lpn = [-1 for i in range(lpns_per_thread)]
    reverse_remap_of_lpn = [[] for i in range(lpns_per_thread)]
    lpn_of_FP = [-1 for i in range(total_lpns_per_thread)]
    waiting_update_lpn_q = deque()
    recent_lpn_q = deque(maxlen=10000)
    recent_unreferenced_FP_q = deque(maxlen=1000)
    unique_lpns = 0
    dedup_lpns = 0
    lpn_list = [i * IO_size for i in range(0, lpns_per_thread // IO_size)]
    lpn_list = distrupt_array(lpn_list)
    FP_list = [i for i in range(0, total_lpns_per_thread)]
    FP_list = distrupt_array(FP_list)

    print_lpns = 0
    lpn_index = 0
    FP_index = 0
    while print_lpns < total_lpns_per_thread:
        IO_time = 0
        IO_device = 0
        IO_ope = WRITE

        if lpn_index == len(lpn_list):
            lpn_list = distrupt_array(lpn_list)
            lpn_index = 0
        if len(waiting_update_lpn_q) > 0:
            IO_lpn = waiting_update_lpn_q.popleft() // IO_size * IO_size
        else:
            IO_lpn = lpn_list[lpn_index]
            lpn_index += 1

        # if lpn_index < len(lpn_list):
        #     IO_lpn = lpn_list[lpn_index]
        #     lpn_index += 1
        # else:
        #     IO_lpn = random.randint(min_lpn, max_lpn-IO_size-1)

        IO_FP = []

        for i in range(IO_size):
            while len(reverse_remap_of_lpn[IO_lpn+i]) > 0:
                remap_lpn = reverse_remap_of_lpn[IO_lpn+i].pop()
                if remap_of_lpn[remap_lpn] == IO_lpn+i:
                    waiting_update_lpn_q.append(remap_lpn)

            while unique_lpns + dedup_lpns <=0:
                dedup_lpns += dedup_ratio
                unique_lpns += (100 - dedup_ratio)

            if len(recent_lpn_q) == 0 or print_lpns - distance_lpn[recent_lpn_q[0]] < 1000 or \
                    random.randint(0, dedup_lpns+unique_lpns-1) < unique_lpns:
                current_FP = FP_list[FP_index]
                FP_index += 1
                IO_FP.append(current_FP)
                FP_of_lpn[IO_lpn+i] = current_FP
                lpn_of_FP[current_FP] = IO_lpn+i
                remap_of_lpn[IO_lpn+i] = IO_lpn+i
                distance_lpn[IO_lpn+i] = print_lpns
                recent_lpn_q.append(IO_lpn+i)
                unique_lpns -= 1
            else:
                if random.randint(0,9) < 3:
                    if FP_index < storLPN_range:
                        unreferenced_FP_index = random.randint(0,FP_index)
                    else:
                        unreferenced_FP_index = random.randint(storLPN_range, FP_index)
                    duplicate_FP = FP_list[unreferenced_FP_index]
                    remap_of_lpn[IO_lpn+i] = lpn_of_FP[duplicate_FP]
                else:
                    index = random.randint(0, int(len(recent_lpn_q)/2))
                    while len(recent_lpn_q) > 0 and print_lpns - distance_lpn[recent_lpn_q[index]] < 1000:
                        index = random.randint(0, int(len(recent_lpn_q)/2))
                    duplicate_FP = FP_of_lpn[recent_lpn_q[index]]
                    remap_of_lpn[IO_lpn+i] = recent_lpn_q[index]
                IO_FP.append(duplicate_FP)
                FP_of_lpn[IO_lpn+i] = duplicate_FP
                reverse_remap_of_lpn[lpn_of_FP[duplicate_FP]].append(IO_lpn+i)
                dedup_lpns -= 1

            is_print_lpn[IO_lpn+i+thread_lpn_offset] = 1
            print_lpns += 1
            if(print_lpns % 100000 == 0):
                print("%.2f %.2f" % (pages_to_GB(print_lpns), pages_to_GB(sum_array(is_print_lpn))))

        print("%d %d %d %d %d" % (IO_time, IO_device, IO_lpn+thread_lpn_offset, IO_size, IO_ope), end="", file=fp)
        for i in range(len(IO_FP)):
            print(" %d" % (IO_FP[i]+thread_FP_offset), end="", file=fp)
        print("", file=fp)

    thread_FP_offset = total_lpns_per_thread * (thread_id + 1)
    thread_lpn_offset = max_lpn + 1   


        



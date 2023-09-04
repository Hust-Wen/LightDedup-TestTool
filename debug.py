#! /usr/bin/env python
#coding=utf-8

import sys
import os
import random
import numpy as np

local_lpn = 1263138
global_lpn = 2937739
src_lpn = 1410342
Offset_LPN = 528704
bug_user_lpn = global_lpn

count = 0
for i in range(8):
    file_name = "latency-%d.log" % (i)
    fp = open(file_name, 'r')
    for line in fp:
        data = line.split()
        if len(data) < 3:
            break
        userLPN = data[0].split(':')[1]
        count+=1
        if userLPN == bug_user_lpn:
            print(userLPN)
print(count)
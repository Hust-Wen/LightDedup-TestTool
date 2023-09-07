#!/bin/bash
sudo cp /home/femu/mod/origins/dm-dedup.ko /lib/modules/4.19.0/kernel/drivers/md
./origin-run-dedup.sh
cd test/
gcc per_test.c -o mytest
sudo ./mytest randwrite-4G-0%

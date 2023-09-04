#!/bin/bash

echo "Begin create"

ismd0=`lsblk | grep md0 -c`
if [ $ismd0 -eq 0 ]
then

sudo mdadm --create /dev/md0 --level=0 -c 4 --raid-devices=4 /dev/nvme0n1 /dev/nvme1n1 /dev/nvme2n1 /dev/nvme3n1 --assume-clean 1>/dev/null 2>&1

echo "Create RAID0..."

else

echo "RAID0 has existed"

fi

device_name="/dev/md0"
trace_name="randwrite"
trace_size=16
duplicateion_ratio=0
thread=32
user_LPN_range=4
dirname=randwrite-"$trace_size"G-"$user_LPN_range"G-"$duplicateion_ratio"%-"$thread"

cd ./test
gcc test_multi_thread.c -o test_multi_thread -lpthread
cp ./test_multi_thread ./"$dirname"/test_multi_thread
cd ./"$dirname"
sudo ./test_multi_thread $device_name $trace_name $trace_size $user_LPN_range $duplicateion_ratio $thread
rm ./test_multi_thread

#!/bin/bash

device_count_in_raid=$1
raid_device_name=$2
meta_dev_size_per=$3
dedup_name=$4
dedup_system=$5
transaction_size=$6
GC_threshould=$7
R_MapTable_ratio=$8
cache_size=$9
GC_block_limit=${10}
raid_level=${11}

echo -e "$# $* \n Begin create dmdedup (device_count_in_raid=${device_count_in_raid}, raid_device_name=${raid_device_name}, \
    meta_dev_size_per=${meta_dev_size_per}, dedup_name=${dedup_name}, dedup_system=${dedup_system}, \
    transaction_size=${transaction_size}, GC_threshould=${GC_threshould}, R_MapTable_ratio=${R_MapTable_ratio}, cache_size=${cache_size}, \
    GC_block_limit=${GC_block_limit}, raid_level=${raid_level})"

Sector=512
let KB=2*${Sector}
let MB=1024*${KB}
let GB=1024*${MB}

if [ ${raid_level} -eq 0 ]; then
let meta_dev_size=${meta_dev_size_per}*${device_count_in_raid}/${MB}
sudo fdisk ${raid_device_name} 1>/dev/null 2>&1 <<EOF
n
p
1

+${meta_dev_size}M
n
p
2


w
EOF
echo "Split RAID0 into two parts..."

else
raid_size=`sudo blockdev --getsz ${raid_device_name}`
let meta_dev_size=${meta_dev_size_per}*${device_count_in_raid}/${MB}
let tt_device_count=${device_count_in_raid}+1
let data_dev_size=${raid_size}/${tt_device_count}*${device_count_in_raid}-${meta_dev_size}
sudo fdisk ${raid_device_name} 1>/dev/null 2>&1 <<EOF
n
p
1

+${meta_dev_size}M
n
p
2

+${data_dev_size}M
n
p
3


w
EOF
echo "Split RAID5 into two parts..."
fi

META_DEV=${raid_device_name}p1
DATA_DEV=${raid_device_name}p2
DATA_DEV_SIZE=`sudo blockdev --getsz ${DATA_DEV}`
TARGET_SIZE=${DATA_DEV_SIZE}
sudo dd if=/dev/zero of=${META_DEV} bs=4096 count=1 1>/dev/null 2>&1
sudo depmod -a
sudo modprobe dm-bufio
echo "${cache_size}" | sudo tee /sys/module/dm_bufio/parameters/max_cache_size_bytes >/dev/null
sudo modprobe dm-dedup
echo "0 ${TARGET_SIZE} dedup ${META_DEV} ${DATA_DEV} 4096 md5 \
    ${dedup_system} ${transaction_size} 0 ${GC_threshould} ${device_count_in_raid} ${R_MapTable_ratio} \
    ${GC_block_limit} ${raid_level}"
echo "0 ${TARGET_SIZE} dedup ${META_DEV} ${DATA_DEV} 4096 md5 \
    ${dedup_system} ${transaction_size} 0 ${GC_threshould} ${device_count_in_raid} ${R_MapTable_ratio} \
    ${GC_block_limit} ${raid_level}" | sudo dmsetup create ${dedup_name}
#<meta_dev> <data_dev> <block_size> <hash_algo> <backend> <flushrq> <corruption_flag> <gc_rate> <ssd_number> <corruption_rate>
#<flushrq>:  [0]:no transaction;  [positive]:commit per <flushrq> writes; [negative]:commit per <flushrq> ms;
#<buffer flush interval (s)>    1:per second    60:per minute   3600: per hour
#echo "36000" | sudo tee /sys/module/dm_bufio/dparameters/max_age_seconds >/dev/null 

ret=`lsblk | grep ${dedup_name} -c`
if [ ${ret} -gt 0 ]; then
    echo "Create deduplication system success..."
else
    echo "Create deduplication system failed..."
fi

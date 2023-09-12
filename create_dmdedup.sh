#!/bin/bash

device_count_in_raid=$1
raid_device_name=$2
tt_meta_size_B=$3
dedup_name=$4
dedup_system=$5
transaction_size=$6
GC_threshould=$7
R_MapTable_ratio=$8
cache_size=$9
GC_block_limit=${10}
raid_level=${11}
if_equal_memory=${12}

echo -e "create_dmdedup.sh parameters: $# $* \n Begin create dmdedup (device_count_in_raid=${device_count_in_raid}, raid_device_name=${raid_device_name}, \
    tt_meta_size_B=${tt_meta_size_B}, dedup_name=${dedup_name}, dedup_system=${dedup_system}, \
    transaction_size=${transaction_size}, GC_threshould=${GC_threshould}, R_MapTable_ratio=${R_MapTable_ratio}, cache_size=${cache_size}, \
    GC_block_limit=${GC_block_limit}, raid_level=${raid_level}, if_equal_memory=${if_equal_memory})"

Sector=512
let KB=2*${Sector}
let MB=1024*${KB}
let GB=1024*${MB}

if [ ${raid_level} -eq 0 ]; then
let meta_dev_size_MB=${tt_meta_size_B}/${MB}
sudo fdisk ${raid_device_name} 1>/dev/null 2>&1 <<EOF
n
p
1

+${meta_dev_size_MB}M
n
p
2


w
EOF
raid_size_Sector=`sudo blockdev --getsz ${raid_device_name}`
let tt_raid_size_GB=${raid_size_Sector}*${Sector}/${GB}
let meta_dev_size_GB=${tt_meta_size_B}/${GB}
let data_dev_size_GB=${tt_raid_size_GB}-${meta_dev_size_GB}
echo "Split RAID0 into two parts... (meta_dev_size_GB=${meta_dev_size_GB}, data_dev_size_GB=${data_dev_size_GB})"

else
let meta_dev_size_MB=${tt_meta_size_B}/${MB}
raid_size_Sector=`sudo blockdev --getsz ${raid_device_name}`
let tt_raid_size_B=${raid_size_Sector}*${Sector}
let data_dev_count=${device_count_in_raid}-1
let data_dev_size_B=${tt_raid_size_B}/${device_count_in_raid}*${data_dev_count}-${tt_meta_size_B}-1*${GB}
let data_dev_size_MB=${data_dev_size_B}/${MB}
let parity_dev_size_B=${tt_raid_size_B}-${tt_meta_size_B}-${data_dev_size_B}
sudo fdisk ${raid_device_name} 1>/dev/null 2>&1 <<EOF
n
p
1

+${meta_dev_size_MB}M
n
p
2

+${data_dev_size_MB}M
n
p
3


w
EOF
let meta_dev_size_GB=${tt_meta_size_B}/${GB}
let data_dev_size_GB=${data_dev_size_B}/${GB}
let parity_dev_size_GB=${parity_dev_size_B}/${GB}
echo "Split RAID5 into three parts... (meta_dev_size_GB=${meta_dev_size_GB}, data_dev_size_GB=${data_dev_size_GB}, parity_dev_size_GB=${parity_dev_size_GB})"
fi

META_DEV=${raid_device_name}p1
DATA_DEV=${raid_device_name}p2
DATA_DEV_SIZE=`sudo blockdev --getsz ${DATA_DEV}`
TARGET_SIZE=${DATA_DEV_SIZE}
sudo dd if=/dev/zero of=${META_DEV} bs=4096 count=1 1>/dev/null 2>&1
sudo depmod -a
sudo modprobe dm-bufio

if [ ${dedup_system} == "xremap" -a ${if_equal_memory} -eq 1 ]; then
let SSD_extra_memory=${data_dev_size_B}/4/${KB}*${R_MapTable_ratio}/100*5
let final_cache_size=${cache_size}-${SSD_extra_memory}
echo "SSD_extra_memory=${SSD_extra_memory}, final_cache_size=${final_cache_size}"
echo "${final_cache_size}" | sudo tee /sys/module/dm_bufio/parameters/max_cache_size_bytes >/dev/null
else
echo "cache_size=${cache_size}"
echo "${cache_size}" | sudo tee /sys/module/dm_bufio/parameters/max_cache_size_bytes >/dev/null
fi

sudo modprobe dm-dedup
echo "dmsetup create ${dedup_name} parameters: 0 ${TARGET_SIZE} dedup ${META_DEV} ${DATA_DEV} 4096 md5 \
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

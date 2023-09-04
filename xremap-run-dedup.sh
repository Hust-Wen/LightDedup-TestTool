#!/bin/bash

echo "Begin create"

ismd0=`lsblk | grep md0 -c`
if [ $ismd0 -eq 0 ]
then

raid_devices=4
if [ $raid_devices -eq 1 ]
then
Array_Device_Name=/dev/nvme0n1
echo "Create Single Device..."
else
Array_Device_Name=/dev/md0
Device_Name="--raid-devices=${raid_devices}"
for((i=0;i<${raid_devices};i++));
do Device_Name+=" /dev/nvme${i}n1"; done
echo "Device_Name=${Device_Name}"
# sudo mdadm --create /dev/md0 --level=0 -c 4 --raid-devices=4 /dev/nvme0n1 /dev/nvme1n1 /dev/nvme2n1 /dev/nvme3n1 --assume-clean 1>/dev/null 2>&1
sudo mdadm --create ${Array_Device_Name} --level=0 -c 4 ${Device_Name} --assume-clean 1>/dev/null 2>&1
echo "Create RAID0..."
fi

Byte=1
let Sector=512*${Byte}
let KB=1024*${Byte}
let MB=1024*${KB}
let GB=1024*${MB}
let meta_dev_size_per=2*${GB}
let tt_meta_dev_size=${meta_dev_size_per}*${raid_devices}/${MB}
echo "tt_meta_dev_size=${tt_meta_dev_size}"


sudo fdisk ${Array_Device_Name} 1>/dev/null 2>&1 <<EOF
n
p
1

+${tt_meta_dev_size}M
n
p
2


w
EOF

echo "Split RAID0 into two parts..."

else

echo "RAID0 has existed"

fi

META_DEV=${Array_Device_Name}p1
DATA_DEV=${Array_Device_Name}p2
DATA_DEV_SIZE=`sudo blockdev --getsz $DATA_DEV`
TARGET_SIZE=$DATA_DEV_SIZE
sudo dd if=/dev/zero of=$META_DEV bs=4096 count=1 1>/dev/null 2>&1

sudo depmod -a
sudo modprobe dm-bufio


let block_size=4*${KB}
#cowbtree (%d)    inram   hybrid(storing Dedup-MapTable and Refcount-Table in extra memory without transaction)
#xremap(DataVer-Table and TgtSSD-Table are all stored in memory)
backend="xremap"

if [ $backend == "cowbtree" ]
then
GC_threshould=100    #20(the last) mean the gc rate: default: 10 for Dmdedup, 10000 for LightDedup 
R_MapTable_size=0
else
GC_threshould=10000    #default: 10 for Dmdedup, 10000 for LightDedup 
R_MapTable_size=100       #corruption rate of R-table: 20, 50, 100, 200
fi

# GC_threshould=5
# R_MapTable_size=100
Memory_Equal="False" #True False

# Metadata_Per_Block=33   #FP-index(16B Key + 8B Value), Dedup-MapTable(8B), RC-Table(1B)
# let cache_size=${DATA_DEV_SIZE}*${Sector}/${block_size}*${Metadata_Per_Block}
let cache_size=128*${MB}
if [ $Memory_Equal == "True" ]
then
let cache_size=${cache_size}-16*${MB}-64*${MB}*R_MapTable_size/100
else
let cache_size=${cache_size}*${raid_devices}/4
fi
# let cache_size/=4
echo "DATA_DEV_SIZE=${DATA_DEV_SIZE}"
echo "cache_size=${cache_size}"
echo "$cache_size" | sudo tee /sys/module/dm_bufio/parameters/max_cache_size_bytes >/dev/null
#<buffer flush interval (s)>    1:per second    60:per minute   3600: per hour
#echo "36000" | sudo tee /sys/module/dm_bufio/dparameters/max_age_seconds >/dev/null 

sudo modprobe dm-dedup

#<flushrq> 0:no transaction; >1:commit per <flushrq> writes; <-1:commit per <flushrq> ms;
#128:512K        1024:4M     8192:32M    16384:64M    65536:256M      0:Unlimited     #-1:1ms     -1000:1s    -60000:1minute
let transaction_size=4*${MB}/${block_size}
echo "transaction_size=${transaction_size}"
#the number of ssd
SSDs_of_array=${raid_devices}
echo "0 $TARGET_SIZE dedup $META_DEV $DATA_DEV $block_size md5 $backend $transaction_size 0 $GC_threshould $SSDs_of_array $R_MapTable_size" | sudo dmsetup create mydedup
#<meta_dev> <data_dev> <block_size> <hash_algo> <backend> <flushrq> <corruption_flag> <gc_rate> <ssd_number> <corruption_rate>

ret=`lsblk | grep mydedup -c`
if [ $ret -gt 0 ]
then
    echo "Create success..."
else
    echo "Create failed..."
fi
echo "End create"

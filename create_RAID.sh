#!/bin/bash

echo "Begin create RAID"

device_count_in_raid=$1
raid_device_name=$2
raid_level=$3

ismd0=`lsblk | grep md0 -c`
if [ $ismd0 -eq 0 ]; then
    if [ $device_count_in_raid -eq 1 ]; then
        echo "There is only single device"
    else
        device_name_list=""
        for((i=0;i<${device_count_in_raid};i++));
            do device_name_list+=" /dev/nvme${i}n1"; done

        # sudo mdadm --create /dev/md0 --level=0 -c 4 --raid-devices=4 /dev/nvme0n1 /dev/nvme1n1 /dev/nvme2n1 /dev/nvme3n1 --assume-clean 1>/dev/null 2>&1
        sudo mdadm --create ${raid_device_name} \
                    --level=${raid_level} -c 4 \
                    --raid-devices=${device_count_in_raid} \
                    ${device_name_list} \
                    --assume-clean 1>/dev/null 2>&1
        echo "Create RAID0(${device_name_list}) success"
    fi
else
    echo "There is a RAID"
fi
#!/bin/bash

echo "Begin create"

ismd0=`lsblk | grep md0 -c`
if [ $ismd0 -eq 0 ]
then

sudo mdadm --create /dev/md0 --level=0 -c 4 --raid-devices=4 /dev/nvme0n1 /dev/nvme1n1 /dev/nvme2n1 /dev/nvme3n1 --assume-clean 1>/dev/null 2>&1

echo "Create RAID0..."


sudo fdisk /dev/md0 1>/dev/null 2>&1 <<EOF
n
p
1

+512M
n
p
2


w
EOF

echo "Split RAID0 into two parts..."

else

echo "RAID0 has existed"

fi

META_DEV=/dev/md0p1
DATA_DEV=/dev/md0p2
DATA_DEV_SIZE=`sudo blockdev --getsz $DATA_DEV`
TARGET_SIZE=$DATA_DEV_SIZE
sudo dd if=/dev/zero of=$META_DEV bs=4096 count=1 1>/dev/null 2>&1

sudo depmod -a
sudo modprobe dm-dedup

#cowbtree (%d)    inram      xremap
echo "0 $TARGET_SIZE dedup $META_DEV $DATA_DEV 4096 md5 inram 100 0" | sudo dmsetup create mydedup

ret=`lsblk | grep mydedup -c`
if [ $ret -gt 0 ]
then
    echo "Create success..."
else
    echo "Create failed..."
fi
echo "End create"

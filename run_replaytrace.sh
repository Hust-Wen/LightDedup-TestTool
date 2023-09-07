#!/bin/bash

username=`whoami`
if [ $username == "root" ]
then
echo "Right user : $username"

Share_Dir="/home/femu/share/"
./mount9p.sh ${Share_Dir}

rm /var/log/syslog
rm /var/log/syslog.1
rm /var/log/kern.log

sudo cp /home/femu/mod/with_gc/dm-dedup.ko /lib/modules/4.19.0+/kernel/drivers/md/dm-dedup.ko

Sector=512
let KB=2*${Sector}
let MB=1024*${KB}
let GB=1024*${MB}

#create RAID----------------------------------------------------------
Device_Count_in_RAID=5
RAID_Device_Name=/dev/md0
RAID_Level=5
./create_RAID.sh ${Device_Count_in_RAID} ${RAID_Device_Name} ${RAID_Level}

#warm FEMU-SSD--------------------------------------------------------
# cd /home/femu/vdbench
# RAID_Size=`sudo blockdev --getsz ${RAID_Device_Name}`
# let device_size=${RAID_Size}*${Sector}/${GB}
# let warm_size=${device_size}
# Warm_SSD_ConfigFile="warm_SSD"
# Warm_SSD_Output="output-warm-SSD"
# echo -e "sd=sd1,lun=${RAID_Device_Name},openflags=o_direct,threads=16,size=${device_size}g" \
#         "\nwd=wd1,sd=sd1,xfersize=128k,seekpct=0,rdpct=0" \
#         "\nrd=run1,wd=wd*,iorate=max,maxdata=${warm_size}g,elapsed=100h,interval=1,warmup=0" > ${Warm_SSD_ConfigFile}
# cp ${Warm_SSD_ConfigFile} ${Share_Dir}
# ./vdbench -f ${Warm_SSD_ConfigFile} -o ${Warm_SSD_Output}
# cp -r ${Warm_SSD_Output} ${Share_Dir}
# cd /home/femu

#create deduplication system------------------------------------------
let tt_Meta_Size_B=32*${GB}
Dedup_Name="mydedup"
Dedup_System=cowbtree    #cowbtree, xremap
let DEDUP_THANSACTION=4*${MB}/4096
Dedup_GC_Threshold=100
Dedup_RMapTable_Ratio=100
let Dedup_Meta_Cache_Size=2*${GB}
Dedup_GC_Block_Limit=${DEDUP_THANSACTION}

if [ $Dedup_System == "cowbtree" ]; then
    Dedup_RMapTable_Ratio=0
else
    Dedup_GC_Threshold=10000
fi

./create_dmdedup.sh ${Device_Count_in_RAID} ${RAID_Device_Name} ${tt_Meta_Size_B} \
                    ${Dedup_Name} ${Dedup_System} ${DEDUP_THANSACTION} ${Dedup_GC_Threshold} ${Dedup_RMapTable_Ratio} ${Dedup_Meta_Cache_Size} \
                    ${Dedup_GC_Block_Limit} ${RAID_Level}

#warm deduplication system---------------------------------------------
cd /home/femu/vdbench
Dedup_Device_Name=/dev/mapper/${Dedup_Name}
Dedup_Size=`sudo blockdev --getsz ${Dedup_Device_Name}`
let device_size=${Dedup_Size}*${Sector}/${GB}
# let device_size=50
let warm_size=${device_size}
Warm_Dedup_ConfigFile="warm_Dedup"
Warm_DedupOutput="output-warm-Dedup"
echo -e "sd=sd1,lun=${Dedup_Device_Name},openflags=o_direct,threads=16,size=${device_size}g" \
        "\nwd=wd1,sd=sd1,xfersize=128k,seekpct=0,rdpct=0" \
        "\nrd=run1,wd=wd*,iorate=max,maxdata=${warm_size}g,elapsed=100h,interval=1,warmup=0" > ${Warm_Dedup_ConfigFile}
cp ${Warm_Dedup_ConfigFile} ${Share_Dir}
./vdbench -f ${Warm_Dedup_ConfigFile} -o ${Warm_DedupOutput}
cp -r ${Warm_DedupOutput} ${Share_Dir}
cd /home/femu

#running workload------------------------------------------------------
DedupInfoFile="/home/femu/dedup_info.log"
IOPSInfoFile="/home/femu/result_IOPS.log"
watch -n 1 ./run_statistic.sh ${Dedup_Name} ${DedupInfoFile} > /dev/null 2>&1 &
gcc replay_trace.c voidQueue.h voidQueue.c -o replay_trace -lpthread

Target_Device="/dev/mapper/${Dedup_Name}"
tt_Device_LBAs=`sudo blockdev --getsz $Target_Device`
Target_Size=$tt_Device_LBAs
Workload="mail-3"    # webvm homes mail-3
Thread_Count=2

echo "" > ${DedupInfoFile}
dmsetup message ${Dedup_Name} 0 notice begin
./replay_trace ${Target_Device} ${Target_Size} ${Workload} ${IOPSInfoFile} ${Thread_Count}
dmsetup message ${Dedup_Name} 0 notice end

#cat /proc/mdstat >> /home/femu/dedup_info.log

ps -ef | grep watch | grep -v grep | grep -v watchdogd | awk '{print $2}' | xargs sudo kill -9

#store statistic files--------------------------------------------------
ConfigFile=$0
rm /var/log/syslog
rm /var/log/syslog.1
rm /var/log/kern.log
cp ${DedupInfoFile} ${Share_Dir}
cp ${IOPSInfoFile} ${Share_Dir}
cp ${ConfigFile} ${Share_Dir}
cp ./run_replaytrace.log ${Share_Dir}
cp ./replay_trace.log ${Share_Dir}
poweroff

else
echo "Wrong user : ${username}, please enter root (sudo su) and then ${ConfigFile}"
fi

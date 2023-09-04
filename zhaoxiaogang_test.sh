#!/bin/bash

username=`whoami`
if [ $username == "root" ]
then
echo "Right user : $username"

./mount9p.sh
rm /var/log/syslog
rm /var/log/syslog.1
rm /var/log/kern.log
rm /home/femu/share/dedup_info.log
rm /home/femu/share/running_res.log
rm /home/femu/share/latency-*.log
rm /home/femu/share/xremap-run-dedup.sh
rm /home/femu/share/my_run.sh
sudo cp /home/femu/mod/with_gc/dm-dedup.ko /lib/modules/4.19.0/kernel/drivers/md/dm-dedup.ko
./xremap-run-dedup.sh

# sudo nohup watch -n 1 ./run_statistic.sh > nohupcmd.out 2>&1 &
# sudo bash -c "echo '' > /home/femu/share/dedup_info.log"
# sudo dmsetup message mydedup 0 notice begin
# sudo ./mytest randwrite-10G-50%-r
# sudo dmsetup message mydedup 0 notice end
# sudo bash -c "echo '?' >> /home/femu/share/dedup_info.log"
# sudo ps -ef | grep watch | grep -v grep | awk '{print $2}' | xargs kill -9

device_name="/dev/mapper/mydedup"
trace_name="randwrite"
trace_size=160
duplicateion_ratio=50
thread=8
user_LPN_range=48
IO_size=4
unref_ratio="-100%Unref"
dirname=randwrite-"$trace_size"G-"$user_LPN_range"G-"$duplicateion_ratio"%-"$thread"-"$IO_size"KB"$unref_ratio"

watch -n 1 ./run_statistic.sh > output.log 2>&1 &
cd ./test
gcc test_multi_thread.c -o test_multi_thread -lpthread
cp ./test_multi_thread ./"$dirname"/test_multi_thread
cd ./"$dirname"
echo "" > /home/femu/dedup_info.log
dmsetup message mydedup 0 notice begin
./test_multi_thread $device_name $trace_name $trace_size $user_LPN_range $duplicateion_ratio $thread $IO_size
dmsetup message mydedup 0 notice end
ps -ef | grep watch | grep -v grep | grep -v watchdogd | awk '{print $2}' | xargs sudo kill -9
rm ./test_multi_thread
rm /var/log/syslog
rm /var/log/syslog.1
#rm /var/log/kern.log
cp /home/femu/dedup_info.log /home/femu/share/
cp /home/femu/running_res.log /home/femu/share/
cp /home/femu/latency-*.log /home/femu/share/
cp /home/femu/xremap-run-dedup.sh /home/femu/share/
cp /home/femu/my_run.sh /home/femu/share/
poweroff

else
echo "Wrong user : $username, please enter root (sudo su) and then ./my_run.sh"
fi

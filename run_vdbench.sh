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
rm /home/femu/share/xremap-run-dedup.sh
rm /home/femu/share/my_run.sh

sudo cp /home/femu/mod/with_gc/dm-dedup.ko /lib/modules/4.19.0+/kernel/drivers/md/dm-dedup.ko
./xremap-run-dedup.sh

watch -n 1 ./run_statistic.sh > output.log 2>&1 &
cd /home/femu/vdbench

./vdbench -f warm_test -o output-warm
echo "" > /home/femu/dedup_info.log

dmsetup message mydedup 0 notice begin
./vdbench -f wen_test
dmsetup message mydedup 0 notice end

ps -ef | grep watch | grep -v grep | grep -v watchdogd | awk '{print $2}' | xargs sudo kill -9

rm /var/log/syslog
rm /var/log/syslog.1
rm /var/log/kern.log
cp /home/femu/dedup_info.log /home/femu/share/
cp /home/femu/xremap-run-dedup.sh /home/femu/share/
cp -r /home/femu/vdbench/output /home/femu/share/
cp -r /home/femu/vdbench/output-warm /home/femu/share/
# poweroff

else
echo "Wrong user : $username, please enter root (sudo su) and then ./run_vdbench.sh"
fi

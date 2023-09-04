#!/bin/bash
trace_size=160
duplicateion_ratio=50
thread=8
user_LPN_range=48
IO_size=4
dirname=randwrite-"$trace_size"G-"$user_LPN_range"G-"$duplicateion_ratio"%-"$thread"-"$IO_size"KB-90%Unref
mkdir $dirname
gcc trace_change_wb.c -o trace_change_wb >&1
cp ./create_randtrace.py ./"$dirname"/
cp ./check_deduplicate_ratio.py ./"$dirname"/
cp ./trace_change_wb ./"$dirname"/
cd $dirname

python3 create_randtrace.py $trace_size $duplicateion_ratio $thread $user_LPN_range $IO_size >&1

python3 check_deduplicate_ratio.py $trace_size $duplicateion_ratio $thread $user_LPN_range $IO_size >&1

for ((thread_id=0;thread_id<$thread;thread_id++));
do
./trace_change_wb $trace_size $duplicateion_ratio $thread_id $user_LPN_range $IO_size >&1
done

rm ./create_randtrace.py 
rm ./check_deduplicate_ratio.py 
rm ./trace_change_wb

#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH

delay=300
cs=170
time=10
thread=40
latency=1500000

#critical len: 5928.796 cycle
# delay : 17619.910 cycle 

echo -n "mutex  "
$LITL_DIR/libpthreadinterpose_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep $time


# echo -n "MCS    "
# $LITL_DIR/libmcs_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

# echo -n "MCSWAKE    "
# $LITL_DIR/libmcswake_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

# echo -n "shfl  "
# $LITL_DIR/libmcssteal_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

# echo -n "mutexee  "
# $LITL_DIR/libmutexee_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# # $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

# echo -n "malthusian  "
# $LITL_DIR/libmalthusian_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

# echo -n "cst  "
# $LITL_DIR/libsecondaryqueue_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time



# echo -n "gcrmcs  "
# $LITL_DIR/libgcrmcs_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

# echo -n "gcrspin  "
# $LITL_DIR/libgcrspin_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

# echo -n "gcr  "
# $LITL_DIR/libgcr_spin_then_park.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/bench_block -t $thread -T $time -d $delay -s $cs > result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

echo -n "htll-0  "
$LITL_DIR/libhtll_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/htll_bench_block -t $thread -T $time -d $delay -s $cs -l 0 >  result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep $time


echo -n "htll-15  "
$LITL_DIR/libhtll_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/htll_bench_block -t $thread -T $time -d $delay -s $cs -l 1500000 >  result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep $time


echo -n "htll-30  "
$LITL_DIR/libhtll_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/htll_bench_block -t $thread -T $time -d $delay -s $cs -l 3000000 >  result
$LOCAL_DIR/measure.sh ./result $thread $thread 0
sleep $time


# echo -n "htll-30  "
# $LITL_DIR/libhtll_original.sh taskset -c 0,2,4,6,8,10,12,14,16,18 $LITL_DIR/bin/htll_bench_block -t $thread -T $time -d $delay -s $cs -l 3000000 >  result
# $LOCAL_DIR/measure.sh ./result $thread $thread 0
# sleep $time

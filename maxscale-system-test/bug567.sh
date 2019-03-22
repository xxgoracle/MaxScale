#!/bin/bash

###
## @file bug567.sh Regression case for the bug "Crash if files from /dev/shm/ removed"
## - try to remove everythign from /dev/shm/$maxscale_pid
## check if Maxscale is alive

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export script_name=`basename $rp`

$test_dir/non_native_setup $1 ${script_name}



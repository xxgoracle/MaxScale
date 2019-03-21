#!/bin/bash

###
## @file run_ctrl_c.sh
## check that Maxscale is reacting correctly on ctrc+c signal and termination does not take ages

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export test_name=`basename $rp`

if [ $maxscale_IP == "127.0.0.1" ] ; then
	echo local test is not supported
	exit 0
fi

$test_dir/non_native_setup $test_name


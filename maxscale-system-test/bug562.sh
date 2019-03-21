#!/bin/bash

###
## @file bug562.sh Regression case for the bug "Wrong error message for Access denied error"
## - try to connect with bad credestials directly to MariaDB server and via Maxscale
## - compare error messages

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export test_name=`basename $rp`

$test_dir/non_native_setup $test_name


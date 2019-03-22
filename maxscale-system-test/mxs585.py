#!/bin/bash

###
## @file mxs585.py Regression case for MXS-585 "Intermittent connection failure with MaxScale 1.2/1.3 using MariaDB/J 1.3"
## - open connection, execute simple query and close connection in the loop

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export script_name=`basename $rp`

$test_dir/non_native_setup $1 ${script_name}


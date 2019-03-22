#!/bin/bash

###
## @file mxs598.py Regression case for MXS-598 "SSL RW Router / JDBC Exception"
## - use SSL for Maxscale client connection
## - simple transactions in the loop

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export script_name=`basename $rp`

$test_dir/non_native_setup $1 ${script_name}


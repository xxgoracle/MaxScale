#!/bin/bash

###
## @file mxs791.sh Simple connect test in bash
## - connects to Maxscale, checks that defined in cmd line DB is selected

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export script_name=`basename $rp`

$test_dir/non_native_setup $1 ${script_name}


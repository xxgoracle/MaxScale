#!/bin/bash

###
## @file run_session_hang.sh
## run a set of queries in the loop (see setmix.sql) using Perl client

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export test_name=`basename $rp`

$test_dir/non_native_setup $1 ${script_name}


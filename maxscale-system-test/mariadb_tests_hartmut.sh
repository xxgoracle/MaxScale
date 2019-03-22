#!/bin/bash

# This is required by one of the tests
#
# TODO: Don't test correctness of routing with mysqltest
#
rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export script_name=`basename $rp`

$test_dir/non_native_setup $1 ${script_name}


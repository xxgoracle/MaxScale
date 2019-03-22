#!/bin/bash

###
## @file bug564.sh Regression case for the bug "Wrong charset settings"
## - call MariaDB client with different --default-character-set= settings
## - check output of SHOW VARIABLES LIKE 'char%'

rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export script_name=`basename $rp`

$test_dir/non_native_setup $1 ${script_name}



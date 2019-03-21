#!/bin/bash

###
## @file bug561.sh Regression case for the bug "Different error messages from MariaDB and Maxscale"
## - try to connect to non existing DB directly to MariaDB server and via Maxscale
## - compare error messages
## - repeat for RWSplit, ReadConn


rp=`realpath $0`
export src_dir=`dirname $rp`
export test_dir=`pwd`
export test_name=`basename $rp`

$test_dir/non_native_setup ${test_name}



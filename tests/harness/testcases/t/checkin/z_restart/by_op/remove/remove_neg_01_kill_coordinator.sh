#!/bin/bash
set -x
FUNCS=$TESTDIR/../../../shared_test_functions.sh
if [ -e $FUNCS ]
then
    source $FUNCS
else
    echo "Cannot find shared functions in \"$FUNCS\""
    exit 1
fi

load_system_library
scidb_sync
iquery -aq "store(build(<v:int64>[i=0:999:0:1], i), remove_neg_01_arr)"
expect_array_versions 2

# kill the coordinator via the test_abort_remove operator.  Verify that
# the array doesn't exist after the coordinator restarts, which
# indicates that the on-instance-startup recovery logic successfully
# reaped the array.
iquery -aq "test_abort_remove(remove_neg_01_arr)"

scidb_sync
expect_array_versions 0

exit 0

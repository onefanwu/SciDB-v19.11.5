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

# Remove array versions with the test operator, itself causing
# SciDB to crash (intentionally, of course).
ovts="$@"  # oldest version to save
iquery -aq "test_callback_remove_versions(remove_neg_02, $ovts)"

# Wait for the cluster to return.
scidb_sync
exit 0

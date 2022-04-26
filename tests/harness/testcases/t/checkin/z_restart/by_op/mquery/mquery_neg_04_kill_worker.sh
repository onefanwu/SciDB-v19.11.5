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
iquery -aq "create array f <v:int64>[i=0:999:0:1]"
iquery -aq "mquery(insert(build(f, i), f), \
                           insert(project(apply(f, vnew, 2*v), vnew), f), \
                           insert(project(apply(f, vnew, 2*v), vnew), f), \
                           insert(project(apply(f, vnew, int64(perf_time_test_sleep(1))), vnew), f))" &
wait_for_query_start "mquery"
expect_array_versions 4

# Pick an instance to terminate.  Use a replicated array to ensure that the killInstance
# function's invoked once on each instance.  Only the lucky instance chosen for termination
# will wind-up terminated, but this ensures that the function runs on every instance.
target_instance=$(iquery -otsv -aq "filter(project(list('instances'),instance_id),instance_id!=0)" | tail -1)
iquery -otsv -aq "create array killInstanceArray <v:int64,k:int64>[i=0:0:0:1] distribution replicated"
iquery -otsv -aq \
  "store(apply(build(<v:int64>[i=0:0:0:1],i),k,killInstance($target_instance,9,false)),killInstanceArray)"

# Wait for the cluster to come back together.  Remove the killInstanceArray, then expect
# only one version of the array f to exist (as rollback must have done its thing at this point).
scidb_sync
iquery -otsv -aq "remove(killInstanceArray)"
expect_array_versions 1

exit 0

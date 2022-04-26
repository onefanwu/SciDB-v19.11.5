#!/bin/bash

source ${TESTDIR}/lock-arrays-common.sh

# Simulate a long-running write query with store(build()).  Since this write
# query started before the global array lock was installed, the installation of the
# global array lock itself will block until the completion of the store(build()).
# This test will cancel the store(build()) before returning.
iquery -C -aq "load_library('perf_time_test')"
iquery -aq "store(apply(build(<v:int64>[i=0:999:0:1],i), s, \
            perf_time_test_sleep(1)), store_build_test_case)" &
wait_for_query_start "store"

# Install the global array lock, which should block because there's an active
# writer in process already.
iquery -aq "lock_arrays(true)" &
wait_for_query_start "lock_arrays"

for i in $(seq 3); do
    # Verify that the 'lock_arrays' query is blocking.  It should block because there's an
    # ongoing write operation that must complete (the store(apply(...))) before the global
    # array lock may be installed.
    query_pending=$(iquery -otsv -aq \
                    "filter(project(list('queries'),query_string), regex(query_string,'^lock_arrays(.*)'))" \
                    | wc -l)
    if [ $query_pending -eq 0 ]; then
        # The query completed, which it shouldn't, because it should be blocked
        # by the ongoing write query.  Cleanup and return failure.
        echo "failed to verify that lock_arrays() blocked"
        iquery -aq "lock_arrays(false)"
        iquery -aq "remove(store_build_test_case)"
        exit 1
    fi
    sleep 1
done

# Cancel the lock_arrays query.
lock_arrays_query_id=$(iquery -otsv -aq \
                       "project(filter(list('queries'),regex(query_string,'^lock_arrays(.*)')),query_id)")
iquery -C -aq "cancel('$lock_arrays_query_id')"

# The store(build()) query is still running, so cancel it.
build_query_id=$(iquery -otsv -aq \
                 "project(filter(list('queries'),regex(query_string,'^store(.*)')),query_id)")
iquery -C -aq "cancel('$build_query_id')"

wait
sleep 1
exit 0

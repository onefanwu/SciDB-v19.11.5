#!/bin/bash
set -e

source $TESTDIR/../../shared_test_functions.sh

iquery -aq "mquery(insert(build(f, i), f), \
                   insert(project(apply(f, vnew, 2*v), vnew), f), \
                   insert(project(apply(f, vnew, 2*v), vnew), f), \
                   insert(project(apply(f, vnew, int64(perf_time_test_sleep(1))), vnew), f))" &
cpid=$!
wait_for_query_start "mquery"
expect_array_versions 4
iquery -aq "op_count(f@1)"
iquery -aq "op_count(f@2)"
iquery -aq "op_count(f@3)"
kill -2 $cpid
wait_for_query_stop "mquery"
expect_array_versions 1

# Palliative, put in place to investigate nature of CDash-only failure of this test.
# Test fails during cleanup with same error as in SDB-6699.
sleep 1

exit 0

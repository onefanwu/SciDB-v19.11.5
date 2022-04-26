#!/bin/bash
set -e

source $TESTDIR/../../shared_test_functions.sh

iquery -aq "mquery(insert(build(f, i), f), \
                           insert(project(apply(f, vnew, int64(perf_time_test_sleep(1))), vnew), f))" &

wait_for_query_start "mquery"
expect_array_versions 2
parent_query_id=$(get_parent_query_id)
iquery -aq "cancel('$parent_query_id')"
wait_for_query_stop "mquery"
expect_array_versions 1
exit 0

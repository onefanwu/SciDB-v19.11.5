#!/bin/bash

wait_for_query_start()
{
    local key=$1
    local query_pending=0
    local total_iters=5

    for i in $(seq $total_iters); do
        query_pending=$(iquery -otsv -aq \
                        "filter(project(list('queries'),query_string),regex(query_string,'^$key(.*)'))" \
                            | wc -l)
        if [ $query_pending -gt 0 ]; then
            return 0
        fi
        sleep 1
    done

    if [ $query_pending -eq 0 ]; then
        # Query didn't start for some reason.
        echo "Query containing '$key' did not start in $total_iters seconds"

        # Kill all ongoing queries--just in case.
        for q in $(iquery -otsv -aq "project(list('queries'),query_id)"); do
            iquery -aq "cancel('$q')" || true;
        done

        # Exit the test with an error.
        exit 1
    fi
}

wait_for_query_stop()
{
    local key=$1
    local query_pending=0
    local total_iters=5

    for i in $(seq $total_iters); do
        query_pending=$(iquery -otsv -aq \
                        "filter(project(list('queries'),query_string),regex(query_string,'^$key(.*)'))" \
                            | wc -l)
        if [ $query_pending -eq 0 ]; then
            return 0
        fi
        sleep 1
    done

    if [ $query_pending -gt 0 ]; then
        # Query didn't stop for some reason.
        echo "Query containing '$key' did not stop in $total_iters seconds"

        # Exit the test with an error.
        exit 1
    fi
}

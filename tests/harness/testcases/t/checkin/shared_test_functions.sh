#!/bin/bash

# Default liveness timeout, the time before a down instance
# is declared dead, is 120s in the out-of-the-box SciDB
# configuration, so retry for at least that long.
# SDB-6840:  The liveness timeout for tests should be
# 1.5X (or other reasonable constant multiplier) of whatever
# the liveness configuration is for SciDB, rather than this
# static constant.
default_timeout=180

# Blocks until the expected number of array versions appears in the output of list('arrays')
# In the event that the expected number of arrays does not appear, this exits with a failure
# code which fails the test.
expect_array_versions () {
    local expected_count=$1
    local attempts=0
    while [ $(iquery -otsv -aq "project(list('arrays',true),name)" | wc -l) -ne $expected_count ]; do
        sleep 1;  # Don't hammer SciDB waiting for the arrays to be written
        attempts=$((attempts + 1))
        if [ $attempts -gt $default_timeout ]; then
            echo "expected arrays did not appear, abort"
            exit 1
        fi
    done
}

# Return the query ID of the Child query currently running.
get_child_query_id () {
    child_id=$(iquery -otsv -aq "project(filter(list('queries'),inst=0 and n=0),query_id)")
    echo $child_id
}

# Return the query ID of the Parent query currently running.
get_parent_query_id () {
    parent_id=$(iquery -otsv -aq "project(filter(list('queries'),inst=0 and n=1),query_id)")
    echo $parent_id
}

# Load the system library if it is available.  If it is not available,
# then exit with a success code to effectively skip the test.
load_system_library () {
    iquery -aq "load_library('system')"
    loaded=$?
    if [ $loaded -ne 0 ]; then
        echo "system library not available, skipping test"
        exit 0
    fi
    hasSync=$(iquery -otsv -aq "op_count(filter(list('operators'),name='sync' and library='system'))")
    if [ $hasSync -ne 1 ]; then
        echo "system library lacks the sync function, skipping test"
        exit 0
    fi
}

# Sync the SciDB cluster and retry for some time.
# In the event that the cluster does not sync, this exits with a failure
# code which fails the test.
scidb_sync () {
    local attempts=0
    local result=1
    while [ $result -ne 0 ]; do
        iquery -aq "sync()"
        result=$?
        attempts=$((attempts + 1))
        if [ $attempts -gt $default_timeout ]; then
            echo "cluster will not sync, abort"
            exit 1
        fi
        if [ $result -ne 0 ]; then
            # Only sleep if we're going to try again, otherwise don't wait.
            sleep 1
        fi
    done
}

# Wait for a query matching a substring to start executing.
# In the event that the query does not start, this exits with a failure
# code which fails the test.
wait_for_query_start () {
    local match_str=$1
    local attempts=0
    while [ $(iquery -otsv -aq "filter(list('queries'),query_string!='')" | grep $match_str | wc -l) -eq 0 ];
    do
        sleep 1;  # Don't hammer SciDB waiting for the query to start.
        attempts=$((attempts + 1))
        if [ $attempts -gt $default_timeout ]; then
            echo "query will not start, abort"
            exit 1
        fi
    done
}

# Wait for a query matching a substring to stop executing.
# In the event that the query does not start, this exits with a failure
# code which fails the test.
wait_for_query_stop () {
    local match_str=$1
    local attempts=0
    while [ $(iquery -otsv -aq "filter(list('queries'),query_string!='')" | grep $match_str | wc -l) -gt 0 ];
    do
        sleep 1;  # Don't hammer SciDB waiting for the query to start.
        attempts=$((attempts + 1))
        if [ $attempts -gt $default_timeout ]; then
            echo "query will not stop, abort"
            exit 1
        fi
    done
}

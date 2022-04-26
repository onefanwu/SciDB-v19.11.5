#!/bin/bash

# When working correctly, the 'create array' will block because
# the global array lock prevents it from executing.
iquery -aq "lock_arrays(true)"
iquery -aq "create array create_array_test_case <v:int64>[i=0:*]" &
create_array_child=$!

# Need to give the create array a chance to run, otherwise we could get
# to the loop below and not see it because it hasn't yet started.
# If this time passes and we don't see the query, then consider that a
# bug.
sleep 1

for i in $(seq 3); do
    # Verify that the 'create array' query is blocking.
    query_pending=$(iquery -otsv -aq "filter(project(list('queries'),query_string), regex(query_string,'^create(.*)'))" | wc -l)
    if [ $query_pending -eq 0 ]; then
        # The query completed, which it shouldn't, because it should be blocked
        # by the global array lock.  Cleanup and return failure.
        iquery -aq "lock_arrays(false)"
        iquery -aq "remove(create_array_test_case)"
        exit 1
    fi
    sleep 1
done

# Remove the global lock, then wait on the 'create array' query to finish.
iquery -aq "lock_arrays(false)"

# This is the create array query, which must complete with the
# removal of the global array lock.
wait $create_array_child

# Remove the created array and return success.
iquery -aq "remove(create_array_test_case)"

sleep 1
exit 0

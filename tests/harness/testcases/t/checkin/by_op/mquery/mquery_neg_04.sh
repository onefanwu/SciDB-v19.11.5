#!/bin/bash

source $TESTDIR/../../shared_test_functions.sh

# If the throw() function from the accelerated_io_tools library
# isn't available, then don't perform this test and return with
# a success error code.
has_throw_function=$(iquery -otsv -aq "\
  op_count(\
    filter(\
      project(\
        list(\
          'functions'\
        ),\
        name,\
        library\
      ),\
      name='throw' and library='accelerated_io_tools'\
    )\
  )")

if [ $has_throw_function -eq 0 ]; then
    echo "accelerated_io_tools not loaded, no throw() function"
    exit 0
fi

# This loop would fail within the first three or so iterations
# when the bug was present.  Check 10 times... to have confidence.
for i in $(seq 10); do
  iquery -aq "create array target_array1 \
                           <name: string not null,
                            age:string not null>[up_idx]"
  iquery -aq "\
    insert(\
      redimension(\
        apply(\
          build(\
            <v:int64>[up_idx=1:1],\
            up_idx\
          ),\
          name, 'bob',\
          age, '32'\
        ),\
        target_array1\
      ),\
      target_array1\
    )"
  iquery -aq "\
    insert(\
      redimension(\
        apply(\
          build(\
            <v:int64>[up_idx=1:1],\
            up_idx\
          ),\
          name, 'alice',\
          age, '33'\
        ),\
        target_array1\
      ),\
      target_array1\
    )"
  iquery -aq "store(target_array1, dfarr)"
  iquery -aq "\
    mquery(\
      insert(\
        redimension(\
          filter(\
            apply(\
              cross_join(\
                dfarr,\
                 op_count(\
                  target_array1\
                )\
              ),\
               check_zero,\
               iif(\
                count > 0,\
                 throw(\
                  'non-zero count'\
                ),\
                 TRUE\
              )\
            ),\
             check_zero\
          ),\
           target_array1\
        ),\
        target_array1\
      )\
    )"
  iquery -aq "remove(target_array1)"

done

iquery -aq "remove(dfarr)"

# This shouldn't have to wait at all and is a paranoia
# check to ensure that the mquery has completed.
wait_for_query_stop "mquery"

exit 0

#!/bin/bash
#
# BEGIN_COPYRIGHT
#
# Copyright (C) 2008-2019 SciDB, Inc.
# All Rights Reserved.
#
# SciDB is free software: you can redistribute it and/or modify
# it under the terms of the AFFERO GNU General Public License as published by
# the Free Software Foundation.
#
# SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
# INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
# NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
# the AFFERO GNU General Public License for the complete license terms.
#
# You should have received a copy of the AFFERO GNU General Public License
# along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
#
# END_COPYRIGHT
#
################################################################
# This script modifies the Test.xml file created by preparecdashreport
#
# If ctest timed out from "set_tests_properties(testname PROPERTIES TIMEOUT value)"
# then find out what test was cut off and create a fake <Test Status="notrun"></Test>
# entry in the Test.xml file for that test.
#
################################################################
# GLOBALS
# working path
wp=$(dirname $0)
wp=$(readlink -f ${wp})
# Test Template file
TEST_TEMPLATE=${wp}/TestTemplate.xml
# Temporary file
tempfile=$(mktemp /var/tmp/TestTemplate.XXXXXX)
trap "rm -f $tempfile" 0 15 3 2 1
#
# If the test that was cut off did actually finish (its in the Test.xml file)
# then the fake <Test> will be named the same as the one that actually finished
# but with a prefix of $NEXT_TEST_PREFIX
# 
NEXT_TEST_PREFIX="NEXT"
################################################################
# Arguments
#   $1 = Test.xml produced by preparecdashreport
#   $2 = Where to put the modified Test.xml
#   $3 = Notes.xml the test log file
#
usage () {
    echo Usage:
    echo   $0 Test.xml REPORTS_DIRPATH Notes.xml
}
if [ $# != 3 ]; then
    usage
    exit 1
fi
if [ ! -f $1 ]; then
    echo Test.xml=$1 NOT FOUND
    usage
    exit 1
else
    TEST_XML="$1"
fi
if [ ! -d $2 ]; then
    echo REPORTS_DIRPATH=$2 NOT FOUND
    usage
    exit 1
else
    REPORTS_DIRPATH="$2"
fi
if [ ! -f $3 ]; then
    echo Notes.xml=$3 NOT FOUND
    usage
    exit 1
else
    NOTES_XML="$3"
fi
################################################################
NEW_TEST_XML=${REPORTS_DIRPATH}/Test.xml
#
# See if there was a timeout
#   Look for a line in Notes.xml like this:
#     1/1 Test #10: ScidbNamespaceTestFull ...........***Timeout 120.08 sec
#
if grep -F -q '***Timeout' $NOTES_XML; then
    # There was a timeout
    # Set TIMEOUT to the value
    TIMEOUT="$(grep -F '***Timeout' $NOTES_XML | awk '{print $(NF-1)}')"
else
    # No timeout so do no processing
    # Just copy the Test.xml to the new Text.xml
    cp $TEST_XML $NEW_TEST_XML
    exit 0
fi
#
# Parse log looking for last test (start or end)
#   Tests look like this in the Notes.xml file:
#     10: [4][2019-07-01 10:59:24]: [end]   t.timeout.4 PASS 5.006 5
#     10: [5][2019-07-01 10:59:24]: [start] t.timeout.5
#
LAST_TEST="$(grep '\[start\]\|\[end\]' $NOTES_XML | tail -1)"
if echo "$LAST_TEST" | grep -q '\[start\]'; then
    # Found a start but no end
    # This test was timed out
    TIMED_OUT_TEST="$(echo $LAST_TEST | awk '{print $5}')"
else
    # Found an end
    # It is the next test that timed out
    # Make the name of the TIMED_OUT_TEST be the completed test with a prefix of $NEXT_TEST_PREFIX
    TIMED_OUT_TEST="$NEXT_TEST_PREFIX.$(echo $LAST_TEST | awk '{print $5}')"
fi
#
# Get the timestamp of the last test
#
START_TIMESTAMP=$(echo $LAST_TEST | awk -F '[][]' '{print $4}')
#
# Get the timestamp of the end of the test run
#   Look for the line in Notes.xml like this:
#     CTEST: TIMESTAMP 2019-07-03 03:56:18 END TEST scidb Assert namespace named
#
END_TEST_TIMESTAMP=$(awk '$1 == "CTEST:" && $2 == "TIMESTAMP" && $5 == "END" && $6 == "TEST" {print $3, $4}' $NOTES_XML)
#
# Now compute the time the cutoff test ran for by subtracting START_TIMESTAMP from END_TEST_TIMESTAMP
#   Convert to epochs and subtract
ETT=$(date -d "$END_TEST_TIMESTAMP" +%s)
ST=$(date -d "$START_TIMESTAMP" +%s)
TIMEOUT=$(expr $ETT - $ST)
#
# Check Test.xml to see if the start but no end test from the previous section
# was just stdout chopped off by ctest() and the test actually did finish
#   Looking for lines in Test.xml like this:
#     <Name>t.timeout.4</Name>
#
LAST_TEST="$(grep '\<Name\>' $TEST_XML | tail -1 | awk  -F '[<>]' '{print $3}')"
if [ "$TIMED_OUT_TEST" == "$LAST_TEST" ]; then
    # The incomplete test in Notes.xml actually did finish
    # Its in Test.xml
    # Change the name of TIMED_OUT_TEST to a prefix of $NEXT_TEST_PREFIX
    TIMED_OUT_TEST="$NEXT_TEST_PREFIX.$TIMED_OUT_TEST"
fi
################################################################
# Now to do the real work
#   Insert a new test in Test.xml
#   with name $TIMED_OUT_TEST
#   with <Test Status="notrun">
#   with <NamedMeasurement name="Execution Time"> with a value of calculated $TIMEOUT
#
rm -f $NEW_TEST_XML $tempfile
# Create the new test from the template
m4 -DTIMED_OUT_TEST="$TIMED_OUT_TEST" -DTIMEOUT="$TIMEOUT" $TEST_TEMPLATE > $tempfile
# Stick it at the end of the tests, before the <EndDateTime> line
while read -r line
do
    if echo "$line" | grep -q 'EndDateTime' ; then
        while read -r line2
        do
            echo $line2 >> $NEW_TEST_XML
        done < $tempfile
    fi
    echo $line >> $NEW_TEST_XML
done < $TEST_XML

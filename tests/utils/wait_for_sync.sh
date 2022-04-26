#!/bin/bash
#
# BEGIN_COPYRIGHT
#
# Copyright (C) 2016-2019 SciDB, Inc.
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

#
# Argument parsing
#

function usage
{
    cat - 1>&2 << EOF
Usage: $0
All parameters are optional:
    <-q|--query>   specify a query to run.  The default is 'sync()'.
    <-w|--wait>	   specify a time in seconds to continue trying the query.
    	           The default is 120 seconds.
    <-l|--loud>	   sends the executing lines to the output via set -x.
    <-h|--help>	   prints this message.

By default, the script will execute on host localhost, and port 1239.  One can
specify a list of hostnames and ports on which to try the query.  These lists
must be redirected in from a file:

wait_for_sync.sh -l -w 60 < /tmp/hosts.txt

Where /tmp/hosts.txt contains something like:

acquinas.local.paradigm4.com:1240
acquinas.local.paradigm4.com:1239
davinci:1240
davinci:1239

The hostnames and ports must obviously be those specified in the SciDB
configuration.
EOF

    exit 1
}

QUERY="sync()"
MAX_WAIT=120
LOUD=

i=0
while [ $# -ne 0 ]
do
    case "$1" in
	-h|--help)
	    usage
	    shift
	    ;;
	-q|--query)
	    QUERY="$2"
            shift
	    ;;
        -w|--wait)
            MAX_WAIT="$2"
            shift
            ;;
        -l|--loud)
            LOUD=yes
            ;;
	-*)
            echo "${0} unknown flag $1"
	    exit 1
	    ;;
    esac
    shift
done

iquery_result=0
run_iquery() {
    if [ -n "$LOUD" ]
    then
        iquery "$@" 2>&1 1>/dev/null
        iquery_result=$?
    else
        iquery "$@" >/dev/null 2>&1
        iquery_result=$?
    fi
}

# Very loud on stderr....
[ "$LOUD" = "yes" ] && set -x

re='^[0-9]+$'
i=0
# Stdin will contain nothing or a list of host:port instances from a file.
# If one isn't supplied by redirection, the read should time out immeidiately.
# This is never typed in from the command line.
while read -t .01 hostport
do
    hosts[i]=$(echo ${hostport} | cut -d : -f 1)
    ports[i]=$(echo ${hostport} | cut -d : -f 2)
    if ! [[ ${ports[i]} =~ $re ]];
    then
	echo must specify integers as port numbers, not ${ports[i]}
	exit 1
    fi
    i=$((i+1))
    readstdin="TRUE"
done

echo $hostport
if [[ $readstdin != "TRUE" ]]
then
    hosts[0]='localhost'
    ports[0]='1239'
fi

echo -n "  $QUERY "
n=${#hosts[@]}
n=$((n-1))
attempts=0
INST=0
while [ $attempts -lt $MAX_WAIT ]; do
    run_iquery -c ${hosts[INST]} -p ${ports[INST]} -aq $QUERY
    if [ $iquery_result -eq 2 ]; then
        # iquery exit code 2 means that the SciDB server to which we tried
        # to connect was not listening on the port (likely because it was
        # dead).  Let's try another server in the list.  If the list has
        # only one entry, then we'll try again on that entry (although
        # there shouldn't be tests of a single-instance cluster that kill
        # that instance and never start it again).
        INST=$(( INST + 1 ))
        if [ $INST -gt $n ]; then
            INST=0
        fi
    elif [ $iquery_result -eq 0 ]; then
        # Success!
        exit 0
    fi
    sleep 1
    echo -n "."
    attempts=$(( attempts + 1 ))
done

echo failed to $QUERY after waiting for $MAX_WAIT cycles
exit 1

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
#set -x

INFILE=$1

LINE1=$(grep "server-0=" $INFILE)
LINE2=$(grep "server-1=" $INFILE)

HOST1=$(echo "$LINE1" | sed s/server-0=// | sed 's/,.*//')
HOST2=$(echo "$LINE2" | sed s/server-1=// | sed 's/,.*//')

TMPFILE=/tmp/$$.config.ini

cp $INFILE $TMPFILE

IP1=$(nslookup $HOST1 | fgrep Address: | cut -d' ' -f2 | tail -1)
IP2=$(nslookup $HOST2 | fgrep Address: | cut -d' ' -f2 | tail -1)
sed --in-place '2,2s/.*/server-0='"$HOST1"',0\nserver-1='"$IP1"',1-1/' $TMPFILE
sed --in-place '4,4s/.*/server-2='"$HOST2"',2-2\nserver-3='"$IP2"',3-3/' $TMPFILE

cat $TMPFILE
rm $TMPFILE


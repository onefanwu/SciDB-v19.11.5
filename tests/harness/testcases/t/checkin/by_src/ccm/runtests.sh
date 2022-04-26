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
# set -e
DIR=$(dirname $0)

# trap finish EXIT
echo "#-*- mode: org -*-"

function runtest()
{
  local test="$1"
  cat  ${DIR}/${test}  | ccmtest.py -A ${DIR}/scidbadmin-good.auth
}

runtest UnauthSession.in
runtest AuthingSession.in
runtest AuthenticatedSession.in
runtest BadQuery.in
runtest InvalidMessages.in

echo "* TODO InvalidProtobuf is incomplete"
echo "       Until the ccmtest.py client can establish a (server-side) severed ~socket~"
echo "       The test *actually* exits prematurely on the second call."
echo "       - For phase 1 that's good enough. The test is to see that the connection is severed by the server."
runtest InvalidProtobuf.in

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

iquery -naq "remove(MEASUREMENT_ACCELERATION)" 2>&1 > /dev/null
iquery -naq "remove(MEASUREMENT_KINECT)" 2>&1 > /dev/null
iquery -naq "remove(MEASUREMENT)" 2>&1 > /dev/null
iquery -naq "remove(EVENT)" 2>&1 > /dev/null
iquery -naq "remove(SUBJECT)" 2>&1 > /dev/null
iquery -naq "remove(ALIGNMENT)" 2>&1 > /dev/null
iquery -naq "remove(DATA_SUMMARY)" 2>&1 > /dev/null
iquery -naq "remove(DEVICE)" 2>&1 > /dev/null

exit 0

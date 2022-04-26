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

# bail on any error
set -e

# execute query identified to be slow
time iquery -aq "op_count(cross_between(filter(project(taqPOC_20160401, timestamp,price,size,source_index,exchange_index,condition,trade_flag), trade_flag), build(<dateLo:int64, rowLo: int64, tickerLo: int64, dateHi: int64, rowHi: int64, tickerHi: int64>[idx=1:4,100000,0],'{1}[(20160401, 0, 6182, 20160401, 1000000000, 6205) , (20160401, 0, 6207, 20160401, 1000000000, 6220) , (20160401, 0, 6222, 20160401, 1000000000, 6239) , (20160401, 0, 6241, 20160401, 1000000000, 6264)]',true))) "

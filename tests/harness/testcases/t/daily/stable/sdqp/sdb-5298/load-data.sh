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

# load axial_aggregate, sourced originally from git@github.com:Paradigm4/axial_aggregate.git
iquery -aq "load_library('axial_aggregate')"

# load local copy of production_macros, sourced originally from
# https://trac.paradigm4.com/browser/poc/trunk/poc/pdq/pdq-git-clones/p4tools/load/PRODUCTION_MACROS
iquery -aq "load_module('${TESTDIR}/production_macros')"

# load data (are all .bin files in this folder needed? each one is ~5GB)
iquery -naq "store(input_and_redimension('/public/FinancePOC/PDQ/BinaryData_tas_20150818/taq_data_20160401/taqPOC_20160401.bin.000000'), taqPOC_20160401)"

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

# bail on any errors
set -e

# scidb_backup restore data
DATA_TARBALL="sdb-5870-PROJECT-DATASET-scidb-no-namespaces.tar.gz"
DATA_DIR="PROJECT-DATASET-no-namespaces"

cp /public/data/tickets/sdb-5870/${DATA_TARBALL} ${TESTDIR}/.
pushd ${TESTDIR} 2>&1 > /dev/null
tar xzf ${TESTDIR}/${DATA_TARBALL}
scidb_backup.py -r opaque ${TESTDIR}/${DATA_DIR}
rm -rf ${TESTDIR}/${DATA_DIR} ${TESTDIR}/${DATA_TARBALL}
popd 2>&1 > /dev/null

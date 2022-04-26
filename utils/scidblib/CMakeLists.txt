########################################
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
########################################

install(PROGRAMS PSF_license.txt
                 __init__.py
                 counter.py
                 iquery_client.py
                 pgpass_updater.py
                 psql_client.py
                 scidb_afl.py
                 scidb_control.py
                 scidb_math.py
                 scidb_progress.py
                 scidb_psf.py
                 scidb_rbac.py
                 scidb_schema.py
                 scidbctl_common.py
                 ssh_runner.py
                 statistics.py
                 util.py
        DESTINATION ${CMAKE_INSTALL_BINDIR}/scidblib
        COMPONENT scidb-utils
  )

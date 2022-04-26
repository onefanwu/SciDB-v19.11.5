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

# Add subfolder of every utility
add_subdirectory("iquery")
add_subdirectory("scidblib")
add_subdirectory("indexmapper")

set(CONFIGURE_PREPARE_DB_METADATA_LOCAL "data/meta.sql")

set(CONFIGURE_PREPARE_DB_METADATA "${CMAKE_INSTALL_PREFIX}/share/scidb/meta.sql")

set(CONFIGURE_SCIDBPY_CONFIG "${CMAKE_INSTALL_PREFIX}/etc/config.ini")

configure_file(log1.properties "${GENERAL_OUTPUT_DIRECTORY}/log1.properties" COPYONLY)
if(CMAKE_BUILD_TYPE STREQUAL "Valgrind")
  set(CONFIGURE_SCIDB_PY_VALGRIND "use_valgrind = True")
else(CMAKE_BUILD_TYPE STREQUAL "Valgrind")
  set(CONFIGURE_SCIDB_PY_VALGRIND "use_valgrind = False")
endif(CMAKE_BUILD_TYPE STREQUAL "Valgrind")


install(FILES prelude.txt.in
        RENAME prelude.txt
        DESTINATION lib/scidb/modules
        COMPONENT scidb
  )

install(PROGRAMS scidb_cores
                 pg_seq_reset.py
                 scidb_backup.py
                 system_report.py
                 scidb_config.py
                 disable.py
                 scidbctl.py
                 rbactool.py
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT scidb
  )

install(PROGRAMS calculate_chunk_length.py
                 loadpipe.py
                 spaam.py
                 arrays.py
                 iid.sh
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT scidb-utils
  )

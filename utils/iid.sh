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
# iid.sh - dump InstanceID-related fields from the cluster instance table
#
# Typical usage:
#     $ iid.sh -U scidb_pg_user mydb
#

psql "$@"<<EOF
SELECT server_id AS sid,           -- short names good
       server_instance_id AS siid, -- long names bad
       (host || ':' || port) AS host_port,
       instance_id,
       ('s'
        || (instance_id >> 32)
        || '-i'
        || instance_id::bit(32)::integer) AS phys_iid
FROM instance;
EOF

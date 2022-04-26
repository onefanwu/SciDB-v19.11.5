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
observed=$(psql -d ${DB_NAME} -U ${DB_USER} -p ${DB_PORT} -h ${DB_HOST} -c \
                "select namespace_name, array_name as arr_name, array_id as max_arr_id from latest_array_version order by namespace_name, arr_name")
expected=$(psql -d ${DB_NAME} -U ${DB_USER} -p ${DB_PORT} -h ${DB_HOST} -c \
                "select ARR.namespace_name, substring(ARR.array_name,'([^@]+).*') as arr_name, max(ARR.array_id) as max_arr_id from namespace_arrays as ARR group by namespace_name, arr_name order by namespace_name, arr_name")
if [ "$expected" = "$observed" ]; then
    echo "latest_array_version has expected content"
    exit 0
else
    echo "Unexpected result!"
    echo "Expected in latest_array_version (derived from namespace_arrays table):"
    echo "$expected"
    echo "Observed in latest_array_version:"
    echo "$observed"
    exit 1
fi

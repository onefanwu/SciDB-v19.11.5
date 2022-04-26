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
time iquery -aq "
project(
 apply(
     cross_between(
      MEASUREMENT_ACCELERATION,
      project(
       apply(
        cross_join(
         filter(MEASUREMENT, sub_device_site='Sternum' and measurement_type='ACCELERATION') as MEAS,
         cross_join(
          filter(EVENT,   event_short_label='ArisingChairFast'  and subject_visit_id = 1) as EVENT,
          filter(SUBJECT, subject_name='01071601006') as SUBJECT,
          EVENT.subject_id, SUBJECT.subject_id
         ),
         MEAS.subject_id, EVENT.subject_id,
         MEAS.subject_visit_id, EVENT.subject_visit_id
        ),
        subject_id_start,       subject_id,
        visit_start,            subject_visit_id,
        measurement_id_start,   subject_measurement_id,
        millis_start,           event_start_millis,
        subject_id_end,         subject_id,
        visit_end,              subject_visit_id,
        measurement_id_end,     subject_measurement_id,
        millis_end,             event_end_millis
       ),
       subject_id_start, visit_start, measurement_id_start, millis_start,
       subject_id_end, visit_end, measurement_id_end, millis_end
      )
     ), millis, millis), millis, x,y,z)"

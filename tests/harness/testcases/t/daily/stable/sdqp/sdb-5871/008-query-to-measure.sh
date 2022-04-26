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

time iquery -aq "
         consume(apply(
          cross_between(
           public.MEASUREMENT_PRESSURE,
           project(
            apply(
             cross_join(
              project(
               filter(public.MEASUREMENT, sub_device_site='Pressure Mat' and measurement_type='PRESSURE'),
               sub_device_site
              ) as MEASUREMENT,

              project(
               cross_join(
                filter(public.EVENT, event_short_label='ArisingChairFast' and subject_visit_id = 2) as EVENT,
                filter(public.SUBJECT, subject_name = '01071601000') as SUBJECT,
                EVENT.subject_id, SUBJECT.subject_id
               ),
               event_start_millis, event_end_millis
              ) as EVENT_CLAUSE,
              MEASUREMENT.subject_id, EVENT_CLAUSE.subject_id,
              MEASUREMENT.subject_visit_id, EVENT_CLAUSE.subject_visit_id
             ),
             subject_id_start,       subject_id,
             visit_start,            subject_visit_id,
             measurement_id_start,   subject_measurement_id,
             millis_start,           int64(
                          floor(event_start_millis + (0) - (0) -
                               (event_start_millis + (0) - (0)) * (0) + 0.5)),
             extra_start_0, -1, extra_start_1, -1,
             subject_id_end,         subject_id,
             visit_end,              subject_visit_id,
             measurement_id_end,     subject_measurement_id,
             millis_end,             int64(
                          floor(event_end_millis + (0) - (0) -
                               (event_end_millis + (0) - (0)) * (0) + 0.5))
             , extra_end_0, 4611686018427387904, extra_end_1, 4611686018427387904
            ),
            subject_id_start, visit_start, measurement_id_start, millis_start,
            extra_start_0, extra_start_1,
            subject_id_end, visit_end, measurement_id_end, millis_end
            , extra_end_0, extra_end_1
           )
          ),
          millis,millis, r,r, c,c
         ))"

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
           public.MEASUREMENT_ACCELERATION,
           project(
            apply(
             cross_join(
              project(
               filter(public.MEASUREMENT, sub_device_site='ECG Lead I' and measurement_type='ACCELERATION'),
               sub_device_site
              ) as MEASUREMENT,

              project(
               cross_join(
                filter(public.EVENT, event_short_label='ArisingChairFast' and subject_visit_id = 1) as EVENT,
                filter(public.SUBJECT, subject_name = '01071601006') as SUBJECT,
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

             subject_id_end,         subject_id,
             visit_end,              subject_visit_id,
             measurement_id_end,     subject_measurement_id,
             millis_end,             int64(
                          floor(event_end_millis + (5000) - (0) -
                               (event_end_millis + (5000) - (0)) * (0) + 0.5))

            ),
            subject_id_start, visit_start, measurement_id_start, millis_start,

            subject_id_end, visit_end, measurement_id_end, millis_end

           )
          ),
          millis, millis
         ))"

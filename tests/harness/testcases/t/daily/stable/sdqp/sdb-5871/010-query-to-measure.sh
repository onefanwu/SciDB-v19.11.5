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
         consume(project(
          equi_join(
           apply(
            cross_between(
             public.MEASUREMENT_ACCELERATION,
             project(
              apply(

             apply(
         project(
          equi_join(
           project(
            filter( public.MEASUREMENT, sub_device_site='Sternum' and measurement_type='ACCELERATION'),
            sub_device_site
           ),

         equi_join(
          project(
           filter( public.EVENT, event_short_label='ArisingChairFast'),
           event_start_millis,
           event_end_millis
          ),

         project(
          equi_join(
           public.SUBJECT,
           build(<subject_name:string, subject_visit_id:int64>[i=0:3], '[(\'01071601000\',2),(\'01071601001\',2),(\'01071601006\',1),(\'01071601006\',2)]', true),
           'left_names=subject_name',
           'right_names=subject_name',
           'keep_dimensions=1',
           'algorithm=hash_replicate_right'
          ),
          subject_id, subject_visit_id
         ),
          'left_names=subject_id,subject_visit_id',
          'right_names=subject_id,subject_visit_id',
          'algorithm=hash_replicate_right'
         ),
           'left_names=subject_id,subject_visit_id',
           'right_names=subject_id,subject_visit_id',
           'algorithm=hash_replicate_right',
           'keep_dimensions=1'
          ),
          subject_id, subject_visit_id, subject_measurement_id, event_start_millis, event_end_millis
         ), offset, double(0), intercept, double(0), slope, double(0))
             ,
               subject_id_start,       subject_id,
               visit_start,            subject_visit_id,
               measurement_id_start,   subject_measurement_id,
               millis_start,           int64(
                          floor(event_start_millis + (-3000) - (offset) -
                               (event_start_millis + (-3000) - (intercept)) * (slope) + 0.5)),

               subject_id_end,         subject_id,
               visit_end,              subject_visit_id,
               measurement_id_end,     subject_measurement_id,
               millis_end,             int64(
                          floor(event_end_millis + (5000) - (offset) -
                               (event_end_millis + (5000) - (intercept)) * (slope) + 0.5))

              ),
              subject_id_start, visit_start, measurement_id_start, millis_start,

              subject_id_end, visit_end, measurement_id_end, millis_end

             )
            ),
            subject_visit_id,subject_visit_id, millis,millis
           ),
           public.SUBJECT,
           'left_names=subject_id',
           'right_names=subject_id',
           'algorithm=hash_replicate_right'
          ),
          subject_name, subject_visit_id, millis, x, y, z
         ))"

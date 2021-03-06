#!/usr/bin/python
#
# BEGIN_COPYRIGHT
#
# Copyright (C) 2015-2019 SciDB, Inc.
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

"""
Utility functions used by several tests in the t/other directory.
"""

import re


def fail(query_out, expected_error, quiet=True):
    """Examine query output, expecting failure with expected_error.

    @param query_out full output from an iquery call
    @param expected_error error string to look for in query_out
    @param quiet suppress full error description for expected_error
    @return 0 on success (got expected error), 1 otherwise

    @note Because the store() operator parallelizes its work, there is
    an inherent race condition among the workers to see which
    out-of-bounds chunk is the first to be reported in error.  Hence
    the 'quiet' option, so that tests can ignore the *particular*
    chunk that went out of bounds and focus on just making sure there
    was a CHUNK_OUT_OF_BOUNDARIES error.
    """
    m = re.search(expected_error, query_out, re.MULTILINE)
    if m:
        if quiet:
            print expected_error
        else:
            print query_out
        return 0
    if "Error id:" in query_out:
        print "---- Expected", expected_error, "but got: ----"
        print query_out
        print "---------------------------"
    else:
        print "---- Expected", expected_error, "but query succeeded: ----"
        print query_out
        print "---------------------------"
    return 1


def ok(query_out):
    """Examine query output, expecting no errors.

    @param query_out full output from an iquery call
    @return 0 on success (got no error), 1 otherwise
    """
    m = re.search(r'^Error id: ', query_out)
    if not m:
        print query_out
        return 0
    print "---- Unexpected error: ----"
    print query_out
    print "---------------------------"
    return 1


def make_grid(row0, col0, row1, col1, chunk_interval=100):
    """Build query to create a grid of ones in the specified rectangle.

    Both points are included within the rectangle.  For simplicity we
    require that (row0, col0) be closest to the origin, and that there
    be at least as many rows as columns.
    """
    # Asserts are just to keep cross_join() 2nd arg smaller, and
    # generally keep things simple.
    assert row0 >= 0 and col0 >= 0, "hey let's not be so negative"
    assert row0 < row1, "row coordinates reversed"
    assert col0 < col1, "col coordinates reversed"
    n_rows = row1 - row0 + 1
    n_cols = col1 - col0 + 1
    assert n_rows >= n_cols, "2nd cross_join arg is bigger (and slower)"
    q = """
        redimension(
          apply(cross_join(build(<v1:int64>[rowz=0:{0}-1,{0},0], rowz),
                           build(<v2:int64>[colz=0:{1}-1,{1},0], colz)),
                r, rowz + {2},
                c, colz + {3},
                value, 1),
          <value:int64>[r=0:*,{4},0,c=0:*,{4},0])
        """.format(n_rows, n_cols, row0, col0, chunk_interval)
    return q

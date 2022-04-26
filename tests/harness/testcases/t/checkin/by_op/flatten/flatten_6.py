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
Test that flatten()ing a dataframe produces a dense dataframe.
"""

import argparse
import os
import sys
import traceback

from scidblib import AppError
from scidblib.iquery_client import IQuery
from simple_test_runner import SimpleTestRunner, prt


_args = None                    # Parsed arguments
_pgm = None                     # Program name for logging
_iquery = None                  # IQuery client object


def vaid_of(array_name):
    """Return versioned array id (vaid) of given array."""
    out, err = _iquery("project(filter(list(), name='%s'), aid)" % array_name)
    assert not err, "vaid_of(%s): %s" % (array_name, err)
    return int(out)


def chunk_count(vaid):
    """Return number of chunks belonging to versioned array id."""
    out, err = _iquery("op_count(filter(list('chunk map'), aid=%s))" % vaid)
    assert not err, "chunk_count(%d): %s" % (vaid, err)
    return int(out)


def check_v_sum(array_name, _cache=[]):
    """Check that op_sum(A, v) has not changed."""
    out, err = _iquery("op_sum(%s, v)" % array_name)
    assert not err, err
    if _cache:
        assert int(out) == _cache[0], "Botched v_sum for %s, %d != %d" % (
            array_name, int(out), _cache[0])
    else:
        _cache.append(int(out))  # first time called


class TheTest(SimpleTestRunner):

    def setUpClass(self):
        prt("Setup ...", crlf=False)
        self._array_cleanups = []
        self._df1_chunks = 0
        prt("done")

    def tearDownClass(self):
        prt("Teardown ...", crlf=False)
        if not _args.keep_arrays:
            for a in self._array_cleanups:
                _iquery("remove(%s)" % a)
        prt("done")

    def test_00_create_sparse_1d_array(self):
        """Create and populate a sparse 1-D array"""
        ncells = 100
        sparsity = 3.0            # 1 / density
        _, err = _iquery("create array SPARSE <v:int64>[i=0:{0}:0:5]".format(
            ncells - 1))
        assert not err, err
        self._array_cleanups.append('SPARSE')
        _, err = _iquery("""
            insert(
              redimension(
                apply(
                  build(<i:int64>[fud=0:{0}], {1}*fud),
                  (v, 1)),
                SPARSE),
              SPARSE)""".format(int(ncells / sparsity) - 1,
                                int(sparsity)))
        assert not err, err
        check_v_sum('SPARSE')
        nchunks = chunk_count(vaid_of('SPARSE'))
        prt("SPARSE has", nchunks, "chunks")

    def test_01_sparse_to_dataframe(self):
        """Store sparse array as dataframe, count its chunks"""
        # Flatten it...
        _, err = _iquery("store(flatten(SPARSE, _fast:1), DF1)")
        assert not err, err
        self._array_cleanups.append('DF1')
        check_v_sum('DF1')
        self._df1_chunks = chunk_count(vaid_of("DF1"))
        prt("DF1 has", self._df1_chunks, "chunks")

    def test_02_dataframe_to_dense_dataframe(self):
        """Flatten dataframe, count its chunks"""
        _, err = _iquery("store(flatten(DF1), DF2)")
        assert not err, err
        self._array_cleanups.append('DF2')
        check_v_sum('DF2')
        nchunks = chunk_count(vaid_of('DF2'))
        prt("DF2 has", nchunks, "chunks")
        assert nchunks < self._df1_chunks, "DF2 did not get dense!"

    def test_03_dataframe_to_dataframe_w_chunksize(self):
        """Flatten dataframe, count its chunks"""
        _, err = _iquery("store(flatten(DF1, cells_per_chunk:5), DF3)")
        assert not err, err
        self._array_cleanups.append('DF3')
        check_v_sum('DF3')
        nchunks = chunk_count(vaid_of('DF3'))
        prt("DF3 has", nchunks, "chunks")
        assert nchunks < self._df1_chunks, "DF3 did not get dense!"


def main(argv=None):
    """Argument parsing and last-ditch exception handling.

    See http://www.artima.com/weblogs/viewpost.jsp?thread=4829
    """
    if argv is None:
        argv = sys.argv

    global _pgm
    _pgm = "%s:" % os.path.basename(argv[0])  # colon for easy use by print

    parser = argparse.ArgumentParser(description="The redim_autochunk_2 test.")
    parser.add_argument('-c', '--host', default=None,
                        help='Target host for iquery commands.')
    parser.add_argument('-p', '--port', default=None,
                        help='SciDB port on target host for iquery commands.')
    parser.add_argument('-r', '--run-id', type=int, default=0,
                        help='Unique run identifier.')
    parser.add_argument('-k', '--keep-arrays', action='store_true',
                        help='Do not remove test arrays during cleanup.')
    parser.add_argument('-v', '--verbosity', default=0, action='count',
                        help='Increase debug logs. 1=info, 2=debug, 3=debug+')

    global _args
    _args = parser.parse_args(argv[1:])
    IQuery.setenv(_args)

    global _iquery
    _iquery = IQuery(afl=True, format='tsv')

    try:
        tt = TheTest()
        return tt.run()
    except AppError as e:
        print >>sys.stderr, _pgm, e
        return 1
    except Exception as e:
        print >>sys.stderr, _pgm, "Unhandled exception:", e
        traceback.print_exc()   # always want this for unexpected exceptions
        return 2


if __name__ == '__main__':
    sys.exit(main())

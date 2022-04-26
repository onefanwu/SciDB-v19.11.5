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
Automated tests for append()ing to arrays.
"""

import argparse
import itertools
import os
import subprocess as subp
import sys
import tempfile
import traceback

from scidblib import AppError
from scidblib.iquery_client import IQuery
from box_of_points import main as boxofpoints
from simple_test_runner import SimpleTestRunner, prt, dbg


_args = None                    # Globally visible parsed arguments
_pgm = None                     # Globally visible program name
_tmpdir = None                  # Dir for generated data files

_CHUNK_LEN = 10

_LG_BOX_SIDE = 30
_LG_BOX_CELLS = 500

_SM_BOX_SIDE = 15
_SM_BOX_CELLS = 125

_RECT_HEIGHT = 9
_RECT_WIDTH = 18
_RECT_CELLS = 80

_BRICK_DEPTH = 6


def tmpdir(s):
    """Shorthand for naming files in _tmpdir"""
    return os.path.join(_tmpdir, s)


def testdir(s):
    """Shorthand for naming files in --test-dir DIR"""
    return os.path.join(_args.test_dir, s)


def diff(f1, f2):
    """Call diff(1), return exit status (zero is success)."""
    p = subp.Popen(['diff', '-u', f1, f2],
                   stdout=subp.PIPE, stderr=subp.STDOUT)
    out = p.communicate()
    if p.returncode:
        prt(out)
    return p.returncode


def verify(f1, f2):
    """Implement FILES_DIFFER-like functionality."""
    if _args.record:
        os.system('cp -p %s %s' % (f1, f2))
    else:
        assert 0 == diff(f1, f2), "Files differ: %s != %s" % (
            f1, f2)


def ugly_plot(array_name, filename=None):
    """Scan array and do ugly plotting."""
    iquery = IQuery(afl=True, format='tsv+')
    out, err = iquery("scan(%s)" % array_name)
    assert not err, err
    cells = []
    for line in out.splitlines():
        cells.append([int(x) for x in line.split()])
    ugly_plot_cells(cells, filename)


def ugly_plot_cells(cells, filename=None):
    """Crude text display of (x, y, [z,] v) tuples.

    In our tests the x-axis is typically longer, so for readability,
    in these plots the x-axis is vertical increasing down, while the
    y-axis is horizontal increasing to the right.  The z-axis (if any)
    is the sum at each (x,y) position of the 2-D output.  (Not so
    great for 3-D, but adequate for "fingerprinting" the shape of the
    data.)

    (Could just dump the scan() output, but that isn't too helpful for
    visualizing whether stuff is appended in the right place or not.)

    Note: because scan() used tsv+ format, 'cells' is a list of (x, y,
    [z,] v), not of box_of_points-style (v, x, y [,z]).

    """
    ul = [min(c[0] for c in cells), min(c[1] for c in cells)]  # upper left
    lr = [max(c[0] for c in cells), max(c[1] for c in cells)]  # lower right

    # Map (x,y) position to sum-of-v along the z-axis.
    dd = dict()
    for c in cells:
        xy = tuple(c[:2])
        dd[xy] = dd.setdefault(xy, 0) + c[-1]

    def _plot(fh):
        print >>fh, "UL: (%d, %d)\nLR: (%d, %d)" % (
            ul[0], ul[1], lr[0], lr[1])
        for x in xrange(ul[0], lr[0]+1):
            for y in xrange(ul[1], lr[1]+1):
                v = dd.get((x, y))
                if v is None:
                    print >>fh, "----",
                else:
                    print >>fh, "%4d" % v,
            print >>fh          # newline

    if filename is None:
        _plot(sys.stdout)
    else:
        with open(filename, 'w') as F:
            _plot(F)


class TheTest(SimpleTestRunner):

    def setUpClass(self):
        """Create some test data files used by all test methods"""
        prt("Setup ...", crlf=False)
        self._iquery = IQuery(afl=True, format='tsv')
        self._array_cleanups = []
        self._files = {}        # map array name to input file
        # Put all our data files in one temp directory so we can
        # easily remove them all during tearDown.
        if os.system("rm -rf {0} ; mkdir -p {0}".format(_tmpdir)):
            raise AppError("Trouble (re)creating %s" % _tmpdir)

        # Convenient spot to save last predicted op_sum() result, for
        # possible use in computing the next one.
        self._predicted = 0

        # Create 30x30 sparse box of data points.
        self._files['box'] = tmpdir("box.tsv")
        if boxofpoints(['boxofpoints',
                        '--lower-corner', '0,0',
                        '--upper-corner', "{0},{0}".format(_LG_BOX_SIDE - 1),
                        '--cells', str(_LG_BOX_CELLS),
                        '--format', 'tsv',
                        '--output', self._files['box'],
                        '--seed', '42']):
            raise AppError("box_of_points could not create box (%s)" %
                           self._files['box'])

        # Create 15x15 sparse box of data points.
        self._files['smallbox'] = tmpdir("smallbox.tsv")
        if boxofpoints(['boxofpoints',
                        '--lower-corner', '0,0',
                        '--upper-corner', "{0},{0}".format(_SM_BOX_SIDE - 1),
                        '--cells', str(_SM_BOX_CELLS),
                        '--format', 'tsv',
                        '--output', self._files['smallbox'],
                        '--seed', '42']):
            raise AppError("box_of_points could not create smallbox (%s)" %
                           self._files['smallbox'])

        # Create 12x26 sparse rectangle of data points.
        self._files['rect'] = tmpdir("rect.tsv")
        if boxofpoints(['boxofpoints',
                        '--lower-corner', '0,0',
                        '--upper-corner', '%s,%s' % (_RECT_WIDTH - 1,
                                                     _RECT_HEIGHT - 1),
                        '--cells', str(_RECT_CELLS),
                        '--format', 'tsv',
                        '--output', self._files['rect'],
                        '--seed', '42']):
            raise AppError("box_of_points could not create rectangle (%s)" %
                           self._files['rect'])

        # 3-D Space Odyssey, directed by Stanley Cube Brick!

        # Create 15x15x15 dense cube of 1's in box-of-points layout.
        self._files['cube'] = tmpdir("cube.tsv")
        with open(self._files['cube'], 'w') as F:
            for pos in itertools.product(xrange(_SM_BOX_SIDE),
                                         xrange(_SM_BOX_SIDE),
                                         xrange(_SM_BOX_SIDE)):
                x = [1]
                x.extend(pos)
                print >>F, '\t'.join(str(y) for y in x)

        # Create 12x26x6 dense brick of 10's in box-of-points layout.
        self._files['brick'] = tmpdir("brick.tsv")
        with open(self._files['brick'], 'w') as F:
            for pos in itertools.product(xrange(_RECT_HEIGHT),
                                         xrange(_RECT_WIDTH),
                                         xrange(_BRICK_DEPTH)):
                x = [10]
                x.extend(pos)
                print >>F, '\t'.join(str(y) for y in x)

        prt("done")

    def tearDownClass(self):
        prt("Teardown ...", crlf=False)
        if not _args.keep_arrays:
            if os.system("rm -rf {0}".format(_tmpdir)):
                raise AppError("Trouble cleaning up %s" % _tmpdir)
            for a in self._array_cleanups:
                self._iquery("remove(%s)" % a)
        prt("done")

    def _load_points(self, which):
        """Load some box_of_points output into a sorted list of lists"""
        result = []
        with open(self._files[which]) as F:
            for line in F:
                entry = [int(x) for x in line.strip().split()]
                result.append(entry)

        def _key(x):
            return x[1:]       # exclude 1st "row number" (v's value)

        result.sort(key=_key)   # sort to load() without setPosition() calls
        return result

    def _save_points(self, FH, points):
        """Write points list-of-lists to file handle in TSV format"""
        for pt in points:
            print >>FH, '\t'.join(str(x) for x in pt)
        FH.flush()

    def _xlate_points(self, points, offset):
        """Translate points (in list-of-lists form) by an offset vector"""
        if not points:
            return points
        assert len(offset) == len(points[0]) - 1, "Wrong offset vector length!"
        off = [0]
        off.extend(offset)
        result = []
        for pt in points:
            newpt = [sum(x) for x in zip(pt, off)]
            result.append(newpt)
        return result

    def test_00_load_unbounded_box_at_0_0(self):
        """Load "box" points into unbounded array at origin"""
        query = """
            store(
              redimension(
                input(<v:int64,x:int64,y:int64>[dummy], '{0}', -2, 'tsv'),
                <v:int64>[x=0:*:0:{1}; y=0:*:0:{1}]), {2})
            """.format(self._files['box'], _CHUNK_LEN, "box_at_0_0")
        _, err = self._iquery(query)
        assert not err, err
        self._array_cleanups.append("box_at_0_0")

        # Box_of_points.py numbers its output rows from zero, and we
        # use these numbers as values for 'v'.  So we can always
        # compute the op_sum(X, v) for any X as sum(range(_X_CELLS)).
        # This lets us verify that all cells were appended without
        # spewing a lot of output.
        predicted = sum(xrange(_LG_BOX_CELLS))
        out, err = self._iquery("op_sum(box_at_0_0, v)")
        assert not err, err
        out = out.strip()
        prt("Initial box_at_0_0 sum:", out)
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

        ugly_plot('box_at_0_0', tmpdir('box0.plot'))
        verify(tmpdir('box0.plot'), testdir('append_ar_2_box0.expected'))

    def test_01_load_rectangle_at_mid_chunk(self):
        """Load "rect" points into bounded array at half-chunk origin"""
        origin = [_CHUNK_LEN / 2] * 2
        corner = [origin[0] + _RECT_WIDTH, origin[1] + _RECT_HEIGHT]
        points = self._load_points("rect")
        points = self._xlate_points(points, origin)
        with tempfile.NamedTemporaryFile() as TF:
            self._save_points(TF, points)
            query = """
                store(
                  redimension(
                    input(<v:int64,x:int64,y:int64>[dummy], '{0}', -2, 'tsv'),
                    <v:int64>[x={1}:{2}:0:{3}; y={4}:{5}:0:{3}]), {6})
                """.format(TF.name, origin[0], corner[0], _CHUNK_LEN,
                           origin[1], corner[1], "rect_at_mid")
            _, err = self._iquery(query)
            assert not err, err
        self._array_cleanups.append("rect_at_mid")

        ugly_plot('rect_at_mid', tmpdir('rect.plot'))
        verify(tmpdir('rect.plot'), testdir('append_ar_2_rect.expected'))

        predicted = sum(xrange(_RECT_CELLS))
        out, err = self._iquery("op_sum(rect_at_mid, v)")
        assert not err, err
        out = out.strip()
        prt("Initial rect_at_mid sum:", out)
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

    def test_02_append_bounded_to_unbounded_x(self):
        """Append bounded rectangle to unbounded box along x dimension."""
        out, err = self._iquery("append(rect_at_mid, box_at_0_0, x)")
        assert not err, err

        ugly_plot('box_at_0_0', tmpdir('boxrect-x.plot'))
        verify(tmpdir('boxrect-x.plot'),
               testdir('append_ar_2_boxrect-x.expected'))

        predicted = sum(xrange(_LG_BOX_CELLS)) + sum(xrange(_RECT_CELLS))
        out, err = self._iquery("op_sum(box_at_0_0, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

    def test_03_append_bounded_to_unbounded_y(self):
        """Append bounded rectangle to unbounded box along y dimension"""
        out, err = self._iquery("append(rect_at_mid, box_at_0_0, y)")
        assert not err, err

        ugly_plot('box_at_0_0', tmpdir('rectbox-y.plot'))
        verify(tmpdir('rectbox-y.plot'),
               testdir('append_ar_2_rectbox-y.expected'))

        predicted = sum(xrange(_LG_BOX_CELLS)) + 2 * sum(xrange(_RECT_CELLS))
        out, err = self._iquery("op_sum(box_at_0_0, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

    def test_04_load_small_box_into_bounded_box(self):
        """Load small box into bounded space anchored at (-12, -12)"""
        query = """
            store(
              redimension(
                input(<v:int64,x:int64,y:int64>[dummy], '{0}', -2, 'tsv'),
                <v:int64>[x=-12:50:0:{1}; y=-12:50:0:{1}]), {2})
            """.format(self._files['smallbox'], _CHUNK_LEN, "box_at_twelves")
        _, err = self._iquery(query)
        assert not err, err
        self._array_cleanups.append("box_at_twelves")

        ugly_plot('box_at_twelves', tmpdir('smbox.plot'))
        verify(tmpdir('smbox.plot'), testdir('append_ar_2_smbox.expected'))

        predicted = sum(xrange(_SM_BOX_CELLS))
        out, err = self._iquery("op_sum(box_at_twelves, v)")
        assert not err, err
        out = out.strip()
        prt("Initial box_at_twelves sum:", out)
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

    def test_05_append_x_until_overflow(self):
        """Append smallbox to itself until bounded space overflows"""
        overflow = False
        offset = [-12, 0]         # just to shake things up
        points = self._load_points("smallbox")

        TRIES = 5
        for i in xrange(TRIES):
            offset[1] += (i+1) * 4  # bump along y-dimension
            pts = self._xlate_points(points, offset)
            with tempfile.NamedTemporaryFile() as TF:
                self._save_points(TF, pts)
                query = """
                    append(
                      redimension(
                        input(<v:int64,x:int64,y:int64>[dummy], '{0}',
                              -2, 'tsv'),
                        <v:int64>[x=-15:*; y=-15:*]),
                      box_at_twelves, x)
                    """.format(TF.name)
                _, err = self._iquery(query)
                if err:
                    assert 'SCIDB_LE_CHUNK_OUT_OF_BOUNDARIES' in err, err
                    overflow = True
                    break
        assert overflow, "No overflow after %d tries?!" % TRIES
        dbg("Overflow on", i+1, "-th append")

        ugly_plot('box_at_twelves', tmpdir('nXsmbox-x.plot'))
        verify(tmpdir('nXsmbox-x.plot'),
               testdir('append_ar_2_nXsmbox-x.expected'))

        predicted = sum(xrange(_SM_BOX_CELLS)) * (i + 1)
        out, err = self._iquery("op_sum(box_at_twelves, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

    def test_06_append_into_empty_bounded_fixed(self):
        """Append into empty, bounded, fixed-chunk array"""
        _, err = self._iquery(
            "create array EB<v:int64>[x=0:99:0:{0}; y=0:99:0:{0}]"
            .format(_CHUNK_LEN))
        assert not err, err
        self._array_cleanups.append("EB")

        # Along y-axis...
        _, err = self._iquery("""
            append(
              redimension(
                input(<v:int64,x:int64,y:int64>[dummy], '{0}', -2, 'tsv'),
                EB),
              EB, y)
            """.format(self._files['box']))
        assert not err, err
        predicted = sum(xrange(_LG_BOX_CELLS))
        out, err = self._iquery("op_sum(EB, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

    def test_07_append_into_empty_bounded_autochunked(self):
        """Append into empty, bounded, autochunked array"""
        _, err = self._iquery("create array EBA<v:int64>[x=0:99; y=0:99]")
        assert not err, err
        self._array_cleanups.append("EBA")

        _, err = self._iquery("""
            append(
              redimension(
                input(<v:int64,x:int64,y:int64>[dummy], '{0}', -2, 'tsv'),
                EBA),
              EBA, x)
            """.format(self._files['box']))
        assert not err, err
        predicted = sum(xrange(_LG_BOX_CELLS))
        out, err = self._iquery("op_sum(EBA, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == predicted, "Bad sum %s != predicted %d" % (
            out, predicted)

    def test_08_make_3d_space(self):
        """Create empty array for 3-D append() testing"""
        _, err = self._iquery(
            "create array SPACE<v:int64>[x=0:99; y=0:99; z=0:99]")
        assert not err, err
        self._array_cleanups.append("SPACE")

    def test_09_append_cube_1(self):
        """Append first cube to empty space, its offset won't matter"""
        points = self._load_points("cube")
        points = self._xlate_points(points, [5, 5, 0])
        with tempfile.NamedTemporaryFile() as TF:
            self._save_points(TF, points)
            query = """
                append(
                  redimension(
                    input(<v:int64,x:int64,y:int64,z:int64>[dummy], '{0}',
                          -2, 'tsv'),
                    SPACE),
                  SPACE, x)
                """.format(TF.name)
            _, err = self._iquery(query)
            assert not err, err

        ugly_plot('SPACE', tmpdir('cube.plot'))
        verify(tmpdir('cube.plot'), testdir('append_ar_2_cube.expected'))

        self._predicted = _SM_BOX_SIDE * _SM_BOX_SIDE * _SM_BOX_SIDE
        out, err = self._iquery("op_sum(SPACE, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == self._predicted, "Bad sum %s != predicted %d" % (
            out, self._predicted)

    def test_12_append_brick_x(self):
        """Append brick to cube in SPACE at offset"""
        points = self._load_points("brick")
        points = self._xlate_points(points, [15, 15, 0])
        with tempfile.NamedTemporaryFile() as TF:
            self._save_points(TF, points)
            # The redimension schema is SPACE-like but won't match the
            # chunk intervals that the previous test got via autochunking.
            query = """
                append(
                  redimension(
                    input(<v:int64,x:int64,y:int64,z:int64>[dummy], '{0}',
                          -2, 'tsv'),
                    <v:int64>[x=0:99:2:12; y=0:99:2:12; z=0:99:2:15]),
                  SPACE, x)
                """.format(TF.name)
            _, err = self._iquery(query)
            assert not err, err

        ugly_plot('SPACE', tmpdir('cube-brick.plot'))
        verify(tmpdir('cube-brick.plot'),
               testdir('append_ar_2_cube-brick.expected'))

        self._predicted += _RECT_HEIGHT * _RECT_WIDTH * _BRICK_DEPTH * 10
        out, err = self._iquery("op_sum(SPACE, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == self._predicted, "Bad sum %s != predicted %d" % (
            out, self._predicted)

    def test_13_append_cube_z(self):
        """Append cube again, on z-axis, no offset"""
        # The redimension schema is SPACE-like but won't match the
        # chunk intervals that the previous test got via autochunking.
        points = self._load_points("cube")
        points = self._xlate_points(points, [5, 10, 0])
        with tempfile.NamedTemporaryFile() as TF:
            self._save_points(TF, points)
            query = """
                append(
                  redimension(
                    input(<v:int64,x:int64,y:int64,z:int64>[dummy], '{0}',
                          -2, 'tsv'),
                    <v:int64>[x=0:99:2:12; y=0:99:2:12; z=0:99:2:15]),
                  SPACE, z)
                """.format(TF.name)
            _, err = self._iquery(query)
            assert not err, err

        ugly_plot('SPACE', tmpdir('brick-2cube.plot'))
        verify(tmpdir('brick-2cube.plot'),
               testdir('append_ar_2_brick-2cube.expected'))

        self._predicted += _SM_BOX_SIDE * _SM_BOX_SIDE * _SM_BOX_SIDE
        out, err = self._iquery("op_sum(SPACE, v)")
        assert not err, err
        out = out.strip()
        assert int(out) == self._predicted, "Bad sum %s != predicted %d" % (
            out, self._predicted)


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
    parser.add_argument('-R', '--record', action='store_true',
                        help='Save the ugly .plot files')
    parser.add_argument('-t', '--test-dir', default=os.curdir,
                        help='Directory where test resides')
    parser.add_argument('-k', '--keep-arrays', action='store_true',
                        help='Do not remove test arrays during cleanup.')
    parser.add_argument('-v', '--verbosity', default=0, action='count',
                        help='Increase debug logs. 1=info, 2=debug, 3=debug+')

    global _args
    _args = parser.parse_args(argv[1:])
    IQuery.setenv(_args)

    global _tmpdir
    _tmpdir = "/tmp/append_ar_2.{0}".format(_args.run_id)

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

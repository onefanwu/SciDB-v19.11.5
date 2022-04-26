#!/usr/bin/python

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

"""
Do (very) minimal testing to ensure that we can read 'opaque'
format data saved from prior releases.
"""

import argparse
import os
import re
import subprocess as subp
import sys
import textwrap
import traceback

from scidblib import AppError
from scidblib.iquery_client import IQuery
from simple_test_runner import prt, warn

_args = None                    # Parsed arguments
_pgm = None                     # Program name
_iquery = None                  # Callable IQuery client object

CHUNK_INTERVAL = 10000
SCHEMA = "<d:int64 NOT NULL> [ X=0:*:0:{0}]".format(CHUNK_INTERVAL)

# SciDB version on origin/master was 19.4 when we last saved an opaque
# data file for compatibility testing.  If the running version exceeds
# this, it's time to record another "historical" version of the data.
#
# For example, the typical sequence of events is:
# - RelEng makes release branch X.Y and sets origin/master version to X.Y+1.
# - This test fails in check_scidb_version() because X.Y+1 > MASTER_VERSION.
# - QA manually runs this test in --save-latest-version mode, creating
#   a version_X.Y+1.opaque file.
# - File version_X.Y+1.opaque is renamed to version_X.Y.opaque and committed
#   (because this data file reflects the newly frozen X.Y release).
#
# Historically we've only ever promised to support the prior release,
# but it's good to know how far back we might go and still have hope
# of restoring an older opaque backup.
#
MASTER_VERSION = (19, 11)


def vtos(v):
    """Version to string (stringify a version tuple)."""
    return '.'.join(str(x) for x in v)


def vtop(v):
    """Version to implied data file path."""
    return "version_{0}.opaque".format(vtos(v))


def ptov(p):
    """Path to version."""
    m = re.match(r'version_(\d+)\.(\d+)\.opaque$', os.path.basename(p))
    return (int(m.group(1)), int(m.group(2))) if m else None


def scidb_version():
    """Return SciDB (major, minor) version as a tuple of ints."""
    scidb = os.path.join(os.environ['SCIDB_INSTALL_PATH'], 'bin', 'scidb')
    p = subp.Popen([scidb, "--version"], stdout=subp.PIPE, stderr=subp.STDOUT)
    text = p.communicate()[0]
    for line in text.splitlines():
        m = re.match(r'scidb version:\s*(\d+)\.(\d+)\.', line, re.I)
        if m:
            return (int(m.group(1)), int(m.group(2)))
    assert False, "Cannot find SciDB version in:\n---\n%s\n---" % text


def check_scidb_version():
    """Time to record another copy of the test data?"""
    v = scidb_version()
    if v <= MASTER_VERSION:
        return
    raise AppError(textwrap.dedent("""
        SciDB version {0} has increased and exceeds {1}, the version
        of the last recorded data file.  Time to re-record the data
        with the new version of SciDB, for future testing.

        1. Run the script using the --save-latest-version option.

        2. Add the resulting data file to the git repository.

        3. Modify the MASTER_VERSION to reflect the new SciDB version
           in the master branch.  This will typically be one minor
           release beyond the official just-released version.  For
           example, after 21.3 is released, master's version will
           become 21.4.

        4. Commit the updated test and new data file to the
           repository.
        """.format(vtos(v), vtos(MASTER_VERSION))))


def save_latest_version():
    """Load most recent data file, then save it for future testing."""

    # Sanity checks.  We only want to save a new version if it's warranted.
    sv = scidb_version()
    if sv <= MASTER_VERSION:
        raise AppError("SciDB version {0} <= {1}, nothing new to save!"
                       .format(vtos(sv), vtos(MASTER_VERSION)))
    v, f = historical_data_files()[0]  # latest, because reverse sort
    if v == sv:
        raise AppError("SciDB version {0} data already recorded in '{1}'"
                       .format(vtos(v), f))

    # Load latest data, save it as a new version.
    ary = "V%d_%d" % (sv[0], sv[1])
    _, err = _iquery("create array %s %s" % (ary, SCHEMA))
    if err:
        raise AppError("Cannot 'create array %s %s': %s" % (
            ary, SCHEMA, err))
    path = os.path.join(_args.testdir, f)
    _, err = _iquery("load({0}, '{1}', format:'opaque')".format(
        ary, path))
    if err:
        raise AppError("Cannot load '%s': %s" % (path, err))
    newpath = os.path.join(_args.testdir, vtop(sv))
    _, err = _iquery("save({0}, '{1}', format:'opaque')".format(
        ary, newpath))
    if err:
        raise AppError("Cannot save '%s': %s" % (newpath, err))

    prt("Array {0} saved as '{1}' (array not removed)".format(
        ary, newpath))
    return 0


def check_array(ary):
    """Check array integrity with AFL, return False on failure.

    This is the original AFL-based test: take a 100-cell slice, and
    use window() to prove that the cell values are in sorted order.
    We do it twice, once for Paul's choice of offset 1000, and once
    for an offset that spans chunks (which seems like a better test).

    This is a very minimal test.  It would be nice to test opaque
    format compatibility *MUCH* more rigorously.
    """
    fails = 0
    for offset in (1000, CHUNK_INTERVAL - 50):
        try:
            _, err = _iquery("store(subarray({0}, {1}, {1}+100), Slice)"
                             .format(ary, offset))
            assert not err, "Cannot slice array %s at offset %d: %s" % (
                ary, offset, err)
            _, err = _iquery("store(window(Slice, 0, 1, min(d)), Window)")
            assert not err, "Cannot build Window for slice: %s" % err
            out, err = _iquery(
                "aggregate(filter(join(Slice, Window), d != d_min), count(*))")
            assert not err, "Aggregate query failed: %s" % err
            assert int(out) == 0, "%d out-of-order cells in slice!" % (
                int(out), offset)
        except AssertionError as e:
            warn(_pgm, "Array", ary, "offset", offset,
                 "failed integrity check:", e)
            fails += 1
        _, err = _iquery("remove(Window) ; remove(Slice)")
        assert not err, "Cleanup in check_array failed: %s" % err
    return fails == 0


def historical_data_files():
    """Build list of (major, minor, filename) tuples, latest entry first.

    File names are relative to _args.testdir.
    """
    vflist = []
    for fname in os.listdir(_args.testdir):
        v = ptov(fname)
        if v:
            vflist.append((v, fname))
    vflist.sort(reverse=True)   # Test latest version first
    return vflist


def process():
    """Re-load and check integrity of every historical 'opaque' file."""
    fails = 0
    for (major, minor), filename in historical_data_files():
        ary = "V%d_%d" % (major, minor)
        _, err = _iquery("create array %s %s" % (ary, SCHEMA))
        if err:
            raise AppError("Cannot 'create array %s %s': %s" % (
                ary, SCHEMA, err))
        path = os.path.join(_args.testdir, filename)
        _, err = _iquery("load({0}, '{1}', format:'opaque')".format(
            ary, path))
        if err:
            fails += 1
            warn("Cannot load '%s':\n%s" % (filename, err))
            continue
        prt("Load and verify", filename, "...", crlf=False)
        if check_array(ary):
            prt("ok")
        else:
            fails += 1
            prt("FAILED")
        _, err = _iquery("remove(%s)" % ary)
        if err:
            warn("Cannot remove array %s:\n%s" % (ary, err))
    return fails


def main(argv=None):
    """Argument parsing and last-ditch exception handling.

    See http://www.artima.com/weblogs/viewpost.jsp?thread=4829
    """
    if argv is None:
        argv = sys.argv

    global _pgm
    _pgm = "%s:" % os.path.basename(argv[0])  # colon for easy use by print

    parser = argparse.ArgumentParser(  # add_help=False,
        description="Smoke test for 'opaque' format backward compatibility.",
        epilog='Type "pydoc %s" for more information.' % _pgm[:-1])
    parser.add_argument('-c', '--host', default='localhost', help="""
        The SciDB host address.""")
    parser.add_argument('-p', '--port', type=int, default=1239, help="""
        The TCP port for connecting to SciDB.""")
    parser.add_argument('-r', '--run-id', default="", help="""
        Uniquifier (such as $HPID) to use in naming files etc.""")
    parser.add_argument('--save-latest-version', action='store_true', help="""
        Save a new copy of the test data via the installed version of SciDB""")
    parser.add_argument('testdir', help="""
        Test directory holding historical opaque save data""")

    global _args
    _args = parser.parse_args(argv[1:])

    IQuery.setenv(_args)
    global _iquery
    _iquery = IQuery(afl=True, format='tsv')

    try:
        if _args.save_latest_version:
            return save_latest_version()
        check_scidb_version()
        return process()
    except AppError as e:
        warn(_pgm, e)
        return 1
    except KeyboardInterrupt:
        warn("^C")
        return 2
    except Exception as e:
        warn(_pgm, "Unhandled exception:", e)
        traceback.print_exc()   # always want this for unexpected exceptions
        return 2


if __name__ == '__main__':
    sys.exit(main())

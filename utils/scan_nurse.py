#!/usr/bin/python

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

"""
SDB-6178: Examine diff files, verify scan_doctor-related changes only.

This script is committed to the repository for review purposes only.

Hundreds of harness *.test files were "doctored" by scan_doctor.py to
include explicit scan() operators where before the tests were relying
on the implicit scan done by load, store, insert, delete, and their
AQL counterparts.  This script, scan_nurse.py, was used to examine the
hundreds of FILES_DIFFER *.diff files to ensure that only expected
differences were found.  If all differences are acceptable, the scan
nurse generates a shell command to re-record the test.

Example scan_nurse.py usage:

    $ cd $SCIDB_SRC_PATH
    $ find stage/build/tests/harness/testcases/r -name *.diff |
    > scan_nurse.py -f - > /tmp/re-record-the-tests.sh

If no errors appeared on stderr:

    $ chmod +x /tmp/re-record-the-tests.sh
    $ /tmp/re-record-the-tests.sh
"""

import argparse
import os
import re
import sys
import traceback

_args = None                    # Parsed arguments
_pgm = None                     # Program name
_out = None                     # Bunch of output file handles


class AppError(Exception):
    """Base class for all exceptions that halt script execution."""
    pass


class Bunch(object):
    """See _Python Cookbook_ 2d ed., "Collecting a Bunch of Named Items"."""
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


def dbg(*args):
    if _args.verbose:
        print >>sys.stderr, _pgm, ' '.join(str(x) for x in args)


def warn(*args):
    print >>sys.stderr, _pgm, ' '.join(str(x) for x in args)


def open_output_files(basename):
    """Open the report file, re-record script, and git-add script."""
    result = Bunch()
    result.report = open(("%s.rpt" % basename), 'w')
    result.record = open(("%s-record.sh" % basename), 'w')
    result.git = open(("%s-git-add.sh" % basename), 'w')
    return result


def close_output_files(bunch):
    """Close all the output files in the bunch."""
    for attr in bunch.__dict__:
        bunch.__dict__[attr].close()


def is_doctor_delta(lines):
    """True iff these lines look like something scan_doctor.py did."""
    assert lines, "Internal script error, mumble."
    n = len(lines)
    if n not in (2, 3):
        return False            # No chance, pal.
    if lines[0] != "+Query was executed successfully":
        return False
    if lines[1] != "+":
        return False
    if n == 3:
        m = re.match(
            r'\+SCIDB QUERY : <scan(.*)(  -- Inserted by scan_doctor)?>',
            lines[2])
        if not m:
            return False
    # We have a winner.
    return True


def make_names(diff_file):
    """Make a bunch of related file names based on the diff_file name.

    Resulting bunch has attributes 'diff', 'dots', 'test', and 'expected'.
    """
    assert '/r/' in diff_file, "%s: Not a test result file?!" % diff_file
    result = Bunch()
    result.diff = diff_file     # Easy.
    dpath, _, rest = diff_file.partition('/r/')
    parts = os.path.splitext(rest)[0].split(os.sep)
    result.dots = '.'.join(parts)  # some.testing.fun
    if '/p4/' in dpath:
        tdir = ['test/testcases/t']
    else:
        tdir = ['tests/harness/testcases/t']
    tdir.extend(parts)
    tpath = '/'.join(tdir)
    result.test = "%s.test" % tpath  # .../t/some/testing/fun.test
    result.expected = "%s.expected" % tpath  # .../t/some/testing/fun.expected
    return result


def process_one_file(file_handle, file_name):
    """
    Process an test's .diff file.

    Return True if problems found.  Return False if it looks good, and
    print a --record command line.  If everything looks good, we can
    pipe that into /bin/sh !
    """
    dbg("Processing", file_name)
    assert '/r/' in file_name, 'Huh, thought I was looking at .../r/.../*.diff'
    short_file_name = re.sub(r'^(.*)/r/', '.../r/', file_name)

    # Hunt for diff hunks not caused by the scan_doctor.
    delta = []
    for line in file_handle:
        line = line.rstrip()
        if line.startswith('--- ') or line.startswith('+++ '):
            continue            # Just the usual diff cruft.
        if line and line[0] in "+-":
            delta.append(line)
        elif delta:
            if not is_doctor_delta(delta):
                break
            delta = []
    if delta and not is_doctor_delta(delta):
        print >>_out.report, "File:", short_file_name
        if _args.verbose:
            print >>_out.report, "---\n%s---", '\n'.join(delta)
        return True

    # Still here?  All good.
    names = make_names(file_name)
    if "/p4/" in file_name:
        cmd = "./run.py plugin_tests -n p4 --record --test-id="
    else:
        cmd = "./run.py tests --record --test-id="
    print >>_out.record, ''.join((cmd, names.dots))
    print >>_out.git, "git add %s" % names.test
    print >>_out.git, "git add %s" % names.expected
    return False


def process():
    """Process input files."""
    fails = 0
    if _args.files_file:
        try:
            if _args.files_file == '-':
                FF = sys.stdin
            else:
                FF = open(_args.files_file)
            for fname in FF:
                fname = fname.strip()
                with open(fname) as F:
                    fails += int(process_one_file(F, fname))
        finally:
            FF.close()
    elif not _args.files:
        fails += int(process_one_file(sys.stdin, '(stdin)'))
    else:
        for fname in _args.files:
            if fname == '-':
                fails += int(process_one_file(sys.stdin, '(stdin)'))
            else:
                with open(fname) as F:
                    fails += int(process_one_file(F, fname))
    if fails:
        warn(fails, "failures found!")
        print >>_out.report, fails, "failures found!"
    else:
        print >>_out.report, "No non-doctor diff hunks, all good"
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
        description="Fill in program description here!",
        epilog='Type "pydoc %s" for more information.' % _pgm[:-1])
    parser.add_argument('-f', '--files-file',
                        help="File containing list of input filenames")
    parser.add_argument('-o', '--output-base', default='nurse',
                        help="Base name of output files, default 'nurse'")
    parser.add_argument('-v', '--verbose', default=0, action='count',
                        help='Debug logging level, 1=info, 2=debug, 3=debug+')
    parser.add_argument('files', nargs=argparse.REMAINDER,
                        help='The input file(s)')

    global _args
    _args = parser.parse_args(argv[1:])

    global _out
    _out = open_output_files(_args.output_base)

    try:
        if _args.files_file and _args.files:
            raise AppError("Use -f/--files-file or provide file arguments,"
                           " but not both")
        return process()
    except AppError as e:
        print >>sys.stderr, _pgm, e
        return 1
    except KeyboardInterrupt:
        print >>sys.stderr, "^C"
        return 2
    except Exception as e:
        print >>sys.stderr, _pgm, "Unhandled exception:", e
        traceback.print_exc()   # always want this for unexpected exceptions
        return 2
    finally:
        close_output_files(_out)


if __name__ == '__main__':
    sys.exit(main())

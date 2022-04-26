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
SDB-6178: Add missing scan() to tests for insert/load/store/delete.

This script is committed to the repository for review purposes only.
May be useful next time changes need to be made to test files in bulk.

It automatically "doctored" hundreds of harness *.test files to
include explicit scan() operators where before the tests were relying
on the implicit scan done by load, store, insert, delete, and their
AQL counterparts.

Tests that required manual fixes were made immune to further
scan_doctor.py meddling by adding a comment, typically

    # Buzz off, scan_doctor.

somewhere in the test script.

There is an accompanying script, scan_nurse.py, that automatically
checked the *.diff files from the inevitable FILES_DIFFER errors.
Once scan_nurse.py is satisfied that a test is healthy, it generates a
bash script to re-record it.

Example scan_doctor.py usage:

    $ cd $SCIDB_SRC_PATH
    $ find tests/harness/testcases/t -name *.test | scan_doctor.py -f -

NOTE: Yes, the script assumes it is being run from the root of the
SciDB source tree.
"""

import argparse
import os
import re
import sys
import tempfile
import traceback

_args = None                    # Parsed arguments
_pgm = None                     # Program name
_disabled = []                  # Unqualified disable.tests entries
_EDIT_SETUP_SECTION = True      # Default --setup handling

# AFL identifier regex.
ID_RGX = r'[A-Za-z_$][A-Za-z0-9_$]*'

# Potentially namespace-qualified array name regex.
ARRAY_RGX = ID_RGX + r'(\.' + ID_RGX + r')*'

# (regex, group_number) pairs for finding target array name.
RGX_GRP = (
    # Target is first parameter.
    (re.compile(r'\b(load|delete)\s*\(\s*(' + ARRAY_RGX + r')\s*,'), 2),

    # Target is last parameter.
    (re.compile(
        r'\b(store|insert)\s*\(.*,\s*(' + ARRAY_RGX + r')\s*\)[^)]*$'), 2),

    # AQL: select ... into TARGET from ...
    (re.compile(r'\bselect\b.*\binto\s+(' + ARRAY_RGX + r')\s+from', re.I), 1),

    # AQL: update TARGET set ...
    (re.compile(r'\bupdate\s+(' + ARRAY_RGX + r')\s+set', re.I), 1),

    # AQL: load TARGET from ...
    (re.compile(r'\bload\s+(' + ARRAY_RGX + r')\s+from', re.I), 1),

    # AQL: insert into TARGET ...
    (re.compile(r'\binsert\s+into\s+(' + ARRAY_RGX + ')', re.I), 1),
)


class AppError(Exception):
    """Base class for all exceptions that halt script execution."""
    pass


def dbg(*args):
    if _args.verbose:
        print >>sys.stderr, _pgm, ' '.join(str(x) for x in args)


def warn(*args):
    print >>sys.stderr, _pgm, ' '.join(str(x) for x in args)


def make_test_name(fname):
    """Given a filename, turn it into its harness.test.name"""
    path, base = os.path.split(fname)
    test = os.path.splitext(base)[0]
    m = re.match(r'.*/t/(.*)$', path)
    if not m:
        raise AppError("{0}: Not a test file".format(fname))
    parts = m.group(1).split(os.sep)
    parts.append(test)
    return '.'.join(parts)


def process_one_file(file_handle, file_name):
    """Process an open input file."""
    # Any mention of "scan_doctor" keeps the doctor away.  If we
    # hacked the script, let's not hack it again.
    skip = not os.system("grep scan_doctor %s > /dev/null" % file_name)
    if skip:
        print "Skip", file_name, "(already doctored)"
        return
    if make_test_name(file_name) in _disabled:
        print "Skip", file_name, "(test disabled)"
        return
    short_file_name = re.sub(r'^(.*)/t/', '.../t/', file_name)
    dbg("Processing", file_name)
    modified = 0
    igdata = False
    setup = False
    partial = ''                # partial line from backslash
    with tempfile.NamedTemporaryFile() as TF:
        for line in file_handle:
            print >>TF, line,   # First, do no harm.
            line = line.strip()
            # Build up partial lines.
            if partial:
                line = partial + line
                partial = ''
            if line.endswith('\\'):
                partial = "%s " % line[:-1]
                continue
            # Partial line completed.  Further screening...
            if line.startswith('#'):
                continue
            if line.startswith('--error'):
                continue
            if not _EDIT_SETUP_SECTION:
                # This logic is probably not needed now that we're
                # ignoring disabled tests.
                if line.startswith('--setup'):
                    setup = True
                    continue
                if line.startswith('--test') or line.startswith('--cleanup'):
                    setup = False
                    continue
                if setup:
                    continue
            if line.startswith('--igdata'):
                continue
            if line.startswith('--start-igdata'):
                igdata = True
                continue
            if line.startswith('--stop-igdata'):
                igdata = False
                continue
            if igdata:
                continue
            if re.search(r'iquery.*\s-(naq|nq)\s', line):
                # Shell-out to 'iquery -n'.  In practice -naq and -nq
                # are the only option combos to worry about.
                continue
            # Maybe we need a scan() here?
            for rgx, grp in RGX_GRP:
                m = re.search(rgx, line)
                if m:
                    if not modified:
                        print "File: %s" % short_file_name
                    target = m.group(grp)
                    print "Found {0} in '{1}'".format(target, line)
                    print >>TF, "scan(%s)  -- Inserted by scan_doctor" % target
                    modified += 1
                    break
        # Copy out if modified.
        if modified:
            if _args.no_op:
                print "{0}: {1} scan() calls not added (no-op mode)".format(
                    file_name, modified)
            else:
                TF.flush()
                backup = ''.join((file_name, _args.backup_ext))
                if not os.path.exists(backup):
                    os.system("mv {0} {1}".format(file_name, backup))
                os.system("cp {0} {1}".format(TF.name, file_name))
                print "{0}: {1} added scan() calls".format(file_name, modified)


def load_disabled_list():
    """Find all disable.test files and load their unqualified entries."""
    global _disabled
    DISABLE_FILE = 'disable.tests'
    nfiles = 0
    for here, subdirs, files in os.walk(os.getcwd()):
        for ignore in ('.git', 'stage'):
            if ignore in subdirs:
                subdirs.remove(ignore)
        if DISABLE_FILE not in files:
            continue
        nfiles += 1
        path = os.sep.join((here, DISABLE_FILE))
        with open(path) as F:
            for line in F:
                tokens = line.strip().split()
                if not tokens or tokens[0].startswith('#') or len(tokens) > 1:
                    continue
                _disabled.append(tokens[0])
    print len(_disabled), "disabled tests found in", nfiles, "files"


def process():
    """Process input files."""
    if _args.files_file:
        try:
            if _args.files_file == '-':
                FF = sys.stdin
            else:
                FF = open(_args.files_file)
            for fname in FF:
                fname = fname.strip()
                with open(fname) as F:
                    process_one_file(F, fname)
        finally:
            FF.close()
    elif not _args.files:
        process_one_file(sys.stdin, '(stdin)')
    else:
        for fname in _args.files:
            if fname == '-':
                process_one_file(sys.stdin, '(stdin)')
            else:
                with open(fname) as F:
                    process_one_file(F, fname)
    return 0


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
    parser.add_argument('--backup-ext', default='~',
                        help="Extension for backup copies")
    g = parser.add_mutually_exclusive_group()
    g.add_argument('--edit-setup', default=False, action='store_true',
                   help="If true, emit scans in --setup sections")
    g.add_argument('--no-edit-setup', default=False, action='store_true',
                   help="If true, leave --setup sections alone")
    parser.add_argument('-f', '--files-file',
                        help="File containing list of input filenames")
    parser.add_argument('-n', '--no-op', action='store_true',
                        help="Do not modify any files")
    parser.add_argument('-v', '--verbose', default=0, action='count',
                        help='Debug logging level, 1=info, 2=debug, 3=debug+')
    parser.add_argument('files', nargs=argparse.REMAINDER,
                        help='The input file(s)')

    global _args
    _args = parser.parse_args(argv[1:])

    global _EDIT_SETUP_SECTION
    if _args.edit_setup:
        _EDIT_SETUP_SECTION = True
    elif _args.no_edit_setup:
        _EDIT_SETUP_SECTION = False

    try:
        if not _args.backup_ext:
            raise AppError("Backup filename extension required")
        elif not _args.backup_ext.startswith(os.extsep):
            _args.backup_ext = ''.join((os.extsep, _args.backup_ext))
        if _args.files_file and _args.files:
            raise AppError("Use -f/--files-file or provide file arguments,"
                           " but not both")
        load_disabled_list()
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


if __name__ == '__main__':
    sys.exit(main())

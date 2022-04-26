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

"""
Report information about system parameters.

The "scidbctl.py collect-diags ..." command runs this script on each server to
to collect information that may be useful for problem diagnosis.
"""

import argparse
import os
import socket
import subprocess as subp
import sys
import traceback

_args = None                    # Parsed arguments
_pgm = None                     # Program name


class AppError(Exception):
    """Base class for all exceptions that halt script execution."""
    pass


def dbg(*args):
    if _args.verbose:
        print >>sys.stderr, _pgm, ' '.join(str(x) for x in args)


def banner(s, underline_with='-'):
    return os.linesep.join((s, underline_with * len(s)))


def process(fh):
    """Run various commands and include their output in the report."""

    # Make sure the PATH is good.  For some reason .../sbin can disappear.
    want = ('/bin', '/usr/bin', '/sbin', '/usr/sbin')
    p = os.environ.get("PATH", ':'.join(want))  # a little paranoid
    dirs = p.split(':')
    for need in want:
        if need not in dirs:
            dirs.append(need)
    os.environ['PATH'] = ':'.join(dirs)

    # Title and hostname...
    if _args.title:
        print >>fh, banner(_args.title, '=')
    print >>fh, "Host: %s" % socket.gethostname()
    print >>fh

    # Use -n alot to avoid DNS lookups, since we might not have any DNS.
    commands = ("sysctl -a",        # Kernel parameters
                "ip a",             # Interfaces and addresses
                "netstat -i",       # NIC statistics
                "netstat -r -n",    # Routes
                "arp -an",          # ARP cache
                "vmstat -s",        # Memory statistics
                "vmstat -a",        # Active/inactive memory
                "sudo vmstat -m",   # Slab stats
                "vmstat -d -w",     # Disk usage
                "dmesg")            # Kernel ring buffer (KEEP LAST: LARGE!)
    for cmd in commands:
        print >>fh, banner("* %s" % cmd)
        try:
            p = subp.Popen(cmd.split(), stdout=subp.PIPE, stderr=subp.STDOUT)
            out = p.communicate()[0]
            print >>fh, out
        except Exception as e:
            print >>fh, "Cannot run '%s': %s" % (cmd, e)
    return 0


def main(argv=None):
    """Argument parsing and last-ditch exception handling.

    See http://www.artima.com/weblogs/viewpost.jsp?thread=4829
    """
    if argv is None:
        argv = sys.argv

    global _pgm
    _pgm = "%s:" % os.path.basename(argv[0])  # colon for easy use by print

    parser = argparse.ArgumentParser(
        description="Write system parameter information etc. to stdout.")
    parser.add_argument('-t', '--title', help='Report title')
    parser.add_argument('-v', '--verbose', default=0, action='count',
                        help='Debug logging level, 1=info, 2=debug, 3=debug+')
    parser.add_argument('output', nargs='?',
                        help="Output file name, default stdout")

    global _args
    _args = parser.parse_args(argv[1:])

    try:
        if _args.output and _args.output != "-":
            with open(_args.output, 'w') as F:
                return process(F)
        else:
            return process(sys.stdout)
    except AppError as e:
        print >>sys.stderr, _pgm, e
        return 1
    except Exception as e:
        print >>sys.stderr, _pgm, "Unhandled exception:", e
        traceback.print_exc()   # always want this for unexpected exceptions
        return 2


if __name__ == '__main__':
    sys.exit(main())

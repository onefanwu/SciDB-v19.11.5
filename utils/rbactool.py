#!/usr/bin/env python

# BEGIN_COPYRIGHT
#
# Copyright (C) 2008-2019 SciDB, Inc.
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
Tool for examining/updating SciDB role based access control metadata.
"""

import argparse
import getpass
import os
import sys
import traceback

from scidblib import AppError
from scidblib.psql_client import Psql
import scidblib.scidb_rbac as rbac

_args = None                    # Parsed arguments
_pgm = None                     # Program name
_psql = None                    # Postgres client

DBA_USER = 'scidbadmin'


def display():
    """Display one of: raw PG data, merge result, input file."""

    # Read one or both inputs.
    pg = None
    bkp = None
    if _args.dbname:
        pg = rbac.read_pg(_psql, canonical=False)
    if _args.input:
        with open(_args.input) as F:
            bkp = rbac.canonicalize(rbac.from_json(F.read()))

    # If we have both, merge them.  Otherwise we're just interested in
    # reformatting whichever one we got.
    if pg and bkp:
        result = rbac.merge(got=rbac.canonicalize(pg), want=bkp)
    elif pg:
        result = pg
    elif bkp:
        result = bkp
    else:
        raise AppError("Missing required dbname or -i/--input option")

    # Emit result in preferred format.
    if _args.format == 'json':
        print rbac.to_json(rbac.canonicalize(result))
    elif _args.format == 'raw-json':
        # This is useful to look directly at pg, which is not
        # canonicalized until it gets merged.
        print rbac.to_json(result)
    elif _args.format == 'afl':
        cmds = rbac.to_afl(rbac.canonicalize(result))
        cmds.append('')         # for final newline
        print ";\n".join(cmds),
    else:
        raise AppError("Unsupported format '%s'" % _args.format)

    return 0


def restore():
    """Load input into Postgres."""
    if not _args.dbname:
        raise AppError("Required database name argument is missing")
    if not _args.input:
        raise AppError("Required -i/--input option is missing")
    with open(_args.input) as F:
        bkp = rbac.canonicalize(rbac.from_json(F.read()))
    if rbac.restore(_psql, bkp, auth_file=_args.auth_file, max_errors=0):
        raise AppError("Failures during RBAC restore operation")
    print "Restore complete"
    return 0


def main(argv=None):
    """Argument parsing and last-ditch exception handling.

    See http://www.artima.com/weblogs/viewpost.jsp?thread=4829
    """
    if argv is None:
        argv = sys.argv

    global _pgm
    _pgm = "%s:" % os.path.basename(argv[0])  # colon for easy use by print

    parser = argparse.ArgumentParser(description="""
        SciDB RBAC table management tool.  If -i/--input file
        specified, reformat input according to -f/--format.  If dbname
        specified, retrieve RBAC records from Postgres and reformat
        (done during backups). If both -i/--input and dbname are specified,
        retrieve RBAC records from the database, modify the input to
        eliminate records already present, and reformat the remaining
        records per -f/--format (done during restores).
        """, epilog='Type "pydoc %s" for more information.' % _pgm[:-1])
    parser.add_argument('-A', '--auth-file', help="""Authentication file for
                        SciDB user, needed only when using -r/--restore""")
    parser.add_argument('-H', '--host', default='127.0.0.1',  # Not localhost,
                        help="Postgres hostname")             # to match run.py
    parser.add_argument('-f', '--format', choices="afl raw-json json".split(),
                        default='json', help="Output format")
    parser.add_argument('-i', '--input', help="Input file, JSON format")
    parser.add_argument('-p', '--port', default='5432',
                        help="Postgres TCP port")
    parser.add_argument('-r', '--restore', action='store_true', help="""
                        Load the input data into Postgres.  Requires both
                        -i/--input and dbname arguments.  Skips pre-existing
                        entities to avoid accidental privilege escalation.""")
    parser.add_argument('-U', '--username', help="Postgres username")
    parser.add_argument('-v', '--verbose', default=0, action='count',
                        help='Debug logging level, 1=info, 2=debug, 3=debug+')
    parser.add_argument('dbname',  nargs='?', help="""Postgres database,
                        also the SciDB cluster name.""")

    global _args
    _args = parser.parse_args(argv[1:])
    if not _args.username:
        _args.username = _args.dbname if _args.dbname else getpass.getuser()

    global _psql
    _psql = Psql(host=_args.host, port=_args.port,
                 database=_args.dbname, user=_args.username,
                 debug=(_args.verbose > 1))  # -vv for extra loud

    try:
        return restore() if _args.restore else display()
    except AppError as e:
        print >>sys.stderr, _pgm, e
        return 1
    except KeyboardInterrupt:
        print >>sys.stderr, "Interrupt"
        return 2
    except Exception as e:
        print >>sys.stderr, _pgm, "Unhandled exception:", e
        traceback.print_exc()   # always want this for unexpected exceptions
        return 2


if __name__ == '__main__':
    sys.exit(main())

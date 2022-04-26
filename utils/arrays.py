#!/usr/bin/env python

# BEGIN_COPYRIGHT
#
# Copyright (C) 2014-2019 SciDB, Inc.
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
Do stuff to arrays that match a regex.  Run 'arrays.py -h' for
complete help.

Examples
========

Without arguments, runs 'list(ns:all)' to print qualified array names:

    $ arrays.py
    ns0.B
    ns0.B_COOL
    ns0_hot.HOT
    ns1.C
    public.A
    $

With arguments, limits output to array names matching '.*arg.*':

    $ arrays.py B
    ns0.B
    ns0.B_COOL
    $

Remove selected arrays.  If the argument matches more than one array,
ask first (unless --force option is present):

    $ arrays.py -r B
    Remove 2 of 5 arrays? [n]
    $

Remove temporary arrays only.

    $ iquery -aq "project(list(), name, temporary, schema)"
    {No} name,temporary,schema
    {0} 'GREEN',false,'GREEN<v:int64> [i=0:*:0:*]'
    {1} 'TGREEN',true,'TGREEN<w:int64,x:int64> [j=0:*:0:*]'
    $
    $ arrays.py --remove --temp
    Remove: TGREEN
    $

Show schemas.  Uses list() by default, but show() if --force is
present:

    $ arrays.py -s
          ns0.B <v:int64> [i=0:9:0:10]
     ns0.B_COOL <v:int64> [i=0:9:0:10]
    ns0_hot.HOT <v:int64> [i=0:9:0:10]
          ns1.C <v:int64> [i=0:9:0:10]
       public.A <v:int64> [i=0:9:0:10]
    $

Dump chunk maps for selected arrays:

    $ arrays.py --map ns0.B_COOL
    ns0.B_COOL:
      {inst,n} == {0,6}
        inst:0 n:6 svrsn:11 instn:4294967299 dsid:7 doffs:0 uaid:7
        aid:8 attid:0 coord:{0} comp:0 flags:0 nelem:10 csize:152
        usize:152 asize:512
      {inst,n} == {0,7}
        inst:0 n:7 svrsn:11 instn:4294967299 dsid:7 doffs:512 uaid:7
        aid:8 attid:1 coord:{0} comp:0 flags:0 nelem:10 csize:48
        usize:48 asize:512
      {inst,n} == {3,6}
        inst:3 n:6 svrsn:11 instn:4294967299 dsid:7 doffs:0 uaid:7
        aid:8 attid:0 coord:{0} comp:0 flags:0 nelem:10 csize:152
        usize:152 asize:512
      {inst,n} == {3,7}
        inst:3 n:7 svrsn:11 instn:4294967299 dsid:7 doffs:512 uaid:7
        aid:8 attid:1 coord:{0} comp:0 flags:0 nelem:10 csize:48
        usize:48 asize:512
    $

Print selected configuration parameters obtained from the running cluster:

    $ arrays.py --info
    liveness-timeout=120
    max-memory-limit=-1
    mem-array-threshold=1024
    pam-options=
    redundancy=1
    replication-receive-queue-size=64
    replication-send-queue-size=4
    security=password
    sg-receive-queue-size=8
    sg-send-queue-size=16
    smgr-cache-size=256
    target-cells-per-chunk=1000000
    $

"""

import argparse
import os
import re
import sys

from scidblib.iquery_client import IQuery
from scidblib.util import make_table

__all__ = []                    # Just to hide function names from pydoc.

_args = None
_iquery = None
_pgm = None


def yes_or_no(s, _state=[]):
    """Return True if yes(ish), False if no(ish), None otherwise."""
    if not _state:
        _state = [set(['yes', 'y', '1', 'on',  'true']),
                  set(['no',  'n', '0', 'off', 'false'])]
    ss = s.lower()
    if ss in _state[0]:
        return True
    elif ss in _state[1]:
        return False
    else:
        return None


def dberr(s):
    """Return SciDB error string found in 's', or None."""
    if _args.verbose and "Error id:" in s:
        return s
    m = re.match(r'Error id:\s+(.*)$', s)
    if m:
        return m.group(1)
    return None


def namespace_mode():
    """Return True iff there are non-public namespaces."""
    out, err = _iquery("list('namespaces')")
    if _iquery.returncode:
        raise RuntimeError("iquery: cannot list namespaces: %s" % err)
    return len(out.splitlines()) > 1


def display_chunk_map(uaid, fields=None, _cache=[]):
    """Display the chunk map of array whose unversioned array id is uaid.

    @param uaid   unversioned array id of entries to display
    @param fields if provided, display only those attributes named here
    """
    if not _cache:
        _cache.extend(make_table('ChunkMap', "list('chunk map')"))
    for line in _cache:
        if line.uaid != uaid:
            continue
        print "  {inst,n} == {%s,%s}" % (line.inst, line.n)
        pairs = []
        plen = 0
        for f in line._fields:
            if fields and f not in fields:
                continue
            if pairs:
                plen += 1
            pairs.append("%s:%s" % (f, getattr(line, f)))
            plen += len(pairs[-1]) + 1
            if plen > 60:
                print "   ", ' '.join(pairs)
                plen = 0
                pairs = []
        if plen:
            print "   ", ' '.join(pairs)


def info_cmd(inst=0):
    """Print selected configuration options as reported by instance 'inst'.

    See src/system/SciDBConfigOptions.cpp for more possibilities.
    """
    options = """
        redundancy smgr-cache-size mem-array-threshold max-memory-limit
        security pam-options target-cells-per-chunk liveness-timeout
        sg-receive-queue-size sg-send-queue-size
        replication-receive-queue-size replication-send-queue-size
        """.split()
    options.sort()
    for opt in options:
        out, _ = _iquery("_getopt('%s')" % opt)
        print '='.join((opt, out.splitlines()[0]))
    return 0


def sort_dict(d):
    """Provide dict (key, value) pairs in sorted key order."""
    keys = d.keys()
    keys.sort()
    return [(k, d[k]) for k in keys]


def process():

    if _args.op_info:
        return info_cmd()

    # Build dict of array_name-->array_listing_entry.
    # Can't use dictcomps until RHEL6 is de-supported, *sigh*.
    namespaces = _args.ns.split(',') if _args.ns else []
    if namespace_mode():
        arrays = dict(('.'.join((x.namespace, x.name)), x)
                      for x in make_table('ArrayTbl', "list(ns:all)"))
    else:
        arrays = dict((x.name, x)
                      for x in make_table('ArrayTbl', "list(ns:all)"))

    long_victim = 0
    victims = {}
    for rgx in _args.regexes:
        for ary in arrays:
            if namespaces and arrays[ary].namespace not in namespaces:
                continue
            if _args.temp and not arrays[ary].temporary:
                continue
            if re.search(rgx, ary):
                victims.update({ary: arrays[ary]})
                if len(ary) > long_victim:
                    long_victim = len(ary)

    def pad(v):
        return ' ' * (long_victim - len(v))

    if _args.op_remove and len(victims) > 1 and not _args.force:
        proceed = raw_input("Remove %d of %d arrays? [n] " % (
                len(victims), len(arrays)))
        if not proceed or not yes_or_no(proceed):
            return 1

    for victim, info in sort_dict(victims):
        if _args.op_count:
            x, _ = _iquery('aggregate(%s, count(*))' % victim)
            print "{0}{1}:".format(pad(victim), victim),
            err = dberr(x)
            print err if err else x.strip()
        elif _args.op_remove:
            print 'Remove:', victim
            _iquery('remove(%s)' % victim)
        elif _args.op_scan:
            print 'Scan:', victim
            cmd = ['-o%s' % _args.output_format]
            cmd.append('scan(%s)' % victim)
            out, _ = _iquery(*cmd)
            print out
        elif _args.op_show:
            if _args.force:
                # Really run a show() query.
                x, err = _iquery('show(%s)' % victim)
                if err:
                    print >>sys.stderr, "{0}: {1}".format(victim, err)
                    continue
            else:
                # Just use the info already obtained from list().
                x = info.schema
            print "{0}{1}".format(pad(victim), victim),
            print re.sub(r'^[^<]*', '', x.strip())
        elif _args.op_map:
            print "%s:" % victim
            fields = _args.select.split(',') if _args.select else None
            display_chunk_map(info.uaid, fields)
            if len(victims) > 1:
                print "----"
        elif _args.op_list:
            print victim
        else:
            print victim

    return 0


def main(argv=None):
    if argv is None:
        argv = sys.argv

    global _pgm
    _pgm = "%s:" % os.path.basename(argv[0])  # colon for easy use by print

    parser = argparse.ArgumentParser(
        description="Do stuff to arrays that match a regex.",
        epilog='Type "pydoc %s" for more information.' % _pgm[:-1])
    parser.add_argument('-A', '--auth-file',
                        default=os.environ.get('SCIDB_CONFIG_USER', None),
                        help="Authentication file to pass to iquery")
    parser.add_argument('-H', '--host', default='127.0.0.1',
                        help='Host name to pass to iquery')
    parser.add_argument('-p', '--port', default='1239',
                        help='Port number pass to iquery')
    parser.add_argument('-o', '--output-format', type=str, default='dcsv',
                        help='Output format for --scan, default dcsv')
    parser.add_argument('-f', '--force', action='store_true', default=False,
                        help="Don't ask permission for dangerous removes")
    parser.add_argument('-v', '--verbose', action='count',
                        help="Increase debug logging")
    parser.add_argument('-n', '--ns', default=None, help="""
                        Limit output to arrays in selected namespaces""")
    parser.add_argument('--select', type=str, default=None, help="""
                        Show only selected output fields.  Only
                        affects --map for now.
                        """)
    parser.add_argument('-t', '--temp', action='store_true',
                        help="Limit actions to temporary arrays only")
    parser.add_argument('regexes', metavar='REGEX', type=str, nargs='*',
                        help="Regular expression to match array names")
    group = parser.add_mutually_exclusive_group()
    group.add_argument('-c', '--count', action='store_true',
                       dest='op_count', default=False,
                       help='Count cells in matching arrays')
    group.add_argument('-i', '--info', action='store_true',
                       dest='op_info', default=False,
                       help='Print info about running SciDB')
    group.add_argument('-l', '--list', action='store_true',
                       dest='op_list', default=False,
                       help='List matching arrays (default)')
    group.add_argument('-m', '--map', action='store_true',
                       dest='op_map', default=False,
                       help='list chunk maps of matching arrays')
    group.add_argument('-r', '--remove', action='store_true',
                       dest='op_remove', default=False,
                       help='Remove matching arrays')
    group.add_argument('-S', '--scan', action='store_true',
                       dest='op_scan', default=False,
                       help='Scan matching arrays')
    group.add_argument('-s', '--show', action='store_true',
                       dest='op_show', default=False,
                       help="""Show schemas of matching arrays; if
                       --force then use show(), else use list()""")
    global _args
    _args = parser.parse_args(argv[1:])
    if not _args.regexes:
        _args.regexes.append('.*')

    global _iquery
    IQuery.setenv(_args)
    _iquery = IQuery(afl=True, format='tsv', auth_file=_args.auth_file)
    if not (_args.op_scan or _args.op_count):
        _iquery.admin = True    # Use admin channel in case cluster is wedged.

    try:
        return process()
    except RuntimeError as e:
        print >>sys.stderr, e
        return 1
    except KeyboardInterrupt:
        return 2


if __name__ == '__main__':
    sys.exit(main())

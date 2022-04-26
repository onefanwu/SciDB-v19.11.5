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
Test Postgres database functions created via 'scidbctl.py init-syscat'.
"""

import argparse
import os
import sys
import traceback

from scidblib.iquery_client import IQuery
from scidblib.psql_client import Psql
from simple_test_runner import (SimpleTestRunner, dbg)


_args = None                    # Parsed arguments
_pgm = None                     # Program name
_iquery = None                  # IQuery client used by tests
_psql = None                    # Psql client etc.


class TheTest(SimpleTestRunner):

    def __init__(self):
        super(TheTest, self).__init__()
        self.array = "PGFUNCS_%d" % _args.run_id
        self.instances = _psql("select * from instance")
        assert self.instances, "Cannot read Postgres instance table"
        # See SystemCatalog::_addInstance()
        self.insert_instance_query = ' '.join("""
            INSERT INTO "instance" (
                 instance_id, host, port, online_since,
                 base_path, server_id, server_instance_id )
            VALUES (
                 {instance_id}, '{host}', {port}, '{online_since}',
                 '{base_path}', {server_id}, {server_instance_id} )
        """.split())

    def setUpClass(self):
        """Test suite initialization."""

        # Need at least one array, with a few versions.
        _, err = _iquery("create array %s<v:int64>[i=0:9]" % self.array)
        assert _iquery.returncode == 0, "Cannot create %s array: %s" % (
            self.array, err)
        _iquery("store(build({0}, 10-i), {0})".format(self.array))
        assert _iquery.returncode == 0, "Cannot load %s array" % self.array
        _iquery("store(build({0}, 2*i), {0})".format(self.array))
        assert _iquery.returncode == 0, "Cannot reload %s array" % self.array

    def tearDownClass(self):
        """Undo setup actions."""
        _iquery("remove(%s)" % self.array)
        assert _iquery.returncode == 0, "Cannot remove %s array" % self.array

    def _fake_srv0_instance(self):
        """Create a plausible fake instance table entry for server-0."""

        # Gather all the server-0=... instances.
        srv0 = [x for x in self.instances if x.server_id == '0']
        hosts = set([x.host for x in srv0])
        assert len(hosts) == 1, (
            "check_server_id_host constraint already busted?!")

        # Use first srv0 entry as our template.
        inst = srv0[0]._asdict()

        # Need some unique fields so we don't violate the instance
        # table's automatically enforced uniqueness constraints.
        siid = 1 + max(int(x.server_instance_id) for x in srv0)
        inst['server_instance_id'] = str(siid)
        port = 1 + max(int(x.port) for x in self.instances)
        inst['port'] = str(port)
        piid = 1 + max(int(x.instance_id) & 0xFFFFFFFF for x in self.instances)
        inst['instance_id'] = str(piid)

        return inst

    def test_00_test_fakes(self):
        """Show that our fake instances can be inserted."""
        fake = self._fake_srv0_instance()
        query = self.insert_instance_query.format(**fake)
        try:
            dbg("SQL:", query)
            _psql(query)
        except Exception as e:
            assert False, "Cannot insert fake instance: %s" % e
        else:
            _psql('DELETE FROM "instance" WHERE instance_id = %s'
                  % fake['instance_id'])

    def test_01_check_server_id_host(self):
        """All instances with server-id=X must have the same host string."""
        # Bork the host string; this is what's supposed to trigger the
        # constraint violation.
        inst = self._fake_srv0_instance()
        inst['host'] += "_borked"
        query = self.insert_instance_query.format(**inst)
        try:
            dbg("SQL:", query)
            _psql(query)
        except Exception as e:
            assert 'violates check constraint' in str(e), (
                "Unexpected Psql() exception: %s" % e)
        else:
            assert False, "Bad host did not trip insertion constraint"

    def test_02_check_base_path(self):
        """All instances must have the same base_path string."""
        # In the future we may allow instances with different base
        # paths, but for now we just test that the existing limitation
        # is enforced.
        inst = self._fake_srv0_instance()
        inst['base_path'] = '/some/other/path'
        query = self.insert_instance_query.format(**inst)
        try:
            dbg("SQL:", query)
            _psql(query)
        except Exception as e:
            assert 'violates check constraint' in str(e), (
                "Unexpected Psql() exception: %s" % e)
        else:
            assert False, "Bad base_path did not trip insertion constraint"

    def test_03_check_uaid_in_array(self):
        """Residency table entries must name a valid unversioned array id."""
        # Compute a uaid that does not appear in the array table.
        arrays = _psql(
            """SELECT * FROM "array" WHERE name NOT LIKE '%@%'""")
        assert arrays, "What happened to my arrays from setUpClass()?"
        bad_uaid = 1 + max(int(x.id) for x in arrays)

        # Can we insert this bad_uaid into the residency table?
        query = """
            INSERT INTO array_residency ( array_id, instance_id )
            VALUES ( {0}, {1} )
        """.format(bad_uaid, self.instances[0].instance_id)

        try:
            dbg("SQL:", query)
            _psql(query)
        except Exception as e:
            assert 'violates check constraint' in str(e), (
                "Unexpected Psql() exception: %s" % e)
        else:
            assert False, "Bad uaid did not trip insertion constraint"
        finally:
            # Clean up any bogus entry so we don't screw other tests.
            _psql("DELETE FROM array_residency WHERE array_id = %d" % bad_uaid)


def main(argv=None):
    """Argument parsing and last-ditch exception handling.

    See http://www.artima.com/weblogs/viewpost.jsp?thread=4829
    """
    if argv is None:
        argv = sys.argv

    global _pgm
    _pgm = "%s:" % os.path.basename(argv[0])

    parser = argparse.ArgumentParser(
        description="Test Postgres constraint functions.")
    parser.add_argument('-c', '--host', default=None,
                        help='Target host for iquery commands.')
    parser.add_argument('-H', '--db-host', required=True,
                        help='Target host for psql commands.')
    parser.add_argument('-p', '--port', default=None,
                        help='SciDB port on target host for iquery commands.')
    parser.add_argument('-P', '--db-port', default=None,
                        help="Postgres port.")
    parser.add_argument('-r', '--run-id', type=int, default=0,
                        help='Unique run identifier.')
    parser.add_argument('-u', '--db-user', required=True,
                        help='Postgres role name.')
    parser.add_argument('-d', '--db-name', required=True,
                        help='Postgres database name.')

    global _args
    _args = parser.parse_args(argv[1:])

    global _iquery
    _iquery = IQuery(afl=True, format='tsv')
    if _args.host:
        _iquery.host = _args.host
    if _args.port:
        _iquery.port = _args.port

    global _psql
    _psql = Psql()
    _psql.user = _args.db_user
    _psql.host = _args.db_host
    if _args.db_port:
        _psql.port = _args.db_port

    try:
        tt = TheTest()
        return tt.run()
    except Exception as e:
        print >>sys.stderr, _pgm, "Unhandled exception:", e
        traceback.print_exc()   # always want this for unexpected exceptions
        return 2


if __name__ == '__main__':
    sys.exit(main())

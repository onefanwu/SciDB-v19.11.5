#!/usr/bin/python

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

"""Start, stop, and manage a SciDB cluster.

IMPLEMENTATION NOTES
====================

There are several kinds of functions, distinguished by their prefix:

  do_   Top-level implementation of a subcommand.  Runs in the main
        process.  Return 0 for success, some other integer on failure.
        These functions take exactly one argument, 'ctx', which is
        an augmented argparse.Namespace returned by com.init().

  _mpw_ Multiprocessing worker.  Runs in a child process.  All
        arguments must be pickleable, and exceptions may not escape
        the function.  Our convention is that these MUST return False
        on failure, but otherwise can return anything else---even
        False-ish stuff like [] or ''.

  com.  From the scidbctl_common.py module, these routines are also
        accessible from plugins.

  com.mpw_   A multiprocessing worker also needed by a plugin, so it
             lives in the scidbctl_common.py module for sharing.

CHANGE NOTES
============

This is a complete rewrite of the scidb.py script.  Here's a (possibly
incomplete) list of behaviors known to have changed.

  - Nearly all config.ini options are passed to the SciDB instances as
    long options.  The scidbctl.py script is not in the business of
    deciding what config parameters the instances should or should not
    see.  This simplifies adding new parameters.

  - Execute remote commands in parallel with the standard Python
    multiprocessing module and scidblib.SshRunner, instead of
    low-level paramiko code.

  - New plugin architecture does not require users to remember to use
    some ugly -m option.

  - The 'chunk-reserve' config entry is no longer supported.  It was
    intended for use by chunk "delta encoding", but that project got
    scrapped.

  - Underscores should no longer be used in any config.ini option
    name.  All multi-word options are now hyphen separated.  For
    example, 'db-user' replaces 'db_user'.

"""

import argparse
import datetime
import errno
import multiprocessing as mp
import os
import pprint
import pwd
import re
import subprocess as subp
import sys
import time
import traceback

from scidblib import AppError
import scidblib.scidbctl_common as com
from scidblib.pgpass_updater import (PgpassUpdater, PgpassError, I_PASS)
from scidblib.psql_client import (Psql, pg_path)
from scidblib.util import (EnvDefault, getVerifiedPassword, is_local_host,
                           ScopedLock)
from scidblib.scidb_psf import confirm

# Shorthand aliases
dbg = com.dbg
prt = com.prt
info = com.info
warn = com.warn
error = com.error

_ctx = None                        # Parsed arguments, plus config.ini, plus...
_plugins = []                      # Loaded plugin modules


def _is_plugin(filename):
    """Return true iff filename appears to be one of our plugins."""
    return filename.endswith("_plugin.py") and not filename.startswith("_")


def _decode_error(err):
    """Parse short and long error codes out of SciDB error text."""
    m = re.search(r'\b(SCIDB_SE_[^:]*)::(SCIDB_LE_[A-Z0-9_]*)', err, re.M)
    assert m, "No SciDB error found in text string:\n%s" % err
    return m.group(1), m.group(2)


def load_plugins(subparsers):
    """Load up the plugins and let them add subparsers."""
    # The args are not yet parsed, so you need SCIDB_DBG=1 to use
    # dbg() here: we haven't seen --debug yet!
    plugin_dir = os.path.join(com.install_path(), 'bin')
    dbg("Loading plugins from", plugin_dir)
    try:
        plugins = [os.path.basename(x)[:-3] for x in os.listdir(plugin_dir)
                   if _is_plugin(x)]
    except OSError as e:
        if e.errno == errno.ENOENT:
            dbg("No plugin directory, nothing to load")
            return
        raise AppError(str(e))
    plugins.sort()

    global _plugins
    mod = None
    for plugin in plugins:
        try:
            mod = __import__(plugin)
            mod.plugin_init(subparsers)
        except Exception as e:
            warn("Cannot import %s.py: %s" % (plugin, e))
            continue
        _plugins.append(mod)


def do_status(ctx):
    """Print pids, hostnames, etc. of running SciDB cluster."""
    pi = com.get_process_info(ctx.server_id)
    if not pi:
        info("SciDB is not running")
        return 1
    if not ctx.no_headers:
        prt("PID\tPPID\tCMD\t\tSERVER")
    mine_running = False
    for x in com.preen_watchdogs(pi):
        prt('\t'.join(x[:-1]))  # skip sid
        if x[2].endswith("-%s" % ctx.cluster):
            mine_running = True
    if not mine_running:
        warn("Cluster", ctx.cluster, "not running")

    return 0


def do_stop(ctx):
    """Stop the SciDB cluster."""
    global _ctx
    assert id(ctx) == id(_ctx), "do_stop: divergent contexts!"

    if not _ctx.scidb_servers:
        raise AppError("Server list is empty?!")
    if com.build_info().build_type.lower() == 'valgrind':
        # Need to stop each pid, killall(1) won't work.
        procs = com.get_process_info()
        if not procs:
            info("Cluster", _ctx.cluster, "is not running")
            return 0
        victims = dict()
        for pi in procs:
            # server_name -> pid_list
            victims.setdefault(pi[3], []).append(pi[0])
        per_server_results = com.run_pool(com.mpw_stop_pids, victims.items())
    else:
        servers = [x.name for x in _ctx.scidb_servers]
        per_server_results = com.run_pool(_mpw_stop, servers)
    if False in per_server_results:
        warn("Could not connect to one or more servers")
        return 1
    attempt = 0
    max_attempts = 10
    while True:
        procs = com.get_process_info()
        if not procs:
            info("Cluster", _ctx.cluster, "stopped")
            return 0
        elif attempt == 0:
            info("Waiting up to", max_attempts, "seconds for cluster",
                 _ctx.cluster, "to stop...")
        time.sleep(1)
        attempt += 1
        if attempt > max_attempts:
            if _ctx.force:
                error("Cluster", _ctx.cluster, "still running after SIGKILL?!")
            else:
                error("Cluster", _ctx.cluster, "still running, try --force")
            break
    return 1


def _mpw_stop(server):
    """Stop all SciDB processes on server.

    It's possible that different "servers" are really the same host,
    for example, when localhost and 127.0.0.1 are used to simulate a
    two-server cluster.  We don't bother to detect this situation (and
    that's why the SSH script uses /bin/true).
    """
    mp.current_process().name = server
    info("Stopping local", _ctx.cluster, "instances")
    try:
        # Kill them!  Kill them ALL!!!
        sig = "-s 9" if _ctx.force else ""
        ssh = com.make_ssh_runner(host=server, port=_ctx.ssh_port)
        script = """killall -r {0} 'SciDB-[0-9]+-[0-9]+-{1}' 2>&1 ;
                    /bin/true""".format(sig, _ctx.cluster)
        out, err = ssh.run(script)
        if ssh.failed():
            warn("No SSH access to {0}: {1}".format(server, err))
            return False
        if out:
            info(out.strip())   # Typically "...: no process found"
        return True

    except Exception as e:
        with ScopedLock(com.iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


def do_version(ctx):
    """Print SciDB version for each server in the cluster."""
    global _ctx
    assert id(ctx) == id(_ctx), "do_version: divergent contexts!"

    if not _ctx.scidb_instances:
        raise AppError("Instance list is empty?!")
    per_server_results = com.run_pool(_mpw_versions, _ctx.scidb_instances)
    result = [x for x in per_server_results if isinstance(x, list)]
    result.sort()
    for x in result:
        prt('\t'.join(x))
    fails = 0
    if False in per_server_results:
        warn("Could not connect to one or more servers")
        fails += 1
    shas = set([x[3] for x in result])  # commit SHAs
    if len(shas) > 1:
        error("Cluster versions are inconsistent!  Commit SHAs:", shas)
        fails += 1
    return fails


def _mpw_versions(ii):
    """Return this instance's version info."""
    mp.current_process().name = "scidbctl-{0}-{1}-{2}".format(
        ii.sid, ii.siid, _ctx.cluster)
    try:
        ssh = com.make_ssh_runner(host=ii.server_name, port=_ctx.ssh_port)
        datadir = com.data_dir(ii)
        linkname = "SciDB-%d-%d-%s" % (ii.sid, ii.siid, _ctx.cluster)
        out, err = ssh.run("%s --version" % os.path.join(datadir, linkname))
        if ssh.failed():
            error("Cannot get version for %s: %s" % (linkname, err))
            return False
        out = [x for x in out.splitlines() if ':' in x]  # "k: v" lines only
        line = [linkname]
        line.extend([re.sub(r'^.*: ', '', x) for x in out])
        line.append(ii.server_name)
        return line

    except Exception as e:
        with ScopedLock(com.iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


def do_start(ctx):
    """Start the SciDB cluster."""
    global _ctx
    assert id(ctx) == id(_ctx), "do_start: divergent contexts"

    if com.get_process_info():
        raise AppError(
            "SciDB still running, use the 'stop' or 'status' subcommand.")
    if not _ctx.scidb_instances:
        raise AppError("Instance list is empty?!")

    # Assorted checks that individual instances can't do themselves.
    com.validate_config(_ctx.config, cluster=_ctx.cluster)

    if com.build_info().build_type.lower() == 'valgrind':
        info("Starting SciDB cluster", _ctx.cluster, "with valgrind...")
    else:
        info("Starting SciDB cluster", _ctx.cluster, "...")

    results = com.run_pool(com.mpw_start_instance, _ctx.scidb_instances)
    if False in results:
        warn("One or more instances failed to start")
        return 1

    if _ctx.max_attempts < 1 or _ctx.max_attempts > 50:
        warn("Bad -M/--max-atempts option", _ctx.max_attempts, "(ignored)")
        _ctx.max_attempts = 30
    info("Started", len(results), "instances, waiting up to",
         _ctx.max_attempts, "seconds for cluster sync")
    qc = _ctx.scidb_instances[0]
    iquery = com.make_iquery(afl=True, host=qc.server_name, port=qc.port,
                             auth_file=_ctx.auth_file)
    for _ in xrange(_ctx.max_attempts):
        time.sleep(1)
        _, err = iquery("list('queries')")
        if not iquery.returncode:
            info("Cluster is ready")
            return 0
        serr, lerr = _decode_error(err)
        if serr == 'SCIDB_SE_AUTHENTICATION':
            # Retrying after auth errors is futile.
            raise AppError(
                "Authentication failed for iquery, SciDB was started but "
                "might not be ready!\n%s" % err)
        if _ctx.debug > 1:    # Usually a connection error, very boring.
            dbg("SciDB launched but not ready:\n%s" % err)

    # What, still here?
    error("Cluster still not ready after", _ctx.max_attempts,
          "queries.")
    error("Last liveness query result:\n%s" % err)
    com.report_start_failure(_ctx.scidb_instances[0])
    error("Failed to start SciDB!")
    return 1


def _get_db_password(ctx, port, dbname, dbuser):
    """Get hold of the Postgres password, by hook or by crook.

    See http://www.postgresql.org/docs/9.3/interactive/libpq-pgpass.html
    """
    dbg("_get_db_password:", [port, dbname, dbuser])
    pup = None
    host = ctx.config['pg-host']
    try:
        pup = PgpassUpdater(ctx.config['pgpassfile'])
    except PgpassError as e:
        warn("PgpassUpdater:", e)
    else:
        entry = pup.find(dbuser, dbname, host, port)
        if entry:
            return entry[I_PASS]
        warn("Cannot find match for dbuser:", dbuser, "dbname:", dbname,
             "host:", host, "port:", port)
    # OK, I guess we'll have to ask.
    dbpass = getVerifiedPassword(
        "Postgres user: {0}\nPostgres password [{0}]: ".format(dbuser))
    if not dbpass:
        dbpass = dbuser
    # Offer to store it...
    if pup and confirm("Update %s?" % pup.filename(), resp=True):
        pup.update(dbuser, dbpass, dbname, host, port)
        pup.write_file()
    return dbpass


def do_init_syscat(ctx):
    """Initialize the SciDB cluster's system catalog."""

    if not is_local_host(ctx.config['pg-host']):
        raise AppError("This command can only be run on the system catalog"
                       " host machine (%s)" % ctx.config['pg-host'])

    if ctx.force:
        info("Skipping the 'Is cluster stopped?' check")
    else:
        # When run as user 'postgres', often that user does not have SSH
        # permission to the other cluster servers, so we can't gather
        # remote process info.  Try to gather local process info.
        local_servers = [x for x in ctx.scidb_servers if is_local_host(x.name)]
        if local_servers:
            if com.get_process_info(local_servers):
                raise AppError("SciDB still running, use the 'stop' or"
                               " 'status' subcommand.")
        else:
            # No local SciDB servers.  Best we can do is ask.
            info("All cluster '%s' servers are remote:" % ctx.cluster,
                 com.wrap(x.name for x in ctx.scidb_servers))
            if not confirm("Are you sure that cluster '%s' is stopped?" % (
                    ctx.cluster), resp=False):
                info("Catalog initialization aborted.")
                return 1

    # Collect some stuff.
    db = ctx.pgdb if ctx.pgdb else ctx.cluster
    port = ctx.config['pg-port']
    dbuser = ctx.config['db-user']  # role used by cluster queries
    try:
        pguser = ctx.pguser if ctx.pguser else ctx.config['pg-user']
    except Exception:
        pguser = 'postgres'
    if ctx.db_password:
        passwd = ctx.db_password
    else:
        passwd = _get_db_password(ctx, port, db, pguser)
    try:
        pgbin = os.path.join(pg_path(), 'bin')
        assert os.path.isdir(pgbin), "No bin subdirectory in %s" % pg_path()
        dbg("Postgres binaries in", pgbin)
    except Exception as e:
        warn("While trying to locate Postgres binaries:", e)
        pgbin = "/usr/bin"

    # Permission checks.
    try:
        pg_uid = pwd.getpwnam(pguser).pw_uid
    except KeyError:
        raise AppError("Postgres user (%s) does not exist" % pguser)
    if os.geteuid() != pg_uid:
        raise AppError("You must run this command as owner of PostgreSQL!")

    # Wipe database if it already exists.
    psql = Psql(user=pguser, port=port, debug=ctx.debug,
                prog=os.path.join(pgbin, 'psql'))
    exists = psql(
        "select 1 from pg_catalog.pg_database where datname = '%s'" % db)
    if exists:
        info("Deleting %s..." % db)
        psql("drop database %s" % db)

    # Create database user if not already present.
    exists = psql(
        "select 1 from pg_catalog.pg_user where usename = '%s'" % dbuser)
    if not exists:
        info("Adding user %s..." % dbuser)
        psql("create role %s with login password '%s'" % (
            dbuser, passwd))

    # Create the database within the Postgres cluster.
    cmd = "{0}/createdb -p {1} --owner {2} {3}".format(pgbin, port, dbuser, db)
    p = subp.Popen(cmd.split(), stdout=subp.PIPE, stderr=subp.PIPE)
    _, err = p.communicate()
    if p.returncode:
        raise AppError("Cannot create database %s: %s" % (db, err))

    # Add support for database triggers.
    exists = psql("select 1 from pg_language where lanname = 'plpgsql'")
    if not exists:
        info("Installing language plpgsql for database %s" % db)
        cmd = "{0}/createlang -p {1} plpgsql {2}".format(pgbin, port, db)
        p = subp.Popen(cmd.split(), stdout=subp.PIPE, stderr=subp.PIPE)
        _, err = p.communicate()
        if p.returncode:
            raise AppError(
                "Cannot add plpgsql language to database '%s': %s" % (db, err))
    psql.database = db
    psql("update pg_language set lanpltrusted = true where lanname = 'c'")
    psql("grant usage on language c to %s" % dbuser)

    info("System catalog initialized.")
    return 0


def do_init_cluster(ctx):
    """Initialize instances and their data directories."""

    if not ctx.scidb_servers:
        raise AppError("No servers configured")
    if com.get_process_info():
        raise AppError(
            "SciDB still running, use the 'stop' or 'status' subcommand.")

    # Assorted checks that individual instances can't do themselves.
    com.validate_config(ctx.config, cluster=ctx.cluster, force=ctx.force)

    # Abandon hope, all ye who enter 'y' here....
    if not ctx.force:
        if not confirm(
                "WARNING: This will delete ALL data and logs!  Proceed?",
                resp=False):
            raise AppError("Re-initialization aborted")

    # First instance also initializes the catalog.  Other instances
    # must wait for that, so just do it directly in the main process.
    qc = ctx.scidb_instances[0]
    if not com.mpw_init_datadir(qc):
        warn("Cannot initialize data directory for s%d-i%d" % (
            qc.sid, qc.siid))
        return 1
    if not com.mpw_init_instance(qc, initialize_catalog=True):
        warn("Cluster initialization failed")
        com.report_init_failure(qc)
        return 1

    if len(ctx.scidb_instances) > 1:
        # Other instances initialize in parallel.  Prepare data
        # directories...
        results = com.run_pool(com.mpw_init_datadir, ctx.scidb_instances[1:])
        if False in results:
            error("Cannot initialize one or more data directories")
            return 1

        # Register the new instances.
        results = com.run_pool(com.mpw_init_instance, ctx.scidb_instances[1:])
        if False in results:
            error("One or more instances failed to initialize")
            # We could make a report for -all- the init failures, but
            # for large clusters that would be messy.  Probably they all
            # failed the same way the first one did.
            first_fail = results.index(False)
            ii = ctx.scidb_instances[first_fail + 1]
            com.report_init_failure(ii)
            return 1

    info("Cluster", ctx.cluster, "instances initialized.")
    return 0


def do_collect_diags(ctx):
    """Collect post-mortem debugging information."""

    # Timestamp we'll use to generate various file and directory names
    # associated with this collection.
    ts = datetime.datetime.now().isoformat().split('.')[0]
    ts = re.sub(':', '', ts)    # Various utilities hate colons.
    info("Collecting diagnostics at", ts)

    # Build argument tuples.  Only the first instance on each server
    # is tasked with collecting system-wide information.
    alist = []
    last_sid = -1
    for ii in ctx.scidb_instances:
        assert ii.sid >= last_sid, "ctx.scidb_instances not sorted?!"
        if ii.sid != last_sid:
            alist.append((ii, ts, True))
            last_sid = ii.sid
        else:
            alist.append((ii, ts, False))

    # Fly, my pretties!
    results = com.run_pool(_mpw_produce_diags, alist)
    if False in results:
        warn("One or more instances failed to produce diagnostics")

    # Normally scidbctl.py is being run on one of the cluster servers,
    # and gathers diagnostics to a local instance's data directory.
    # But if there isn't one, we'll make our own directory and put
    # stuff there.
    target = ''
    lazy_ii = None
    for ii in ctx.scidb_instances:
        if not target and is_local_host(ii.server_name):
            target = os.path.join(com.data_dir(ii), 'diags', ts)
            lazy_ii = ii
            break
    if not target:
        # No local instance data directories.  Let's pretend.
        try:
            com.run_cmd("mkdir -p ./diags/%s" % ts)
        except Exception as e:
            error("Cannot make local diags/%s directory: %s" % (ts, e))
            error("Nowhere to put collected diagnostics, bailing out!")
            raise
        target = os.path.abspath('./diags/%s' % ts)

    # Build (ii, ts, target) arguments *only* for those instances
    # that have to do some work.
    alist = [(ii, ts, target) for ii in ctx.scidb_instances
             if ii != lazy_ii]

    # The harvest!  Moi ha ha hahahahaha!!!!
    info("Gathering collected diagnostics")
    results = com.run_pool(_mpw_gather_diags, alist)
    if False in results:
        warn("One or more instances failed to gather diagnostics")
    dbg("All diagnostics gathered in %s" % target)

    # And finally, roll everything up into one honking great tarball.
    # No compression, since contents are already compressed.
    parent = os.path.split(target)[0]
    tarball = os.path.join(parent, 'all-%s.tar' % ts)
    assert parent.endswith('/diags'), "I really should have a diags parent!"
    try:
        com.run_cmd(['tar', 'cf', tarball, '-C', parent, ts])
    except AppError as e:
        warn("While rolling up %s: %s" % (tarball, e))
    else:
        info("Diagnostics in", tarball)
        try:
            com.run_cmd(['rm', '-rf', target])
        except AppError as e:
            warn("Cannot remove '%s': %s" % (target, e))

    return 0


def _mpw_produce_diags(proc_arg):
    """Produce diagnostics locally for one instance."""
    ii, ts, want_sys_info = proc_arg
    mp.current_process().name = "scidbctl-{0}-{1}-{2}".format(
        ii.sid, ii.siid, _ctx.cluster)
    info("Producing diagnostics...")
    try:
        # Preliminaries.
        ssh = com.make_ssh_runner(host=ii.server_name, port=_ctx.ssh_port)
        datadir = com.data_dir(ii)
        diagdir = os.path.join(datadir, 'diags', ts)
        instpath = com.install_path()
        bindir = os.path.join(instpath, 'bin')
        cores_prog = os.path.join(bindir, 'scidb_cores')
        sysrpt_prog = os.path.join(bindir, 'system_report.py')

        dbg("Writing diagnostics to", diagdir)

        # Stuff we want in the per-instance tarball.
        wanted = ['*.log*', 'mpi_*/*', 'stack*']
        if not _ctx.light:
            wanted.append('core*')    # Cores too!

        # Make datadir subdirectory where we'll gather the stuff, and
        # cd into it.
        _, err = ssh.run("mkdir -p %s" % diagdir)
        if ssh.failed():
            raise AppError("Cannot create diagnostics directory '%s': %s" % (
                diagdir, err))
        _, err = ssh.cd(diagdir)
        if ssh.failed():
            raise AppError("Cannot cd to %s: %s" % (diagdir, err))

        # If I'm an instance tasked with collecting server-wide
        # information, do that now.
        if want_sys_info:

            # Run the system report to get routing table, ARP cache, etc. etc.
            _, err = ssh.run([sysrpt_prog, '-t', ts,
                              "system-%d-%s.rpt" % (ii.sid, ts)])
            if ssh.failed():
                warn("Cannot run %s: %s" % (sysrpt_prog, err))

            # Heavy or light, it's good to have the install-path md5
            # checksums.
            _, err = ssh.run("""
                (cd {0}; find . -type f | sort | xargs md5sum) \
                    > install-{1}-{2}.md5
                """.format(instpath, ii.sid, ts))
            if ssh.failed():
                warn("Cannot run md5sum in %s: %s" % (instpath, err))

            if ('SCIDB_HEAVY_DIAGS' in os.environ
                    and ii == _ctx.scidb_instances[0]):
                # Tar up the whole friggin' install directory (only if
                # undocumented envariable is set: this is way overkill).
                info("Creating install-path heavy tarball ...")
                _, err = ssh.run(
                    "tar zcf {0} --ignore-failed-read -C {1} .".format(
                        "install-%d-%s-full.tgz" % (ii.sid, ts), instpath))
                if ssh.failed():
                    warn("Cannot create full install-path tarball: %s" % err)
                dbg("Done creating heavy tarball")
            else:
                # Lightweight sampling of install directory's contents.
                dbg("Creating install-path ls-lR, tarball ...")
                _, err = ssh.run(
                    "(cd {0}; pwd; ls -lR .) > install-{1}-{2}.ls-lR".format(
                        instpath, ii.sid, ts))
                if ssh.failed():
                    warn("Cannot run 'ls -lR' in %s: %s" % (instpath, err))
                _, err = ssh.run(
                    "tar zcf {0} --ignore-failed-read -C {1} etc share".format(
                        "install-%d-%s.tgz" % (ii.sid, ts), instpath))
                if ssh.failed():
                    warn("Cannot create install-path tarball: %s" % err)

            # If the cluster is running, get backtraces of running
            # instances' threads.  We're already in a subprocess, so just
            # call com.mpw_status() directly.
            si = [x for x in _ctx.scidb_servers if x.sid == ii.sid][0]
            out, _ = ssh.run("which gstack")
            if not out:
                warn(com.wrap(
                    "No gstack command on server %s, cannot get stack traces "
                    "of running SciDB instances.  Install gdb if you need "
                    "this information." % si.name,))
            else:
                with com.PreserveProcName():
                    proc_info = com.preen_watchdogs(com.mpw_status(si))
                for pi in proc_info:
                    info("Tracing stack for running SciDB pid", pi[0], "...")
                    out, err = ssh.run(
                        "gstack {0} > running-{1}.stack".format(pi[0], pi[2]))
                    if ssh.failed():
                        warn("Cannot run 'gstack %s': %s" % (pi[0], err))

        # Back to producing ordinary per-instance stuff.
        # Create the stack traces for any core files.
        dbg("Generating stack traces from core files ...")
        _, err = ssh.run(' '.join((
            cores_prog, '--data-dir', datadir, '--output-dir', datadir, ts)))
        if ssh.failed():
            warn("Cannot generate stack traces: %s" % err)

        # Cd back to the datadir, or else the 'wanted' patterns won't
        # glob cleanly.
        _, err = ssh.cd(datadir)
        if ssh.failed():
            error("Cannot cd to '%s': %s" % (datadir, err))
            return False        # we were almost done anyway

        # Tar 'em up.  Using --ignore-failed-read means we don't have
        # to care if one of the glob patterns doesn't expand.
        dbg("Creating tarball for %s ..." % wanted)
        tarball = os.path.join(diagdir, "srv-%d-%d-%s.tgz" % (
            ii.sid, ii.siid, ts))
        _, err = ssh.run("tar zcvf {0} --ignore-failed-read {1}".format(
            tarball, ' '.join(wanted)))
        if ssh.failed():
            warn("Cannot create archive '%s': %s" % (tarball, err))
        elif err:
            if _ctx.debug:
                # Complete tar stderr...
                warn("Tar complained ('No such file...' is OK):\n", err)
            else:
                # We expect some "No such file" errors if, for example,
                # there were no core dumps.
                err = [x for x in err.splitlines() if 'No such file' not in x]
                if err:
                    warn("Tar complained:\n", err)

        # Hurray, <datadir>/diags/<ts> populated!
        info("Diagnostics generated in <datadir>/diags/%s" % ts)
        return True

    except Exception as e:
        with ScopedLock(com.iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


def _mpw_gather_diags(proc_arg):
    """Pull per-instance diagnostics to local target directory."""
    ii, ts, target = proc_arg
    mp.current_process().name = "scidbctl-{0}-{1}-{2}".format(
        ii.sid, ii.siid, _ctx.cluster)
    try:
        # Preliminaries.
        datadir = com.data_dir(ii)
        diagdir = os.path.join(datadir, 'diags', ts)

        if is_local_host(ii.server_name):
            # We can just move stuff.
            fails = 0
            for f in os.listdir(diagdir):
                fullpath = os.path.join(diagdir, f)
                try:
                    com.run_cmd(['mv', fullpath, target])
                except AppError as e:
                    error(e)
                    fails += 1
            if not fails:
                dbg("Moved diagnostics:\nFrom:", diagdir,
                    "\n  To:", target)
                try:
                    com.run_cmd(['rm', '-rf', diagdir])
                except AppError as e:
                    warn("Cannot remove '%s': %s" % (diagdir, e))
                return True
            else:
                return False

        # Remote, we must use scp(1).
        ssh = com.make_ssh_runner(host=ii.server_name, port=_ctx.ssh_port)
        parent = os.path.split(target)[0]
        _, err = ssh.get_dir(diagdir, parent)
        if ssh.failed():
            error("Cannot 'scp -r %s %s': %s" % (diagdir, target, err))
            return False

        # Clean up remote diag directory.
        _, err = ssh.run("rm -rf %s" % diagdir)
        if ssh.failed():
            warn("Cannot remove '%s': %s" % (diagdir, err))

        return True

    except Exception as e:
        with ScopedLock(com.iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


def do_show_config(ctx):
    """Dump parsed arguments, parsed config.ini."""
    pp = pprint.PrettyPrinter(indent=2)
    pp.pprint(ctx.__dict__)
    return 0


def main(argv=None):
    """Argument parsing, plugin load, and last-ditch exception handling.

    Oh yeah, and invoke the subcommand too!
    """
    if argv is None:
        argv = sys.argv

    # Set main program name for logging.
    com.init(pgm=os.path.splitext(os.path.basename(argv[0]))[0])

    if 'P4_TESTCASES_DIR' in os.environ:
        com.init(stderr=sys.stdout)
        dbg("Running under the test harness.")

    # Set up the main parser (but don't parse any arguments yet:
    # plugins must first be given a chance to add their own
    # subparsers).
    parser = argparse.ArgumentParser(
        description="SciDB cluster management",
        epilog="""Type "{0} <subcommand> -h" for <subcommand> help.""".format(
            os.path.basename(argv[0])))

    # General options, applicable to all subcommands.
    parser.add_argument('-A', '--auth-file', help="""
        Authentication file for SciDB user""")
    parser.add_argument('-c', '--config', dest='config_file', help="""Cluster
        config.ini file, default is /opt/scidb/<version>/etc/config.ini""")
    parser.add_argument('--ssh-port', type=int, help="""
        TCP port for contacting remote SSH daemons""")
    parser.add_argument('-d', '--debug', action='count', help="""
        Increase logging, -d: debug, -dd: extra debug""")
    subparsers = parser.add_subparsers(dest='verb', title='subcommands')

    # Subcommands implemented in -this- script.  Please keep them in
    # alphabetical order.
    p = subparsers.add_parser('collect-diags', help="""Collect diagnostic
        information from all instances.""")  # fka 'dbginfo'
    p.add_argument('-l', '--light', action='store_true', help="""Skip large
        objects such as binaries and core files""")
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_collect_diags)

    p = subparsers.add_parser('show-config', help="""Display parsed arguments
        and config.ini""")
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_show_config)

    p = subparsers.add_parser('init-syscat', help="""Initialize the system
        catalog on the local machine.""")
    p.add_argument('-f', '--force', action='store_true',
                   help="Don't check whether cluster has been stopped")
    p.add_argument('-p', '--db-password', help="""Postgres password.  Option
        used only by certain internal scripts, because PUTTING PASSWORDS ON
        THE COMMAND LINE IS A VERY BAD IDEA INDEED.""")
    p.add_argument('-U', '--dbuser', help="""
        Role to use for catalog queries, default is cluster name""")
    p.add_argument('-u', '--pguser', default='postgres', help="""
        Postgres administrative/OS user name, default is 'postgres'""")
    p.add_argument('-d', '--pgdb', help="""
        Postgres database name, default is cluster name""")
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_init_syscat)

    # Formerly known as "init-all".
    p = subparsers.add_parser('init-cluster', help="""Register SciDB
        instances in the catalog and initialize their data directories""")
    p.add_argument('-f', '--force', action='store_true',
                   help="Don't ask before wiping data.  DANGEROUS!!!")
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_init_cluster)

    p = subparsers.add_parser('start', help='Start SciDB cluster')
    p.add_argument('-M', '--max-attempts', type=int,
                   default=30, help="""Retry count for liveness queries after
                   cluster start""")
    p.add_argument('--valgrind', help="Use valgrind(1).", action='store_true')
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_start)

    p = subparsers.add_parser('status', help="""Display process ids of
        a running SciDB cluster""")
    p.add_argument('--no-headers', action='store_true',
                   help="Suppress output column headers")
    p.add_argument('-s', '--server-id', type=int, help="""Limit output to
        specified server only.""")
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_status)

    p = subparsers.add_parser('stop', help='Stop SciDB cluster')
    p.add_argument('-f', '--force', action='store_true',
                   help="Forced shutdown (kill -9)")
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_stop)

    p = subparsers.add_parser('version', help="""Print version info for
        each configured instance.""")
    p.add_argument('cluster', help='Name of SciDB cluster',
                   nargs='?', action=EnvDefault, envvar='SCIDB_NAME')
    p.set_defaults(func=do_version)

    try:
        # Add new subcommands from any found plugins.
        load_plugins(subparsers)

        global _ctx
        _ctx = parser.parse_args(argv[1:])
        com.init(context=_ctx)    # Digest config.ini and add to _ctx
        assert id(_ctx) == id(com._ctx), "main: divergent contexts!"

        # It's time that you do that voodoo that you do so well!
        return _ctx.func(_ctx)

    except AppError as e:
        error(e)
        if _ctx.debug:
            traceback.print_exc(file=com.stderr())
        return 2
    except KeyboardInterrupt:
        warn("Interrupt", nolock=True)
        com.terminate_pool()
        return 2
    except Exception as e:
        error("Unhandled exception:", e)
        traceback.print_exc(file=com.stderr())
        return 3


if __name__ == '__main__':
    sys.exit(main())

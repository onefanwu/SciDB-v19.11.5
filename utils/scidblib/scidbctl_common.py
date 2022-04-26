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

"""
Utility routines common to scidbctl.py and its plugins.
"""

import getpass
import inspect
import multiprocessing as mp
import os
import pprint
import pwd
import re
import signal
import subprocess as subp
import sys
import textwrap
import traceback

from ConfigParser import RawConfigParser
from collections import (namedtuple, OrderedDict)
from contextlib import contextmanager
from scidblib import AppError
from scidblib.iquery_client import IQuery
from scidblib.psql_client import Psql
from scidblib.ssh_runner import SshRunner
from scidblib.util import (py_cast, ScopedLock, is_local_host)

_ctx = None                        # Parsed arguments, plus config.ini, plus...
_pgm = '(prog)'                    # Short program name
_iolock = mp.Lock()                # Avoid intermingled subprocess output
_stderr = sys.stderr               # Test harness?  Have to use stdout, grrr
_main_proc = mp.current_process()  # This process right here
_pool = None                       # Current worker pool, see run_pool() below

LOG_LINE_SEP = "\n   ....:   "  # Used for continuation of long log messages

# Types for parsed server and instance information, from config.ini.
#
# "sid" = server id, the high 32 bits of a physical instance id
# "siid" = server instance id, unique per server
#
ServInfo = namedtuple("ServInfo", "sid, name")
InstInfo = namedtuple("InstInfo", "sid, siid, server_name, port")

# These are the minimal options that MUST appear in a valid config.ini
# cluster configuration section.
#
REQUIRED_CONFIG_INI = (
    "base-path",
    "base-port",
    "db-user",                  # Internal Postgres role name for cluster
    "install-root",             # Eventually, 'install-path'?
    "logconf",
    "pluginsdir",
)

# Boost option parsing treats boolean options specially, so we must
# too.  See _fix_bool_option() below.
#
BOOL_OPTIONS = (
    'no-watchdog',
    'daemon-mode',
    'version',
    'output-proc-stats',
    'array-emptyable-by-default',
    'enable-catalog-upgrade',
    'preallocate-shared-mem',
    'input-double-buffering',
    'enable-chunkmap-recovery',
    'skip-chunkmap-integrity-check',
    'window-old-or-new',
    'resource-monitoring',
    'pooled-allocation',
    # 'perf-wait-timing',   # It can be Config::BOOLEAN again!
    'filter-use-rtree',
)

# Entries with these prefixes are important for the operation of this
# script, but individual instances don't need them.  No need to pass
# them to the instance at launch time.
#
IGNORED_CONFIG_PREFIXES = (
    "vg-",
    "server-",
    "data-dir-prefix",
)

IGNORE_REGEX = '|'.join(IGNORED_CONFIG_PREFIXES)


def iolock():
    """Sometimes launcher and plugins need direct access to the I/O lock."""
    return _iolock


def stderr():
    """Return file object we're currently using for stderr."""
    return _stderr


def install_path(args=None):
    """Return the SciDB installation root directory.

    We may need this *very* early on, so we can't necessarily rely on
    having a parsed config.ini yet.
    """
    def f(args):
        # Try config.ini
        try:
            # Not install-path, *sigh*
            return 'cfg', args.config['install-root']
        except Exception:
            pass
        # Try environment variable
        try:
            return 'env', os.environ['SCIDB_INSTALL_PATH']
        except Exception:
            pass
        # Try presuming the running script is in the bin directory
        result = os.path.abspath(inspect.getfile(inspect.currentframe()))
        tail = True
        while tail:
            result, tail = os.path.split(result)
            if tail == 'bin':
                return 'scriptpath', result       # Found it!
        # Try hoping that /opt/scidb has only one entry, a subdirectory
        subdirs = os.listdir('/opt/scidb')
        if len(subdirs) == 1:
            result = os.path.join('/opt/scidb', subdirs[0])
            if os.path.isdir(result):
                return '/opt', result
        # I'm out of ideas.
        raise AppError("Cannot determine install-root aka SCIDB_INSTALL_PATH")
    via, result = f(args)
    dbg("Computed install_path via %s: %s" % (via, result))
    return result


class Bunch(object):
    """See _Python Cookbook_ 2d ed., "Collecting a Bunch of Named Items"."""
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


@contextmanager
def PreserveProcName():
    """Restore process name when calling mpw code from other mpw code."""
    saved_name = mp.current_process().name
    yield
    mp.current_process().name = saved_name


def _debugging(_result=[]):
    """Return true if we are debug logging.

    This function is used by dbg(), so that you can do debug logging
    even before arguments are parsed (i.e. before _ctx.debug is set).
    """
    if _result:
        return _result[0]       # The cached result from prior calls.

    def scidb_dbg():
        try:
            return int(os.environ.get("SCIDB_DBG", "0"))
        except ValueError:
            raise AppError("SCIDB_DBG={0} but must be int".format(
                os.environ['SCIDB_DBG']))

    if _ctx is None:
        # No args yet, return our best guess but don't cache it.
        return scidb_dbg()
    if not _ctx.debug:
        _ctx.debug = scidb_dbg()
    _result.append(_ctx.debug)
    return _result[0]


def _log(*args, **kwargs):
    """Internal routine for status logging."""
    try:
        stream = kwargs['file']
    except KeyError:
        stream = sys.stdout
    if mp.current_process() is _main_proc:
        prefix = '[%s]' % _pgm
    else:
        prefix = '[%s]' % mp.current_process().name
    try:
        lock = None if kwargs['nolock'] else _iolock
    except KeyError:
        lock = _iolock
    with ScopedLock(lock):
        print >>stream, prefix, ' '.join(str(x) for x in args)
        stream.flush()


def dbg(*args, **kwargs):
    """If debug, log to stderr with locking."""
    if _debugging():
        kwargs['file'] = _stderr
        a = ["[D]"]
        a.extend(args)
        _log(*a, **kwargs)


def warn(*args, **kwargs):
    """Log to stderr with locking."""
    kwargs['file'] = _stderr
    a = ["[W]"]
    a.extend(args)
    _log(*a, **kwargs)


def error(*args, **kwargs):
    """Log to stderr with locking."""
    kwargs['file'] = _stderr
    a = ["[E]"]
    a.extend(args)
    _log(*a, **kwargs)


def info(*args, **kwargs):
    """Log to stdout with locking."""
    _log(*args, **kwargs)


def prt(*args, **kwargs):
    """Print to stdout with locking.

    No prefixing, just "print" plus the I/O lock.
    """
    with ScopedLock(_iolock):
        print ' '.join(str(x) for x in args)
        sys.stdout.flush()


class _LoggingSshRunner(SshRunner):
    """Sometimes you want to see the command being sent."""
    def run(self, *args, **kwargs):
        dbg("SSH(%s):" % self.host, ' '.join(str(x) for x in args))
        return SshRunner.run(self, *args, **kwargs)


def wrap(x, linesep=LOG_LINE_SEP):
    """Wrap and fill long lists of things to be logged"""
    if not isinstance(x, basestring):
        x = ', '.join(str(y) for y in x)
    lines = textwrap.wrap(x, width=(72 - len(linesep)))
    return linesep.join(lines)


def run_cmd(cmd):
    """Run a local command, translating any error to AppError.

    Returns whatever was printed on stdout.  Throws if there's a
    problem with subprocess fork/exec.  Be aware: no shell globbing
    here, "ls /tmp/*" won't work!
    """
    if isinstance(cmd, basestring):
        cmd = cmd.split()
    p = subp.Popen(cmd, stdout=subp.PIPE, stderr=subp.PIPE)
    out, err = p.communicate()
    if p.returncode:
        raise AppError("Cannot run '%s': %s (%d)" % (
            ' '.join(cmd), err, p.returncode))
    return out


def run_pool(mpw_func, iterable):
    """Run a multiprocessing.Pool on the iterable in a SIGINT-safe way.

    XXX This still needs some work.  Maybe the main scidbctl.py
    process has to become a process group leader.

    See https://stackoverflow.com/questions/1408356/keyboard-interrupts-\
    with-pythons-multiprocessing-pool
    """
    def init_workers():
        signal.signal(signal.SIGINT, signal.SIG_IGN)

    global _pool
    _pool = mp.Pool(len(iterable), init_workers)
    results = _pool.map_async(mpw_func, iterable).get(sys.maxint)
    _pool.close()
    _pool.join()
    _pool = None
    return results


def terminate_pool():
    """Called on SIGINT to terminate running worker processes.

    XXX More work needed here, if the children have children then
    those become zombies.
    """
    global _pool
    if _pool:
        dbg("Terminating running children", nolock=True)
        _pool.terminate()
        _pool.join()


def make_ssh_runner(*args, **kwargs):
    """Wrap SshRunner to set common options."""
    try:
        kwargs['options'].setdefault("StrictHostKeyChecking", "no")
    except KeyError:
        kwargs.setdefault('options', {"StrictHostKeyChecking": "no"})
    if 'host' in kwargs:
        kwargs.update({"local_hint": kwargs['host'] in _ctx.local_servers})
    if _ctx.debug > 1:
        return _LoggingSshRunner(*args, **kwargs)
    else:
        return SshRunner(*args, **kwargs)


def make_iquery(*args, **kwargs):
    """Use client from install_path() unless explicit prog given."""
    kwargs.setdefault('prog', os.path.join(install_path(), 'bin', 'iquery'))
    return IQuery(*args, **kwargs)


def build_info(_cache=[]):
    """Return a bunch of build information.

    The returned object should have the following attributes:

    scidb_version - string, 'major.minor.patch'
    version - tuple of (major, minor, patch) integers
    build_type - string, one of 'Debug', 'RelWithDebInfo', etc.
    commit - string, a Git SHA

    We try to be resistant to minor changes in the "scidb --version"
    output.  If at least the above aren't present, it's a bug.
    """
    if _cache:
        return _cache[0]
    cmd = [os.path.join(install_path(_ctx), 'bin', 'scidb'),
           '--version']
    p = subp.Popen(cmd, stdout=subp.PIPE, stderr=subp.PIPE)
    out, err = p.communicate()
    if p.returncode:
        raise AppError("Cannot get SciDB --version information: %s" % err)
    result = Bunch()
    for line in out.splitlines():
        what, sep, value = line.partition(": ")
        if not sep:
            continue            # probably the copyright notice
        what = '_'.join(what.lower().split())
        setattr(result, what, value)
    try:
        result.version = tuple(int(x) for x in result.scidb_version.split('.'))
    except Exception:
        warn("SciDB version string looks bogus in ", result.__dict__)
    _cache.append(result)
    return result


def using_systemd():
    """Return True iff systemd(1) is used for service management.

    This is the One True Place for deciding whether the cluster is
    using systemd.  The scidbctl.py script must be run on one of the
    cluster hosts, so if *this* host is running systemd, then the
    cluster is too.
    """
    return os.path.exists("/bin/systemctl")


def _pg_connstr(ctx):
    """Build an unquoted Postgres connection string."""
    return "host={0} port={1} dbname={2} user={3}".format(
        ctx.config['pg-host'],
        ctx.config['pg-port'],
        ctx.cluster,
        ctx.config['db-user'])


def validate_config(config, inst_count=None, srvr_count=None,
                    cluster=None, force=False):
    """Do config.ini checks that individual instances can't do themselves.

    If it's just a question of whether a particular 'key=value' entry
    is bogus, just pass the --key=value option along and let the
    instance complain.  But if it's something the instance is unable
    or unlikely to check, this is the place.
    """

    if cluster is None:
        # Sometimes it's squirreled away here.
        cluster = config.get('scidb-cluster')

    def _count(x):
        try:
            return int(x)
        except Exception:
            return len(x)

    # You cannot intermix data-dir-prefix and data-dir-prefix-X-Y,
    # *especially* not during instance mpw_init_instance() related
    # action.
    prefixes = [x for x in config if x.startswith('data-dir-prefix')]
    if 'data-dir-prefix' in prefixes and len(prefixes) > 1:
        err = ("Cannot mix data-dir-prefix option with "
               "data-dir-prefix-X-Y option(s)")
        if force:
            warn(err)
        else:
            raise AppError(err)

    if inst_count is not None:
        inst_count = _count(inst_count)
        # Postgres must be configured to support simultaneous connections
        # from every instance.
        psql = Psql(user=config['pg-user'],
                    host=config['pg-host'],
                    port=config['pg-port'],
                    database=cluster)
        out = psql("SELECT * FROM pg_settings WHERE name = 'max_connections'")
        if len(out) != 1:
            raise AppError("Cannot determine Postgres 'max_connections' value")
        dbg("Postgres allows", out[0].setting, "connections and there are",
            inst_count, "SciDB instances.")
        if int(out[0].setting) < inst_count:
            err = textwrap.dedent("""
            Not enough Postgres connections for this SciDB configuration.

            Postgres allows only {0} simultaneous connections but
            there are {1} SciDB instances in the current configuration.

            Make sure that Postgres max_connections value is greater
            than the total number of scidb instances in config.ini.
            To modify the max_connections parameter, edit the
            postgresql.conf file located under /var/lib/pgsql .  For
            more information please consult the Postgres web site:

                https://wiki.postgresql.org/wiki/Tuning_Your_PostgreSQL_Server

            After changing max_connections (or any config setting), the
            Postgres service must be restarted.
                """)[1:].format(out[0].setting, inst_count)
            if force:
                warn(err)
            else:
                raise AppError(err)

    # There must be enough distinct servers to support the redundancy
    # parameter.
    if srvr_count is not None:
        srvr_count = _count(srvr_count)
        if 'redundancy' in config:
            red = int(config['redundancy'])
            if red < 0 or red >= srvr_count:
                err = ("Configured redundancy %s, but server count is %s,"
                       " legal range is 0..%s inclusive" % (
                           red, srvr_count, srvr_count - 1))
                if force:
                    warn(err)
                else:
                    raise AppError(err)


def parse_server_entry(spec, explicit=False):
    """Break 'host,n,m-p,q-r...'-style spec into (host, id_list).

    With explicit=False (the unfortunate default), the first 'n' in
    the above example is taken implicitly to mean '0-n'.  With
    explicit=True it is just plain 'n'.

    IMPLICIT 'n' == '0-n' SHOULD BE ABOLISHED AS SOON AS POSSIBLE.
    Unfortunately this VERY BAD IDEA is probably baked into a lot of
    config.ini files out there.

    See the "Configuring SciDB" section of the Administration Guide.
    """
    parts = [x.strip() for x in spec.split(',')]
    if not parts:
        raise AppError("Empty entry host/instance specification")

    ids = []
    host = parts[0]
    if len(parts) == 1:
        return host, [0]        # Implied instance zero
    parts = parts[1:]

    # Grab the instance ids as given, we'll check for overlap below.
    try:
        first = True
        for p in parts:
            left, dash, right = p.partition('-')
            if not dash:
                if first and not explicit:
                    # n means 0..n, what a VERY BAD IDEA, grrr....
                    ll = int(left)
                    if ll < 0:
                        raise AppError("Negative instance id in '%s'" % spec)
                    ids.extend(range(0, ll+1))
                else:
                    # n means n
                    ids.append(int(left))
            else:
                ll = int(left)
                rr = int(right)
                if ll < 0 or rr < ll:
                    raise AppError("Bad instance range {0}-{1} in '{2}'"
                                   .format(ll, rr, spec))
                ids.extend(range(ll, rr+1))
            first = False
    except AppError:
        raise
    except Exception as e:
        # Probably a ValueError because of a non-int somewhere.
        raise AppError("Bad instance range in '%s': %s" % (spec, e))

    # No overlapping ranges!
    if len(ids) > len(set(ids)):
        dbg("len(", ids, ") != len(", set(ids), ")")
        raise AppError("Duplicate instances in '%s'" % spec)

    return host, ids


def parse_config_ini(config_file, cluster=None):
    """Parse a SciDB config.ini file.

    filename - the file to parse
    cluster - the [section] to parse, if None there must be only one
              section in the file and the returned dict will have a
              'scidb-cluster' entry with the discovered name.

    Returns a 3-tuple of:
    - an OrderedDict of the key=value pairs,
    - a sorted list of ServInfo namedtuples (sid, name), and
    - a list of lists: each sublist is [sid, server_instance_id, host],
      one for each instance.

    Note: We don't return any InstInfo list, because some callers of
          this routine (the plugin's 'edit-config' subcommand) don't
          require that the file have a base-port entry, and without
          one we cannot compute the InstInfo port field.  See
          make_inst_info() below.

    """

    if cluster:
        dbg("Parsing", config_file, "for", cluster)
        want_cluster = False
    else:
        dbg("Parsing", config_file)
        want_cluster = True

    # First, some helper closures to deal with the server-N
    # directives... they are the tricky part!

    def get_server_id(key):
        """Parse server id out of server-N key."""
        m = re.match(r"server-(\d+)$", key)
        if not m:
            raise AppError("Bad server key '{0}' in {1}".format(
                key, config_file))
        return int(m.group(1))

    # And now, on with the parse.
    p = RawConfigParser()
    try:
        with open(config_file) as F:
            p.readfp(F)
    except Exception as e:
        raise AppError("Cannot read {0}: {1}".format(config_file, e))

    # Use first "[cluster]" section if none specified.
    if cluster:
        if cluster not in p.sections():
            raise AppError("Cluster [{0}] not found in {1}".format(
                cluster, config_file))
    elif not p.sections():
        raise AppError("No clusters found in %s" % config_file)
    elif len(p.sections()) == 1:
        # The only cluster in this config file.
        cluster = p.sections()[0]
        assert cluster, "Empty cluster section in %s?!" % config_file
    else:
        # Many clusters in this config file; best not to guess.
        warn("Using config file", config_file)
        raise AppError("Cluster name not specified, choose one of: %s" % (
            ", ".join(p.sections())))

    # Postgres will use the SciDB cluster name as a database name, and
    # CREATE DATABASE will force it to lowercase.  Therefore, to avoid
    # case mismatch horror, we insist on a lower case cluster name.
    if not cluster.islower():
        raise AppError("Sorry, cluster names (%s) must be lower case." %
                       cluster)

    cfg = OrderedDict()
    if want_cluster:
        cfg['scidb-cluster'] = cluster
    sids = set()
    iids = []
    servers = []
    for k, v in p.items(cluster):
        k = '-'.join(k.split('_'))  # backward compat
        cfg[k] = py_cast(v)
        if k.startswith("server-"):
            sid = get_server_id(k)
            try:
                host, ids = parse_server_entry(v)
            except Exception as e:
                raise AppError("Error parsing {0}={1} in {2}:\n{3}".format(
                    k, v, config_file, e))
            if sid in sids:
                raise AppError("Duplicate {0} entry in {1}".format(
                    k, config_file))
            sids.add(sid)
            # We'll make the InstInfo tuples later once we can compute
            # their port numbers (may not have the base-port yet).
            servers.append(ServInfo._make((sid, host)))
            iids.extend([[sid, x, host] for x in ids])
    servers.sort()
    iids.sort()
    return cfg, servers, iids


def make_inst_infos(base_port, sid_id_host_list):
    """Given a SciDB base-port, compute an InstInfo list.

    The sid_id_host_list is as produced by parse_config_ini().
    """
    result = []
    for entry in sid_id_host_list:
        entry.append(int(entry[1]) + base_port)
        result.append(InstInfo._make(entry))
    return result


def _process_args(args):
    """Do argument post-processing in light of config.ini contents.

    This takes an argparse.Namespace parameter and transforms it into
    a "context": the parsed command line options plus the parsed
    config.ini parameters plus a few other tidbits.

    The convention is that any totally artificial attribute created
    here MUST begin with "scidb_".  Artificial attributes are those
    not naturally present after running the argparse parser.
    (However, it's OK to synthesize attributes that were optional in
    config.ini but, if not supplied, can be derived.  That way the
    instance need not care where the setting came from.)

    """
    # Supply install path.
    args.scidb_install_path = install_path()
    dbg("Install path is", args.scidb_install_path)

    # Supply default config.ini file if needed, and parse it.
    if not hasattr(args, 'config_file') or not args.config_file:
        args.config_file = os.path.join(args.scidb_install_path,
                                        'etc', 'config.ini')
    if not os.path.isfile(args.config_file):
        raise AppError("Missing config file %s" % args.config_file)
    dbg("Cluster config.ini file is", args.config_file)
    if not hasattr(args, 'cluster'):
        args.cluster = None
    cfg, servers, iids = parse_config_ini(args.config_file, args.cluster)
    args.scidb_servers = servers
    args.local_servers = set(x.name for x in servers if is_local_host(x.name))
    if not args.cluster:
        args.cluster = cfg['scidb-cluster']
        del cfg['scidb-cluster']

    # We want to pass EVERY config.ini directive on to the SciDB
    # instance, and let the >>instance<< complain if it's wrong.
    # Explicit command line options with matching names should
    # override corresponding config.ini content.

    args.config = OrderedDict()
    for k, v in cfg.items():
        attr = '_'.join(k.split('-'))  # --foo-bar option is foo_bar attribute
        if attr in args:
            if getattr(args, attr) is None:
                # Corresponding command option exists but was not specified.
                args.config[k] = v
            else:
                warn("{0}: '{1}={2}' ignored, overridden by command option"
                     .format(args.config_file, k, v))
                args.config[k] = py_cast(getattr(args, attr))
        else:
            args.config[k] = v

    # Make sure the required stuff is present.
    missing = []
    for k in REQUIRED_CONFIG_INI:
        if k not in args.config:
            missing.append(k)
    if missing:
        raise AppError("Required options missing from {0}: {1}".format(
            args.config_file, ", ".join(missing)))

    # Now that we have all required options including base-port, we
    # can compute the instance ports and create InstInfos.
    base_port = int(args.config['base-port'])
    args.scidb_instances = make_inst_infos(base_port, iids)
    pairs = [(x.sid, x.siid) for x in args.scidb_instances]
    if len(pairs) != len(set(pairs)):
        error("Found duplicates in instance list:\n",
              pprint.pformat(args.scidb_instances))
        raise AppError("Duplicate instances in %s" % args.config_file)

    # If an args.config entry wasn't present in config.ini but can be
    # synthesized, do that here.
    pghost = args.config.setdefault('db-host',
                                    args.scidb_instances[0].server_name)
    args.config['pg-host'] = pghost  # Preferred name: host where Postgres runs
    args.config.setdefault('pg-port', 5432)
    args.config.setdefault('pg-user', 'postgres')
    args.config.setdefault('ssh-port', 22)

    pgpassfile = os.environ.get("PGPASSFILE")
    if not pgpassfile:
        pwent = pwd.getpwnam(getpass.getuser())
        pgpassfile = os.sep.join((pwent.pw_dir, ".pgpass"))
        if not os.path.isfile(pgpassfile):
            pgpassfile = None
    args.config.setdefault('pgpassfile', pgpassfile)

    return args


def init(stderr=None, pgm=None, context=None):
    """Initialize logging routines, combined argparse/config.ini context.

    Intended for use by the main program only; plugins MUST NOT call
    this.
    """
    if stderr is not None:
        global _stderr
        _stderr = stderr
    if pgm is not None:
        global _pgm
        _pgm = pgm
    if context is not None:
        global _ctx
        _ctx = Bunch(debug=context.debug)  # Set this ASAP!
        _ctx = _process_args(context)
        assert id(_ctx) == id(context), "com.init: divergent contexts"


def get_context():
    """For plugin code that needs to explicitly ask for the context."""
    return _ctx


def data_dir(ii):
    """Return the launch directory path for the ii instance.

    *Not* necessarily where instance user data is stored, see
     storage_dir() below.

    This directory always exists for each instance, and is a
    subdirectory of the base-path.  Even if the instance's datastores
    are relocated using data-dir-prefix* directives, it will always
    have a launch directory.
    """
    return os.path.join(_ctx.config['base-path'], str(ii.sid), str(ii.siid))


def storage_dir(ii):
    """Return special storage directory for the instance, or None.

    If 'data-dir-prefix-<sid>-<iid>' or 'data-dir-prefix' config
    parameters apply to this instance, then return the directory path
    where the datastores are to be kept.  If no config option applies,
    return None (and the calling code will use the launch_dir() for
    datastores as well).

    Formerly if any instance used 'data-dir-prefix-X-Y', then all had
    to use them.  Experimentally, we're no longer as strict and will
    use whichever of those cases we encounter first, from most to
    least specific.  More flexible on the face of it; we'll see if
    that's good or bad.
    """

    # Try instance-specific...
    key = "data-dir-prefix-%d-%d" % (ii.sid, ii.siid)
    result = _ctx.config.get(key)
    if result:
        if not os.path.isabs(result):
            raise AppError("Configured %s path must be absolute ('%s')" % (
                key, result))
        return result

    # Try general prefix...
    key = "data-dir-prefix"
    result = _ctx.config.get(key)
    if result:
        if not os.path.isabs(result):
            raise AppError("Configured %s path must be absolute ('%s')" % (
                key, result))
        # Backward compat: the prefix is not truly a directory name, sigh.
        return "%s.%d.%d" % (result, ii.sid, ii.siid)

    # No special configuration directive found.
    return None


def _fix_bool_option(opt, val):
    """Special treatment for boolean options to appease Boost."""
    # The goal is:
    #
    #   In config.ini       On the command line
    #   -------------       -------------------
    #   foo=0               --no-foo
    #   foo=1               --foo
    #   no-foo=0            --foo
    #   no-foo=1            --no-foo
    #
    if opt.startswith('no-'):
        opt = opt[3:]
        return '--no-{0}'.format(opt) if py_cast(val) else '--{0}'.format(opt)
    return '--{0}'.format(opt) if py_cast(val) else '--no-{0}'.format(opt)


def make_launch_command(ii, command=None):
    """Build a command list for launching a SciDB instance.

    If a 'command' list is given, use that as the executable name plus
    initial options, otherwise generate the executable name etc. as if
    for starting (rather than for init-all/init-syscat/etc.).
    """

    cmd = []

    # No valgrind for special 'command' lists (that is, for init-all etc.)
    use_valgrind = False
    if command is None:
        use_valgrind = build_info().build_type.lower() == 'valgrind'

    # Set environment variables.
    if 'LD_LIBRARY_PATH' in os.environ:
        cmd.append("LD_LIBRARY_PATH=%s" % os.environ['LD_LIBRARY_PATH'])
    if 'preload' in _ctx.config:
        if use_valgrind:
            raise AppError("Cannot use 'preload' option with valgrind")
        cmd.append("LD_PRELOAD=%s" % _ctx.config['preload'])

    # Now build the command itself.
    cmd.append('exec')          # Don't leave shells lying around
    if use_valgrind:
        assert os.path.exists('/usr/bin/valgrind'), (
            "Missing /usr/bin/valgrind")
        # WARNING: Place only tool-agnostic options here!  See below.
        cmd.extend(['/usr/bin/valgrind',
                    '-v',
                    '--num-callers=50',
                    '--log-file=/tmp/valgrid.%s.log' % ii.siid])
        # If config.ini contains 'vg-foo-bar = baz', then add
        # '--foo-bar=baz' option to valgrind.  If config.ini contains
        # 'vg-foo-0 = blah', 'vg-foo-1 = bleh', etc.  then add
        # multiple --foo options to valgrind.  (Our config.ini parser
        # won't allow more than one 'vg-foo' option.)
        for entry in _ctx.config:
            if entry[:3] == 'vg-':
                vg_opt = re.sub(r'-[0-9]+$', '', entry[3:])
                vg_val = _ctx.config[entry]
                cmd.append('--%s=%s' % (vg_opt, vg_val))
        # Not all valgrind tools allow all options, be careful!
        # E.g. only memcheck allows --track-origins.
        vg_tool = _ctx.config.get('vg-tool')
        if not vg_tool or vg_tool.lower() == 'memcheck':
            if 'vg-track-origins' not in _ctx.config:
                cmd.append('--track-origins=yes')

    # SciDB binary name...
    if command is None:
        linkname = "SciDB-%d-%d-%s" % (ii.sid, ii.siid, _ctx.cluster)
        prog = os.path.join(data_dir(ii), linkname)
        cmd.append(prog)
    else:
        cmd.extend(command)

    # ...and required options...
    cmd.extend([
        '--host', ii.server_name,
        '--port', ii.port,
        '--storage', os.path.join(data_dir(ii), 'storage.cfg17')])

    # Wholesale injection of config.ini options.  The instance should
    # ignore what it doesn't understand.
    for entry in _ctx.config:
        if re.match(IGNORE_REGEX, entry):
            continue
        opt = '-'.join(entry.split('_')).lower()
        val = _ctx.config[entry]
        if opt in BOOL_OPTIONS:
            cmd.append(_fix_bool_option(opt, val))
        elif val is None:
            cmd.append('--%s' % opt)
        elif isinstance(val, bool):
            cmd.append("--%s=%d" % (opt, int(val)))
        else:
            cmd.append("--%s=%s" % (opt, val))

    # Build the Postgres connection string.
    cmd.append("--catalog='%s'" % _pg_connstr(_ctx))

    # Watchdog and valgrind are incompatible.
    if use_valgrind and 'no-watchdog' not in _ctx.config:
        cmd.append('--no-watchdog')

    return cmd


def mpw_start_instance(ii):
    """Start one SciDB instance."""
    mp.current_process().name = "scidbctl-{0}-{1}-{2}".format(
        ii.sid, ii.siid, _ctx.cluster)
    info("Starting s{0}-i{1} on server {2}".format(
        ii.sid, ii.siid, ii.server_name))
    try:
        # Preliminaries.
        ssh = make_ssh_runner(host=ii.server_name, port=_ctx.ssh_port)
        datadir = data_dir(ii)
        binfile = os.path.join(_ctx.config['install-root'], 'bin', 'scidb')
        linkname = "SciDB-%d-%d-%s" % (ii.sid, ii.siid, _ctx.cluster)

        # Re-create symlink to .../bin/scidb, so that when we exec it,
        # it will have a name with embedded server and instance ids.
        # XXX Excessively paranoid?  Should have been done by 'init',
        # why again?

        _, err = ssh.run("ln -sf %s %s/%s" % (binfile, datadir, linkname))
        if ssh.failed():
            warn("Cannot recreate '%s' symlink in %s: %s" % (
                linkname, datadir, err))
        else:
            dbg("Remade symlink %s, hooray." % linkname)

        # Prepare to launch from data directory.
        out, err = ssh.cd(datadir)
        if ssh.failed():
            error("SSH client cannot cd to data directory '%s': %s" % (
                datadir, err))
            return False
        dbg("SSH will run commands from", out)

        # Launch!
        outfile = os.path.join(datadir, 'scidb-stdout.log')
        errfile = os.path.join(datadir, 'scidb-stderr.log')
        cmd = make_launch_command(ii)
        command = "{0} 1>{1} 2>{2} &".format(
            ' '.join(map(str, cmd)), outfile, errfile)
        dbg("Cmd:", command)
        out, err = ssh.run(command)
        if ssh.failed():
            warn("Cannot launch SciDB:", err)
            return False
        if err:
            warn(err)
        if out:
            info(out)

    except Exception as e:
        with ScopedLock(iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False

    return True


def mpw_init_datadir(ii):
    """Initialize one SciDB instance data directory.

    Create/validate the data directory before actually invoking SciDB
    with --register.  That way any problems with data directories can
    be addressed *before* any instances are registered in the catalog.
    """
    mp.current_process().name = "scidbctl-{0}-{1}-{2}".format(
        ii.sid, ii.siid, _ctx.cluster)
    info("Preparing data directory on %s" % ii.server_name)
    try:
        # Preliminaries.
        ssh = make_ssh_runner(host=ii.server_name, port=_ctx.ssh_port)
        datadir = data_dir(ii)
        stgdir = storage_dir(ii)
        binfile = os.path.join(_ctx.config['install-root'], 'bin', 'scidb')
        linkname = "SciDB-%d-%d-%s" % (ii.sid, ii.siid, _ctx.cluster)

        # Remove the old data directory.
        _, err = ssh.run("rm -rf %s" % datadir)
        if ssh.failed():
            # We could warn and press on, but perhaps it's better to quit.
            error("Cannot remove data directory '%s': %s" % (datadir, err))
            return False

        # Create the per-server parent of the data directory.
        parent = os.path.split(datadir)[0]
        _, err = ssh.run("mkdir -p %s" % parent)
        if ssh.failed():
            error("Cannot create server directory '%s': %s" % (parent, err))
            return False

        if stgdir is None:
            # Easy case: no data-dir-prefix, so datadir is a simple directory.
            _, err = ssh.run("mkdir -p %s" % datadir)
            if ssh.failed():
                error("Cannot create data directory '%s': %s" % (datadir, err))
                return False
        else:
            # The storage directory must exist (as a plain directory
            # or a symlink to one) and be empty.
            out, err = ssh.run("stat -c%F $(readlink -evn {0})".format(stgdir))
            if ssh.failed():
                error("Cannot stat '%s': %s" % (stgdir, err))
                return False
            if out.strip() != 'directory':
                error("Storage directory", stgdir, "is not a directory:", out)
                return False
            out, err = ssh.run("ls -A1 %s | wc -l" % stgdir)
            if ssh.failed():
                error("Unexpected error listing '%s': %s" % (stgdir, err))
                return False
            if int(out) != 0:
                error("Storage directory", stgdir, "is not empty")
                return False
            # Create data directory as a symlink to the storage directory.
            _, err = ssh.run("ln -s %s %s" % (stgdir, datadir))
            if ssh.failed():
                error("Cannot link '%s' -> '%s': %s" % (datadir, stgdir, err))
                return False

        # Create symlink to .../bin/scidb, so that when we exec it,
        # it will have a name with embedded server and instance ids.
        _, err = ssh.run("ln -sf %s %s/%s" % (binfile, datadir, linkname))
        if ssh.failed():
            warn("Cannot recreate '%s' symlink in %s: %s" % (
                linkname, datadir, err))
            return False
        dbg("Remade symlink %s, hooray." % linkname)

        # Data directory is prepared, we are ready to launch!
        return True

    except Exception as e:
        with ScopedLock(iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


def mpw_init_instance(ii, initialize_catalog=False):
    """Initialize one SciDB instance.

    Assumes that mpw_init_datadir() has succeeded!  Called with
    initialize_catalog=True only from the main thread.
    """
    mp.current_process().name = "scidbctl-{0}-{1}-{2}".format(
        ii.sid, ii.siid, _ctx.cluster)
    info("Initializing s{0}-i{1} on server {2}".format(
        ii.sid, ii.siid, ii.server_name))
    try:
        # Preliminaries.
        ssh = make_ssh_runner(host=ii.server_name, port=_ctx.ssh_port)
        datadir = data_dir(ii)
        binfile = os.path.join(_ctx.config['install-root'], 'bin', 'scidb')

        # Cd to data directory.
        _, err = ssh.cd(datadir)
        if ssh.failed():
            error("Cannot cd to data directory '%s': %s" % (datadir, err))
            return False

        # Build command to run the SciDB binary and register it in the
        # system catalog (and let it do whatever else it does with
        # --register).
        cmd = [binfile, '--register']
        if initialize_catalog:
            # I'm responsible for system catalog upgrade.
            cmd.append('--initialize')
        if hasattr(_ctx, 'scidb_online_option'):
            cmd.append('--online=%s' % _ctx.scidb_online_option)
        cmd = make_launch_command(ii, cmd)

        # Launch!
        outfile = os.path.join(datadir, 'init-stdout.log')
        errfile = os.path.join(datadir, 'init-stderr.log')
        command = "{0} 1>{1} 2>{2}".format(  # NOT backgrounded!
            ' '.join(map(str, cmd)), outfile, errfile)
        dbg("Cmd:", command)
        out, err = ssh.run(command)
        if ssh.failed():
            error("Cannot initialize instance (status %d): %s" % (
                ssh.returncode, err))
            return False
        if out:
            warn("Unexpected output from instance initialization:\n", out)
        if err:
            warn(err)

        return True

    except Exception as e:
        with ScopedLock(iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


def report_start_failure(ii):
    """Try to say something more helpful than "you lose"."""
    return _report_failure(ii, starting=True)


def report_init_failure(ii):
    """Try to say something more helpful than "you lose"."""
    return _report_failure(ii, starting=False)


def _report_failure(ii, starting):
    """If 'starting' then trying to launch, else trying to --register."""
    # Hopefully all instances failed for the same reason, so the
    # first instance on the list is as good as any.
    error("Retrieving stderr from instance", ii, "...")
    ssh = make_ssh_runner(host=ii.server_name, port=_ctx.ssh_port)
    datadir = data_dir(ii)
    errfile = os.path.join(datadir, '{0}-stderr.log'.format(
        'scidb' if starting else 'init'))
    out, err = ssh.run("cat %s" % errfile)
    if ssh.failed():
        error("Cannot retrieve '%s': %s" % (errfile, err))
    else:
        error("Instance %s:\n%s" % (errfile, out))
    error("Retrieving recent errors from scidb.log ...")
    logfile = os.path.join(datadir, 'scidb.log')
    # Is it huge?  If so, seek to last MiB before starting the grep.
    cat_cmd = 'cat %s' % logfile
    out, err = ssh.run("stat --format=%s {0}".format(logfile))  # pass 1st %s!
    if ssh.failed():
        warn("Cannot stat(%s): %s" % (logfile, err))
    else:
        nbytes = int(out)
        MiB = 1024 * 1024
        if nbytes > 2 * MiB:
            blocksize = MiB
            skip = (nbytes - MiB) / MiB  # in blocks
            cat_cmd = 'dd ibs=%d skip=%d if=%s' % (blocksize, skip, logfile)
    dbg("Cat command:", cat_cmd)
    # This'll get the last three errors ((-A3 plus 1) * 3 == 12).
    out, err = ssh.run(r"%s | grep -A3 '\[ERROR\]' | tail -12" % cat_cmd)
    if ssh.failed():
        error("Cannot grep '%s': %s" % (logfile, err))
    else:
        error("Recent errors:\n%s" % wrap(out))


def preen_watchdogs(proc_info):
    """Remove watchdog processes from get_process_info() output."""
    # If some procs have PPID == '1' but others don't, the '1' entries
    # are watchdogs and should be preened.  If all procs have PPID ==
    # '1' then either (a) we're running with no-watchdog=true and so
    # we should preen nothing, or (b) there's some startup failure and
    # only the watchdogs are present, continually trying to refork the
    # instances with backoff.  Again, OK to preen nothing: we want to
    # indicate that the cluster is *trying* to run.
    if any(x[1] != '1' for x in proc_info):
        return [x for x in proc_info if x[1] != '1']
    else:
        return proc_info


def get_process_info(which=None, sorted=True):
    """Return process information for the running SciDB cluster.

    Returns a list of (pid, ppid, ucmd, server_name, sid) tuples.
    By default, the list is sorted by sid and ucmd.

    Note: We ignore _ctx.server_id to avoid conflating a --server-id=X
    option with whether or not the subcommand wants to limit the
    returned process info.  It's quite possible to supply
    --server-id=X but still want this function to return *all* cluster
    process info.
    """
    if which is None:
        servers = _ctx.scidb_servers
    elif isinstance(which, int):
        servers = [x for x in _ctx.scidb_servers if x.sid == which]
        if not servers:
            raise AppError("Invalid server id %s" % which)
    else:
        servers = which
    assert servers, "Server list is empty?!"
    per_server_results = run_pool(mpw_status, servers)
    if False in per_server_results:
        warn("Could not connect to one or more servers")
    results = list()
    for x, s in zip(per_server_results, servers):
        if x is False:
            warn("Could not connect to server", s.name)
            continue
        assert x is not True, "Bogus True from mpw_status()"
        results.extend(x)
    if sorted:
        def _key(x):
            #       sid   cmd
            return (x[4], x[2])
        results.sort(key=_key)
    return results


def mpw_status(si):
    """Gather pids etc. for one SciDB ServInfo.

    We only produce tuples for instances that match si.sid, so that no
    tricky dedup logic is needed when 'localhost' and '127.0.0.1' are
    used to simulate a two-server cluster during development.
    """
    mp.current_process().name = si.name
    try:
        # Get raw pid info from remote server.
        ssh = make_ssh_runner(host=si.name, port=_ctx.ssh_port)
        script = """
            ps --no-headers -eww -o pid,ppid,args |
                egrep 'SciDB-{0}-[0-9]+-{1}' ;
            /bin/true  # Empty grep output is OK.
            """.format(si.sid, "" if _ctx.verb == 'status' else _ctx.cluster)
        out, err = ssh.run(script)
        if ssh.failed():
            warn("No SSH access to {0}: {1}".format(si.name, err))
            return False

        # Scrape results to get list of per-process tuples.
        lines = out.splitlines()

        def make_tuple(line):
            parts = line.split()
            # With valgrind builds, SciDB-x-y-z can be anywhere in the args.
            prog = [os.path.basename(x) for x in parts[2:] if 'SciDB-' in x][0]
            assert prog, "Cannot find 'SciDB-' but I grepped for it?!"
            return (parts[0], parts[1], prog, si.name, si.sid)

        return [make_tuple(x) for x in lines]

    except Exception as e:
        with ScopedLock(iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


def mpw_stop_pids(proc_args):
    """Stop given pids on this server.

    Either because of the --only option or because of valgrind, we
    can't just use killall(1) but must kill the processes
    individually.
    """
    server, pids = proc_args
    mp.current_process().name = server
    info("Stopping", len(pids), "processes in cluster", _ctx.cluster)
    dbg("PIDs:", pprint.pformat(pids))
    try:
        sig = "-s 9" if _ctx.force else ""
        ssh = make_ssh_runner(host=server, port=_ctx.ssh_port)
        # /bin/true because we don't care if some pids are already gone
        script = "kill {0} {1} 2>&1 ; /bin/true".format(sig, ' '.join(pids))
        _, err = ssh.run(script)
        if ssh.failed():
            warn("No SSH access to {0}: {1}".format(server, err))
            return False

        return True

    except Exception as e:
        with ScopedLock(iolock()):
            warn("Unhandled exception:", e, nolock=True)
            traceback.print_exc()
        return False


if 'P4_TESTCASES_DIR' in os.environ:
    _stderr = sys.stdout
    dbg("Running under the test harness, stderr=stdout.")

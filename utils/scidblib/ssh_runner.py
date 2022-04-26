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
SSH wrapper for running remote commands and copying remote files.

See scidb_backup.py for usage examples.
"""

import os
import pwd
import subprocess as subp
import sys

from scidblib.util import is_local_host


def _nop(*args):
    pass


class SshRunner(object):

    """SSH wrapper for running successive remote commands."""

    def __init__(self, user=None, host=None, wdir=None, options=None,
                 port=None, ssh_always=None, debug_func=None, local_hint=None):
        """Construct an object to run ssh/scp commands.

        user - remote user, default is local user
        host - remote hostname
        wdir - remote working directory, cd there before running commands
        port - remote SSH port
        options - dict of SSH options and values, see ssh_config(5)
        ssh_always - force use of SSH even for the local host
        debug_func - callable for printing debug messages
        local_hint - caller already did is_local_host() check (SDB-6752)
        """
        self.dbg = debug_func if debug_func else _nop
        self.wdir = wdir
        self.user = user if user else pwd.getpwuid(os.getuid())[0]
        if ssh_always is None:
            ssh_always = int(os.environ.get('SCIDB_SSH_ALWAYS', '0'))
        if host is None:
            self.host = 'localhost'
            local_hint = True   # Not self.is_local, ssh_always overrides.
        else:
            self.host = host
        if ssh_always:
            self.is_local = False
        elif local_hint is None:
            self.is_local = is_local_host(self.host)
        else:
            self.is_local = local_hint
        self.sshcmd = ['ssh']
        if port is None:
            self.port = None
        else:
            self.sshcmd.extend(['-p', str(port)])
            self.port = int(port)
        if options:
            for k, v in options.items():
                self.sshcmd.extend(['-o', '%s=%s' % (k, v)])
        self.sshcmd.append('@'.join((self.user, self.host)))
        self.path = "PATH=/bin:/usr/bin:/sbin:/usr/sbin"
        self.returncode = 0

    def __repr__(self):
        return "{0}({1})".format(
            type(self).__name__,
            ','.join("%s=%s" % x for x in self.__dict__.items()))

    def _cd(self, cmd):
        """Wrap a remote command with 'cd' in subshell."""
        if not isinstance(cmd, basestring):
            cmd = (' '.join(str(x) for x in cmd))
        if self.wdir is None:
            return ' '.join((self.path, cmd))
        return "({0} cd {1} && {2})".format(self.path, self.wdir, cmd)

    def run(self, cmd):
        """Run remote command."""
        if isinstance(cmd, basestring):
            cmd = cmd.split()
        if self.is_local:
            args = "bash -c".split()
        else:
            args = list(self.sshcmd)
        self.dbg("DBG: cmd:", cmd)
        args.append(self._cd(cmd))
        self.dbg("DBG: args:", args)
        p = subp.Popen(args, stdout=subp.PIPE, stderr=subp.PIPE)
        out, err = p.communicate()
        self.returncode = p.returncode
        return out, err

    def cd(self, wdir):
        """Change to working directory 'wdir' and return $(pwd) result."""
        saved_wdir = self.wdir
        self.wdir = wdir
        out, err = self.run("pwd")
        if self.ok():
            # Use $(pwd) output in case we traversed a relative symlink.
            self.wdir = out.strip()
        else:
            self.wdir = saved_wdir
        return self.wdir, err

    def ok(self):
        """Make returncode checking easy: exit status 0 is success."""
        return self.returncode == 0

    def failed(self):
        """Make returncode checking easy: non-zero exit status is failure.

        @note This denotes failure of the command being run, not of SSH itself.
        """
        return self.returncode != 0

    def _scp(self, source, dest, is_get=True, is_dir=False):
        """Run scp(1) with these strings as parameters."""

        # Fix relative remote paths to use wdir.
        def make_absolute(path):
            if not path.startswith(os.sep) and self.wdir is not None:
                return os.sep.join((self.wdir, path))
            else:
                return path

        # Tack on correct credentials.
        def adorn(path):
            return ':'.join(('@'.join((self.user, self.host)), path))

        if not self.is_local:
            if is_get:
                # source is remote
                source = adorn(make_absolute(source))
            else:
                # dest is remote
                dest = adorn(make_absolute(dest))

        # Run it, baby!
        cmd = ['cp'] if self.is_local else ['scp']
        if not is_get:
            cmd.append('-p')     # preserve mode, access time, etc. on put
        if is_dir:
            cmd.append('-r')
        if self.port and not self.is_local:
            cmd.extend(['-P', str(self.port)])
        cmd.extend([source, dest])
        p = subp.Popen(cmd, stdout=subp.PIPE, stderr=subp.PIPE)
        out, err = p.communicate()
        self.returncode = p.returncode
        return out, err

    def get_file(self, remote, local):
        """Copy remote file to local file."""
        return self._scp(remote, local, is_get=True)

    def put_file(self, local, remote):
        """Copy local file to remote file."""
        return self._scp(local, remote, is_get=False)

    def get_dir(self, remote, local):
        """Copy remote directory to local directory."""
        return self._scp(remote, local, is_get=True, is_dir=True)

    def put_dir(self, local, remote):
        """Copy local directory file to remote directory."""
        return self._scp(local, remote, is_get=False, is_dir=True)


if __name__ == '__main__':
    print >>sys.stderr, "No main program, sorry!"
    sys.exit(1)

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

import argparse
import datetime
import getpass
import multiprocessing as mp
import os
import re
import socket
import sys
import traceback

from collections import namedtuple
from contextlib import contextmanager
from scidblib.iquery_client import IQuery


def getVerifiedPassword(prompt=None, verify=None):
    """Read and verify a password from the tty.

    @param prompt the prompt string for initial password entry
    @param verify the prompt string for verification
    """
    if prompt is None:
        prompt = "Password: "
    if verify is None:
        verify = "Re-enter password: "
    while True:
        p1 = getpass.getpass(prompt)
        p2 = getpass.getpass(verify)
        if p1 == p2:
            break
        try:
            with open("/dev/tty", "w") as F:
                print >>F, "Passwords do not match"
        except OSError:
            print >>sys.stderr, "Passwords do not match"
    return p1


def py_cast(x):
    """Convert x to a native Python equivalent."""
    try:
        return int(x)
    except ValueError:
        pass
    try:
        return float(x)
    except ValueError:
        pass
    xx = x.lower().strip()
    if xx == 'true':
        return True
    if xx == 'false':
        return False
    return x


def make_table(entry_name, query, namespace=None, host=None, port=None):
    """Build a list of named tuples based on result of the given AFL query.

    @param entry_name name of type to be created by collections.namedtuple
    @param query      AFL query from whose output we will make a table
    @param namespace  use as current namespace when executing query
    @param host       host for iquery connections
    @param port       port for iquery connections

    Because the entire query result is read into memory, best to use
    this only with queries returning smallish results.

    Fields that can be converted to ints, floats, or bools are so converted.

    An example:
    >>> t = make_table('ArrayTable', "list('arrays',true)")
    >>> all_versioned_array_ids = [x.aid for x in t if x.aid != x.uaid]

    @note The namedtuple type only allows alphanumerics and
          underscores for attribute names, and doesn't allow leading
          underscores.  Substitute '_' for any illegal character, and
          tack an 'x' on the front if needed.  For example, if making
          a table from a sorted query, the $n dimension becomes the
          x_n namedtuple attribute.
    """
    # Format tsv+:l gives dimension/attribute names used for tuple attributes.
    iquery = IQuery(afl=True, format='tsv+:l', namespace=namespace,
                    host=host, port=port)
    out, err = iquery(query)
    if err:
        raise RuntimeError(err)
    table_data = out.splitlines()
    # Sometimes SciDB gives the same label to >1 attribute; make them unique.
    attrs = []
    seen = dict()
    for raw_label in table_data[0].split():
        # Sanitize the raw_label so namedtuple won't hate it.
        label = re.sub(r'[^a-zA-Z0-9_]', '_', raw_label)
        if label.startswith('_'):
            label = "x%s" % label  # Hates leading _ too.
        # OK, onward.
        if label in seen:
            seen[label] += 1
            label = '_'.join((label, str(seen[label])))
        else:
            seen[label] = 1
        attrs.append(label)

    # Create our data type and fill in the table.
    tuple_type = namedtuple(entry_name, attrs)
    table = []
    for line in table_data[1:]:
        table.append(tuple_type._make(py_cast(x) for x in line.split('\t')))
    return table


def timedelta_to_seconds(tdelta):
    """The missing datetime.timedelta.to_seconds() method.

    @return tdelta, in seconds, as a floating point number
    @see http://stackoverflow.com/questions/1083402/\
         missing-datetime-timedelta-to-seconds-float-in-python
    """
    return ((tdelta.days * 3600 * 24) +
            tdelta.seconds +
            (tdelta.microseconds / 1e6))


def seconds_to_timedelta(seconds):
    """Inverse of the missing datetime.timedelta.to_seconds() method.

    @return seconds converted to a datetime.timedelta
    @see http://stackoverflow.com/questions/1083402/\
         missing-datetime-timedelta-to-seconds-float-in-python
    """
    return datetime.timedelta(int(seconds) // (3600 * 24),
                              int(seconds) % (3600 * 24),
                              (seconds - int(seconds)) * 1e6)


class ElapsedTimer(object):
    """Context manager for computing elapsed times of tests."""
    enabled = True

    def __init__(self):
        self.start = None

    def __enter__(self):
        if ElapsedTimer.enabled:
            self.start = datetime.datetime.now()

    def __exit__(self, typ, val, tb):
        if ElapsedTimer.enabled:
            print "Elapsed time:", str(datetime.datetime.now() - self.start)
        return False            # exception (if any) not handled


class EnvDefault(argparse.Action):

    """
    Let argparse options take defaults from the environment.

    See https://stackoverflow.com/questions/10551117/\
       setting-options-from-environment-variables-when-using-argparse#10551389
    """

    def __init__(self, envvar, required=True, default=None, **kwargs):
        if not default and envvar:
            if envvar in os.environ:
                default = os.environ[envvar]
        if required and default:
            required = False
        super(EnvDefault, self).__init__(default=default, required=required,
                                         **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, values)


def multiple_replace(text, adict):
    """Perform several string replacements in a single pass.

    Substitutes each adict key 'k' with adict[k] in the text.  See
    _Python Cookbook_ 2d ed., "Replacing Multiple Patterns in a Single
    Pass".
    """
    rgx = re.compile('|'.join(re.escape(x) for x in adict))

    def one_xlat(match):
        return adict[match.group(0)]

    return rgx.sub(one_xlat, text)


def is_local_host(host_or_ip):
    """Return true iff the hostname is an alias for this local machine.

    Whether it's an IP address or a hostname, if we can bind a socket
    to it, then it must be local.  Full credit to Tigor!

    NOTE: Calls to socket.getaddrinfo() intermittently deadlock when
          done from a multiprocessing child process.  One workaround
          is to pre-compute is_local_host() in the parent. See
          SDB-6752, https://jira.mongodb.org/browse/PYTHON-607 , and
          https://emptysqua.re/blog/weird-green-bug/ .
    """
    s = None
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        addr = socket.getaddrinfo(host_or_ip, None,
                                  socket.AF_INET, socket.SOCK_STREAM)
        s.bind(addr[0][4])
        return True
    except Exception:
        return False
    finally:
        if s:
            s.close()


class Bunch(object):
    """See _Python Cookbook_ 2d ed., "Collecting a Bunch of Named Items".

    Used in many places; let's try to make this one the master copy!
    """
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


@contextmanager
def ScopedLock(lk=None):
    """Used for locking console I/O."""
    if lk:
        lk.acquire()
    yield
    if lk:
        lk.release()


class MpwContext(object):

    """Multiprocessing worker context manager.

    This context manager does common exception handling for
    multiprocessing module child processes.  Exceptions aren't allowed
    to escape from pool worker map() functions.  If they do, debugging
    gets ugly.

    Use this context manager to enclose the child process business
    logic in a 'with statement.  Inside the body of the 'with'
    statement, return true-ish or false-ish depending on your
    application logic.  If the enclosing MpwContext absorbs an
    exception, you need do nothing outside the 'with' statement: None
    will be returned, which is false-ish and tells the main thread
    there was an error.

    @note Not used in scidbctl.py or scidbctl_common.py, because those
          have stricter semantics on the worker function's return
          value.  Returning None (as happens when our __exit__()
          method swallows the exception and the worker exits without a
          return statement) wouldn't be interpreted correctly but the
          scidbctl.py main process.  Might fix it later.
    """

    def __init__(self, lock=None, file=None):
        self.lock = lock
        self.fh = sys.stdout if file is None else file

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            return False        # No exception, so didn't handle it
        with ScopedLock(self.lock):
            if exc_type is KeyboardInterrupt:
                print >>self.fh, "%s: Interrupt" % mp.current_process().name
                self.fh.flush()
            else:
                print >>self.fh, "%s: Unhandled exception: %s" % (
                    mp.current_process().name, exc_val)
                traceback.print_exc(file=self.fh)
                self.fh.flush()
        return True     # Always handled, never re-raised!

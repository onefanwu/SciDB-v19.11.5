#!/usr/bin/python
# Initialize, start and stop scidb in a cluster.
#
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
#

import datetime
import os
import subprocess
import sys
import time


# bad style to use from, removes the namespace colission-avoidance mechanism
from ConfigParser import RawConfigParser


def printDebug(string, force=False):
    if _DBG or force:
        print >> sys.stderr, "%s: DBG: %s" % (sys.argv[0], string)
        sys.stderr.flush()


def printDebugForce(string):
    printDebug(string, force=True)


def printInfo(string):
    sys.stdout.flush()
    print >> sys.stdout, "%s" % (string)
    sys.stdout.flush()


def printError(string):
    print >> sys.stderr, "%s: ERROR: %s" % (sys.argv[0], string)
    sys.stderr.flush()


def usage():
    print ""
    print "Usage: %s [<config_file>]" % sys.argv[0]
    print "Commands:"
    print "\t all"


# Parse a config file
def parseGlobalOptions(filename, section_name):
    config = RawConfigParser()
    config.read(filename)

    # First process the "global" section.
    try:
        # print "Parsing %s section." % (section_name)
        for (key, value) in config.items(section_name):
            _configOptions[key] = value
    except Exception, e:
        printError("config file parser error in file: %s, reason: %s"
                   % (filename, e))
        sys.exit(1)


#
# Execute OS command
# This is a wrapper method for subprocess.Popen()
# If waitFlag=True and raiseOnBadExitCode=True and the exit code of the
# child process != 0, an exception will be raised.
def executeIt(cmdList,
              nocwd=False,
              useShell=False,
              cmd=None,
              stdoutFile=None, stderrFile=None,
              waitFlag=True,
              raiseOnBadExitCode=True):
    ret = 0

    dataDir = "./"
    if nocwd:
        currentDir = None
    else:
        currentDir = dataDir
    # print currentDir

    my_env = os.environ

    if useShell:
        cmdList = [" ".join(cmdList)]

    p = None
    try:
        sout = None
        if stdoutFile:
            # print "local - about to open stdoutFile log file:", stdoutFile
            sout = open(stdoutFile, "w")
        elif not waitFlag:
            sout = open("/dev/null", "w")

        serr = None
        if stderrFile:
            # print "local - about to open stderrFile log file:", stderrFile
            serr = open(stderrFile, "w")
        elif not waitFlag:
            serr = open("/dev/null", "w")

        p = subprocess.Popen(cmdList,
                             env=my_env, cwd=currentDir,
                             stderr=serr, stdout=sout,
                             shell=useShell, executable=cmd)
        if waitFlag:
            p.wait()
            ret = p.returncode
            if ret != 0 and raiseOnBadExitCode:
                raise Exception("Abnormal return code: %s on command %s"
                                % (ret, cmdList))
    finally:
        if (sout):
            sout.close()
        if (serr):
            serr.close()

    return p


#
# a class to use with the "with" statement that
# collects the time on entering and exiting a
# block of code.
# Time is wall-clock time, unlike time.clock() which is actually accumulated
# cpu time, not a wall-clock.
#
class TimerRealTime():
    def __enter__(self):
        self.start = time.time()
        return self

    def __exit__(self, *args):
        self.end = time.time()
        self.interval = self.end - self.start
        return False


def doScript(script):
    cmdList = ["/bin/bash", script]
    executeIt(cmdList,
              nocwd=False,
              cmd=None,
              stdoutFile=None, stderrFile=None,
              waitFlag=True,
              raiseOnBadExitCode=True)


def doTests(num, iterations, script):
    """ Executes tests in a shell script num x iterations times.

    This function takes the name of a shell script - which has been created
    containing iquery commands - and creates 'num' processes executing that script.
    As those, processes finish, more processes are created to replace them.  That
    process continues, 'iterations' number of times.
    """

    rc = True
    procs = []
    cmdList = ["/bin/bash", script]
    # Total number of processes - decremented as they are started.
    total = num * iterations

    # Create num processes executing the test script.
    for i in range(num):
        if total <= 0:
            break
        p = executeIt(cmdList,
                      nocwd=False,
                      cmd=None,
                      stdoutFile=None, stderrFile=None,
                      waitFlag=False,
                      raiseOnBadExitCode=True)
        procs.append(p)
        total -= 1
        printDebug("Started proc[%d] = %s" % (i, str(procs[i])))

    # Every second, poll each of the num process running.  If it has finished,
    # check its exit code and start another one.
    while total > 0:
        for i in range(num):
            if total <= 0:
                break
            p = procs[i]
            res = p.poll()
            if res is None:
                pass
            else:
                if res != 0:
                    rc = False
                    printError("FAILURE exit code: %d" % res)
                p = executeIt(cmdList,
                              nocwd=False,
                              cmd=None,
                              stdoutFile=None, stderrFile=None,
                              waitFlag=False,
                              raiseOnBadExitCode=True)
                procs[i] = p
                total -= 1
                printDebug("Starting proc[%d] = %s" % (i, str(procs[i])))
        printDebug("Total = %d - sleeping ..." % (total))
        time.sleep(1)

    # Once all the processes have started (some have already ended), wait()
    # for each to end and check its exit code.
    for i in range(num):
        p = procs[i]
        res = p.wait()
        if res != 0:
            rc = False
            printError("FAILURE exit code: %d" % res)
    return rc


# config file options
_configOptions = {}
#
# SciDB connection port
_basePort = "1239"
#
# SciDB connection target host
_targetHost = "localhost"
#
# The name of the iquery executable
_iqueryBin = "iquery"
#
# Time iquery executions
_timePrefix = None
#
# Number of concurrent clients
_numClients = 6

_DBG = False


# The main entry routine that does command line parsing
def main(argv=None):
    if argv is None:
        argv = sys.argv
    # Very basic check.
    if len(argv) > 2:
        usage()
        return 2

    if len(argv) == 2:
        arg = argv[1]
        if len(arg) <= 0:
            usage()
            return 2
    else:
        arg = ""

    if arg == "--help" or arg == "-h":
        usage()
        return 0

    if len(arg) > 0:
        configfile = arg
        parseGlobalOptions(configfile, "multi_query_test")

    global _DBG
    try:
        _DBG = int(os.environ['SCIDB_DBG'])
    except KeyError:
        pass
    except ValueError:
        printError("SCIDB_DBG set but value '%s' is not an integer" %
                   os.environ['SCIDB_DBG'])
        return 3

    global _basePort
    global _targetHost
    global _iqueryBin
    global _timePrefix
    global _numClients

    if "install-path" in _configOptions:
        installPath = _configOptions.get("install-path")
        if len(installPath) > 0:
            _iqueryBin = installPath+"/bin/"+_iqueryBin

    if "base-port" in _configOptions:
        _basePort = _configOptions.get("base-port")

    if "time-queries" in _configOptions:
        _timePrefix = "time"

    if "target-host" in _configOptions:
        _targetHost = _configOptions.get("target-host")
    else:
        usage()
        return 3

    if "setup" in _configOptions:
        setupScript = _configOptions.get("setup")
    else:
        usage()
        return 3

    if "cleanup" in _configOptions:
        cleanupScript = _configOptions.get("cleanup")
    else:
        usage()
        return 3

    if "tests" in _configOptions:
        testScript = _configOptions.get("tests")
    else:
        usage()
        return 3

    if "num-clients" in _configOptions:
        _numClients = int(_configOptions.get("num-clients"))

    if _numClients < 0:
        raise Exception("Invalid value for %s: %d" %
                        ("num-clients", _numClients))

    if "num-iterations" in _configOptions:
        _numIterations = int(_configOptions.get("num-iterations"))

    if _numIterations < 0:
        raise Exception("Invalid value for %s: %d" % ("num-iterations",
                                                      _numIterations))

    printDebugForce("Start %s: numClients %d, numIterations %d, "
                    "setup %s, tests %s cleanup %s" %
                    (datetime.datetime.utcnow(), _numClients, _numIterations,
                     setupScript, testScript, cleanupScript))
    ok = True
    doScript(setupScript)

    with TimerRealTime() as timer:
        ok = doTests(_numClients, _numIterations, testScript)
    printDebugForce("%s took %s" % ("tests", timer.interval))

    doScript(cleanupScript)

    printDebugForce("Finish %s: numClients %d, numIterations %d, "
                    "setup %s, tests %s cleanup %s" %
                    (datetime.datetime.utcnow(), _numClients, _numIterations,
                     setupScript, testScript, cleanupScript))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

This directory holds tests that restart the SciDB cluster.  Forcing
these tests to run last lets developers attach a debugger to one or
more SciDB instances, so that difficult-to-reproduce failures can be
caught without having to hurriedly re-attach after a restart.  See
SDB-6734.

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
#
import os
import sys
import scidbapi as scidb
def usage():
  print "Usage: python basic.py"
  print "This file is being invoked by python_1.test."
  sys.exit(2)
def partition(mystr,mysep):
  mylist = mystr.split(mysep)
  lside = mylist[0]
  mside = mysep
  rside = ''
  for i in range( len(mylist) ):
    if i > 0:
      rside = rside + mylist[i] + mysep
  rside = rside[0:-1]
  return( [lside,mside,rside] )
def q_execute(query, db):
    #print >> sys.stderr, "DEBUG q_execute: '%s','%s'" % (query[0], query[2],)
  try:
    result=db.executeQuery(query[2], query[0])
  except Exception, e:
    handleException(e, True, op="query execution")
  return result
def q_complete(result):
  db.completeQuery(result.queryID)

def print_dataitem(dtypeStr, dataitem):
  print "Data: %s" % scidb.getTypedValue(dataitem, dtypeStr)
def print_result(res):
    desc = res.array.getArrayDesc()
    dims = desc.getDimensions()
    attrs = desc.getAttributes()
    out_file.write("# Dimensions: %d" % dims.size())
    for i in range (dims.size()):
        d = dims[i]
        out_file.write("%s=%d:%d, %d, %d" % (d.getBaseName(), d.getStartMin(), d.getEndMax(),
                                                              d.getChunkInterval(), d.getChunkOverlap()))
    for i in range (attrs.size()):
        a = attrs[i]
        out_file.write("%s:%s" % (a.getName(), a.getType()))
    if (dims.size()+attrs.size() > 0):
        out_file.write("\n")

    iters = []
    for i in range (attrs.size()):
        attrid = attrs[i].getId()
        iters.append(res.array.getConstIterator(attrid))

    nc = 0
    while not iters[0].end():
      for i in range (attrs.size()):
        if (attrs[i].getName() == "EmptyTag"):
          continue
        print "Getting iterator for attribute %d, chunk %d.\n" % (i, nc)
        currentchunk = iters[i].getChunk()
        chunkiter = currentchunk.getConstIterator((scidb.swig.ConstChunkIterator.IGNORE_OVERLAPS))

        printed=False
        while not chunkiter.end():
          if not chunkiter.isEmpty():
            dataitem = chunkiter.getItem()
            print_dataitem(attrs[i].getType(), dataitem)
            printed=True

          chunkiter.increment_to_next()
        if printed:
          print       # add an extra newline to separate the data from the next query
      nc += 1;
      for i in range(attrs.size()):
        iters[i].increment_to_next();


if __name__ == "__main__":
    files = ['querylist.csv']             # files to execute statements from

    host="localhost"
    port="1239"

    if ('IQUERY_HOST' in os.environ.keys()):
      host = os.environ['IQUERY_HOST']

    if ('IQUERY_PORT' in os.environ.keys()):
      port = os.environ['IQUERY_PORT']

    if len(sys.argv) >2:
      host = sys.argv[1]
      port = sys.argv[2]
    db = scidb.connect(host, int(port))
    for ifile in files:
        stmt = []
        qno = 0
        # print ""
        if not os.path.exists(ifile):
            print "File: ",ifile," does not exist!"
            sys.exit(2)
        print "Reading file %s" % ifile
        fileHandle = open(ifile,'r')
        for row in fileHandle:
            stmt.append(row)
        fileHandle.close()
        for stmt_str in stmt:
            query = partition(stmt_str,',')
            if( len(query[2].strip()) > 3 ):
                qno+=1
                # remove any trailing newline from the query
                queryStr=query[2]
                if queryStr[-1:] == '\n' :
                    queryStr = queryStr[0:-1]
                # or leading space
                if queryStr[0] == ' ' :
                    queryStr = queryStr[1:]
                query=(query[0],query[1],queryStr)
                print "Query %d (%s): \"%s\"" % (qno, query[0], query[2])
                out_file=open('/tmp/%s_%d' % (ifile, qno), 'w')
                out_file.write( "Query %d (%s): %s\n" % (qno, query[0], query[2] ))
                try:
                    qr = q_execute(query, db)
                    if(qr.selective):
                        print_result(qr)
                    else:
                        out_file.write( "Query was executed successfully\n" )
                        out_file.close()
                    q_complete(qr)
                except Exception, inst:
                    print >> sys.stderr, "Exception while executing query: ", query[2]
                    print "     Exception Type: %s" % type(inst)     # the exception instance
                    print "     %s" % inst
    print "Done!"

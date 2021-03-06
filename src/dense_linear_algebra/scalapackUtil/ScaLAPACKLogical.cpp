/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2008-2019 SciDB, Inc.
* All Rights Reserved.
*
* SciDB is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

///
/// @file ScaLAPACKLogical.cpp
///

// local
#include "scalapackUtil/ScaLAPACKLogical.hpp"

// standards
#include <utility>

// de-facto standards
#include <log4cxx/logger.h>

// SciDB
#include <system/Warnings.h>

using namespace std;

namespace scidb {

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.linear_algebra.ops.scalapack"));

inline bool hasSingleAttribute(ArrayDesc const& desc)
{
    return (desc.getAttributes().size() == 1 ||
            (desc.getAttributes().size() == 2 && desc.getAttributes().hasEmptyIndicator()));
}

// Called from both logical and physical operators, via the wrappers that immediately follow it.
//
// @note We use SCIDB_SE_INFER_SCHEMA here but it would be nice to use either that or
// SCIDB_SE_OPTIMIZER depending on the caller.  See Trac ticket #5048.
//
static void checkScaLAPACKSchemasInternal(std::vector<ArrayDesc const*> schemas,
                                          std::shared_ptr<Query> query,
                                          size_t nMatsMin,
                                          size_t nMatsMax,
                                          bool fromLogical)
{
    enum dummy  {ROW=0, COL=1};

    const size_t NUM_MATRICES = schemas.size();

    if(schemas.size() < nMatsMin ||
       schemas.size() > nMatsMax) {
        assert(fromLogical);
        throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR2);
    }

    // Check the properties first by argument, then by order property is determined in AFL statement:
    // size, chunkSize, overlap.
    // Check individual properties in the loop, and any inter-matrix properties after the loop
    // TODO: in all of these, name the argument # at fault
    for(size_t iArray=0; iArray < NUM_MATRICES; iArray++) {
        assert(schemas[iArray]);

        // check: attribute count == 1
        if (!hasSingleAttribute(*schemas[iArray])) {
            assert(fromLogical);
            throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR2);
            // TODO: offending matrix is iArray
        }

        // check: attribute type is double
        if (schemas[iArray]->getAttributes().firstDataAttribute().getType() != TID_DOUBLE) {
            assert(fromLogical);
            throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR5);
            // TODO: offending matrix is iArray
        }

        // check: nDim == 2 (a matrix)
        // TODO: relax nDim to be 1 and have it imply NCOL=1  (column vector)
        //       if you want a row vector, we could make transpose accept the column vector and output a 1 x N matrix
        //       and call that a "row vector"  The other way could never be acceptable.
        //
        const size_t SCALAPACK_IS_2D = 2 ;
        if (schemas[iArray]->getDimensions().size() != SCALAPACK_IS_2D) {
            assert(fromLogical);
            throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR3);
            // TODO: offending matrix is iArray
        }

        // check: size is bounded
        const Dimensions& dims = schemas[iArray]->getDimensions();
        if (dims[ROW].isMaxStar() || dims[COL].isMaxStar()) {
            assert(fromLogical);
            throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR9);
        }
        // TODO: check: sizes are not larger than largest ScaLAPACK fortran INTEGER

        // TEMPORARY until #2202 defines how to interpret arrays not starting at 0
        // "dimensions must start at 0"
        for(unsigned dim =ROW; dim <= COL; dim++) {
            if(dims[dim].getStartMin() != 0) {
                assert(fromLogical);
                throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR44);
            }
        }

        // Check intervals at physical execute() time too, since they may be unspecified
        // (autochunked) at logical inferSchema() time.
        bool rowAutochunked = dims[ROW].isAutochunked();
        bool colAutochunked = dims[COL].isAutochunked();
        ASSERT_EXCEPTION(fromLogical || (!rowAutochunked && !colAutochunked),
                         "Unresolved chunk intervals at execute() time");

        // check: chunk interval not too small
        // If autochunked, interval checks must wait until execute().
        if ((!rowAutochunked && dims[ROW].getChunkInterval() < slpp::SCALAPACK_MIN_BLOCK_SIZE) ||
            (!colAutochunked && dims[COL].getChunkInterval() < slpp::SCALAPACK_MIN_BLOCK_SIZE) ) {
            // the cache will thrash and performance will be unexplicably horrible to the user
            throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR41); // too small
        }


        // check: chunk interval not too large
        // If autochunked, interval checks must wait until execute().
        if ((!rowAutochunked && dims[ROW].getChunkInterval() > slpp::SCALAPACK_MAX_BLOCK_SIZE) ||
            (!colAutochunked && dims[COL].getChunkInterval() > slpp::SCALAPACK_MAX_BLOCK_SIZE) ) {
            // the cache will thrash and performance will be unexplicably horrible to the user
            throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR42); // too large
        }


        // TODO: the following does not work correctly.  postWarning() itself uses SCIDB_WARNING
        //       does not work correctly from a plugin, so seeking an example of how to do
        //       postWarning() from a plugin.
        if (false) {
            // broken code inside postWarning(SCIDB_WARNING()) faults and needs a different argument.
            for(size_t d = ROW; d <= COL; d++) {
                // If autochunked, interval checks must wait until execute().
                if(dims[d].isAutochunked())
                    continue;
                // If executing, just warn once (on the coordinator).
                if (!fromLogical && !query->isCoordinator())
                    continue;
                if(dims[d].getRawChunkInterval() != slpp::SCALAPACK_EFFICIENT_BLOCK_SIZE) {
                    query->postWarning(SCIDB_WARNING(DLA_WARNING4) << slpp::SCALAPACK_EFFICIENT_BLOCK_SIZE
                                       << slpp::SCALAPACK_EFFICIENT_BLOCK_SIZE);
                }
            }
        }

        // check: no overlap allowed
        //        TODO: improvement? if there's overlap, we may be able to ignore it,
        //              else invoke a common piece of code to remove it
        //              and in both cases emit a warning about non-optimality
        if (dims[ROW].getChunkOverlap()!=0 ||
            dims[COL].getChunkOverlap()!=0) {
            stringstream ss;
            ss<<"in matrix "<<iArray;
            throw (PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR40)
                   << ss.str());
        }
    }

    // check: the chunkSizes from the user must be identical (until auto-repart is working)
    const bool AUTO_REPART_WORKING = false ;  // #2032
    if( ! AUTO_REPART_WORKING ) {
        int64_t commonChunkSize = schemas[0]->getDimensions()[ROW].getRawChunkInterval();
        if (commonChunkSize == DimensionDesc::AUTOCHUNKED) {
            throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR10);
        }
        // TODO: remove these checks if #2023 is fixed and requiresRedimensionOrRepartition()
        // is functioning correctly
        for(size_t iArray=0; iArray < NUM_MATRICES; iArray++) {
            const Dimensions& dims = schemas[iArray]->getDimensions();
            // arbitrarily take first mentioned chunksize as the one for all to share
            if (dims[ROW].getRawChunkInterval() != commonChunkSize ||
                dims[COL].getRawChunkInterval() != commonChunkSize ) {
                throw PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR10);
                // TODO: name the matrix
            }
        }
    }

    // Chunksize matching critique
    //    This is not what we want it to be, but has to be until #2023 is fixed, which
    //    will allow the query planner and optimizer to repartition automatically, instead
    //    of putting the burden on the user.
    //
    //    (1) The required restriction to make ScaLAPACK work is that they are equal
    //    in both dimensions (square chunks) and equal for all matrices.
    //    (2) Legal values are in a range, expressed by SCALAPACK_{MIN,MAX}_BLOCK_SIZE
    //    (3) So what do we do if the chunksize is not optimal?  Can we go ahead and compute
    //    the answer if the matrix is below a size where it will really matter?
    //    Can we fix query->postWarning to warn in that case?
    //    (4) If the user gives inputs that match, and don't need a repart, we can proceed.
    //    (5) Else we will have to add reparts for the user [not implemented]
    //    Should we repart some of them to another size?  Or should we repart all of them
    //    to the optimial aize?  Unforunately, we don't have the information we would need
    //    to make an intelligent choice ...
    //    Due to the api of PhysicalOperator::requiresRedimensionOrRepartition() we can't
    //    tell which situation it is, because it still only functions on the first input only.
    //
    // TODO: after #2032 is fixed, have James fix note(4) above.
    //
}


// Check dimensions from logical inferSchema()
void checkScaLAPACKLogicalInputs(std::vector<ArrayDesc> const& schemas,
                                 std::shared_ptr<Query> query,
                                 size_t nMatsMin, size_t nMatsMax)
{
    std::vector<ArrayDesc const*> schemaPtrs(schemas.size());
    size_t i = 0;
    for (auto const& schema : schemas) {
        schemaPtrs[i++] = &schema;
    }

    checkScaLAPACKSchemasInternal(schemaPtrs, query, nMatsMin, nMatsMax,
                                  /*fromLogical:*/ true);
}


// Check dimensions from physical execute()
void checkScaLAPACKPhysicalInputs(std::vector< std::shared_ptr<Array> > const& inputs,
                                  std::shared_ptr<Query> query,
                                  size_t nMatsMin, size_t nMatsMax)
{
    std::vector<ArrayDesc const*> schemaPtrs(inputs.size());
    size_t i = 0;
    for (auto& arrayPtr : inputs) {
        schemaPtrs[i++] = &arrayPtr->getArrayDesc();
    }

    checkScaLAPACKSchemasInternal(schemaPtrs, query, nMatsMin, nMatsMax,
                                  /*fromLogical:*/ false);
}


// PGB: the requirement on names is that until such a time as we have syntax to disambiguate them by dimesion index
//      or other means, they must be distinct, else if stored, we will lose access to any but the first.
// JHM: in math, its annoying to have the names keep getting longer for the same thing.  So we only want to do
//      the appending of _? when required.

std::pair<string, string> ScaLAPACKDistinctDimensionNames(const string& a, const string& b)
{
    typedef std::pair<string, string> result_t ;
    result_t result;

    if (a != b) {
        // for algebra, avoid the renames when possible
        return result_t(a,b);
    } else {
        // fallback to appending _1 or _2 to both... would rather do it to just one,
        // but this is the only convention we have for conflicting in general.
        return result_t(a + "_1", b + "_2");
    }
}

void log4cxx_debug_dimensions(const std::string& prefix, const Dimensions& dims)
{
    if(logger->isDebugEnabled()) {
        for (size_t i=0; i<dims.size(); i++) {
            LOG4CXX_DEBUG(logger, prefix << " dims["<<i<<"] from " << dims[i].getStartMin() << " to " << dims[i].getEndMax());
        }
    }
}

} // namespace

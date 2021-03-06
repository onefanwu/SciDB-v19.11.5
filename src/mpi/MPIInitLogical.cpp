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

/**
 * @file MPIInitLogical.cpp
 *
 * @brief The logical operator interface for the MPI initialization.
 */

#include <query/Query.h>
#include <array/Array.h>
#include <query/LogicalOperator.h>

namespace scidb
{
using namespace std;

/**
 * @brief The operator: mpi_init().
 *
 * @par Synopsis:
 *   mpi_init()
 *
 * @par Summary:
 *   Initializes the MPI subsystem and cleans up any resources left around
 *   by the previous incarnation/process of this SciDB instance.
 *
 * @par Input:
 *   n/a
 *
 * @par Output array:
 *        <
 *   <br>   mpi_init_attribute: string
 *   <br> >
 *   <br> [
 *   <br>   mpi_init_dimension: start=end=chunk interval=0.
 *   <br> ]
 *
 * @par Examples:
 *   n/a
 *
 * @par Errors:
 *   n/a
 *
 * @par Notes:
 *   - Are you sure it is fine to have chunk interval = 0?
 *   - Needs to be checked by the author.
 *
 */
class MPIInitLogical: public LogicalOperator
{
    public:
    MPIInitLogical(const string& logicalName, const std::string& alias):
    LogicalOperator(logicalName, alias)
    {
    }
    ArrayDesc inferSchema(std::vector<ArrayDesc> schemas, std::shared_ptr< Query> query)
    {
        Attributes attributes;
        attributes.push_back(AttributeDesc("mpi_init_attribute", TID_STRING,  // DJG
                                           0, CompressorType::NONE));
        vector<DimensionDesc> dimensions(1);
        dimensions[0] = DimensionDesc(string("mpi_init_dimension"),
                                      Coordinate(0), Coordinate(0),
                                      uint32_t(0), uint32_t(0));
        return ArrayDesc("mpi_init_array",
                         attributes,
                         dimensions,
                         createDistribution(getSynthesizedDistType()),
                         query->getDefaultArrayResidency());
    }
};

REGISTER_LOGICAL_OPERATOR_FACTORY(MPIInitLogical, "mpi_init");

}  // namespace scidb

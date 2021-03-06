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

/*
 * LogicalReshape.cpp
 *
 *  Created on: Apr 20, 2010
 *      Author: Knizhnik
 */

#include <query/LogicalOperator.h>

#include <array/ArrayDistributionInterface.h>
#include <array/Dense1MChunkEstimator.h>
#include <system/Exceptions.h>

namespace scidb
{
using namespace std;

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("LogicalReshape"));

/**
 * @brief The operator: reshape().
 *
 * @par Synopsis:
 *   reshape( srcArray, schema )
 *
 * @par Summary:
 *   Produces a result array containing the same cells as, but a different shape from, the source array.
 *
 * @par Input:
 *   - srcArray: the source array with srcAttrs and srcDims.
 *   - schema: the desired schema, with the same attributes as srcAttrs, but with different size and/or number of dimensions.
 *     The restriction is that the product of the dimension sizes is equal to the number of cells in srcArray.
 *
 * @par Output array:
 *        <
 *   <br>   srcAttrs
 *   <br> >
 *   <br> [
 *   <br>   dimensions from the provided schema
 *   <br> ]
 *
 * @par Examples:
 *   n/a
 *
 * @par Errors:
 *   n/a
 *
 * @par Notes:
 *   n/a
 *
 */
class LogicalReshape: public LogicalOperator
{
public:
    LogicalReshape(const string& logicalName, const std::string& alias) :
            LogicalOperator(logicalName, alias)
    {
        // When schema parameter is a schema and not a reference, that schema is
        // allowed to refer to reserved attribute or dimension names.
        _properties.schemaReservedNames = true;

        // Use flatten() first if you want to reshape a dataframe.
        _properties.dataframe = false;
    }

    static PlistSpec const* makePlistSpec()
    {
        static PlistSpec argSpec {
            { "", // positionals
              RE(RE::LIST, {
                 RE(PP(PLACEHOLDER_INPUT)),
                 RE(PP(PLACEHOLDER_SCHEMA))
              })
            }
        };
        return &argSpec;
    }


    ArrayDesc inferSchema(std::vector<ArrayDesc> schemas, std::shared_ptr<Query> query)
    {
        assert(schemas.size() == 1);
        assert(_parameters.size() == 1);

        ArrayDesc dstArrayDesc =
                ((std::shared_ptr<OperatorParamSchema>&) _parameters[0])->getSchema();

        ArrayDesc const& srcArrayDesc = schemas[0];
        Attributes const& srcAttributes = srcArrayDesc.getAttributes();
        Dimensions const& srcDimensions = srcArrayDesc.getDimensions();
        Dimensions& dstDimensions = dstArrayDesc.getDimensions();

        // The user can refer to reserved names but not introduce new dimensions or attributes
        // with reserved names.
        if (dstArrayDesc.hasReservedNames() &&
            !srcArrayDesc.hasReservedNames()) {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INFER_SCHEMA,
                                   SCIDB_LE_RESERVED_NAMES_IN_SCHEMA)
                << getLogicalName();
        }

        if (dstArrayDesc.getName().empty()) {
            dstArrayDesc.setName(srcArrayDesc.getName() + "_reshape");
        }

        Coordinate srcArraySize = 1;
        for (size_t i = 0, n = srcDimensions.size(); i < n; i++) {
            srcArraySize *= srcDimensions[i].getLength();
            if (srcDimensions[i].getChunkOverlap() != 0)
                throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_RESHAPE_ERROR2);
        }
        Coordinate dstArraySize = 1;
        Dense1MChunkEstimator estimator;
        for (size_t i = 0, n = dstDimensions.size(); i < n; i++) {
            estimator.add(dstDimensions[i].getRawChunkInterval());
            dstArraySize *= dstDimensions[i].getLength();
            if (dstDimensions[i].getChunkOverlap() != 0)
                throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_RESHAPE_ERROR2);
        }
        if (srcArraySize != dstArraySize) {
            LOG4CXX_ERROR(logger, "LogicalReshape::inferSchema srcArraySize " << srcArraySize
                                  << " != dstArraySize " << dstArraySize);
            throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_RESHAPE_ERROR3);
        }
        estimator.apply(dstDimensions);

        return ArrayDesc(dstArrayDesc.getName(),
                         srcAttributes,
                         dstDimensions,
                         createDistribution(dtUndefined),
                         srcArrayDesc.getResidency());
    }
};

DECLARE_LOGICAL_OPERATOR_FACTORY(LogicalReshape, "reshape")

} //namespace

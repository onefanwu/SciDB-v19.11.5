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
 * LogicalRegrid.cpp
 *
 *  Created on: Jul 25, 2011
 *      Author: poliocough@gmail.com
 */

#include <query/LogicalOperator.h>

#include <array/ArrayDistributionInterface.h>
#include <query/AutochunkFixer.h>
#include <query/Expression.h>
#include <query/LogicalExpression.h>
#include <query/Query.h>
#include <query/UserQueryException.h>

namespace scidb {

using namespace std;

/**
 * @brief The operator: regrid().
 *
 * @par Synopsis:
 *   regrid( srcArray {, blockSize}+ {, AGGREGATE_CALL}+ {, chunkSize}* )
 *   <br> AGGREGATE_CALL := AGGREGATE_FUNC(inputAttr) [as resultName]
 *   <br> AGGREGATE_FUNC := approxdc | avg | count | max | min | sum | stdev | var | some_use_defined_aggregate_function
 *
 * @par Summary:
 *   Partitions the cells in the source array into blocks (with the given blockSize in each dimension), and for each block,
 *   calculates the required aggregates.
 *
 * @par Input:
 *   - srcArray: the source array with srcAttrs and srcDims.
 *   - A list of blockSizes, one for each dimension.
 *   - 1 or more aggregate calls.
 *     Each aggregate call has an AGGREGATE_FUNC, an inputAttr and a resultName.
 *     The default resultName is inputAttr followed by '_' and then AGGREGATE_FUNC.
 *     For instance, the default resultName for sum(sales) is 'sales_sum'.
 *     The count aggregate may take * as the input attribute, meaning to count all the items in the group including null items.
 *     The default resultName for count(*) is 'count'.
 *   - 0 or numDims chunk sizes.
 *     If no chunk size is given, the chunk sizes from the input dims will be used.
 *     If at least one chunk size is given, the number of chunk sizes must be equal to the number of dimensions,
 *     and the specified chunk sizes will be used.
 *
 * @par Output array:
 *        <
 *   <br>   the aggregate calls' resultNames
 *   <br> >
 *   <br> [
 *   <br>   srcDims, with reduced size in every dimension, and the provided chunk sizes if any.
 *   <br> ]
 *
 * @par Examples:
 *   n/a
 *
 * @par Errors:
 *   n/a
 *
 * @par Notes:
 *   - Regrid does not allow a block to span chunks. So for every dimension, the chunk interval needs to be a multiple of the block size.
 *
 */
class LogicalRegrid: public LogicalOperator
{
    AutochunkFixer _fixer;

public:
    LogicalRegrid(const std::string& logicalName, const std::string& alias)
        : LogicalOperator(logicalName, alias)
    {
        _properties.dataframe = false; // Dataframe inputs not supported.
    }

    static PlistSpec const* makePlistSpec()
    {
        static PlistSpec argSpec {
            { "", // positionals
              RE(RE::LIST, {
                 RE(PP(PLACEHOLDER_INPUT)),
                 RE(RE::PLUS, {
                    RE(PP(PLACEHOLDER_CONSTANT, TID_INT64))  // Block sizes
                 }),
                 RE(RE::PLUS, {
                    RE(PP(PLACEHOLDER_AGGREGATE_CALL))  // Aggregates
                 }),
                 RE(RE::STAR, {
                    RE(PP(PLACEHOLDER_CONSTANT, TID_INT64))  // Chunk sizes
                 })
              })
            }
        };
        return &argSpec;
    }

    std::string getInspectable() const override
    {
        return _fixer.str();
    }

    ArrayDesc inferSchema(std::vector<ArrayDesc> schemas, std::shared_ptr<Query> query)
    {
        assert(schemas.size() == 1);

        ArrayDesc const& inputDesc = schemas[0];
        size_t nDims = inputDesc.getDimensions().size();
        if (nDims >= _parameters.size()) {
            throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_WRONG_OPERATOR_ARGUMENTS_COUNT2)
                << getLogicalName();
        }

        // Grid count must match dimension count.
        // TODO: Grid vector should be an RE::GROUP of PP(PARAM_LOGICAL_EXPRESSION), then we could
        // just check its length.
        for (size_t i = 0; i < nDims; ++i) {
            if (_parameters[i]->getParamType() != PARAM_LOGICAL_EXPRESSION) {
                // An AQL-ish error, oh well.
                throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_WRONG_REGRID_REDIMENSION_SIZES_COUNT);
            }
        }

        // How many parameters are of each type.
        size_t numAggregateCalls = 0;
        size_t numChunkSizes = 0;
        for (size_t i = nDims, n = _parameters.size(); i < n; ++i)
        {
            if (_parameters[i]->getParamType() == PARAM_AGGREGATE_CALL)
            {
                if (numChunkSizes) {
                    throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_WRONG_OPERATOR_ARGUMENT)
                        << "expression" << (i+1) << getLogicalName() << "aggregate call";
                }
                ++numAggregateCalls;
            }
            else if (_parameters[i]->getParamType() == PARAM_LOGICAL_EXPRESSION)
            {
                // TODO: Document chunk size arguments.
                if (numAggregateCalls == 0) {
                    throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_WRONG_OPERATOR_ARGUMENT)
                        << "aggregate call" << (i+1) << getLogicalName() << "expression";
                }
                ++numChunkSizes;
            }
            else
            {
                stringstream ss;
                _parameters[i]->toString(ss, 0); // Ugh.
                throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_INVALID_OPERATOR_ARGUMENT)
                    << getLogicalName() << ss.str();
            }
        }
        if (numAggregateCalls == 0) {
            throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OPERATOR_NEEDS_AGGREGATES)
                << getLogicalName();
        }
        if (numChunkSizes && numChunkSizes != nDims) {
            throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_NUM_CHUNKSIZES_NOT_MATCH_NUM_DIMS)
                << getLogicalName();
        }

        // Generate the output dims.
        Dimensions outDims(nDims);
        for (size_t i = 0; i < nDims; i++)
        {
            int64_t blockSize = evaluate(
                ((std::shared_ptr<OperatorParamLogicalExpression>&)_parameters[i])->getExpression(),
                TID_INT64).getInt64();
            if (blockSize <= 0) {
                throw USER_QUERY_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_OP_REGRID_ERROR1,
                                           _parameters[i]->getParsingContext());
            }
            DimensionDesc const& srcDim = inputDesc.getDimensions()[i];
            int64_t chunkSize = srcDim.getRawChunkInterval();
            if (numChunkSizes) {
                size_t index = i + nDims + numAggregateCalls;
                chunkSize = evaluate(
                    ((std::shared_ptr<OperatorParamLogicalExpression>&)_parameters[index])->getExpression(),
                    TID_INT64).getInt64();
                if (chunkSize<=0) {
                    throw USER_EXCEPTION(SCIDB_SE_INFER_SCHEMA, SCIDB_LE_CHUNK_SIZE_MUST_BE_POSITIVE);
                }
            }
            outDims[i] = DimensionDesc( srcDim.getBaseName(),
                                        srcDim.getNamesAndAliases(),
                                        srcDim.getStartMin(),
                                        srcDim.getStartMin(),
                                        srcDim.getEndMax() == CoordinateBounds::getMax()
                                            ? CoordinateBounds::getMax()
                                            : srcDim.getStartMin() + (srcDim.getLength() + blockSize - 1)/blockSize - 1,
                                        srcDim.getEndMax() == CoordinateBounds::getMax()
                                            ? CoordinateBounds::getMax()
                                            : srcDim.getStartMin() + (srcDim.getLength() + blockSize - 1)/blockSize - 1,
                                        chunkSize,
                                        0  );
        }

        // Input and output dimensions are 1-to-1, so...
        _fixer.takeAllDimensions(inputDesc.getDimensions());

        ArrayDesc outSchema(inputDesc.getName(), Attributes(), outDims,
                            createDistribution(getSynthesizedDistType()),
                            query->getDefaultArrayResidency() );

        for (size_t i = nDims, j=nDims+numAggregateCalls; i<j; i++)
        {
            bool isInOrderAggregation = false;
            addAggregatedAttribute( (std::shared_ptr <OperatorParamAggregateCall> &) _parameters[i], inputDesc, outSchema,
                    isInOrderAggregation);
        }

        outSchema.addEmptyTagAttribute();

        return outSchema;
    }
};

DECLARE_LOGICAL_OPERATOR_FACTORY(LogicalRegrid, "regrid")


}  // namespace ops

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
 * PhysicalBuild.cpp
 *
 *  Created on: Apr 20, 2010
 *      Author: Knizhnik
 */

#include "query/PhysicalOperator.h"

#include "BuildArray.h"
#include "query/ops/input/InputArray.h"

using namespace std;

namespace scidb {

namespace {

log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.build"));
#define debug(e) LOG4CXX_DEBUG(logger, "PhysicalBuild: " << e)
#define trace(e) LOG4CXX_TRACE(logger, "PhysicalBuild: " << e)

class PhysicalBuild: public PhysicalOperator
{
    using super = PhysicalOperator;

public:
    PhysicalBuild(const string& logicalName,
                  const string& physicalName,
                  const Parameters& parameters,
                  const ArrayDesc& schema)
        : PhysicalOperator(logicalName, physicalName, parameters, schema)
        , _asArrayLiteral(false)
    {
        // Can't check keywords until setKeywordParamHook() below.  Must allow for "build(schema,
        // from: string)" here: not an error if only one parameter given.
        if (_parameters.size() == 3) {
            // "build(schema, expr, bool)"
            _asArrayLiteral =
                ((std::shared_ptr<OperatorParamPhysicalExpression>&)_parameters[2])->getExpression()->evaluate().getBool();
            _exprParam = _parameters[1];
        } else if (_parameters.size() == 2) {
            _exprParam = _parameters[1];
        } else {
            // "build(schema, from: string)" but haven't seen the keywords yet.
            // Could set _asArrayLiteral here if need be.
            SCIDB_ASSERT(_parameters.size() == 1);
        }
    }

protected:
    void setKeywordParamHook() override
    {
        Parameter p = findKeyword("from");
        if (p) {
            SCIDB_ASSERT(!_exprParam);
            _exprParam = p;
            _asArrayLiteral = true;
        } else {
            SCIDB_ASSERT(_exprParam);
        }
    }

private:
    std::shared_ptr<Expression> getExpr() const
    {
        return ((std::shared_ptr<OperatorParamPhysicalExpression>&)_exprParam)->getExpression();
    }

    DistType getOutDist() const
    {
        // Build honors its inherited dist type to the degree possible.
        // The exceptions are (1) for _asArrayLiteral (where the input is taken
        // from a string in the query that is only present on the coordinator)
        // and (2) for an inherited parameterized distribution (where we don't
        // have the parameters to construct it at this point in the evolution
        // of the system)
        DistType result;
        if (_asArrayLiteral) {
            result = dtLocalInstance;
            LOG4CXX_TRACE(logger, "PhysicalBuild::getOutDist: _arrayLiteral case");
        } else {
            // use whatever was inherited from its context when legitimate
            auto distType = getInheritedDistType();
            bool isReplicatedUnsafe = !isPartition(distType) && !(getExpr()->isInstIdentical());
            // Note: we do not need to consider whether there is only one instance.
            // When not inst-identical the build of a given chunk is run on only one instance.
            if (not isDataframe(distType) &&      // can't build dataframes
                !isReplicatedUnsafe &&         // if it is, replication has to be replaced with a partition
                isParameterless(distType)) {    // TODO: maybe see if we can build parametrized someday
                                                //       ideally we should be able to build eg. scalapack
                                                //       when receiving a ArrayDistrbution instead of DistType
                result = distType;

                LOG4CXX_TRACE(logger, "PhysicalBuild::getOutDist: inherited case");
            } else {
                // use the default partitioning
                result = defaultDistType();
                LOG4CXX_TRACE(logger, "PhysicalBuild::getOutDist: defaultDistTypeNotReplicated case");
            }
            SCIDB_ASSERT(isParameterless(result));
            _schema.setDistribution(createDistribution(result)); // update for inherited distributions
        }

        SCIDB_ASSERT(not isDataframe(result));
        LOG4CXX_TRACE(logger, "PhysicalBuild::getOutDist: returning " << distTypeToStr(result));
        return result;
    }
public:

    //
    // synthesis
    //
    DistType inferSynthesizedDistType(std::vector<DistType> const& /*inDist*/, size_t depth) const override
    {
        LOG4CXX_TRACE(logger, "PhysicalBuild::inferSynthesizedDistType("<<depth<<") returning getOutDist()"
                              << distTypeToStr(getOutDist()));
        return getOutDist();
    }

    virtual RedistributeContext getOutputDistribution(const std::vector<RedistributeContext> & inputDistributions,
                                                      const std::vector< ArrayDesc> & inputSchemas) const
    {
        SCIDB_ASSERT(inputDistributions.size() == 0);
        LOG4CXX_TRACE(logger, "PhysicalBuild::getOutputDistribution: getOutDist(): "
                               << distTypeToStr(getOutDist()));
        LOG4CXX_TRACE(logger, "PhysicalBuild::getOutputDistribution: _schema distType:"
                               << distTypeToStr(_schema.getDistribution()->getDistType()));
        SCIDB_ASSERT(getOutDist() == _schema.getDistribution()->getDistType());
        return RedistributeContext(_schema.getDistribution(),
                                   _schema.getResidency());
    }

    //
    // execute
    //
    std::shared_ptr<Array> execute(vector< std::shared_ptr<Array> >& inputArrays, std::shared_ptr<Query> query)
    {
        SCIDB_ASSERT(inputArrays.size() == 0);
        debug("execute: _schema  distribution:  " << _schema.getDistribution()
              << ", getOutDist(): " << distTypeToStr(getOutDist()));
        SCIDB_ASSERT(_schema.getDistribution()->getDistType() == getOutDist());

        std::shared_ptr<Array> result;

        auto expr = getExpr();

        if (_asArrayLiteral)
        {
            SCIDB_ASSERT(isLocal(_schema.getDistribution()->getDistType()));   // cf getOutputDistribution
            //We will produce this array only on coordinator
            if (query->isCoordinator())
            {
                //InputArray is very access-restrictive, but we're building it from a string - so it's small!
                //So why don't we just materialize the whole literal array:
                static const bool dontEnforceDataIntegrity = false;
                static const bool notInEmptyMode = false;
                static const int64_t maxCnvErrors(0);
                static const bool notParallelLoad = false;
                InputArray* ary = new InputArray(_schema,
                                                 _schema.getDistribution(), // might have to make this dtUndefined or something
                                                 "",
                                                 query,
                                                 shared_from_this(),
                                                 notInEmptyMode,
                                                 dontEnforceDataIntegrity,
                                                 maxCnvErrors,
                                                 notParallelLoad);
                std::shared_ptr<Array> input(ary);
                ary->openString(expr->evaluate().getString());
                std::shared_ptr<Array> materializedInput(new MemArray(input->getArrayDesc(),query));
                materializedInput->appendHorizontal(input);
                return materializedInput;
            }
            else
            {
                result = std::make_shared<MemArray>(_schema,query);
            }
        }
        else
        {
            result = std::make_shared<BuildArray>(query, _schema, expr);
        }

        debug("execute: returning array"
              << " with distribution: " << distTypeToStr(result->getArrayDesc().getDistribution()->getDistType())
              << " vs getOutDist(): " << getOutDist());
        SCIDB_ASSERT(result->getArrayDesc().getDistribution()->getDistType() == getOutDist());
        return result;
    }

private:
    bool _asArrayLiteral;
    Parameter _exprParam;
};

} // namespace

DECLARE_PHYSICAL_OPERATOR_FACTORY(PhysicalBuild, "build", "physicalBuild")

}  // namespace scidb

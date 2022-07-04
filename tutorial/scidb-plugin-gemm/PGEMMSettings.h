/*
 * Copyright (c) 2021-2022 ZJU Database Group, Zhejiang University. 
 * All rights reserved. 
 * 
 * This file is covered by the LICENSE.txt license file in the root directory.
 * 
 * @Description: 
 * 
 * @Author: Yifan Wu
 * @Date: 2022-05-18 14:17:14
 * @LastEditTime: 2022-05-18 14:39:09
 */
#ifndef PGEMM_SETTINGS
#define PGEMM_SETTINGS

#include <query/PhysicalOperator.h>
#include <query/AttributeComparator.h>
#include <query/Aggregate.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <query/Query.h>
#include <system/Config.h>

namespace scidb
{
namespace grouped_aggregate
{

using std::string;
using std::vector;
using std::shared_ptr;
using std::dynamic_pointer_cast;
using std::ostringstream;
using boost::algorithm::trim;
using boost::starts_with;
using boost::lexical_cast;
using boost::bad_lexical_cast;

// Logger for operator. static to prevent visibility of variable outside of file
static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.operators.grouped_aggregate"));

static const char* const KW_MAX_TABLE_SZ = "max_table_size";
static const char* const KW_INPUT_SORTED = "input_sorted";
static const char* const KW_NUM_HASH_BCKTS = "num_hash_buckets";
static const char* const KW_SPILL_CHUNK_SZ = "spill_chuck_size";
static const char* const KW_MERGE_CHUNK_SZ = "merge_chunk_size";
static const char* const KW_OUTPUT_CHUNK_SZ = "output_chunk_size";

typedef std::shared_ptr<OperatorParamLogicalExpression> ParamType_t ;

/**
 * Table sizing considerations:
 *
 * We'd like to see a load factor of 4 or less. A group occupies at least 32 bytes in the structure,
 * usually more - depending on how many values and states there are and also whether they are variable sized.
 * An empty bucket is an 8-byte pointer. So the ratio of group data / bucket overhead is at least 16.
 * With that in mind we just pick a few primes for the most commonly used memory limits.
 * We start with that many buckets and, at the moment, we don't bother rehashing:
 *
 * memory_limit_MB     max_groups    desired_buckets   nearest_prime   buckets_overhead_MB
 *             128        4194304            1048576         1048573                     8
 *             256        8388608            2097152         2097143                    16
 *             512       16777216            4194304         4194301                    32
 *           1,024       33554432            8388608         8388617                    64
 *           2,048       67108864           16777216        16777213                   128
 *           4,096      134217728           33554432        33554467                   256
 *           8,192      268435456           67108864        67108859                   512
 *          16,384      536870912          134217728       134217757                 1,024
 *          32,768     1073741824          268435456       268435459                 2,048
 *          65,536     2147483648          536870912       536870909                 4,096
 *         131,072     4294967296         1073741824      1073741827                 8,192
 *            more                                        2147483647                16,384
 */

static const size_t NUM_SIZES = 12;
static const size_t memLimits[NUM_SIZES]  = {    128,     256,     512,    1024,     2048,     4096,     8192,     16384,     32768,     65536,     131072,  ((size_t)-1) };
static const size_t tableSizes[NUM_SIZES] = {1048573, 2097143, 4194301, 8388617, 16777213, 33554467, 67108859, 134217757, 268435459, 536870909, 1073741827,    2147483647 };

static size_t chooseNumBuckets(size_t maxTableSize)
{
   for(size_t i =0; i<NUM_SIZES; ++i)
   {
       if(maxTableSize <= memLimits[i])
       {
           return tableSizes[i];
       }
   }
   return tableSizes[NUM_SIZES-1];
}

/*
 * Settings for the grouped_aggregate operator.
 */
class Settings
{
private:
    uint32_t _groupSize;
    uint32_t _numAggs;
    size_t _maxTableSize;
    bool   _maxTableSizeSet;
    size_t _spilloverChunkSize;
    bool   _spilloverChunkSizeSet;
    size_t _mergeChunkSize;
    bool   _mergeChunkSizeSet;
    size_t _outputChunkSize;
    bool   _outputChunkSizeSet;
    size_t const _numInstances;
    bool   _inputSorted;
    bool   _inputSortedSet;
    size_t _numHashBuckets;
    bool   _numHashBucketsSet;
    vector<int64_t> _groupIds;
    vector<bool>    _isGroupOnAttribute;
    vector<string> _groupNames;
    vector<TypeId> _groupTypes;
    vector<AttributeComparator> _groupComparators;
    vector<DoubleFloatOther> _groupDfo;
    vector<AggregatePtr> _aggregates;
    vector<TypeId> _inputAttributeTypes;
    vector<TypeId> _stateTypes;
    vector<TypeId> _outputAttributeTypes;
    vector<AttributeID> _inputAttributeIds;
    vector<string> _outputAttributeNames;

public:
    Settings(ArrayDesc const& inputSchema,
             vector< shared_ptr<OperatorParam> > const& operatorParameters,
             KeywordParameters const& kwParams,
             shared_ptr<Query>& query):
        _groupSize              (0),
        _numAggs                (0),
        _maxTableSize           ( Config::getInstance()->getOption<int>(CONFIG_MERGE_SORT_BUFFER)),
        _maxTableSizeSet        ( false ),
        _spilloverChunkSize     ( 100000 ),
        _spilloverChunkSizeSet  ( false ),
        _mergeChunkSize         ( 100000 ),
        _mergeChunkSizeSet      ( false ),
        _outputChunkSize        ( 100000 ),
        _outputChunkSizeSet     ( false ),
        _numInstances           ( query -> getInstancesCount() ),
        _inputSorted            ( false ),
        _inputSortedSet         ( false ),
        _numHashBuckets         ( 1048573 ),
        _numHashBucketsSet      ( false )
    {
        bool autoInputSorted = true;
        for(size_t i = 0; i<operatorParameters.size(); ++i)
        {
            shared_ptr<OperatorParam> param = operatorParameters[i];
            if (param->getParamType() == PARAM_AGGREGATE_CALL)
            {
                AttributeDesc inputAttDesc;
                string outputAttName;
                AggregatePtr agg = resolveAggregate((shared_ptr <OperatorParamAggregateCall> &) param, inputSchema.getAttributes(), &inputAttDesc, &outputAttName);
                _aggregates.push_back(agg);
                _stateTypes.push_back(agg->getStateType().typeId());
                _outputAttributeTypes.push_back(agg->getResultType().typeId());
                if(agg->isOrderSensitive())
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_AGGREGATION_ORDER_MISMATCH) << agg->getName();
                }
                _inputAttributeIds.push_back(inputAttDesc.getId());
                _outputAttributeNames.push_back(outputAttName);
                ++_numAggs;
            }
            else if(param->getParamType() == PARAM_ATTRIBUTE_REF)
            {
                autoInputSorted=false;
                shared_ptr<OperatorParamReference> ref = dynamic_pointer_cast<OperatorParamReference> (param);
                AttributeID attId = ref->getObjectNo();
                string groupName  = ref->getObjectName();
                TypeId groupType  = inputSchema.getAttributes().findattr(attId).getType();
                _groupIds.push_back(attId);
                _groupNames.push_back(groupName);
                _groupTypes.push_back(groupType);
                _groupComparators.push_back(AttributeComparator(groupType));
                _groupDfo.push_back(getDoubleFloatOther(groupType));
                _isGroupOnAttribute.push_back(true);
                ++_groupSize;
            }
            else if(param->getParamType() == PARAM_DIMENSION_REF)
            {
                shared_ptr<OperatorParamReference> ref = dynamic_pointer_cast<OperatorParamReference> (param);
                int64_t dimNo = ref->getObjectNo();
                if(static_cast<size_t>(dimNo) == inputSchema.getDimensions().size() - 1)
                {
                    autoInputSorted = false;
                }
                string dimName  = ref->getObjectName();
                _groupIds.push_back(dimNo);
                _groupNames.push_back(dimName);
                _groupTypes.push_back(TID_INT64);
                _groupComparators.push_back(AttributeComparator(TID_INT64));
                _groupDfo.push_back(getDoubleFloatOther(TID_INT64));
                _isGroupOnAttribute.push_back(false);
                ++_groupSize;
            }
        }
        setKeywordParamBool(kwParams, KW_INPUT_SORTED, _inputSortedSet, _inputSorted);
        setKeywordParamInt64(kwParams, KW_MAX_TABLE_SZ, _maxTableSizeSet, _maxTableSize);
        setKeywordParamInt64(kwParams, KW_NUM_HASH_BCKTS, _numHashBucketsSet, _numHashBuckets);
        setKeywordParamInt64(kwParams, KW_SPILL_CHUNK_SZ, _spilloverChunkSizeSet, _spilloverChunkSize);
        setKeywordParamInt64(kwParams, KW_MERGE_CHUNK_SZ, _mergeChunkSizeSet, _mergeChunkSize);
        setKeywordParamInt64(kwParams, KW_OUTPUT_CHUNK_SZ, _outputChunkSizeSet, _outputChunkSize);

        if(_numAggs == 0)
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Aggregate not specified";
        }
        if(_groupSize == 0)
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "No groups specified";
        }
        AttributeID scapeGoat = 0;
        ssize_t scapeGoatSize = 0;
        for(size_t i =0; i<_inputAttributeIds.size(); ++i)
        {
            AttributeID const& inAttId = _inputAttributeIds[i];
            if (inAttId != INVALID_ATTRIBUTE_ID)
            {
                ssize_t attSize = inputSchema.getAttributes().findattr(_inputAttributeIds[i]).getSize();
                if(attSize > 0 && (scapeGoatSize == 0 || attSize < scapeGoatSize))
                {
                    scapeGoat = inAttId;
                    scapeGoatSize = attSize;
                }
            }
        }
        for(size_t i =0; i<_inputAttributeIds.size(); ++i)
        {
            AttributeID& inAttId = _inputAttributeIds[i];
            if (inAttId == INVALID_ATTRIBUTE_ID)
            {
                inAttId = scapeGoat;
            }
            _inputAttributeTypes.push_back(inputSchema.getAttributes().findattr(_inputAttributeIds[i]).getType());
        }
        if(!_inputSortedSet)
        {
            _inputSorted = autoInputSorted;
        }
        if(!_numHashBucketsSet)
        {
            _numHashBuckets = chooseNumBuckets(_maxTableSize);
        }
        LOG4CXX_DEBUG(logger, "GAGG maxTableSize "<<_maxTableSize<<
                              " spillChunkSize " <<_spilloverChunkSize<<
                              " mergeChunkSize " <<_mergeChunkSize<<
                              " outputChunkSize "<<_outputChunkSize<<
                              " sorted "<<_inputSorted<<
                              " numHashBuckets "<<_numHashBuckets<<
                              " buckets set "<<_numHashBucketsSet);
    }

private:
    int64_t getParamContentInt64(Parameter& param)
    {
        size_t paramContent;

        if(param->getParamType() == PARAM_LOGICAL_EXPRESSION) {
            ParamType_t& paramExpr = reinterpret_cast<ParamType_t&>(param);
            paramContent = evaluate(paramExpr->getExpression(), TID_INT64).getInt64();
        } else {
            OperatorParamPhysicalExpression* exp =
                dynamic_cast<OperatorParamPhysicalExpression*>(param.get());
            SCIDB_ASSERT(exp != nullptr);
            paramContent = exp->getExpression()->evaluate().getInt64();

        }
        return paramContent;
    }

    void setKeywordParamInt64(KeywordParameters const& kwParams, const char* const kw, bool& alreadySet, size_t& valueToSet)
    {
        if (!alreadySet) {
            Parameter kwParam = getKeywordParam(kwParams, kw);
            if (kwParam) {
                valueToSet = getParamContentInt64(kwParam);
                alreadySet = true;
            } else {
                LOG4CXX_DEBUG(logger, "GA findKeyword null: " << kw);
            }
        }
    }

    bool getParamContentBool(Parameter& param)
    {
        bool paramContent;

        if(param->getParamType() == PARAM_LOGICAL_EXPRESSION) {
            ParamType_t& paramExpr = reinterpret_cast<ParamType_t&>(param);
            paramContent = evaluate(paramExpr->getExpression(), TID_BOOL).getBool();
        } else {
            OperatorParamPhysicalExpression* exp =
                dynamic_cast<OperatorParamPhysicalExpression*>(param.get());
            SCIDB_ASSERT(exp != nullptr);
            paramContent = exp->getExpression()->evaluate().getBool();
        }
        return paramContent;
    }

    void setKeywordParamBool(KeywordParameters const& kwParams, const char* const kw, bool& alreadySet, bool& valueToSet)
    {
        if (!alreadySet) {
            Parameter kwParam = getKeywordParam(kwParams, kw);
            if (kwParam) {
                bool paramContent = getParamContentBool(kwParam);
                valueToSet = paramContent;
                alreadySet = true;
            } else {
                LOG4CXX_DEBUG(logger, "GA findKeyword null: " << kw);
            }
        }
    }

    Parameter getKeywordParam(KeywordParameters const& kwp, const std::string& kw) const
    {
        auto const& kwPair = kwp.find(kw);
        return kwPair == kwp.end() ? Parameter() : kwPair->second;
    }

    bool checkSizeTParam(string const& param, string const& header, size_t& target, bool& setFlag)
    {
        string headerWithEq = header + "=";
        if(starts_with(param, headerWithEq))
        {
            if(setFlag)
            {
                ostringstream error;
                error<<"illegal attempt to set "<<header<<" multiple times";
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << error.str().c_str();
            }
            string paramContent = param.substr(headerWithEq.size());
            trim(paramContent);
            try
            {
                int64_t val = lexical_cast<int64_t>(paramContent);
                if(val<=0)
                {
                    ostringstream error;
                    error<<header<<" must be positive";
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << error.str().c_str();
                }
                target = val;
                setFlag = true;
                return true;
            }
            catch (bad_lexical_cast const& exn)
            {
                ostringstream error;
                error<<"could not parse "<<header;
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << error.str().c_str();
            }
        }
        return false;
    }

    bool checkBoolParam(string const& param, string const& header, bool& target, bool& setFlag)
    {
        string headerWithEq = header + "=";
        if(starts_with(param, headerWithEq))
        {
            if(setFlag)
            {
                ostringstream error;
                error<<"illegal attempt to set "<<header<<" multiple times";
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << error.str().c_str();
            }
            string paramContent = param.substr(headerWithEq.size());
            trim(paramContent);
            try
            {
                target= lexical_cast<bool>(paramContent);
                setFlag = true;
                return true;
            }
            catch (bad_lexical_cast const& exn)
            {
                ostringstream error;
                error<<"could not parse "<<header;
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << error.str().c_str();
            }
        }
        return false;
    }

public:
    uint32_t getGroupSize() const
    {
        return _groupSize;
    }

    vector<int64_t> const& getGroupIds() const
    {
        return _groupIds;
    }

    bool isGroupOnAttribute(size_t const groupNo) const
    {
        return _isGroupOnAttribute[groupNo];
    }

    vector<string> const& getGroupNames() const
    {
        return _groupNames;
    }

    vector<TypeId> const& getGroupTypes() const
    {
        return _groupTypes;
    }

    vector<TypeId> const& getInputAttributeTypes() const
    {
        return _inputAttributeTypes;
    }

    vector<AttributeID> const& getInputAttributeIds() const
    {
        return _inputAttributeIds;
    }

    vector<string> const& getOutputAttributeNames() const
    {
        return _outputAttributeNames;
    }

    vector<TypeId> const& getStateTypes() const
    {
        return _stateTypes;
    }

    vector<TypeId> const& getResultTypes() const
    {
        return _outputAttributeTypes;
    }

    AggregatePtr cloneAggregate() const
    {
        return _aggregates[0]->clone();
    }

    size_t getMaxTableSize() const
    {
        return _maxTableSize * 1024 * 1024;
    }

    size_t getSpilloverChunkSize() const
    {
        return _spilloverChunkSize;
    }

    size_t getMergeChunkSize() const
    {
        return _mergeChunkSize;
    }

    size_t getOutputChunkSize() const
    {
        return _outputChunkSize;
    }

    bool inputSorted() const
    {
        return _inputSorted;
    }

    size_t getNumHashBuckets() const
    {
        return _numHashBuckets;
    }

    bool numHashBucketsSet() const
    {
        return _numHashBucketsSet;
    }

    uint32_t getNumAggs() const
    {
        return _numAggs;
    }

    enum SchemaType
    {
        SPILL,
        MERGE,
        FINAL
    };

    ArrayDesc makeSchema(shared_ptr< Query> query, SchemaType const type, string const name = "") const
    {
        Attributes outputAttributes;
        if(type != FINAL)
        {
            outputAttributes.push_back( AttributeDesc("hash",   TID_UINT32,    0, CompressorType::NONE));
        }
        for (size_t j =0; j<_groupSize; ++j)
        {
            outputAttributes.push_back( AttributeDesc(_groupNames[j],  _groupTypes[j], 0, CompressorType::NONE));
        }
        for (size_t j =0; j<_numAggs; ++j)
        {
            outputAttributes.push_back( AttributeDesc(_outputAttributeNames[j],
                                                      type == SPILL ? _inputAttributeTypes[j] :
                                                      type == MERGE ? _stateTypes[j] :
                                                                      _outputAttributeTypes[j],
                                                      AttributeDesc::IS_NULLABLE, CompressorType::NONE));
        }
        outputAttributes.addEmptyTagAttribute();
        Dimensions outputDimensions;
        if(type == MERGE)
        {
            outputDimensions.push_back(DimensionDesc("dst_instance_id", 0, _numInstances-1, 1, 0));
            outputDimensions.push_back(DimensionDesc("src_instance_id", 0, _numInstances-1, 1, 0));
        }
        else if(type == FINAL)
        {
            outputDimensions.push_back(DimensionDesc("instance_id", 0,     _numInstances-1, 1, 0));
        }
        outputDimensions.push_back(DimensionDesc("value_no",  0, CoordinateBounds::getMax(),
                                                 type == SPILL ? _spilloverChunkSize :
                                                 type == MERGE ? _mergeChunkSize :
                                                                 _outputChunkSize, 0));

        return ArrayDesc(name.size() == 0 ? "grouped_agg_state" : name, outputAttributes, outputDimensions, createDistribution(dtUndefined), query->getDefaultArrayResidency());
    }

    /**
     * Determine if g is a valid group for aggregation. g must be getGroupSize() large.
     * @return true if g is a valid aggregation group, false otherwise.
     */
    inline bool groupValid(std::vector<Value const*> const& g) const
    {
        for(size_t i =0; i<_groupSize; ++i)
        {
            Value const& v = *(g[i]);
            if(v.isNull() || isNan(v, _groupDfo[i]))
            {
                return false;
            }
        }
        return true;
    }

    /**
     * Compare two groups, both must have getGroupSize() values and be groupValid().
     * @return true if g1 < g2, false otherwise.
     */
    inline bool groupLess(std::vector<Value const*> const& g1, std::vector<Value const*> const& g2) const
    {
        for(size_t i =0; i<_groupSize; ++i)
        {
            Value const& v1 = *(g1[i]);
            Value const& v2 = *(g2[i]);
            if(_groupComparators[i](v1, v2))
            {
                return true;
            }
            else if( v1 == v2 )
            {
                continue;
            }
            else
            {
                return false;
            }
        }
        return false;
    }

    /**
     * Compare two groups, both must have getGroupSize() values and be groupValid(). g1 is assumed c-style allocated.
     * @return true if g1 < g2, false otherwise.
     */
    inline bool groupLess(Value const* g1, std::vector<Value const*> const& g2) const
    {
        for(size_t i =0; i<_groupSize; ++i)
        {
            Value const& v1 = g1[i];
            Value const& v2 = *(g2[i]);
            if(_groupComparators[i](v1, v2))
            {
                return true;
            }
            else if( v1 == v2 )
            {
                continue;
            }
            else
            {
                return false;
            }
        }
        return false;
    }

    /**
     * Compare two groups, both must have getGroupSize() values and be groupValid().
     * @return true if g1 is equivalent g2, false otherwise.
     */
    inline bool groupEqual(std::vector<Value const*> g1, std::vector<Value const*> const& g2) const
    {
        for(size_t i =0; i<_groupSize; ++i)
        {
            Value const& v1 = *(g1[i]);
            Value const& v2 = *(g2[i]);
            if(v1.size() == v2.size()  &&  memcmp(v1.data(), v2.data(), v1.size()) == 0)
            {
                continue;
            }
            return false;
        }
        return true;
    }

    /**
     * Compare two groups, both must have getGroupSize() values and be groupValid(). g1 is assumed c-style allocated.
     * @return true if g1 is equivalent g2, false otherwise.
     */
    inline bool groupEqual(Value const* g1, std::vector<Value const*> const& g2) const
    {
        for(size_t i =0; i<_groupSize; ++i)
        {
            Value const& v1 = g1[i];
            Value const& v2 = *(g2[i]);
            if(v1.size() == v2.size()  &&  memcmp(v1.data(), v2.data(), v1.size()) == 0)
            {
                continue;
            }
            return false;
        }
        return true;
    }

    inline void aggInitState(Value* states)
    {
        for(size_t i=0; i<_numAggs; ++i)
        {
            _aggregates[i]->initializeState( states[i] );
        }
    }

    inline void aggAccumulate(Value* states, std::vector<Value const*> const& inputs)
    {
        for(size_t i=0; i<_numAggs; ++i)
        {
            _aggregates[i]->accumulateIfNeeded( states[i], *(inputs[i]));
        }
    }

    inline void aggMerge(Value* states, std::vector<Value const*> const& inStates)
    {
        for(size_t i=0; i<_numAggs; ++i)
        {
            _aggregates[i]->mergeIfNeeded( states[i], *(inStates[i]));
        }
    }

    inline void aggFinal(Value* results, Value const* inStates)
    {
        for(size_t i=0; i<_numAggs; ++i)
        {
            _aggregates[i]->finalResult( results[i], inStates[i]);
        }
    }
};

} } //namespaces

#endif //grouped_aggregate_settings

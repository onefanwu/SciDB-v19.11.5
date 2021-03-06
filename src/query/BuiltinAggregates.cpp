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
 * @file BuiltinAggregates.cpp
 *
 * @author poliocough@gmail.com
 */

#include <math.h>
#include <log4cxx/logger.h>
#include <MurmurHash/MurmurHash3.h>

#include <query/Aggregate.h>
#include <query/FunctionLibrary.h>
#include <query/Expression.h>
#include <query/TileFunctions.h>

using namespace std;

namespace scidb
{

// Logger for operator. static to prevent visibility of variable outside of file
static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.qproc.builtin_aggregates"));

namespace {
static constexpr bool TILE_MODE = true;
}

class ExpressionAggregate : public Aggregate
{
protected:
    Type _stateType;

    string _accumulateOp;
    Expression _accumulateExpression;
    std::shared_ptr<ExpressionContext> _accumulateContext;

    string _mergeOp;
    Expression _mergeExpression;
    std::shared_ptr<ExpressionContext> _mergeContext;

    bool _initByFirstValue;

    virtual bool isMergeable(Value const& srcState) const
    {
        if (! isStateInitialized(srcState)) {
            return false;
        }
        if (_initByFirstValue && srcState.getMissingReason()==1) {
            return false;
        }
        assert(!srcState.isNull());

        return true;
    }

    void accumulate(Value& dstState, Value const& srcValue)
    {
        assert(isStateInitialized(dstState));
        assert(isAccumulatable(srcValue));

        if (! isMergeable(dstState)) {
            dstState = srcValue;
        } else {
            (*_accumulateContext)[0] = dstState;
            (*_accumulateContext)[1] = srcValue;
            dstState = _accumulateExpression.evaluate(*_accumulateContext);
        }
    }

    void merge(Value& dstState, Value const& srcState)
    {
        assert(isStateInitialized(dstState));
        assert( isMergeable(srcState));

        (*_mergeContext)[0] = dstState;
        (*_mergeContext)[1] = srcState;
        dstState = _mergeExpression.evaluate(*_mergeContext);
    }

public:
    ExpressionAggregate(const string& name, Type const& aggregateType, Type const& stateType, Type const& resultType,
                        const string& accumulateOp, const string& mergeOp, bool initByFirstValue = false)
        : Aggregate(name, aggregateType, resultType)
        , _stateType(stateType)
        , _accumulateOp(accumulateOp)
        , _mergeOp(mergeOp)
        , _initByFirstValue(initByFirstValue)
    {
        vector<string> names(2);
        names[0] = "a";
        names[1] = "b";
        vector<TypeId> types(2);
        types[0] = stateType.typeId();
        types[1] = aggregateType.typeId();
        _accumulateExpression.compileExpressionString(accumulateOp, !TILE_MODE, names, types, stateType.typeId());
        _accumulateContext = std::shared_ptr<ExpressionContext>(new ExpressionContext(_accumulateExpression));

        types[1] = stateType.typeId();
        _mergeExpression.compileExpressionString(mergeOp, !TILE_MODE, names, types);
        _mergeContext = std::shared_ptr<ExpressionContext>(new ExpressionContext(_mergeExpression));
    }

    virtual AggregatePtr clone() const
    {
        return AggregatePtr(new ExpressionAggregate(
                                getName(),
                                getInputType(),
                                getStateType(),
                                getResultType(),
                                _accumulateOp,
                                _mergeOp,
                                _initByFirstValue));
    }

    AggregatePtr clone(Type const& aggregateType) const
    {
        return AggregatePtr(new ExpressionAggregate(
                                getName(),
                                aggregateType,
                                aggregateType,
                                (_resultType.typeId() == TID_VOID
                                    ? aggregateType
                                    : _resultType),
                                _accumulateOp,
                                _mergeOp,
                                _initByFirstValue));
    }

    bool ignoreNulls() const
    {
        return true;
    }

    Type getStateType() const
    {
        return _stateType;
    }

    void initializeState(Value& state)
    {
        state = Value(getStateType());
        state = TypeLibrary::getDefaultValue(getStateType().typeId());
        if (_initByFirstValue) {
            //We use missing code 1 because missing code 0 has special meaning to the aggregate framework.
            state.setNull(1);
        }
    }

    virtual void accumulateIfNeeded(Value& state, ConstRLEPayload const* tile)
    {
        if (! isStateInitialized(state)) {
            initializeState(state);
            assert(isStateInitialized(state));
        }

        ConstRLEPayload::iterator iter = tile->getIterator();
        while (!iter.end())
        {
            if (iter.isNull())
            {
                iter.toNextSegment();
            }
            else
            {
                if (_initByFirstValue && state.isNull()) {
                    iter.getItem(state);
                } else {
                    (*_accumulateContext)[0]=state;
                    iter.getItem((*_accumulateContext)[1]);
                    state = _accumulateExpression.evaluate(*_accumulateContext);
                }
                ++iter;
            }
        }
    }

    void finalResult(Value& dstValue, Value const& srcState)
    {
        if (! isMergeable(srcState))
        {
            //we didn't see any values, return null
            dstValue.setNull();
        }
        else {
            dstValue = srcState;
        }
    }
};

class CountAggregate : public CountingAggregate
{
private:
    bool _ignoreNulls;

protected:
    void accumulate(Value& dstState, Value const& srcValue) override
    {
        assert(isStateInitialized(dstState));
        assert(isAccumulatable(srcValue));

        (*dstState.getData<uint64_t>())++;
    }

    void merge(Value& dstState, Value const& srcState) override
    {
        assert(isStateInitialized(dstState));
        assert(isMergeable(srcState));

        (*dstState.getData<uint64_t>()) += srcState.getUint64();
    }

public:
    CountAggregate(Type const& aggregateType)
        : CountingAggregate("count", aggregateType, TypeLibrary::getType(TID_UINT64))
        , _ignoreNulls(aggregateType.typeId() != TID_VOID)
    {}

    AggregatePtr clone() const override
    {
        return AggregatePtr(new CountAggregate(getInputType()));
    }

    AggregatePtr clone(Type const& aggregateType) const override
    {
        return AggregatePtr(new CountAggregate(aggregateType));
    }

    Type getStateType() const override
    {
        return TypeLibrary::getType(TID_UINT64);
    }

    bool supportAsterisk() const override
    {
        return true;
    }

    void initializeState(Value& state) override
    {
        state = Value(getStateType());
        state = TypeLibrary::getDefaultValue(getStateType().typeId());
    }

    bool ignoreNulls() const override
    {
        return _ignoreNulls;
    }

    bool needsAccumulate() const override
    {
        //if _ignoreNulls is true, that means this is a "count(attribute_name)" aggregate.
        //this can be made more optimal but right now we are disabling the optimization to fix the bug
        //aggregate(build(<v:double null>[i=1:2,2,0],null), count(v)) --> 0
        return _ignoreNulls;
    }

    void accumulateIfNeeded(Value& state, ConstRLEPayload const* tile) override
    {
        if (! isStateInitialized(state)) {
            initializeState(state);
            assert(isStateInitialized(state));
        }

        if (!ignoreNulls()) {
            *state.getData<uint64_t>() += tile->count();
        } else {
            ConstRLEPayload::iterator iter = tile->getIterator();
            while (!iter.end())
            {
                if (!iter.isNull())
                {
                    *state.getData<uint64_t>() += iter.getSegLength();
                }
                iter.toNextSegment();
            }
        }
    }

    void overrideCount(Value& state, uint64_t newCount) override
    {
        *state.getData<uint64_t>() = newCount;
    }

    void finalResult(Value& dstValue, Value const& srcState) override
    {
        if (! isMergeable(srcState))
        {
            dstValue = TypeLibrary::getDefaultValue(getResultType().typeId());
        }
        else {
            dstValue = srcState;
        }
    }
};

/**
 * This class implements ApproxDC.
 *
 * @note As a historic note, in the 14.12 release and earlier, the code was in AnalyzeAggregate.h/cpp, originally added by egor.pugin@gmail.com.
 */
class ApproxDCAggregate : public Aggregate
{
private:
    static const size_t k       =  17;       //16 = 64K, 17 = 128K, ...
    static const size_t k_comp  =  64 - k;
    static const size_t m       =  1 << k;

protected:
    virtual void accumulate(Value& dstState, Value const& srcValue)
    {
        assert(isStateInitialized(dstState));
        assert(isAccumulatable(srcValue));

        const uint32_t seed = 0x5C1DB;
        uint64_t h[2];
        MurmurHash3_x64_128(srcValue.getData<uint8_t>(), srcValue.size(), seed, h);

        size_t  mask  = ((size_t) (m-1) << k_comp);
        size_t  j     = h[0] >> k_comp;
        uint8_t r     = static_cast<uint8_t>(getTrailingZeros(h[0] | mask) + 1);

        uint8_t *M = dstState.getData<uint8_t>();
        M[j] = max(M[j], r);
    }

    virtual void merge(Value& dstState, Value const& srcState)
    {
        assert(isStateInitialized(dstState));
        assert(isMergeable(srcState));

        uint8_t *dest = dstState.getData<uint8_t>();
        uint8_t const *src = srcState.getData<uint8_t>();

        for (size_t i = 0; i < m; i++)
        {
            dest[i] = max(dest[i], src[i]);
        }
    }

public:
    ApproxDCAggregate()
    : Aggregate("approxdc", TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_UINT64))
    {}

    virtual bool ignoreNulls() const
    {
        return true;
    }

    virtual Type getStateType() const
    {
        return TypeLibrary::getType(TID_BINARY);
    }

    virtual AggregatePtr clone() const
    {
        return AggregatePtr(new ApproxDCAggregate());
    }

    virtual AggregatePtr clone(Type const& aggregateType) const
    {
        return clone();
    }

    virtual void initializeState(Value& state)
    {
        memset(state.setSize<Value::IGNORE_DATA>(m), 0, m);
    }

    virtual void finalResult(Value& dstValue, Value const& srcState)
    {
        if (! isMergeable(srcState))
        {
            dstValue.setUint64(0);
            return;
        }

        double alpha_m;

        switch (m)
        {
        case 16:
            alpha_m = 0.673;
            break;
        case 32:
            alpha_m = 0.697;
            break;
        case 64:
            alpha_m = 0.709;
            break;
        default:
            alpha_m = 0.7213 / (1 + 1.079 / (double)m);
            break;
        }

        uint8_t const *M = srcState.getData<uint8_t>();

        double c = 0;
        for (size_t i = 0; i < m; i++)
        {
            c += 1 / pow(2., (double)M[i]);
        }
        double E = alpha_m * m * m / c;

        const double pow_2_64_minus_1 = static_cast<double>(SIZE_MAX);


        //corrections
        if (E <= (5 / 2. * m))
        {
            double V = 0;
            for (size_t i = 0; i < m; i++)
            {
                if (M[i] == 0) V++;
            }

            if (V > 0)
            {
                E = m * log(m / V);
            }
        }
        else if (E > (1 / 30. * pow_2_64_minus_1))
        {
            E = -pow_2_64_minus_1 * log(1 - E / pow_2_64_minus_1);
        }
        dstValue.setUint64(static_cast<uint64_t>(E));
    }
};

/**
 * This class implements MaxRepeatCount.
 */
class MaxRepeatCountAggregate : public Aggregate
{
    // Our goal is to come up with an approximate collision count using constant space.  Don't
    // really care if there are some false collisions.
    uint32_t const N_BUCKETS = 4096;
    uint32_t const HASH_SEED = 42; // Well known to be the most random integer.
    typedef uint8_t bucket_type;

protected:
    virtual void accumulate(Value& dstState, Value const& srcValue)
    {
        assert(isStateInitialized(dstState));
        assert(isAccumulatable(srcValue));

        // MurmurHash is probably overkill, oh well.  Also, not sure how
        // uniformly distributed the indeces will be once we mod by
        // N_BUCKETS, but since we want to compute a worst-case synthetic
        // interval, it's OK to err on the high side.

        uint32_t index;
        SCIDB_ASSERT(N_BUCKETS < std::numeric_limits<uint32_t>::max());
        MurmurHash3_x86_32(srcValue.data(), srcValue.size(), HASH_SEED, &index);
        index %= N_BUCKETS;
        SCIDB_ASSERT(index < N_BUCKETS);
        bucket_type* bucket = static_cast<bucket_type*>(dstState.data());
        if (bucket[index] < std::numeric_limits<bucket_type>::max()) {
            ++bucket[index];
        }
    }

    virtual void merge(Value& dstState, Value const& srcState)
    {
        assert(isStateInitialized(dstState));
        assert(isMergeable(srcState));

        bucket_type const* src = static_cast<bucket_type const*>(srcState.data());
        bucket_type* dest = static_cast<bucket_type*>(dstState.data());
        for (size_t i = 0; i < N_BUCKETS; ++i, ++src, ++dest) {
            if ((std::numeric_limits<bucket_type>::max() - *dest) > *src) {
                *dest += *src;
            } else {
                *dest = std::numeric_limits<bucket_type>::max(); // overflow
            }
        }
    }

public:
    MaxRepeatCountAggregate()
    : Aggregate("_maxrepcnt", TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_UINT64))
    {}

    virtual bool ignoreNulls() const
    {
        return true;
    }

    virtual Type getStateType() const
    {
        return TypeLibrary::getType(TID_BINARY);
    }

    virtual AggregatePtr clone() const
    {
        return AggregatePtr(new MaxRepeatCountAggregate());
    }

    virtual AggregatePtr clone(Type const& aggregateType) const
    {
        return clone();
    }

    virtual void initializeState(Value& state)
    {
        size_t stateBytes = N_BUCKETS * sizeof(bucket_type);
        ::memset(state.setSize<Value::IGNORE_DATA>(stateBytes), 0, stateBytes);
    }

    virtual void finalResult(Value& dstValue, Value const& srcState)
    {
        bucket_type theMax = 0;
        bucket_type* bucket = static_cast<bucket_type*>(srcState.data());
        for (size_t i = 0; i < N_BUCKETS; ++i, ++bucket) {
            if (*bucket > theMax) {
                theMax = *bucket;
            }
        }
        dstValue.setUint64(safe_static_cast<uint64_t>(theMax));
    }
};

AggregateLibrary::AggregateLibrary()
{
    /** SUM **/
    addAggregate(std::make_shared<ExpressionAggregate> ("sum", TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), "a+b", "a+b"));

    addAggregate(std::make_shared<BaseAggregate<AggSum, int8_t, int64_t> >("sum", TypeLibrary::getType(TID_INT8), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, int16_t, int64_t> >("sum", TypeLibrary::getType(TID_INT16), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, int32_t, int64_t> >("sum", TypeLibrary::getType(TID_INT32), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, int64_t, int64_t> >("sum", TypeLibrary::getType(TID_INT64), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, uint8_t, uint64_t> >("sum", TypeLibrary::getType(TID_UINT8), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, uint16_t, uint64_t> >("sum", TypeLibrary::getType(TID_UINT16), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, uint32_t, uint64_t> >("sum", TypeLibrary::getType(TID_UINT32), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, uint64_t, uint64_t> >("sum", TypeLibrary::getType(TID_UINT64), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, float, double> >("sum", TypeLibrary::getType(TID_FLOAT), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggSum, double, double> >("sum", TypeLibrary::getType(TID_DOUBLE), TypeLibrary::getType(TID_DOUBLE)));

    /** PROD **/
    addAggregate(std::make_shared<ExpressionAggregate>("prod", TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), "a*b", "a*b"));

    addAggregate(std::make_shared<BaseAggregate<AggProd, int8_t, int64_t> >("prod", TypeLibrary::getType(TID_INT8), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, int16_t, int64_t> >("prod", TypeLibrary::getType(TID_INT16), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, int32_t, int64_t> >("prod", TypeLibrary::getType(TID_INT32), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, int64_t, int64_t> >("prod", TypeLibrary::getType(TID_INT64), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, uint8_t, uint64_t> >("prod", TypeLibrary::getType(TID_UINT8), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, uint16_t, uint64_t> >("prod", TypeLibrary::getType(TID_UINT16), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, uint32_t, uint64_t> >("prod", TypeLibrary::getType(TID_UINT32), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, uint64_t, uint64_t> >("prod", TypeLibrary::getType(TID_UINT64), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, float, double> >("prod", TypeLibrary::getType(TID_FLOAT), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggProd, double, double> >("prod", TypeLibrary::getType(TID_DOUBLE), TypeLibrary::getType(TID_DOUBLE)));

    /** COUNT **/
    addAggregate(std::make_shared<CountAggregate>(TypeLibrary::getType(TID_VOID)));

    /** AVG **/
    addAggregate(std::make_shared<BaseAggregate<AggAvg, int8_t, double> >("avg", TypeLibrary::getType(TID_INT8), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, int16_t, double> >("avg", TypeLibrary::getType(TID_INT16), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, int32_t, double> >("avg", TypeLibrary::getType(TID_INT32), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, int64_t, double> >("avg", TypeLibrary::getType(TID_INT64), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, uint8_t, double> >("avg", TypeLibrary::getType(TID_UINT8), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, uint16_t, double> >("avg", TypeLibrary::getType(TID_UINT16), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, uint32_t, double> >("avg", TypeLibrary::getType(TID_UINT32), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, uint64_t, double> >("avg", TypeLibrary::getType(TID_UINT64), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, float, double> >("avg", TypeLibrary::getType(TID_FLOAT), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggAvg, double, double> >("avg", TypeLibrary::getType(TID_DOUBLE), TypeLibrary::getType(TID_DOUBLE)));

    /** MIN **/
    addAggregate(std::make_shared<ExpressionAggregate>("min", TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), "iif(a <b, a, b)", "iif(a <b, a, b)", true));

    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, int8_t, int8_t> >("min", TypeLibrary::getType(TID_INT8), TypeLibrary::getType(TID_INT8)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, int16_t, int16_t> >("min", TypeLibrary::getType(TID_INT16), TypeLibrary::getType(TID_INT16)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, int32_t, int32_t> >("min", TypeLibrary::getType(TID_INT32), TypeLibrary::getType(TID_INT32)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, int64_t, int64_t> >("min", TypeLibrary::getType(TID_INT64), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, uint8_t, uint8_t> >("min", TypeLibrary::getType(TID_UINT8), TypeLibrary::getType(TID_UINT8)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, uint16_t, uint16_t> >("min", TypeLibrary::getType(TID_UINT16), TypeLibrary::getType(TID_UINT16)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, uint32_t, uint32_t> >("min", TypeLibrary::getType(TID_UINT32), TypeLibrary::getType(TID_UINT32)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, uint64_t, uint64_t> >("min", TypeLibrary::getType(TID_UINT64), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, float, float> >("min", TypeLibrary::getType(TID_FLOAT), TypeLibrary::getType(TID_FLOAT)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMin, double, double> >("min", TypeLibrary::getType(TID_DOUBLE), TypeLibrary::getType(TID_DOUBLE)));

    /** MAX **/
    addAggregate(std::make_shared<ExpressionAggregate>("max", TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), TypeLibrary::getType(TID_VOID), "iif(a > b, a, b)", "iif(a > b, a, b)", true));

    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, int8_t, int8_t> >("max", TypeLibrary::getType(TID_INT8), TypeLibrary::getType(TID_INT8)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, int16_t, int16_t> >("max", TypeLibrary::getType(TID_INT16), TypeLibrary::getType(TID_INT16)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, int32_t, int32_t> >("max", TypeLibrary::getType(TID_INT32), TypeLibrary::getType(TID_INT32)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, int64_t, int64_t> >("max", TypeLibrary::getType(TID_INT64), TypeLibrary::getType(TID_INT64)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, uint8_t, uint8_t> >("max", TypeLibrary::getType(TID_UINT8), TypeLibrary::getType(TID_UINT8)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, uint16_t, uint16_t> >("max", TypeLibrary::getType(TID_UINT16), TypeLibrary::getType(TID_UINT16)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, uint32_t, uint32_t> >("max", TypeLibrary::getType(TID_UINT32), TypeLibrary::getType(TID_UINT32)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, uint64_t, uint64_t> >("max", TypeLibrary::getType(TID_UINT64), TypeLibrary::getType(TID_UINT64)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, float, float> >("max", TypeLibrary::getType(TID_FLOAT), TypeLibrary::getType(TID_FLOAT)));
    addAggregate(std::make_shared<BaseAggregateInitByFirst<AggMax, double, double> >("max", TypeLibrary::getType(TID_DOUBLE), TypeLibrary::getType(TID_DOUBLE)));

    /** VAR **/
    addAggregate(std::make_shared<BaseAggregate<AggVar, int8_t, double> >("var", TypeLibrary::getType(TID_INT8), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, int16_t, double> >("var", TypeLibrary::getType(TID_INT16), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, int32_t, double> >("var", TypeLibrary::getType(TID_INT32), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, int64_t, double> >("var", TypeLibrary::getType(TID_INT64), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, uint8_t, double> >("var", TypeLibrary::getType(TID_UINT8), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, uint16_t, double> >("var", TypeLibrary::getType(TID_UINT16), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, uint32_t, double> >("var", TypeLibrary::getType(TID_UINT32), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, uint64_t, double> >("var", TypeLibrary::getType(TID_UINT64), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, float, double> >("var", TypeLibrary::getType(TID_FLOAT), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggVar, double, double> >("var", TypeLibrary::getType(TID_DOUBLE), TypeLibrary::getType(TID_DOUBLE)));

    /** STDEV **/
    addAggregate(std::make_shared<BaseAggregate<AggStDev, int8_t, double> >("stdev", TypeLibrary::getType(TID_INT8), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, int16_t, double> >("stdev", TypeLibrary::getType(TID_INT16), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, int32_t, double> >("stdev", TypeLibrary::getType(TID_INT32), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, int64_t, double> >("stdev", TypeLibrary::getType(TID_INT64), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, uint8_t, double> >("stdev", TypeLibrary::getType(TID_UINT8), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, uint16_t, double> >("stdev", TypeLibrary::getType(TID_UINT16), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, uint32_t, double> >("stdev", TypeLibrary::getType(TID_UINT32), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, uint64_t, double> >("stdev", TypeLibrary::getType(TID_UINT64), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, float, double> >("stdev", TypeLibrary::getType(TID_FLOAT), TypeLibrary::getType(TID_DOUBLE)));
    addAggregate(std::make_shared<BaseAggregate<AggStDev, double, double> >("stdev", TypeLibrary::getType(TID_DOUBLE), TypeLibrary::getType(TID_DOUBLE)));

    /** Composite **/
    addAggregate(std::make_shared<CompositeAggregate>());

    /** ApproxDC **/
    addAggregate(std::make_shared<ApproxDCAggregate>());

    /** MaxRepeatCount **/
    addAggregate(std::make_shared<MaxRepeatCountAggregate>());
}

} // namespace scidb

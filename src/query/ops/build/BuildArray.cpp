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
 * BuildArray.cpp
 *
 *  Created on: Apr 11, 2010
 *      Author: Knizhnik
 */

#include "BuildArray.h"


#include <array/MemArray.h>

using namespace std;

namespace scidb {

    //
    // Build chunk iterator methods
    //
    int BuildChunkIterator::getMode() const
    {
        return _iterationMode;
    }

    Value const& BuildChunkIterator::getItem()
    {
        if (!_hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);

        if (getChunk().getAttributeDesc().isEmptyIndicator()) {
            return _trueValue;
        }
        const size_t nBindings =  _array._bindings.size();

        for (size_t i = 0; i < nBindings; i++) {
            switch (_array._bindings[i].kind) {
            case BindInfo::BI_COORDINATE:
            {
                _params[i].setInt64(_currPos[_array._bindings[i].resolvedId]);
            } break;
            case BindInfo::BI_VALUE:
            {
                _params[i] = _array._bindings[i].value;
            } break;
            default:
            SCIDB_ASSERT(false);
            }
        }
        if (_converter) {
            const Value* v = &_expression.evaluate(_params);
            _converter(&v, &_value, NULL);
        }
        else {
            _value = _expression.evaluate(_params);
        }

        if (!_nullable && _value.isNull())
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_ASSIGNING_NULL_TO_NON_NULLABLE);

        return _value;
    }

    void BuildChunkIterator::operator ++()
    {
        if (!_hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        for (int i = safe_static_cast<int>(_currPos.size()); --i >= 0;) {
            if (++_currPos[i] > _lastPos[i]) {
                _currPos[i] = _firstPos[i];
            } else {
                _hasCurrent = true;
                return;
            }
        }
        _hasCurrent = false;
    }

    bool BuildChunkIterator::end()
    {
        return !_hasCurrent;
    }

    bool BuildChunkIterator::isEmpty() const
    {
        return false;
    }

    Coordinates const& BuildChunkIterator::getPosition()
    {
        return _currPos;
    }

    bool BuildChunkIterator::setPosition(Coordinates const& pos)
    {
        for (size_t i = 0, n = _currPos.size(); i < n; i++) {
            if (pos[i] < _firstPos[i] || pos[i] > _lastPos[i]) {
                return _hasCurrent = false;
            }
        }
        _currPos = pos;
        return _hasCurrent = true;
    }

    void BuildChunkIterator::restart()
    {
        _currPos = _firstPos;
        _hasCurrent = true;
    }

    ConstChunk const& BuildChunkIterator::getChunk()
    {
        return *_chunk;
    }

    BuildChunkIterator::BuildChunkIterator(BuildArray& outputArray,
                                           ConstChunk const* aChunk,
                                           AttributeID attr, int mode)
    :   _iterationMode(mode),
        _array(outputArray),
        _firstPos(aChunk->getFirstPosition((mode & IGNORE_OVERLAPS) == 0)),
        _lastPos(aChunk->getLastPosition((mode & IGNORE_OVERLAPS) == 0)),
        _currPos(_firstPos.size()),
        _attrID(attr),
        _chunk(aChunk),
        _converter(outputArray._converter),
        _value(TypeLibrary::getType(aChunk->getAttributeDesc().getType())),
        _expression(*_array._expression),
        _params(_expression),
        _nullable(aChunk->getAttributeDesc().isNullable()),
        _query(Query::getValidQueryPtr(_array._query))
    {
        _trueValue.setBool(true);
        restart();
    }

    //
    // Build chunk methods
    //
    Array const& BuildChunk::getArray() const
    {
        return _array;
    }

    const ArrayDesc& BuildChunk::getArrayDesc() const
    {
        return _array._desc;
    }

    const AttributeDesc& BuildChunk::getAttributeDesc() const
    {
        return _array._desc.getAttributes().findattr(_attrID);
    }

        Coordinates const& BuildChunk::getFirstPosition(bool withOverlap) const
    {
        return withOverlap ? _firstPosWithOverlap : _firstPos;
    }

        Coordinates const& BuildChunk::getLastPosition(bool withOverlap) const
    {
        return withOverlap ? _lastPosWithOverlap : _lastPos;
    }

        std::shared_ptr<ConstChunkIterator> BuildChunk::getConstIterator(int iterationMode) const
    {
        return std::shared_ptr<ConstChunkIterator>(new BuildChunkIterator(_array, this, _attrID, iterationMode));
    }

    CompressorType BuildChunk::getCompressionMethod() const
    {
        return _array._desc.getAttributes().findattr(_attrID).getDefaultCompressionMethod();
    }

    void BuildChunk::setPosition(Coordinates const& pos)
    {
        _firstPos = pos;
        Dimensions const& dims = _array._desc.getDimensions();
        for (size_t i = 0, n = dims.size(); i < n; i++) {
            _firstPosWithOverlap[i] = _firstPos[i] - dims[i].getChunkOverlap();
            if (_firstPosWithOverlap[i] < dims[i].getStartMin()) {
                _firstPosWithOverlap[i] = dims[i].getStartMin();
            }
            _lastPos[i] = _firstPos[i] + dims[i].getChunkInterval() - 1;
            _lastPosWithOverlap[i] = _lastPos[i] + dims[i].getChunkOverlap();
            if (_lastPos[i] > dims[i].getEndMax()) {
                _lastPos[i] = dims[i].getEndMax();
            }
            if (_lastPosWithOverlap[i] > dims[i].getEndMax()) {
                _lastPosWithOverlap[i] = dims[i].getEndMax();
            }
        }
    }

    BuildChunk::BuildChunk(BuildArray& arr, AttributeID attr)
    : _array(arr),
      _firstPos(arr._desc.getDimensions().size()),
      _lastPos(_firstPos.size()),
      _firstPosWithOverlap(_firstPos.size()),
      _lastPosWithOverlap(_firstPos.size()),
      _attrID(attr)
    {
    }


    //
    // Build array iterator methods
    //

    void BuildArrayIterator::operator ++()
    {
        if (!_hasCurrent) {
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        }
        Query::getValidQueryPtr(_array._query);
        _nextChunk();
    }

    bool BuildArrayIterator::end()
    {
        return !_hasCurrent;
    }

    Coordinates const& BuildArrayIterator::getPosition()
    {
        if (!_hasCurrent) {
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        }
        return _currPos;
    }

    void BuildArrayIterator::_nextChunk()
    {
        _chunkInitialized = false;

        // search delegated to Distribution::getNextChunkCoord::getNextChunk() after sha 2be3e13a
        auto desc = _array.getArrayDesc();
        auto dist = desc.getDistribution();
        auto dims = desc.getDimensions();
        _hasCurrent = dist->getNextChunkCoord(_currPos, dims, _currPos, _array._nInstances, _array._instanceID);
    }

    bool BuildArrayIterator::setPosition(Coordinates const& pos)
    {
        Query::getValidQueryPtr(_array._query);
        for (size_t i = 0, n = _currPos.size(); i < n; i++) {
            if (pos[i] < _dims[i].getStartMin() || pos[i] > _dims[i].getEndMax()) {
                return _hasCurrent = false;
            }
        }
        _currPos = pos;
        _array._desc.getChunkPositionFor(_currPos);
        _chunkInitialized = false;
        if (isReplicated(_array.getArrayDesc().getDistribution()->getDistType())) {
            _hasCurrent = true;
        }
        else {
            _hasCurrent = _array._desc.getPrimaryInstanceId(_currPos, _array._nInstances) == _array._instanceID;
        }
        return _hasCurrent;
    }

    void BuildArrayIterator::restart()
    {
        _chunkInitialized = false;

        Query::getValidQueryPtr(_array._query);
        size_t nDims = _currPos.size();
        for (size_t i = 0; i < nDims; i++) {
            _currPos[i] = _dims[i].getStartMin();
        }

        auto desc = _array.getArrayDesc();
        auto dist = desc.getDistribution();
        auto dims = desc.getDimensions();
        _hasCurrent = dist->getFirstChunkCoord(_currPos, dims, _currPos, _array._nInstances, _array._instanceID);
    }

    ConstChunk const& BuildArrayIterator::getChunk()
    {
        if (!_hasCurrent) {
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        }
        Query::getValidQueryPtr(_array._query);
        if (!_chunkInitialized) {
            _chunk.setPosition(_currPos);
            _chunkInitialized = true;
        }
        return _chunk;
    }


    BuildArrayIterator::BuildArrayIterator(BuildArray& arr, AttributeID attrID)
        : ConstArrayIterator(arr),
          _array(arr),
          _chunk(arr, attrID),
          _dims(arr._desc.getDimensions()),
          _currPos(_dims.size())
    {
        restart();
    }


    //
    // Build array methods
    //

    ArrayDesc const& BuildArray::getArrayDesc() const
    {
        return _desc;
    }

    std::shared_ptr<ConstArrayIterator> BuildArray::getConstIteratorImpl(const AttributeDesc& attr) const
    {
        return std::shared_ptr<ConstArrayIterator>(new BuildArrayIterator(*(BuildArray*)this, attr.getId()));
    }

    BuildArray::BuildArray(std::shared_ptr<Query>& query,
                           ArrayDesc const& desc,
                           std::shared_ptr< Expression> expression)
    : _desc(desc),
      _expression(expression),
      _bindings(_expression->getBindings()),
      _converter(NULL),
      _nInstances(0),
      _instanceID(INVALID_INSTANCE)
    {
       SCIDB_ASSERT(query);
       _query=query;
       _nInstances = query->getInstancesCount();
       _instanceID = query->getInstanceID();
        for (size_t i = 0; i < _bindings.size(); i++) {
            if (_bindings[i].kind == BindInfo::BI_ATTRIBUTE)
                throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_OP_BUILD_ERROR1);
        }
        const auto& fda = _desc.getAttributes().firstDataAttribute();
        TypeId attrType = fda.getType();

        // Search converter for init value to attribute type
         TypeId exprType = expression->getType();
        if (attrType != exprType) {
            _converter = FunctionLibrary::getInstance()->findConverter(exprType, attrType);
        }
        SCIDB_ASSERT(_nInstances > 0 && _instanceID < _nInstances);
    }
}

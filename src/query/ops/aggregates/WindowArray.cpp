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
 * @file WindowArray.cpp
 *
 * @brief Window array implementation
 *
 * @author Konstantin Knizhnik <knizhnik@garret.ru>, poliocough@gmail.com,
           pbrown@paradigm4.com
 */
#include <log4cxx/logger.h>
#include <math.h>

#include "system/Config.h"
#include "system/SciDBConfigOptions.h"

#include "WindowArray.h"

#include <util/RegionCoordinatesIterator.h>

using namespace std;

// Logger for operator. static to prevent visibility outside file
static log4cxx::LoggerPtr windowLogger(log4cxx::Logger::getLogger("scidb.query.ops.window"));

namespace scidb
{

    // Materialized Window Chunk Iterator
    MaterializedWindowChunkIterator::MaterializedWindowChunkIterator(WindowArrayIterator const& arrayIterator, WindowChunk const& chunk, int mode)
   : _array(arrayIterator.array),
     _chunk(chunk),
     _aggregate(_array._aggregates[_chunk._attrID]->clone()),
     _defaultValue(_chunk.getAttributeDesc().getDefaultValue()),
     _iterationMode(mode),
     _nextValue(TypeLibrary::getType(_chunk.getAttributeDesc().getType())),
     _stateMap(_chunk._stateMap),
     _inputMap(_chunk._inputMap),
     _currPos(0),
     _nDims(chunk._nDims),
     _coords(_nDims),
     _useOLDWindowAlgorithm( arrayIterator.useOLDWindowAlgorithm() ),
     _windowStartCoords(_nDims),
     _windowEndCoords(_nDims),
     _stripeStartCoords(_nDims),
     _stripeEndCoords(_nDims)
    {
       restart();
    }

    /**
     * @see ConstChunkIterator::getMode()
     */
    int MaterializedWindowChunkIterator::getMode() const
    {
        return _iterationMode;
    }

    /**
     *   Calculate next value using materialized input chunk
     *
     *   Private function used when the input chunk's contents
     *  have been materialized. As we scan the cells in the input
     *  chunk, this method computes the window aggregate(s) for each
     *  non-empty cell in the input.
     *
     *   NOTE: This implementation replaces the original, which is
     *         in the calculateNextValueOLD() function. If we don't
     *         encounter any problems with the NEW algorithm in the
     *         field, we'll get rid of the OLD completely, and
     *         replace it with the NEW all the time.  function. If we don't
     *         encounter any problems with the NEW algorithm in the
     *         field, we'll get rid of the OLD completely, and
     *         replace it with the NEW all the time.
     */
    void MaterializedWindowChunkIterator::calculateNextValueNEW()
    {
        SCIDB_ASSERT ((_nDims == _chunk._array._dimensions.size()));
        SCIDB_ASSERT ((_nDims > 0 ));

        //
        // Where are we?
        Coordinates const& currPos = getPosition();

        //
        //  Figure out the start and end positions of the window.
        //
        //  We need to check that we're not stepping out over the limit of the
        // entire array's dimensional boundaries when the chunk is at the array edge.
        for (size_t i = 0; i < _nDims; i++)
        {
            _windowStartCoords[i] = std::max(currPos[i] - _chunk._array._window[i]._boundaries.first, _chunk._array._dimensions[i].getStartMin());
            _windowEndCoords[i]   = std::min(currPos[i] + _chunk._array._window[i]._boundaries.second, _chunk._array._dimensions[i].getEndMax());
        }

        //
        //  Set up the object we'll use to track the several start and
        // end stripes of the window.
        RegionCoordinatesIterator regionCoordinatesIterator( _windowStartCoords, _windowEndCoords );

        //
        //  The map<> we're using to hold the data is keyed on the logical
        // position within the chunk, not on the coordinates. So we need to
        // convert the Coordinates for each stripe to the logical position.
        uint64_t windowStartPos = _chunk.coord2pos( regionCoordinatesIterator.getPosition() );
        uint64_t windowEndPos   = _chunk.coord2pos( _windowEndCoords );

        //
        //  Data object used to hold the state of the aggregate as
        //  we process the values in the window.
        _state.setNull(0);

        //
        //  The _inputMap contains an entry for every non-NULL cell in the
        // input. So set iterators at the start and the end of the window
        // to ensure we're not going outside it's bounds.
        map<uint64_t, Value>::const_iterator windowIteratorCurr =
                                        _inputMap.lower_bound(windowStartPos);
        map<uint64_t, Value>::const_iterator windowIteratorEnd  =
                                        _inputMap.upper_bound(windowEndPos);

        //
        //  We process the data stripe at a time. So we need another iterator
        // to point to the end of the current "stripe".
        map<uint64_t, Value>::const_iterator endOfStripeIter;
        uint64_t stripeStartPos = 0, stripeEndPos = 0;

        while( windowIteratorCurr != windowIteratorEnd )
        {
            //
            //  Where are we in coordinates space?
            _chunk.pos2coord( windowIteratorCurr->first, _stripeStartCoords );

            //
            //  Find the "next" cell that's inside the window in coordinate
            // space ...
            regionCoordinatesIterator.advanceToAtLeast( _stripeStartCoords );

            //
            //  We're inside a window. Let's find the map<> position at the 'start'
            // of the stripe ...
            _stripeStartCoords         = regionCoordinatesIterator.getPosition();
            stripeStartPos             = _chunk.coord2pos(_stripeStartCoords);
            windowIteratorCurr         = _chunk._inputMap.lower_bound(stripeStartPos);

            //
            // Now find the coordinate at the 'end' of the stripe ...
            _stripeEndCoords           = _stripeStartCoords;
            _stripeEndCoords[_nDims-1] = _windowEndCoords[_nDims-1];
            stripeEndPos               = _chunk.coord2pos(_stripeEndCoords);
            endOfStripeIter            = _inputMap.upper_bound(stripeEndPos);

            //
            //  Check that we found anything from this stripe in the map<> at all, and
            // if not proceed to the next stripe...
            if ( windowIteratorCurr == endOfStripeIter )
                continue;

            //
            //  Now, just zip through the stripe of values in the map<>,
            // accumulating data into the aggregate as we go.
            while ( windowIteratorCurr != endOfStripeIter )
            {
               _aggregate->accumulateIfNeeded(_state, windowIteratorCurr->second);
               windowIteratorCurr++;
            }
        }

        //
        // Done. Finalize the aggregate and compute the result ...
        _aggregate->finalResult(_nextValue, _state);

    }
    /**
     *   Calculate next value using materialized input chunk
     *
     *   Private function used when the input chunk's contents
     *  have been materialized. As we scan the cells in the input
     *  chunk, this method computes the window aggregate(s) for each
     *  non-empty cell in the input.
     *
     *    NOTE: This is the original implementation to be replaced
     *          (we hope) if the NEW algorithm doesn't stir up any
     *          issues in the field.
     */
    void MaterializedWindowChunkIterator::calculateNextValueOLD()
    {
        Coordinates const& currPos = getPosition();
        Coordinates windowStart(_nDims);
        Coordinates windowEnd(_nDims);

        //
        //  We need to check that we're not stepping out over the limit of the
        // array's dimensional boundaries when the chunk is at the array edge.
        for (size_t i = 0; i < _nDims; i++)
        {
            windowStart[i] = std::max(currPos[i] - _chunk._array._window[i]._boundaries.first, _chunk._array._dimensions[i].getStartMin());
            windowEnd[i] = std::min(currPos[i] + _chunk._array._window[i]._boundaries.second, _chunk._array._dimensions[i].getEndMax());
        }

        uint64_t windowStartPos = _chunk.coord2pos(windowStart);
        uint64_t windowEndPos = _chunk.coord2pos(windowEnd);

        Value state;
        state.setNull(0);
        Coordinates probePos(_nDims);

        //
        //  The _inputMap contains an entry for every non-NULL cell in the input.
        // So set markers at the start and the end of the window.
        map<uint64_t, Value>::const_iterator windowIteratorCurr = _inputMap.lower_bound(windowStartPos);
        map<uint64_t, Value>::const_iterator windowIteratorEnd = _inputMap.upper_bound(windowEndPos);

        while(windowIteratorCurr != windowIteratorEnd)
        {
            uint64_t pos = windowIteratorCurr->first;
            _chunk.pos2coord(pos,probePos);

            //
            //  Sanity check. We should never go beyond the end of
            // the window as defined by the value of windowEndPos.
            SCIDB_ASSERT(( windowStartPos <= windowEndPos ));

            //
            //  Check to see if this cell is outside the window's box.
            for(size_t i=0; i<_nDims; i++)
            {
                if (probePos[i]<windowStart[i] || probePos[i]>windowEnd[i])
                {
                    //
                    //  We're now out of the window box. So calculate
                    // next probe position, reset windowIteratorCurr, and bounce
                    // along.
                    //
                    //  NOTE: This code is optimized for the 2D case.
                    //        I could calculate, depending on the
                    //        dimension that passed the disjunction
                    //        above, precisely by how much I should
                    //        step the probe. But to do so would
                    //        complicate this logic, and probably
                    //        won't help performance much.
                    SCIDB_ASSERT ((_nDims == _chunk._array._dimensions.size()));
                    SCIDB_ASSERT ((_nDims > 0 ));

                    do {
                        windowStartPos += _chunk.getStep();
                    } while ( windowStartPos <= pos );
                    windowIteratorCurr = _chunk._inputMap.lower_bound(windowStartPos);

                    goto nextIter;
                }
            }

            _aggregate->accumulateIfNeeded(state, windowIteratorCurr->second);
            windowIteratorCurr++;

            nextIter:;
        }
        _aggregate->finalResult(_nextValue, state);
    }

    void MaterializedWindowChunkIterator::calculateNextValue()
    {
        if ( useOLDWindowAlgorithm() )
        {
          calculateNextValueOLD();
        } else {
          calculateNextValueNEW();
        }
    }

    /**
     * @see ConstChunkIterator::getItem()
     */
    Value const& MaterializedWindowChunkIterator::getItem()
    {
        if (end())
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        return _nextValue;
    }

    /**
     * @see ConstIterator::getPosition()
     */
    Coordinates const& MaterializedWindowChunkIterator::getPosition()
    {
        if (end())
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        uint64_t pos = _iter->first;
        _chunk.pos2coord(pos,_coords);

        return _coords;
    }

    /**
     * @see ConstIterator::setPosition()
     */
    bool MaterializedWindowChunkIterator::setPosition(Coordinates const& pos)
    {
        uint64_t Pos = _chunk.coord2pos(pos);
        _iter = _stateMap.find(Pos);

        if(end())
        {
            return false;
        }

        SCIDB_ASSERT(( _iter != _stateMap.end()));

        calculateNextValue();

        if (_iterationMode & IGNORE_NULL_VALUES && _nextValue.isNull())
        {
            return false;
        } else if (_iterationMode & IGNORE_DEFAULT_VALUES &&
                   _nextValue == _defaultValue)
        {
            return false;
        }

        return true;
    }

    /**
     *  @see ConstChunkIterator::isEmpty()
     */
    bool MaterializedWindowChunkIterator::isEmpty() const
    {
        return false;
    }

    void MaterializedWindowChunkIterator::stepToNextValidValue()
    {
        while (!end()) {
            calculateNextValue();
            if ((_iterationMode & IGNORE_NULL_VALUES && _nextValue.isNull()) ||
                (_iterationMode & IGNORE_DEFAULT_VALUES && _nextValue == _defaultValue)) {
                ++ _iter;
            } else {
                break;
            }
        }
    }

    /**
     *  @see ConstIterator::restart()
     */
    void MaterializedWindowChunkIterator::restart()
    {
        _iter = _stateMap.begin();
        stepToNextValidValue();
    }

    /**
     *  @see ConstIterator::operator ++()
     */
    void MaterializedWindowChunkIterator::operator ++()
    {
        ++_iter;
        stepToNextValidValue();
    }

    /**
     *  @see ConstIterator::end()
     */
    bool MaterializedWindowChunkIterator::end()
    {
        return (_iter == _stateMap.end());
    }

    /**
     *  @see ConstChunkIterator::getChunk()
     */
    ConstChunk const& MaterializedWindowChunkIterator::getChunk()
    {
        return _chunk;
    }

    // Window Chunk Iterator
    WindowChunkIterator::WindowChunkIterator(WindowArrayIterator const& arrayIterator, WindowChunk const& chunk, int mode)
    : _array(arrayIterator.array),
      _chunk(chunk),
      _firstPos(_chunk.getFirstPosition(false)),
      _lastPos(_chunk.getLastPosition(false)),
      _currPos(_firstPos.size()),
      _attrID(_chunk._attrID),
      _aggregate(_array._aggregates[_attrID]->clone()),
      _defaultValue(_chunk.getAttributeDesc().getDefaultValue()),
      _iterationMode(mode),
      _nextValue(TypeLibrary::getType(_chunk.getAttributeDesc().getType()))
    {
        int iterMode = ConstChunkIterator::DEFAULT;
        if (_aggregate->ignoreNulls())
        {
            iterMode |= IGNORE_NULL_VALUES;
        }

        if ( _aggregate->ignoreZeroes() && attributeDefaultIsSameAsTypeDefault() )
        {
            iterMode |= IGNORE_DEFAULT_VALUES;
        }

        _inputIterator = arrayIterator.iterator->getChunk().getConstIterator(iterMode);

        if (_array.getArrayDesc().getEmptyBitmapAttribute())
        {
            const auto* eAttrId = _array.getPipe(0)->getArrayDesc().getEmptyBitmapAttribute();
            assert(eAttrId);
            _emptyTagArrayIterator = _array.getPipe(0)->getConstIterator(*eAttrId);

            if (! _emptyTagArrayIterator->setPosition(_firstPos))
            {
                throw SYSTEM_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_OPERATION_FAILED) << "setPosition";
            }
            _emptyTagIterator = _emptyTagArrayIterator->getChunk().getConstIterator();
        }

        restart();
    }

    /**
     *   Private function used to determine whether the input attribute's default is the type's default
     */
    bool WindowChunkIterator::attributeDefaultIsSameAsTypeDefault() const
    {
        const AttributeDesc& a = _array.getPipe(0)->getArrayDesc().getAttributes().findattr(_array._inputAttrIDs[_attrID]);

        return isDefaultFor(a.getDefaultValue(),a.getType());
    }

    /**
     * @see ConstChunkIterator::getMode()
     */
    int WindowChunkIterator::getMode() const
    {
        return _iterationMode;
    }

    /**
     *  Private function to calculate the next value without materializing input
     */
    Value& WindowChunkIterator::calculateNextValue()
    {
        size_t nDims = _currPos.size();
        Coordinates firstGridPos(nDims);
        Coordinates lastGridPos(nDims);
        Coordinates currGridPos(nDims);

        for (size_t i = 0; i < nDims; i++) {
            currGridPos[i] = firstGridPos[i] = std::max(_currPos[i] - _chunk._array._window[i]._boundaries.first,
                _chunk._array._dimensions[i].getStartMin());
            lastGridPos[i] = std::min(_currPos[i] + _chunk._array._window[i]._boundaries.second,
                _chunk._array._dimensions[i].getEndMax());
        }

        currGridPos[nDims-1] -= 1;
        Value state;
        state.setNull(0);

        while (true)
        {
            for (size_t i = nDims-1; ++currGridPos[i] > lastGridPos[i]; i--)
            {
                if (i == 0)
                {
                    _aggregate->finalResult(_nextValue, state);
                    return _nextValue;
                }
                currGridPos[i] = firstGridPos[i];
            }

            if (_inputIterator->setPosition(currGridPos))
            {
                Value const& v = _inputIterator->getItem();
                _aggregate->accumulateIfNeeded(state, v);
            }
        }
    }

    /**
     * @see ConstChunkIterator::getItem()
     */
    Value const& WindowChunkIterator::getItem()
    {
        if (!_hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        return _nextValue;
    }

    /**
     * @see ConstIterator::getPosition()
     */
    Coordinates const& WindowChunkIterator::getPosition()
    {
        return _currPos;
    }

    /**
     * @see ConstIterator::setPosition()
     */
    bool WindowChunkIterator::setPosition(Coordinates const& pos)
    {
        for (size_t i = 0, n = _currPos.size(); i < n; i++)
        {
            if (pos[i] < _firstPos[i] || pos[i] > _lastPos[i])
            {
                return false;
            }
        }
        _currPos = pos;

        if (_emptyTagIterator.get() && !_emptyTagIterator->setPosition(_currPos))
        {
            return false;
        }

        calculateNextValue();
        if (_iterationMode & IGNORE_NULL_VALUES && _nextValue.isNull())
        {
            return false;
        }
        if (_iterationMode & IGNORE_DEFAULT_VALUES && _nextValue == _defaultValue)
        {
            return false;
        }

        return true;
    }

    /**
     *  @see ConstChunkIterator::isEmpty()
     */
    bool WindowChunkIterator::isEmpty() const
    {
        return false;
    }

    /**
     *  @see ConstIterator::restart()
     */
    void WindowChunkIterator::restart()
    {
        if (setPosition(_firstPos))
        {
            _hasCurrent = true;
            return;
        }
        ++(*this);
    }

    /**
     *  @see ConstIterator::operator ++()
     */
    void WindowChunkIterator::operator ++()
    {
        bool done = false;
        while (!done)
        {
            size_t nDims = _firstPos.size();
            for (size_t i = nDims-1; ++_currPos[i] > _lastPos[i]; i--)
            {
                if (i == 0)
                {
                    _hasCurrent = false;
                    return;
                }
                _currPos[i] = _firstPos[i];
            }

            if (_emptyTagIterator.get() && !_emptyTagIterator->setPosition(_currPos))
            {
                continue;
            }

            calculateNextValue();
            if (_iterationMode & IGNORE_NULL_VALUES && _nextValue.isNull())
            {
                continue;
            }
            if (_iterationMode & IGNORE_DEFAULT_VALUES && _nextValue == _defaultValue)
            {
                continue;
            }

            done = true;
            _hasCurrent = true;
        }
    }

    /**
     *  @see ConstIterator::end()
     */
    bool WindowChunkIterator::end()
    {
        return !_hasCurrent;
    }

    /**
     *  @see ConstChunkIterator::getChunk()
     */
    ConstChunk const& WindowChunkIterator::getChunk()
    {
        return _chunk;
    }

    //Window Chunk
    WindowChunk::WindowChunk(WindowArray const& arr, AttributeID attr)
    : _array(arr),
      _arrayIterator(NULL),
      _nDims(arr._desc.getDimensions().size()),
      _firstPos(_nDims),
      _lastPos(_nDims),
      _attrID(attr),
      _materialized(false),
      _mapper()
    {
        if (arr._desc.getEmptyBitmapAttribute() == 0 || attr!=arr._desc.getEmptyBitmapAttribute()->getId())
        {
            _aggregate = arr._aggregates[_attrID]->clone();
        }
    }

    /**
     * @see ConstChunk::getArray()
     */
    Array const& WindowChunk::getArray() const
    {
        return _array;
    }

    inline uint64_t WindowChunk::getStep() const
    {
        if (false == isMaterialized()) {
            throw USER_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_OP_WINDOW_ERROR6);
        }
        SCIDB_ASSERT(_mapper);
        return _mapper->getChunkInterval(_nDims-1);
    }

    /**
     * @see ConstChunk::getArrayDesc()
     */
    const ArrayDesc& WindowChunk::getArrayDesc() const
    {
        return _array._desc;
    }

    /**
     * @see ConstChunk::getAttributeDesc()
     */
    const AttributeDesc& WindowChunk::getAttributeDesc() const
    {
        return _array._desc.getAttributes().findattr(_attrID);
    }

    /**
     * @see ConstChunk::getFirstPosition()
     */
    Coordinates const& WindowChunk::getFirstPosition(bool withOverlap) const
    {
        return _firstPos;
    }

    /**
     * @see ConstChunk::getLastPosition()
     */
    Coordinates const& WindowChunk::getLastPosition(bool withOverlap) const
    {
        return _lastPos;
    }

    /**
     * @see ConstChunk::getConstIterator()
     */
    std::shared_ptr<ConstChunkIterator> WindowChunk::getConstIterator(int iterationMode) const
    {
        SCIDB_ASSERT(( NULL != _arrayIterator ));
        ConstChunk const& inputChunk = _arrayIterator->iterator->getChunk();
        if (_array.getArrayDesc().getEmptyBitmapAttribute() && _attrID == _array.getArrayDesc().getEmptyBitmapAttribute()->getId())
        {
            return inputChunk.getConstIterator((iterationMode & ~ChunkIterator::INTENDED_TILE_MODE) | ChunkIterator::IGNORE_OVERLAPS);
        }

        if (isMaterialized())
        {
            return std::shared_ptr<ConstChunkIterator>(new MaterializedWindowChunkIterator(*_arrayIterator, *this, iterationMode));
        }

        return std::shared_ptr<ConstChunkIterator>(new WindowChunkIterator(*_arrayIterator, *this, iterationMode));
    }

    /**
     * @see ConstChunk::getCompressionMethod()
     */
    CompressorType WindowChunk::getCompressionMethod() const
    {
        return _array._desc.getAttributes().findattr(_attrID).getDefaultCompressionMethod();
    }

    /**
     * Private function to compute the position in the chunk from coordinates
     */
    inline uint64_t WindowChunk::coord2pos(Coordinates const& coord) const
    {
        SCIDB_ASSERT(_materialized);
        position_t pos = _mapper->coord2pos(coord);
        SCIDB_ASSERT(pos >= 0);
        return pos;
    }

    /**
     * Private function to compute the position in the chunk from coordinates
     */
    inline void WindowChunk::pos2coord(uint64_t pos, Coordinates& coord) const
    {
        SCIDB_ASSERT(_materialized);
        _mapper->pos2coord(pos, coord);
    }

    /**
     *  Private function that returns true iff the value passed in needed by aggregate
     */
    inline bool WindowChunk::valueIsNeededForAggregate ( const Value & val, const ConstChunk & inputChunk ) const
    {
        return (!((val.isNull() && _aggregate->ignoreNulls()) ||
                  (isDefaultFor(val,inputChunk.getAttributeDesc().getType()) && _aggregate->ignoreZeroes())));
    }

    /**
     *
     *   Private function used to process the input chunk's contents
     *  and to build the _inputState and _inputMap trees.
     */
    void WindowChunk::materialize()
    {
        _materialized = true;
        _stateMap.clear();
        _inputMap.clear();

        int64_t nInputElements = 0;
        int64_t nResultElements = 0;

        Coordinates oLastPos;
        {
            ConstChunk const& chunk = _arrayIterator->iterator->getChunk();

            //
            // Initialize the coordinate mapper
            _mapper = std::shared_ptr<CoordinatesMapper> (new CoordinatesMapper(chunk));

            //
            // Get the number of logical elements in the chunk, excluding the
            // overlapping region
            Coordinates const& firstPos = chunk.getFirstPosition(false);
            Coordinates const& lastPos =  chunk.getLastPosition(false);

            //
            // FIXME: Would be very useful to have an alternative to the
            //  std::map<> used for _stateMap and _inputMap here because
            //  values being read from the input chunkIter are usually going
            //  to be organized in coord2pos(currPos) order. As this is
            //  implemented, we're paying an O ( N. log ( N ) ) cost to
            //  build each of these structures, when (as the inputs are
            //  usually ordered) we could just do it in O ( N ). A vector,
            //  or an n-D array (if it's dense enough) for example.
            //
            // FIXME: Having two of these state data structures is wasteful.
            //  Better to use a single data structure.
            //
            std::shared_ptr<ConstChunkIterator> chunkIter = chunk.getConstIterator(ConstChunkIterator::DEFAULT);
            while(!chunkIter->end())
            {
                Coordinates const& currPos = chunkIter->getPosition();
                Value const& currVal = chunkIter->getItem();
                uint64_t pos = _mapper->coord2pos(currPos);

                bool insideOverlap=true;
                for (size_t i=0; i<_nDims; i++)
                {
                    if(currPos[i]<firstPos[i] || currPos[i]>lastPos[i])
                    {
                        insideOverlap=false;
                        break;
                    }
                }

                if (insideOverlap)
                {
                    //
                    //  Every non-empty input cell within "core" generates output.
                    _stateMap[pos] = true;
                    nResultElements +=1;
                }

                //
                // If the agg ignores nulls, or if the value we have is attribute's
                // default and we've been told to ignore defaults, then we can
                // filter the Value out at this stage, before we put it into the
                // _inputMap[].
                if (valueIsNeededForAggregate( currVal, chunk ))
                {
                    _inputMap[pos]=currVal;
                    nInputElements +=1;
                }
                ++(*chunkIter);
            }
        }
    }

    /**
     *  Private function to setPosition in a WindowChunk
     */
    void WindowChunk::setPosition(WindowArrayIterator const* iterator, Coordinates const& pos)
    {
        _arrayIterator = iterator;
        _firstPos = pos;
        Dimensions const& dims = _array._desc.getDimensions();

        for (size_t i = 0, n = dims.size(); i < n; i++) {
            _lastPos[i] = _firstPos[i] + dims[i].getChunkInterval() - 1;
            if (_lastPos[i] > dims[i].getEndMax())
            {
                _lastPos[i] = dims[i].getEndMax();
            }
        }
        _materialized = false;
        if (_aggregate.get() == 0)
        {
            return;
        }

        if (_array._desc.getEmptyBitmapAttribute())
        {
            //
            //  At this point, we need to make a 1-bit decision about how we
            // will compute the window(...) result. Do we materialize all of
            // the cells in the inputChunk into a coords -> value map before
            // we compute the per-cellwindow aggregate, or do we probe the
            // inputChunk's iterator on demand?
            //
            //  The way we figure this out is to (a) compute the total size of
            // the materialization by taking at the size of the inputChunk
            // (number of elements) and calculating how big the in-memory map
            // data structure would be. Then (b) we compare this size to a
            // (configurable) threshhold, which is a constant (configurable)
            // multiplier of the CONFIG_MEM_ARRAY_THRESHHOLD.
            //
            //  Although using size estimations appears to be a significant
            // improvement over using a simple estimate of the sparsity of the
            // input, there are several problems with the mechanism
            // implemented here.
            //
            //  1. The calculation of the inputChunk.count() can involve a
            //  complete iteration through the inputChunk's values, which
            //  means that we might be computing a subquery's results
            //  for the operator twice.
            //
            //  Consider: window ( filter ( A, expr ), ... ).
            //
            //  FIXME: Need to support some kind of cheap and reasonably
            //         accurate estimate of the size of an operator's
            //         output chunk, given the size(s) of its input chunk(s).
            //
            //  2.  The real thing we are trying to minimize here is the
            //   expense of all of the of probe calls to into the inputChunk.
            //   The total number of probes calls is a product of the input
            //   size, the number of cells, and the chunk's sparsity. Probing
            //   (or ideally scanning) a materialized inputChunk is usually
            //   a lot less expensive than probing an unmaterialized
            //   inputChunk.
            //
            //    BUT the constant overhead to materialize the inputChunk is
            //   quite high. So we would probably benefit from a smarter way
            //   to choose between the two algorithms that incorporated the
            //   fixed cost.
            //
            //  3.  As input chunk is often going to be ordered, the cost of
            //   materializing the inputChunk by using a map<> is higher than
            //   it needs to be. See detailed note in the materialize()
            //   function.
            //
            if (_arrayIterator->getMethod() == WindowArray::MATERIALIZE)
            {
                materialize();
            } else if (_arrayIterator->getMethod() != WindowArray::PROBE)
            {
                //
                //  The operator has expressed no preference about the
                // algorithm. So we figure out whther materializing the source
                // involves too much memory.
                ConstChunk const& inputChunk = _arrayIterator->iterator->getChunk();
                size_t varSize = getAttributeDesc().getVarSize();

                if (varSize <= 8)
                {
                    varSize=0;
                } else if (varSize ==0)
                {
                    varSize=Config::getInstance()->getOption<int>(CONFIG_STRING_SIZE_ESTIMATION);
                }

                size_t materializedChunkSize = inputChunk.count() *
                                               ( sizeof( _Rb_tree_node_base ) +
                                               sizeof ( scidb::Value ) +
                                               sizeof ( position_t ) +
                                               varSize );

                size_t maxMaterializedChunkSize = (
                    Config::getInstance()->getOption<int>(CONFIG_MATERIALIZED_WINDOW_THRESHOLD)
                    * MiB);   // All config.ini params are in Mebibytes.

                if ( materializedChunkSize <= maxMaterializedChunkSize )
                {
                    materialize();
                } else {

                    LOG4CXX_TRACE ( windowLogger,
                                    "WindowChunk::setPosition(..) - NOT MATERIALIZING \n"
                                    << "\t materializedChunkSize = " << materializedChunkSize
                                    << " as inputChunk.count() = " << inputChunk.count() << " and varSize = " << varSize
                                    << " and maxMaterializedChunkSize = " << maxMaterializedChunkSize
                                  );
                }
            }
        }
    }

    // Window Array Iterator
    WindowArrayIterator::WindowArrayIterator(WindowArray const& arr,
                                             const AttributeDesc& attrID,
                                             const AttributeDesc& input,
                                             string const& method)
        : ConstArrayIterator(arr),
          array(arr),
          iterator(arr.getPipe(0)->getConstIterator(input)),
          currPos(arr._dimensions.size()),
          chunk(arr, attrID.getId()),
          _method(method),
          _useOLDWindowAlgorithm(Config::getInstance()->getOption<bool>(CONFIG_OLD_OR_NEW_WINDOW))
    {
        restart();
    }

    /**
     *  @see ConstIterator::operator ++()
     */
    void WindowArrayIterator::operator ++()
    {
        if (!hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        chunkInitialized = false;
        ++(*iterator);
        hasCurrent = !iterator->end();
        if (hasCurrent)
        {
            currPos = iterator->getPosition();
        }
    }

    /**
     *  @see ConstIterator::end()
     */
    bool WindowArrayIterator::end()
    {
        return !hasCurrent;
    }

    /**
     *  @see ConstIterator::getPosition()
     */
    Coordinates const& WindowArrayIterator::getPosition()
    {
        if (!hasCurrent)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        return currPos;
    }

    /**
     *  @see ConstIterator::setPosition()
     */
    bool WindowArrayIterator::setPosition(Coordinates const& pos)
    {
        chunkInitialized = false;
        if (!iterator->setPosition(pos))
        {
            return hasCurrent = false;
        }
        currPos = iterator->getPosition();
        return hasCurrent = true;
    }

    /**
     *  @see ConstIterator::restart()
     */
    void WindowArrayIterator::restart()
    {
        chunkInitialized = false;
        iterator->restart();
        hasCurrent = !iterator->end();
        if (hasCurrent)
        {
            currPos = iterator->getPosition();
        }
    }

    /**
     *  @see ConstArrayIterator::getChunk()
     */
    ConstChunk const& WindowArrayIterator::getChunk()
    {
        if (!chunkInitialized)
        {
            assert(iterator->getPosition() == currPos);
            chunk.setPosition(this, currPos);
            chunkInitialized = true;
        }
        return chunk;
    }

    // Window Array

    const std::string WindowArray::PROBE="probe";
    const std::string WindowArray::MATERIALIZE="materialize";

    WindowArray::WindowArray(ArrayDesc const& desc,
                             std::shared_ptr<Array> const& inputArray,
                             vector<WindowBoundaries> const& window,
                             vector<AttributeID> const& inputAttrIDs,
                             vector<AggregatePtr> const& aggregates,
                             string const& method):
      Array(inputArray),
      _desc(desc),
      _inputDesc(inputArray->getArrayDesc()),
      _window(window),
      _dimensions(_desc.getDimensions()),
      _inputAttrIDs(inputAttrIDs),
      _aggregates(aggregates),
      _method(method)
    { }

    /**
     * @see Array::getArrayDesc()
     */
    ArrayDesc const& WindowArray::getArrayDesc() const
    {
        return _desc;
    }

    /**
     * @see Array::getConstIterator()
     */
    std::shared_ptr<ConstArrayIterator> WindowArray::getConstIteratorImpl(const AttributeDesc& attr) const
    {
        if (_desc.getEmptyBitmapAttribute() && attr.getId() == _desc.getEmptyBitmapAttribute()->getId())
        {
            return std::shared_ptr<ConstArrayIterator>(new WindowArrayIterator(
                                                           *this,
                                                           attr,
                                                           *getPipe(0)->getArrayDesc().getEmptyBitmapAttribute(),
                                                           _method));
        }

        const auto& outAttrDesc = getPipe(0)->getArrayDesc().getAttributes().findattr(_inputAttrIDs[attr.getId()]);
        return std::shared_ptr<ConstArrayIterator>(new WindowArrayIterator(*this, attr, outAttrDesc, _method));
    }
}

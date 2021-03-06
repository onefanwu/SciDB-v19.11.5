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
 * MergeArray.cpp
 *
 *  Created on: Apr 11, 2010
 *      Author: Knizhnik
 */


#include <array/Array.h>
#include <query/ops/merge/MergeArray.h>

namespace scidb
{
    using namespace std;

    /**
     * Dummy stateless object used for Coordinate Comparisons.
     */
    static CoordinatesLess cl;

    /**
     * Compare coordinates.
     * @return true if pos1 is less than or equal to pos2, false otherwise.
     */
    inline bool coordinatesLessOrEqual(Coordinates const& pos1, Coordinates const& pos2)
    {
        return !cl(pos2,pos1);
    }

    /**
     * Compare a positioned ChunkIterator with a coordinate pair.
     * @return true if i1 is positioned at coordinates less than pos2, false otherwise
     */
    inline bool precede(std::shared_ptr<ConstChunkIterator>& i1, Coordinates const& pos2)
    {
        return cl(i1->getPosition(), pos2);
    }

    /**
     * Compare the positions of two ChunkIterators.
     * @return true if i1 is positioned at coordinates less than i2, false otherwise.
     */
    inline bool precede(std::shared_ptr<ConstChunkIterator>& i1, std::shared_ptr<ConstChunkIterator>& i2)
    {
        return precede(i1, i2->getPosition());
    }

    /**
     * Compare the positions of two ArrayIterators
     * @return true if i1 is positioned at coordinates less than i2, false otherwise.
     */
    inline bool precede(std::shared_ptr<ConstArrayIterator>& i1, std::shared_ptr<ConstArrayIterator>& i2)
    {
        return cl(i1->getPosition(), i2->getPosition());
    }

    //
    // Merge chunk iterator methods
    //

    bool MergeChunkIterator::isEmpty() const
    {
        if (currIterator < 0)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        return iterators[currIterator]->isEmpty();
    }

    bool MergeChunkIterator::end()
    {
        return currIterator < 0 || iterators[currIterator]->end();
    }

    void MergeChunkIterator::restart()
    {
        // Restart all of our iterators, and set currIterator to the lowest position.

        currIterator = -1;
        for (size_t i = 0, n = iterators.size(); i < n; i++) {
            iterators[i]->restart();
            if (!iterators[i]->end() && (currIterator < 0 || precede(iterators[i], iterators[currIterator])))
            {
                currIterator = safe_static_cast<int>(i);
            }
        }
    }

    bool MergeChunkIterator::setPosition(Coordinates const& pos)
    {
        //simply set currIterator to the first iterator that has pos (if any)

        currIterator = -1;
        for (size_t i = 0, n = iterators.size(); i < n; i++)
        {
            if (iterators[i]->setPosition(pos))
            {
                currIterator = safe_static_cast<int>(i);
                return true;
            }
            else
            {
                iterators[i]->restart();
            }
        }
        return false;
    }

    Value const& MergeChunkIterator::getItem()
    {
        if (currIterator < 0)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        return iterators[currIterator]->getItem();
    }

    void MergeChunkIterator::operator ++()
    {
        //fast-forward all iterators until their position is greater than currPos,
        //then pick the iterator with the lowest position

        if (currIterator < 0)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);

        Coordinates currPos = iterators[currIterator]->getPosition();
        size_t n = iterators.size();
        currIterator = -1;
        for (size_t i = 0; i < n; i++)
        {
            while(!iterators[i]->end() && coordinatesLessOrEqual(iterators[i]->getPosition(), currPos))
            {
                ++(*iterators[i]);
            }
            if(!iterators[i]->end() && (currIterator<0 || precede(iterators[i], iterators[currIterator])))
            {
                currIterator = safe_static_cast<int>(i);
            }
        }
    }

    Coordinates const& MergeChunkIterator::getPosition()
    {
        if (currIterator < 0)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        return iterators[currIterator]->getPosition();
    }

    MergeChunkIterator::MergeChunkIterator(vector< ConstChunk const* > const& inputChunks, DelegateChunk const* chunk, int iterationMode)
    : DelegateChunkIterator(chunk, iterationMode),
      iterators(inputChunks.size())
    {
        currIterator = -1;
        for (size_t i = 0, n = inputChunks.size(); i < n; i++) {
            iterators[i] = inputChunks[i]->getConstIterator(iterationMode & ~INTENDED_TILE_MODE);
            if (!iterators[i]->end() && (currIterator < 0 || precede(iterators[i], iterators[currIterator])))
            {
                currIterator = safe_static_cast<int>(i);
            }
        }
    }

    //
    // Merge array iterator methods
    //

    bool MergeArrayIterator::setPosition(Coordinates const& pos)
    {
        currIterator = -1;
        currentChunk = NULL;
        for (size_t i = 0, n = iterators.size(); i < n; i++)
        {
            if (iterators[i]->setPosition(pos))
            {
                if (currIterator < 0) {
                    currIterator = safe_static_cast<int>(i);
                }
            }
            else
            {
                iterators[i]->restart();
            }
        }
        return currIterator >= 0;
    }

    void MergeArrayIterator::restart()
    {
        currIterator = -1;
        currentChunk = NULL;
        for (size_t i = 0, n = iterators.size(); i < n; i++) {
            iterators[i]->restart();
            if (!iterators[i]->end() && (currIterator < 0 || precede(iterators[i], iterators[currIterator])))
            {
                currIterator = safe_static_cast<int>(i);
            }
        }
    }

    bool MergeArrayIterator::end()
    {
        return currIterator < 0 || iterators[currIterator]->end();
    }

    void MergeArrayIterator::operator ++()
    {
        //fast-forward all iterators until their position is greater than currPos,
        //then pick the iterator with the lowest position
        if (currIterator < 0)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);

        Coordinates currPos = iterators[currIterator]->getPosition();
        currIterator = -1;
        currentChunk = NULL;
        for (size_t i = 0, n = iterators.size(); i < n; i++)
        {
            while(!iterators[i]->end() && coordinatesLessOrEqual(iterators[i]->getPosition(), currPos))
            {
                ++(*iterators[i]);
            }
            if(!iterators[i]->end() && (currIterator<0 || precede(iterators[i], iterators[currIterator])))
            {
                currIterator = safe_static_cast<int>(i);
            }
        }
    }

    Coordinates const& MergeArrayIterator::getPosition()
    {
        if (currIterator < 0)
            throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
        return iterators[currIterator]->getPosition();
    }

    ConstChunk const& MergeArrayIterator::getChunk()
    {
        if (currentChunk == NULL) {
            if (currIterator < 0)
                throw USER_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_CURRENT_ELEMENT);
            Coordinates const& currPos = iterators[currIterator]->getPosition();
            ConstChunk const& currChunk = iterators[currIterator]->getChunk();
            if (!isEmptyable) {
                currentChunk = &currChunk;
                return currChunk;
            }
            chunk.inputChunks.clear();
            chunk.inputChunks.push_back(&currChunk);
            for (size_t i = currIterator+1, n = iterators.size(); i < n; i++) {
                if (!iterators[i]->end() && iterators[i]->getPosition() == currPos) {
                    ConstChunk const& mergeChunk = iterators[i]->getChunk();
                    if (!mergeChunk.getConstIterator(ConstChunkIterator::DEFAULT)->end()) {
                        chunk.inputChunks.push_back(&mergeChunk);
                    } else {
                        ++(*iterators[i]);
                    }
                }
            }
            if (chunk.inputChunks.size() == 1) {
                currentChunk = &currChunk;
                return currChunk;
            }
            chunk.setInputChunk(currChunk);
            currentChunk = &chunk;
        }
        return *currentChunk;
    }



    MergeArrayIterator::MergeArrayIterator(MergeArray const& array, const AttributeDesc& attrID)
    : DelegateArrayIterator(array, attrID, array.getPipe(0)->getConstIterator(attrID)),
      chunk(array, *this, attrID.getId()),
      iterators(array.getPipeCount()),
      currIterator(-1),
      currentChunk(NULL)
    {
        isEmptyable = array.getArrayDesc().getEmptyBitmapAttribute() != NULL;
        for (size_t i = 0, n = iterators.size(); i < n; i++) {
            iterators[i] = array.getPipe(i)->getConstIterator(attrID);
            if (!iterators[i]->end() && (currIterator < 0 || precede(iterators[i], iterators[currIterator])))
            {
                currIterator = safe_static_cast<int>(i);
            }
        }
    }


    //
    // Merge array methods
    //
    DelegateChunkIterator* MergeArray::createChunkIterator(DelegateChunk const* chunk, int iterationMode) const
    {
        return new MergeChunkIterator(((MergeChunk const*)chunk)->inputChunks, chunk, iterationMode);
    }

    DelegateArrayIterator* MergeArray::createArrayIterator(const AttributeDesc& attrID) const
    {
        return new MergeArrayIterator(*this, attrID);
    }

    MergeArray::MergeArray(ArrayDesc const& desc, vector< std::shared_ptr<Array> > const& arrays)
        : DelegateArray(desc, arrays[0])
    {
        // DelegateArray installed first input pipe, we must install the rest manually.
        for (size_t i = 1; i < arrays.size(); i++) {
            pushPipe(arrays[i]);
        }
        SCIDB_ASSERT(getPipeCount() == arrays.size());

        // By now any attribute count mismatch is due to absence of an empty bitmap attribute on an
        // input array (aka pipe).
        for (size_t i = 0; i < arrays.size(); i++) {
            if (getPipe(i)->getArrayDesc().getAttributes().size() != desc.getAttributes().size()) {
                setPipe(i, std::shared_ptr<Array>(new NonEmptyableArray(getPipe(i))));
            }
        }
    }
}

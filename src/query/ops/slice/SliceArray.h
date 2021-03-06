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
 * @file SliceArray.h
 *
 * @brief The implementation of the array iterator for the slice operator
 *
 * The array iterator for the slice maps incoming getChunks calls into the
 * appropriate getChunks calls for its input array. Then, if the requested chunk
 * fits in the slice range, the entire chunk is returned as-is. Otherwise,
 * the appropriate piece of the chunk is carved out.
 *
 * NOTE: In the current implementation if the slice window stretches beyond the
 * limits of the input array, the behavior of the operator is undefined.
 *
 * The top-level array object simply serves as a factory for the iterators.
 */

#ifndef SLICE_ARRAY_H_
#define SLICE_ARRAY_H_

#include <string>


#include <array/Array.h>
#include <array/Chunk.h>
#include <array/ArrayDesc.h>

namespace scidb
{
class ConstChunk;
class ConstArrayIterator;


class SliceArray;
class SliceArrayIterator;
class SliceChunkIterator;

class SliceChunk : public ConstChunk
{
    friend class SliceChunkIterator;
    friend class SimpleSliceChunkIterator;
  public:
    const ArrayDesc& getArrayDesc() const;
    const AttributeDesc& getAttributeDesc() const;
    CompressorType getCompressionMethod() const;
    Coordinates const& getFirstPosition(bool withOverlap) const;
    Coordinates const& getLastPosition(bool withOverlap) const;
    std::shared_ptr<ConstChunkIterator> getConstIterator(int iterationMode) const;
    void setInputChunk(ConstChunk const* inputChunk);
    Array const& getArray() const;

    SliceChunk(SliceArray const& array, AttributeID attrID);

  private:
    SliceArray const& array;
    AttributeID attr;
    ConstChunk const* inputChunk;
    Coordinates firstPos;
    Coordinates firstPosWithOverlap;
    Coordinates lastPos;
    Coordinates lastPosWithOverlap;
};

class SliceChunkIterator : public ConstChunkIterator
{
  public:
    int getMode() const override;
    Value const& getItem() override;
    bool isEmpty() const override;
    bool end() override;
    void operator ++() override;
    Coordinates const& getPosition() override;
    bool setPosition(Coordinates const& pos) override;
    void restart() override;
    ConstChunk const& getChunk() override;

    SliceChunkIterator(SliceChunk const& chunk, int iterationMode);

  private:
    void moveNext();

    SliceArray const& array;
    SliceChunk const& chunk;
    std::shared_ptr<ConstChunkIterator> inputIterator;
    Coordinates inPos;
    Coordinates outPos;
    Coordinates firstPos;
    Coordinates lastPos;
    bool hasCurrent;
};

class SimpleSliceChunkIterator : public ConstChunkIterator
{
  public:
    int getMode() const override;
    Value const& getItem() override;
    bool isEmpty() const override;
    bool end() override;
    void operator ++() override;
    Coordinates const& getPosition() override;
    bool setPosition(Coordinates const& pos) override;
    void restart() override;
    ConstChunk const& getChunk() override;

    SimpleSliceChunkIterator(SliceChunk const& chunk, int iterationMode);

  private:
    SliceArray const& array;
    SliceChunk const& chunk;
    std::shared_ptr<ConstChunkIterator> inputIterator;
    Coordinates inPos;
    Coordinates outPos;
};

/***
 * The iterator of the slice implements all the calls of the array iterator API though setPosition
 * calls.
 * NOTE: This looks like a candidate for an intermediate abstract class: PositionConstArrayIterator.
 */
class SliceArrayIterator : public ConstArrayIterator
{
  public:
	/***
	 * Constructor for the slice iterator
	 * Here we initialize the current position vector to all zeros, and obtain an iterator for the appropriate
	 * attribute in the input array.
	 */
	SliceArrayIterator(SliceArray const& slice, const AttributeDesc& attrID);

	/***
	 * Get chunk method retrieves the chunk at current position from the input iterator and either
	 * passes it up intact (if the chunk is completely within the slice window), or carves out
	 * a piece of the input chunk into a fresh chunk and sends that up.
	 *
	 * It performs a mapping from the slice coordinates to the coordinates of the input array
	 * It does not check any bounds, since the setPosition call will not set an invalid position.
	 *
	 * NOTE: Currently we don't check where the slice window streches beyond the limits of the
	 * input array. What is the behavior then - padd with NULLs?
	 */
	ConstChunk const& getChunk() override;

	/***
	 * The end call checks whether we're operating with the last chunk of the slice
	 * window.
	 */
	bool end() override;

	/***
	 * The ++ operator advances the current position to the next chunk of the slice
	 * window.
	 */
	void operator ++() override;

	/***
	 * Simply returns the current position
	 * Initial position is a vector of zeros of appropriate dimensionality
	 */
	Coordinates const& getPosition() override;

	/***
	 * Here we only need to check that we're not moving beyond the bounds of the slice window
	 */
	bool setPosition(Coordinates const& pos) override;

	/***
	 * Restart simply changes the current position to all zeros
	 */
	void restart() override;

  private:
    void moveNext();

    SliceArray const& array;
    std::shared_ptr<ConstArrayIterator> inputIterator;
    SliceChunk chunk;
    Coordinates inPos;
    Coordinates outPos;
    bool hasCurrent;
    bool chunkInitialized;
};

class InfiniteSliceArrayIterator : public ConstArrayIterator
{
  public:
	/***
	 * Constructor for the slice iterator
	 * Here we initialize the current position vector to all zeros, and obtain an iterator for the appropriate
	 * attribute in the input array.
	 */
	InfiniteSliceArrayIterator(SliceArray const& slice, const AttributeDesc& attrID);

	/***
	 * Get chunk method retrieves the chunk at current position from the input iterator and either
	 * passes it up intact (if the chunk is completely within the slice window), or carves out
	 * a piece of the input chunk into a fresh chunk and sends that up.
	 *
	 * It performs a mapping from the slice coordinates to the coordinates of the input array
	 * It does not check any bounds, since the setPosition call will not set an invalid position.
	 *
	 * NOTE: Currently we don't check where the slice window streches beyond the limits of the
	 * input array. What is the behavior then - padd with NULLs?
	 */
	ConstChunk const& getChunk() override;

	/***
	 * The end call checks whether we're operating with the last chunk of the slice
	 * window.
	 */
	bool end() override;

	/***
	 * The ++ operator advances the current position to the next chunk of the slice
	 * window.
	 */
	void operator ++() override;

	/***
	 * Simply returns the current position
	 * Initial position is a vector of zeros of appropriate dimensionality
	 */
	Coordinates const& getPosition() override;

	/***
	 * Here we only need to check that we're not moving beyond the bounds of the slice window
	 */
	bool setPosition(Coordinates const& pos) override;

	/***
	 * Restart simply changes the current position to all zeros
	 */
	void restart() override;

  private:
    void nextAvailable();

    SliceArray const& array;
    std::shared_ptr<ConstArrayIterator> inputIterator;
    SliceChunk chunk;
	Coordinates inPos;
	Coordinates outPos;
    bool chunkInitialized;
};

class SliceArray : public Array
{
    friend class SliceChunk;
    friend class SliceChunkIterator;
    friend class SliceArrayIterator;
    friend class SimpleSliceChunkIterator;
    friend class InfiniteSliceArrayIterator;

    void mapPos(Coordinates& outPos, Coordinates const& inPos) const;

  public:

    /**
     * This is part of a pattern that has two different possible array iterators, and picks an iterator based
     * on the logical size of the array. The same pattern is used in Between and Subarray.
     * See comment in BetweenArray.h for why 6,000 is a good number.
     * We should merge these constants somehow but making them one config does not seem right.
     */
    static const size_t SLICE_INFINITE_ITERATOR_THRESHOLD = 6000;

	SliceArray(ArrayDesc& d, Coordinates const& slice, uint64_t mask, std::shared_ptr<Array> input);

	virtual std::string const& getName() const;
	virtual ArrayID getHandle() const;
	virtual const ArrayDesc& getArrayDesc() const;
        std::shared_ptr<ConstArrayIterator> getConstIteratorImpl(const AttributeDesc& id) const override;

  private:
	ArrayDesc desc;
	Coordinates slice;
    uint64_t mask;
    bool     useInfiniteIterator;
    bool     simple;
    Dimensions const& inputDims;
};


} //namespace

#endif /* SLICE_ARRAY_H_ */

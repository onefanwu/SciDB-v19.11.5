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
 * @file PullSGContext.h
 *
 * @brief Pull-based SG context serving as a chunk data producer
 */

#ifndef PULL_SG_CONTEXT_H_
#define PULL_SG_CONTEXT_H_

#include <iostream>
#include <vector>
#include <deque>
#include <string>

#include <memory>
#include <unordered_set>

#include <array/Array.h>
#include <array/MemChunk.h>
#include <query/Query.h>
#include <query/PhysicalOperator.h>
#include <query/PullSGArray.h>
#include <network/MessageDesc.h>
#include <network/proto/scidb_msg.pb.h>

namespace scidb
{

/**
 * This class is an implementation of the data producer for
 * the pull-based redistribute() (i.e. the Scatter side)
 * @see scidb::PullSGArray for the data consumer side (i.e. the Gather side)
 */
class PullSGContext : virtual public OperatorContext
{
private:

    std::shared_ptr<Array> _inputSGArray;
    bool _isEmptyable;
    std::shared_ptr<PullSGArray> _resultArray;
    SGInstanceLocator _instanceLocator;
    typedef std::deque< std::shared_ptr<MessageDesc> > MessageQueue;
    struct InstanceState
    {
        MessageQueue _chunks;
        uint64_t _requestedNum;
        uint64_t _lastFetchId;
        InstanceState() : _requestedNum(0), _lastFetchId(0) {}
    };

    struct AttributeState
    {
        // Total size of cached chunks (i.e # of chunks) per attribute
        size_t _size;
        // This value reflects the number of shallow chunks in the cache.
        // Shallow chunks either share the payload with some other chunks or have no payload at all.
        size_t _shallowSize;
        // NO more data available for attribute
        bool   _eof;
        bool   _bgTask;
        AttributeState() : _size(0), _shallowSize(0), _eof(0), _bgTask(false) {}
    };

    // This is the prefetch chunk cache. Each remote request tries to drain iterator until the cache is full
    // and then scatters the chunks eligible for delivery.
    std::vector< std::vector<InstanceState> >  _instanceStates;
    std::vector<std::shared_ptr<ConstArrayIterator> >  _attributeIterators;
    std::vector<AttributeState> _attributeStates;
    size_t _perAttributeMaxSize;
    /// A set of attributes for which the chunks are temporarily unavailable
    /// Generally, SinglePassArray iterators can indicate such a condition
    /// because their attribute chunks have to be consumed "horizontally".
    std::unordered_set<size_t> _unavailableAttributes;

    /// true if a data integrity issue has been found
    bool _hasDataIntegrityIssue;

public:

    static InstanceID instanceForChunk(const std::shared_ptr<Query>& query,
                                       const Coordinates& chunkPosition,
                                       const ArrayDesc& arrayDesc,
                                       const ArrayDistPtr& outputArrayDist,
                                       const ArrayResPtr& outputArrayRes)


    {
        const InstanceID destInstance = getInstanceForChunk(chunkPosition,
                                                            arrayDesc.getDimensions(),
                                                            outputArrayDist,
                                                            outputArrayRes,
                                                            query);
        return destInstance;
    }

    static InstanceID instanceForChunk(const std::shared_ptr<Query>& query,
                                       const Coordinates& chunkPosition,
                                       const ArrayDesc& arrayDesc,
                                       const ArrayDistPtr& outputArrayDist)
    {
        const InstanceID destInstance = getInstanceForChunk(chunkPosition,
                                                            arrayDesc.getDimensions(),
                                                            outputArrayDist,
                                                            query);
        return destInstance;
    }


    /**
     * Constructor
     * @param source array of data to scatter
     * @param result array of data to gather
     * @param instNum number of participating instances
     * @param cacheSizePerAttribute the maximum number of input array chunks to be cached per attribute;
     *        CONFIG_SG_SEND_QUEUE_SIZE by default
     * @note
     * Right now all attributes are serialized via Query::_operatorQueue.
     * Performing the SG one attribute at a time should be optimal because of
     * the maximum utilization of the prefetch chunk cache (controlled by cacheSizePerAttribute).
     * Multiple attributes can be SGed in parallel trading-off the cache size
     * (if the total cache size has to remain constant).
     * However, by default, cacheSizePerAttribute will be set to  the same value for all attributes.
     */
    PullSGContext(const std::shared_ptr<Array>& source,
                  const std::shared_ptr<PullSGArray>& result,
                  const size_t instNum,
                  const SGInstanceLocator& instLocator,
                  size_t cacheSizePerAttribute=0);

    virtual ~PullSGContext() {}

    std::shared_ptr<PullSGArray> getResultArray()
    {
        return _resultArray;
    }

    /// A list of network messages with their destinations
    typedef std::list< std::pair<InstanceID, std::shared_ptr<MessageDesc> > > ChunksWithDestinations;

    /**
     * Get the next set of chunks to send to their destinations (i.e. scatter)
     * @param query current query context
     * @param pullingInstance the instance making the request
     * @param attrId attribute ID
     * @param positionOnlyOK if true the pulling instance is willing to accept just the next position data
     * @param prefetchSize the number of *data* chunks the pulling instance is willing to accept
     * @param fetchId the pulling instance message ID for this request
     * @param chunksToSend [out] a list of chunks and their destination instances to scatter
     */
    void getNextChunks(const std::shared_ptr<Query>& query,
                       const InstanceID pullingInstance,
                       const AttributeID attrId,
                       const bool positionOnlyOK,
                       const uint64_t prefetchSize,
                       const uint64_t fetchId,
                       ChunksWithDestinations& chunksToSend);

    /**
     * Get the next set of chunks to send to their destinations (i.e. scatter)
     * It is intended to be called by a background job (hence no pulling instance).
     * Every invocation must be preceeded with a successful call to setupToRunInBackground().
     * @see setupToRunInBackground()
     * @param query current query context
     * @param attrId attribute ID
     * @param chunksToSend [out] a list of chunks and their destination instances to scatter
     */
    void produceChunksInBackground(const std::shared_ptr<Query>& query,
                                   const AttributeID attrId,
                                   ChunksWithDestinations& chunksToSend);

    /**
     * Prepare for a background chunk generation.
     * It is intended to be called before a background job calling produceChunksInBackground()
     * @see produceChunksInBackground()
     * @param attrId attribute ID
     * @return true if the background can be spawn to produce more chunks;
     *         false otherwise (i.e. no more chunks can be produced at this point)
     */
    bool setupToRunInBackground(const AttributeID attrId);

private:
    /// @return true if a chunk has any values/cells
    bool hasValues(ConstChunk const& chunk);

    // Helpers to manipulate scidb_msg::Chunk messages

    void
    setNextPosition(std::shared_ptr<MessageDesc>& chunkMsg,
                    const InstanceID pullingInstance,
                    std::shared_ptr<ConstArrayIterator>& inputArrIter,
                    const std::shared_ptr<Query>& query);
    void
    setNextPosition(std::shared_ptr<MessageDesc>& chunkMsg,
                    const InstanceID nextDestSGInstance,
                    const Coordinates& nextChunkPosition);

    void
    setNextPosition(std::shared_ptr<MessageDesc>& chunkMsg,
                    std::shared_ptr<MessageDesc>& nextChunkMsg);

    std::shared_ptr<MessageDesc>
    getPositionMesg(const QueryID queryId,
                    const AttributeID attributeId,
                    const InstanceID destSGInstance,
                    const Coordinates& chunkPosition);

    std::shared_ptr<MessageDesc>
    getPositionMesg(const std::shared_ptr<MessageDesc>& fullChunkMsg);

    std::shared_ptr<MessageDesc>
    getEOFChunkMesg(const QueryID queryId,
                    const AttributeID attributeId);

    std::shared_ptr<MessageDesc>
    getChunkMesg(const std::shared_ptr<Query>& query,
                 const AttributeID attributeId,
                 const InstanceID destSGInstance,
                 const ConstChunk& chunk,
                 const Coordinates& chunkPosition,
                 std::shared_ptr<CompressedBuffer>& buffer);
    void verifyPositions(ConstChunk const& chunk,
                         std::shared_ptr<ConstRLEEmptyBitmap>& emptyBitmap);

    /**
     * Extract a chunk and/or position message from the chunk cache
     */
    std::shared_ptr<MessageDesc>
    reapChunkMsg(const QueryID queryId,
                 const AttributeID attributeId,
                 InstanceState& destState,
                 const InstanceID destInstance,
                 const bool positionOnly);
    /**
     * Find the chunks and/or position messages eligible for sending to their destination instances
     */
    bool
    findCachedChunksToSend(std::shared_ptr<ConstArrayIterator>& inputArrIter,
                           const std::shared_ptr<Query>& query,
                           const InstanceID pullingInstance,
                           const AttributeID attrId,
                           const bool positionOnly,
                           ChunksWithDestinations& chunksToSend);
    /**
     * Pull on the input array iterator (i.e. produce chunks) and put them into the chunk cache
     */
    bool
    drainInputArray(std::shared_ptr<ConstArrayIterator>& inputArrIter,
                    const std::shared_ptr<Query>& query,
                    const AttributeID attrId);
    /**
     * Insert EOF messages for every instance into the cache
     */
    void
    insertEOFChunks(const QueryID queryId,
                    const AttributeID attributeId);
    /**
     * Advance an iterator to the next chunk
     * @return false if the next chunk is not yet available; true otherwise
     */
    bool
    advanceInputIterator(const AttributeID attrId,
                         std::shared_ptr<ConstArrayIterator>& inputArrIter);

    /**
     * Helper method that that attempts to find chunks eligible for delivery
     * only for a given attribute
     */
    void
    getNextChunksInternal(const std::shared_ptr<Query>& query,
                          const InstanceID pullingInstance,
                          const AttributeID attrId,
                          const bool positionOnlyOK,
                          ChunksWithDestinations& chunksToSend);

    /// @return true if the cache for a given attribute is full
    /// (i.e. no more input chunks can be inserted); false otherwise
    bool isCacheFull(const AttributeID attrId)
    {
        return (_attributeStates[attrId]._size >= _perAttributeMaxSize);
    }

    /// @return true if a given attribute has no more chunks; false otherwise
    bool isInputEOF(const AttributeID attrId)
    {
        return _attributeStates[attrId]._eof;
    }
};

} // namespace

#endif /* OPERATOR_H_ */

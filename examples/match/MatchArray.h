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
 * MatchArray.h
 *
 *  Created on: Apr 04, 2012
 *      Author: Knizhnik
 */
#ifndef _MATCH_ARRAY_H_
#define _MATCH_ARRAY_H_

#include "array/DelegateArray.h"
#include "util/Mutex.h"
#include <map>

namespace scidb {


class MatchArray;
class MatchArrayIterator;

struct MatchHash
{
    struct Elem {
        Coordinates coords;
        int64_t hash;
        Elem* collisionChain;

        Elem(Coordinates const& pos, int64_t h, Elem* next) : coords(pos), hash(h), collisionChain(next) {}
    };
    std::vector<Elem*> table;
    bool  initialized;
    bool  busy;
    bool  waiting;

    Elem*& collisionChain(int64_t hash) {
        return table[hash % table.size()];
    }

    Elem* find(int64_t hash) const;

    void addCatalogEntry(Coordinates const& pos, size_t i, int64_t hash, int64_t error);

    MatchHash();
    MatchHash(size_t size);
    ~MatchHash();
};


class MatchArrayIterator : public DelegateArrayIterator
{
  public:
	virtual ConstChunk const& getChunk();
	virtual Coordinates const& getPosition();
	virtual bool setPosition(Coordinates const& pos);

    MatchArrayIterator(MatchArray const& array,
                       const AttributeDesc& attrID,
                       std::shared_ptr<ConstArrayIterator> patIterator,
                       std::shared_ptr<ConstArrayIterator> catIterator);

  private:
    Coordinates outPos;
    MemChunk chunk;
    std::shared_ptr<MatchHash> match;
    std::shared_ptr<ConstArrayIterator> catalogIterator;
};

class MatchArray : public DelegateArray
{
    friend class MatchArrayIterator;
  public:
    std::shared_ptr<MatchHash> findMatch(Coordinates const& chunkPos);
    int64_t getElemPosition(Coordinates const& pos, ConstChunk const& chunk);

    DelegateArrayIterator* createArrayIterator(const AttributeDesc& id) const override;

    MatchArray(ArrayDesc const& desc, std::shared_ptr<Array> pattern, std::shared_ptr<Array> catalog, int64_t error);

  private:
    Mutex mutex;
    Event event;
    std::map<Coordinates, std::weak_ptr<MatchHash> > matches;
    std::shared_ptr<Array> pattern;
    std::shared_ptr<Array> catalog;
    std::shared_ptr<ConstArrayIterator> patternIterator;
    std::shared_ptr<ConstArrayIterator> catalogIterator;

    int64_t error;
    size_t nPatternAttributes;
    size_t nCatalogAttributes;
    AttributeDesc patternIteratorAttr;
    AttributeDesc catalogIteratorAttr;
};

}

#endif

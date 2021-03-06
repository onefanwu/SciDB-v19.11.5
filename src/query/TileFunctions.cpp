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
 * @file TileFunctions.cpp
 *
 * @author roman.simakov@gmail.com
 *
 * @brief Full template specializations of tile functions
 */

#include <query/TileFunctions.h>

namespace scidb
{

void rle_unary_bool_not(const Value** args,  Value* result, void*)
{
    const Value& v = *args[0];
    Value& res = *result;
    res.getTile()->clear();
    res.getTile()->assignSegments(*v.getTile());
    const size_t valuesCount = v.getTile()->getValuesCount();
    addPayloadValues<bool>(res.getTile(), valuesCount);
    const char* s = const_cast<const char*>(v.getTile()->getFixData());
    // There is no fixed data in the case of a null
    if (s) {
        char* r = res.getTile()->getFixData();
        const char* end = s + (valuesCount >> 3) + 1;
        // Probably can be optimized by using DWORD instead of char
        while (s < end) {
            *r++ = static_cast<char>(~(*s++));
        }
    }
}

/**
 * tile implementation of is_null function
 * this function is polymorphic and will require changes to provide inferring type result function.
 */
void inferIsNullArgTypes(const ArgTypes& factInputArgs, std::vector<ArgTypes>& possibleInputArgs, std::vector<TypeId>& possibleResultArgs)
{
    possibleInputArgs.resize(1);
    possibleInputArgs[0] = factInputArgs;
    possibleResultArgs.resize(1);
    possibleResultArgs[0] = TID_BOOL;
}

void rle_unary_bool_is_null(const Value** args, Value* result, void*)
{
    const RLEPayload* vTile = args[0]->getTile();
    RLEPayload* rTile =  result->getTile();
    rTile->clear();
    rTile->addBoolValues(2);
    *rTile->getFixData() = 2;
    const RLEPayload::Segment* v = NULL;
    position_t tail = 0;
    for (size_t i = 0; i < vTile->nSegments(); i++)
    {
        size_t vLen;
        v = &vTile->getSegment(i, vLen);
        RLEPayload::Segment r;
        r.setNull(false);
        r.setPPosition(v->pPosition());
        r.setSame(vLen > 1);
        r.setValueIndex(v->null() ? 1 : 0);
        rTile->addSegment(r);
        tail = v->pPosition() + vLen;
    }
    rTile->flush(tail);
}

void rle_unary_null_to_any(const Value** args, Value* result, void*)
{
    const RLEPayload* vTile = args[0]->getTile();
    RLEPayload* rTile =  result->getTile();
    rTile->clear();
    rTile->assignSegments(*vTile);
}

}

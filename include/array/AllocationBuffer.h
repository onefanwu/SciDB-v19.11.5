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

#ifndef ARRAY_ALLOCATION_BUFFER_H_
#define ARRAY_ALLOCATION_BUFFER_H_

/****************************************************************************/

#include <util/Arena.h>                                  // For Arena
#include <array/SharedBuffer.h>                          // For SharedBuffer

/****************************************************************************/
namespace scidb {
/****************************************************************************/
/**
 *  @brief      A SharedBuffer that works with operator new.
 *
 *  @details    Class AllocationBuffer offers a simple implementation of the
 *              SharedBuffer interface that is suitable for use with operator
 *              new. For example,
 *  @code
 *                  AllocationBuffer buffer;
 *
 *                  ... = new (buffer) Object(...);
 *  @endcode
 *              allocates the underlying storage for the object from memory
 *              that is owned by the SharedBuffer 'buffer'.
 *
 *  @author jbell@paradigm4.com
 */

class AllocationBuffer : public SharedBuffer, boost::noncopyable
{
public:                   // Construction
                              AllocationBuffer(const arena::ArenaPtr& = arena::getArena());
                             ~AllocationBuffer();

public:                   // Operations
    const void*       getConstData()       const override;
    void*             getWriteData()             override;
    size_t            getSize()            const override;
    bool              pin()                const override;
    void              unPin()              const override;

public:                   // Operations
    void              allocate(size_t n)   override;
    void              free()               override;

private:                  // Representation
     arena::ArenaPtr    const _arena;                    // The allocating arena
            void*             _data;                     // The allocation
            size_t            _size;                     // Its size
};

/****************************************************************************/
} // namespace
/****************************************************************************/

inline void* operator new(size_t sz,scidb::SharedBuffer& ab)
{
    ab.allocate(sz);                                     // Allocate storage
    assert(ab.getSize() >= sz);                          // Validate override
    return ab.getWriteData();                            // Return allocation
}

/****************************************************************************/
#endif
/****************************************************************************/

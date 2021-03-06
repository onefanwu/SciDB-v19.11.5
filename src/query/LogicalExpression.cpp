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
 * @file LogicalExpression.cpp
 *
 * @brief Instances of logical expressions
 *
 * @author Artyom Smirnov <smirnoffjr@gmail.com>
 * @author roman.simakov@gmail.com
 */
#include "query/LogicalExpression.h"

#include "array/Array.h"
#include "system/Exceptions.h"
#include "util/Indent.h"

namespace scidb
{

void LogicalExpression::toString(std::ostream &out, int indent) const
{
    Indent prefix(indent);
    out << prefix(' ', false);
    out << "[logicalExpression]\n";
}

void AttributeReference::toString(std::ostream &out, int indent) const
{
    Indent prefix(indent);
    out << prefix(' ', false);
    out << "[attributeReference] array " << _arrayName;
    out << " attr " << _attributeName<<"\n";
}

void Constant::toString(std::ostream &out, int indent) const
{
    Indent prefix(indent);
    out << prefix(' ', false);
    out << "[constant] type " << _type;
    out <<" value "<< _value.toString(_type) << "\n";
}

void Function::toString(std::ostream &out, int indent) const
{
    Indent prefix(indent);
    out << prefix(' ', false);
    out << "[function] " << _function;
    out << " args:\n";
    for ( size_t i = 0; i < _args.size(); ++i)
    {
        _args[i]->toString(out, indent + 1);
    }
}

} // namespace scidb

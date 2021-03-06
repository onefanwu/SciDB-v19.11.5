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
 * @file LogicalSave.cpp
 *
 * @author roman.simakov@gmail.com
 *
 * Save operator for saveing data from external files into array
 */

#include <query/LogicalOperator.h>

#include <query/Expression.h>
#include <query/UserQueryException.h>
#include <util/IoFormats.h>

using namespace std;

namespace scidb
{

/**
 * @brief The operator: save().
 *
 * @par Synopsis:
 *   save( srcArray, file, instanceId = -2, format = 'store' )
 *
 * @par Summary:
 *   Saves the data in an array to a file.
 *
 * @par Input:
 *   - srcArray: the source array to save from.
 *   - file: the file to save to.
 *   - instanceId: positive number means an instance ID on which file will be saved.
 *                 -1 means to save file on every instance. -2 - on coordinator.
 *   - format: I/O format in which file will be stored
 *
 * @see iofmt::isOutputFormat
 *
 * @par Output array:
 *   the srcArray is returned
 *
 * @par Examples:
 *   n/a
 *
 * @par Errors:
 *   n/a
 *
 * @par Notes:
 *   n/a
 *
 */
/**
 * Must be called as SAVE('existing_array_name', '/path/to/file/on/instance')
 */
class LogicalSaveOld: public LogicalOperator
{
public:
    LogicalSaveOld(const std::string& logicalName, const std::string& alias):
        LogicalOperator(logicalName, alias)
    {
    }

    static PlistSpec const* makePlistSpec()
    {
        static PlistSpec argSpec {
            { "", // positionals
              RE(RE::LIST, {
                 RE(PP(PLACEHOLDER_INPUT)),
                 RE(PP(PLACEHOLDER_CONSTANT, TID_STRING)),
                 RE(RE::QMARK, {
                    RE(PP(PLACEHOLDER_CONSTANT, TID_INT64)),
                    RE(RE::QMARK, {
                       RE(PP(PLACEHOLDER_CONSTANT, TID_STRING))
                    })
                 })
              })
            }
        };
        return &argSpec;
    }

    ArrayDesc inferSchema(std::vector< ArrayDesc> inputSchemas, std::shared_ptr< Query> query)
    {
        assert(inputSchemas.size() == 1);
        assert(_parameters.size() >= 1);

        if (_parameters.size() >= 3) {
            Value v = evaluate(
                ((std::shared_ptr<OperatorParamLogicalExpression>&)_parameters[2])->getExpression(),
                TID_STRING);
            string const& format = v.getString();

            if (!format.empty()
                && compareStringsIgnoreCase(format, "auto") != 0
                && !iofmt::isOutputFormat(format))
            {
                throw  USER_QUERY_EXCEPTION(SCIDB_SE_INFER_SCHEMA,
                                            SCIDB_LE_UNSUPPORTED_FORMAT,
                                            _parameters[2]->getParsingContext())
                    << format;
            }
        }

        return inputSchemas[0];
    }

};


DECLARE_LOGICAL_OPERATOR_FACTORY(LogicalSaveOld, "_save_old")


} //namespace

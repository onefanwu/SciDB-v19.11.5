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
 * @file ArrayWriter.h
 *
 *  Created on: 19.02.2010
 *      Author: knizhnik@garret.ru
 *   Rewritten: 29.05.2014 by mjl@paradigm4.com
 * Description: Write out arrays in various formats.
 */

#ifndef ARRAY_WRITER_H_
#define ARRAY_WRITER_H_

#include <stddef.h>
#include <string>
#include <limits>

#include "array/Array.h"

namespace scidb
{

    class ArrayWriter
    {
      public:
        enum { DEFAULT_PRECISION = std::numeric_limits<double>::digits10 };

        /// Values for #save method 'flags' parameter.
        enum Flags {
            F_APPEND = 0x01,    ///< Open file in append mode
            F_PARALLEL = 0x02   ///< This is a parallel save
        };

        /**
         * Save data from in text format in specified file
         * @param array array to be saved
         * @param file path to the exported data file
         * @param query doing the save
         * @param format output format: csv, csv+, tsv, tsv+, sparse, auto, etc.
         * @param flags see ArrayWriter::Flags
         * @param fp a file pointer to write to rather than using the @file
         * @return number of saved tuples
         */
        static uint64_t save(Array const& array,
                             std::string const& file,
                             const std::shared_ptr<Query>& query,
                             std::string const& format = "auto",
                             unsigned flags = 0,
                             FILE* fp = nullptr);

        /**
         * Return the number of digits' precision used to format output.
         * @return number of digits
         */
        static int getPrecision()
        {
            return _precision;
        }

        /**
         * Set the number of digits' precision used to format output.
         * @param prec precision value, or < 0 to restore default
         * @return previous precision value
         */
        static int setPrecision(int prec)
        {
            int prev = _precision;
            _precision = (prec < 0) ? DEFAULT_PRECISION : prec;
            return prev;
        }

    private:
        static int _precision;

    };
}

#endif

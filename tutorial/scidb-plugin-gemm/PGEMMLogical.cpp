/*
 * Copyright (c) 2021-2022 ZJU Database Group, Zhejiang University.
 * All rights reserved.
 *
 * This file is covered by the LICENSE.txt license file in the root directory.
 *
 * @Description:
 *
 * @Author: Yifan Wu
 * @Date: 2022-05-18 14:12:39
 * @LastEditTime: 2022-05-19 03:52:01
 */

// C++
// std C
#include <stdlib.h>
// de-facto standards
// SciDB
#include <log4cxx/logger.h>
#include <query/AutochunkFixer.h>
#include <query/LogicalOperator.h>
#include <query/OperatorLibrary.h>
#include <system/BlockCyclic.h>
#include <system/Exceptions.h>
#include <system/SystemCatalog.h>

// local
#include "DLAErrors.h"

using namespace std;
using namespace scidb;

namespace scidb {
///
/// @brief The operator: pgemm().
///
/// @par Synopsis:
///   pgemm( leftArray, rightArray, accumulateArray )
///
/// @par Summary:
///   Produces a result array via matrix multiplication of leftArray with
///   rightArray and addition of accumulateArray All matrices must have a single
///   numeric attribute of type 'double', two dimensions, and the chunk size of
///   32x32 leftArray and rightArray must have the same size of 'inner'
///   dimension, i.e. leftArray second dimension and rightArray first dimension.
///    acumulateArray must have the shape of a matrix-multiplication-product,
///    i.e. leftArray first dimension by rightArray second dimension.
///
/// @par Input:
///   - leftArray: the left matrix with two dimensions: leftDim1, leftDim2
///   - rightArray: the right matrix with two dimensions: rightDim1, rightDim2
///
/// @par Output array:
///        <
///   <br>   <double:gemm>: the result attribute
///   <br> >
///   <br> [
///   <br>   leftDim1
///   <br>   rightDim2
///   <br> ]
///
/// @par Examples:
///   n/a

/// @par Notes:
///   n/a
///
class PGEMMLogical : public LogicalOperator {
  AutochunkFixer _fixer;

 public:
  PGEMMLogical(const std::string& logicalName, const std::string& alias)
      : LogicalOperator(logicalName, alias) {
    _properties.dataframe = false;  // Disallow dataframe input.
  }


  static PlistSpec const* makePlistSpec() {
    static PlistSpec argSpec {
        {"",  // positionals 
        RE(RE::LIST, {RE(PP(PLACEHOLDER_INPUT)), RE(PP(PLACEHOLDER_INPUT)), RE(PP(PLACEHOLDER_INPUT))})},
        {"transa", RE(PP(PLACEHOLDER_CONSTANT, TID_BOOL))},
        {"transb", RE(PP(PLACEHOLDER_CONSTANT, TID_BOOL))},
        {"alpha", RE(PP(PLACEHOLDER_CONSTANT, TID_DOUBLE))},
        {"beta", RE(PP(PLACEHOLDER_CONSTANT, TID_DOUBLE))}};
    return &argSpec;
  }

  std::string getInspectable() const override { return _fixer.str(); }

  ArrayDesc inferSchema(std::vector<ArrayDesc> schemas, std::shared_ptr<Query> query);
};

ArrayDesc PGEMMLogical::inferSchema(std::vector<ArrayDesc> schemas, std::shared_ptr<Query> query) {
  LOG4CXX_TRACE(logger, "PGEMMLogical::inferSchema(): begin.");

  enum dummy { ROW = 0, COL = 1 };
  enum dummy2 { AA = 0, BB, CC, NUM_MATRICES};  // which matrix: f(AA,BB,CC) = alpha AA BB + beta CC

  //
  // array checks (first 3 arguments)
  //
  assert(schemas.size() == NUM_MATRICES);
  checkScaLAPACKLogicalInputs(schemas, query, NUM_MATRICES, NUM_MATRICES);

  GEMMOptions options(_kwParameters, /*logicalOp:*/ true);

  //
  // cross-matrix constraints:
  //

  // check: cross-argument sizes
  if (nCol(schemas[AA], options.transposeA) != nRow(schemas[BB], options.transposeB)) {
    throw(PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR4)
          << "first matrix columns must equal second matrix rows (after optional transposes) ");
  }
  if (nRow(schemas[AA], options.transposeA) != nRow(schemas[CC])) {
    throw(PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR4)
          << "first and third matrix must have equal number of rows (after optional 1st matrix transpose)");
  }
  if (nCol(schemas[BB], options.transposeB) != nCol(schemas[CC])) {
    throw(PLUGIN_USER_EXCEPTION(DLANameSpace, SCIDB_SE_INFER_SCHEMA, DLA_ERROR4)
          << "first and third matrix must have equal number of columns (after optional 1st matrix transpose)");
  }

  //
  // inputs look good, create and return the output schema
  // note that the output has the dimensions and name bases of the third
  // argument C so that we can iterate on C, by repeating the exact same query,
  // NOTE: we are SUPER careful not to change its dim names if they are already
  // distinct.
  //       to make the iteration as simple as possible
  //
  const Dimensions& dimsCC = schemas[CC].getDimensions();

  std::pair<string, string> distinctNames = ScaLAPACKDistinctDimensionNames(dimsCC[ROW].getBaseName(), dimsCC[COL].getBaseName());
  _fixer.clear();
  Dimensions outDims(2);
  outDims[ROW] = DimensionDesc(
      distinctNames.first, dimsCC[ROW].getStartMin(),
      dimsCC[ROW].getCurrStart(), dimsCC[ROW].getCurrEnd(),
      dimsCC[ROW].getEndMax(), dimsCC[ROW].getRawChunkInterval(), 0);
  _fixer.takeDimension(ROW).fromArray(CC).fromDimension(ROW);

  outDims[COL] = DimensionDesc(
      distinctNames.second, dimsCC[COL].getStartMin(),
      dimsCC[COL].getCurrStart(), dimsCC[COL].getCurrEnd(),
      dimsCC[COL].getEndMax(), dimsCC[COL].getRawChunkInterval(), 0);
  _fixer.takeDimension(COL).fromArray(CC).fromDimension(COL);

  Attributes atts;
  atts.push_back(AttributeDesc("pgemm", TID_DOUBLE, 0, CompressorType::NONE));

  LOG4CXX_TRACE(logger, "PGEMMLogical::inferSchema(): end.");
  // ArrayDesc consumes the new copy, source is discarded.
  return ArrayDesc("PGEMM", atts.addEmptyTagAttribute(), outDims,
                   scidb::createDistribution(dtUndefined),
                   query->getDefaultArrayResidency());
}

REGISTER_LOGICAL_OPERATOR_FACTORY(PGEMMLogical, "pgemm");

}  // namespace scidb

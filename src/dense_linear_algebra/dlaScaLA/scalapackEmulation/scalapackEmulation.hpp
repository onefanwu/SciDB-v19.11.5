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
#ifndef SCALAPACK_EMULATION__HPP
#define SCALAPACK_EMULATION__HPP

#include <sys/types.h>
#include <cmath>
#include <dense_linear_algebra/scalapackUtil/scalapackTypes.hpp>

///
/// These methods are temporary scaffolding to allow SciDB to make calls that mimick
/// the original SciDB prototype where ScaLAPACK was called directly from SciDB.
/// The only remaining use of ScaLAPACK calls in SciDB is to the methods that concern
/// setting up ScaLAPACK array descriptors [descinit_()], reading and writing individual
/// subscripts form ScaLAPACK-formatted matrix/vector memory [pdelset_(), pdelget_()]
/// calculating a bound on the number of rows or columns of a local pieces of distributed
/// array memory[numroc_()], and getting information about the process grid
/// [blacs_gridinfo_()]
///
/// The goal is to replace and repackage this functionality as native C++ functionality
/// to reduce or eliminate the need to have SciDB link ScaLAPACK code, and all (or most)
/// of this code will be eliminated.
///
/// Therefore, we're not going to document how to use these calls at this time, we'll
/// wait until the ScaLAPACK emulation in SciDB is refined.  This is scheduled during
/// Aug-Sept/2012.
///

extern "C" {
    // these declarations are for routines that are work-alikes to to real ScaLAPACK
    // calls, (with the exception of those with "fake" in the name, which are additional)
    // but allow those calls to work in a non-mpi process.
    // For the moment, these routines are implemented in FORTRAN (mostly copies of the
    // originals, with slight mods sometimes) and that is why they are
    // a) extern "C" (to defeat C++ name-mangling)
    // b) end in "_" because fortran adds that
    // c) specify arguments as <type>&, because this forms an automatic conversion to
    //    the right type, followed by delivering its address to FORTRAN, and all
    //    variables in FORTRAN are passed by such references.

    // Utilities (all copies of the FORTRAN ones, with their names prefixed with scidb_)
    // these are modified by pruning their descent into the full ScaLAPACK call tree.
    // just enough have been kept to make them work-alike in a local-array only
    // and SciDB context.
    //

    // for the implementation of scidb_numroc-max
    inline size_t div_ceil(const size_t numerator, const size_t denominator) {
        size_t result;
        result = size_t((__uint128_t(numerator)+denominator-1)/denominator);
        return result;
    }
    // this version of the operator that follows is necessary for doing checks in logical operators
    // where the grid has not yet been assigned, so
    // 1) it does not need position in the processor grid
    // 2) it computes in full integer precision, so that the logical operator can make logical tests
    //    on the size, since in principle the total array could be larger than what is supported
    //    per-instance (depending on charactersistics of SCALAPACK/BLAS used)
    inline size_t scidb_numroc_max(const size_t MN, const size_t mn_b, const size_t np_rowcol) {
        slpp::int_t MN_B = slpp::int_t(mn_b);
        slpp::int_t NP_ROWCOL = slpp::int_t(np_rowcol);

        return div_ceil( div_ceil(MN,MN_B),NP_ROWCOL ) * MN_B;
    }

    // a local-only operation (even in its ScaLAPACK incarnation), giving exact sizes (partial block at right and bottom)
    slpp::int_t scidb_numroc_(const slpp::int_t&, const slpp::int_t&, const slpp::int_t&, const slpp::int_t&, const slpp::int_t&);

    // a local-only operation (also true of its ScaLAPACK incarnation)
    void scidb_descinit_(slpp::desc_t& desc,
                   const slpp::int_t& m, const slpp::int_t& n,
                   const slpp::int_t& mb, const slpp::int_t& nb,
                   const slpp::int_t& irSrc, const slpp::int_t& icSrc, const slpp::context_t& cTxt,
                   const slpp::int_t& lld, slpp::int_t& info);

    // scidb_ version is local only (vs ScaLPACK implementation which is global)
    void scidb_pdelset_(double* data, const slpp::int_t& row, const slpp::int_t& col,
                        const slpp::desc_t& desc, const double& val);

    // scidb_ version is local only (vs ScaLPACK implementation which is global)
    void scidb_pdelget_(const char& SCOPE, const char& TOP, double& ALPHA, const double* A,
                  const slpp::int_t& IA, const slpp::int_t& JA, const slpp::desc_t& DESCA);

    // this call does not have a local memory / global memory distinction.  It just provides the mapping
    // from global to local coordinates.  The Fortran call precedes the declaration, since there are so
    // many arguments:
    // SUBROUTINE INFOG2L(GRINDX, GCINDEX, DESCA, NPROW, NPCOL, MYROW, MYCOL, LRINDX, LCINDX, RSRC, CSRC )
    void scidb_infog2l_(const slpp::int_t& GRINDX, const slpp::int_t& GCINDEX, const slpp::desc_t& DESCA,
                        const slpp::int_t& NPROW, const slpp::int_t& NPCOL, const slpp::int_t& MYROW, const slpp::int_t& MYCOL,
                        const slpp::int_t& LRINDX, const slpp::int_t& LCINDX, const slpp::int_t& RSRC, const slpp::int_t& CSRC);

    // scidb_ version only returns exactly what was set by "scidb_set_blacs_gridinfo_" below
    // it is actually implemented as an "extern C" function written in C++
    // note that blacsContext is an integer index in real ScaLAPACK, referring to an allocated context.
    // our emulation is so much simpler, the context is just the four integers of the grid information
    void scidb_blacs_gridinfo_(const slpp::context_t&, const slpp::int_t&,
                               const slpp::int_t&, slpp::int_t&, slpp::int_t&);

    // This one does not even exist in ScaLAPACK it is only used to modify the behavior of
    // the above, which works differently than in ScaLAPACK (where there is a stack of ictxts)
    // it is actually implemented as an "extern C" function written in C++
    void scidb_set_blacs_gridinfo_(slpp::context_t& blacsContext,
                                   const slpp::int_t& nprow, const slpp::int_t& npcol,
                                   const slpp::int_t& myprow, const slpp::int_t& mypcol);

}

#endif // SCALAPACK_EMULATION__HPP

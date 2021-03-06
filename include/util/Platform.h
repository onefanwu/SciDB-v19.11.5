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

#ifndef UTIL_PLATFORM_H_
#define UTIL_PLATFORM_H_

/****************************************************************************/

#include <assert.h>                                      // For assert() macro

/****************************************************************************/
namespace scidb {
/****************************************************************************/

/**
 * Informs the compiler that the given function can never return. This enables
 * it to optimize without regard to what would happen if the function ever did
 * in fact return, and so generate slightly better code.  More importantly, it
 * helps to prevent spurious warnings of uninitialized variables,  unreachable
 * code, and the like.
 *
 * Defined with a macro as a nod toward portability.
 *
 * @see https://gcc.gnu.org/onlinedocs/gcc-4.0.4/gcc/Function-Attributes.html
 * for a full description of this and other per function attributes.
 */
#if defined(__GNUC__)
# define SCIDB_NORETURN       __attribute__((noreturn))
#else
# define SCIDB_NORETURN
#endif

/**
 * Instructs the compiler to work harder than usual to inline all calls to the
 * given inline function, which (very occasionally) leads to better code being
 * generated by the compiler. Use sparingly.
 *
 * Defined with a macro as a nod toward portability.
 *
 * @see https://gcc.gnu.org/onlinedocs/gcc-4.0.4/gcc/Function-Attributes.html
 * for a full description of this and other per function attributes.
 */
#if defined(__GNUC__)
# define SCIDB_FORCEINLINE    __attribute__((always_inline))
#else
# define SCIDB_FORCEINLINE
#endif

/**
 * Informs the compiler that the point in the code is known a priori not be be
 * reachable. This can be useful in situations where the compiler is otherwise
 * unable to deduce the unreachability of the code. Consider, for example, the
 * statement:
 * @code
 *
 *     switch (enum color = ...)
 *     {
 *         default:    SCIDB_UNREACHABLE();   // cases are exhaustive
 *         case red:   ...
 *         case blue:  ...
 *     }
 *
 * @endcode
 * which can be compiled without the need for the usual bounds checks on the
 * switch scrutinee.
 *
 * @see http://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html for additional
 * examples.
 */
#if defined(__GNUC__)
# define SCIDB_UNREACHABLE()  do {assert(false);__builtin_unreachable();} while(0)
#else
# define SCIDB_UNREACHABLE()  assert(false)
#endif

/**
 * Informs the compiler that an expression is known a priori to evaluate true.
 * This can be useful when optimizing hotspots in a program.  If, for example,
 * we know that our loop will run at least once, we can add an assume hint:
 * @code
 *
 *     SCIDB_ASSUME(0 < n);  // Hint to the optimizer
 *
 *     for (size_t i=0; i < n; ++i) { ...
 *
 * @endcode
 * enabling the compiler to eliminate the comparison of 'n' against zero prior
 * to entering the loop body for the first time, but without rewriting it to a
 * do-while structure, which is usually less desirable than a for-loop.
 *
 * @see http://en.chys.info/2010/07/counterpart-of-assume-in-gcc/ for further
 * examples.
 */
#define SCIDB_ASSUME(e)       do {if (!(e))SCIDB_UNREACHABLE();} while(0)

/**
 * Returns the number of elements in a native one dimensional array - that is,
 * an array whose length is visible to the compiler at compilation time.
 *
 * @param a a native C style array.
 */
#define SCIDB_SIZE(a)         (sizeof(a) / sizeof((a)[0]))

/**
 * Return true if invoked from within a release build - that is, if the NDEBUG
 * preprocessor symbol is defined.  Helpful for obviating what would otherwise
 * require a mess of preprocessor mumbo jumbo. For example:
 * @code
 *
 *     if (!isRelease()) {only_in_a_debug_build();} else { ...
 *
 * @endcode
 * has the advantage that the conditional code is still parsed,  type-checked,
 * and analyzed for correctness in a release build,  while remaining every bit
 * as efficient as the equivalent #ifdef version would be, since the optimizer
 * has exactly the information it needs to omit the dead code from the release
 * build.
 */
inline bool isRelease()
{
#if defined(NDEBUG)
    return true;
#else
    return false;
#endif
}

/**
 * Return true if called from within a debugging build- that is, if the NDEBUG
 * preprocessor symbol is undefined. Useful for obviating what would otherwise
 * require a mess of preprocessor mumbo jumbo. For example:
 * @code
 *
 *     if (isDebug()) {only_in_a_debug_build();} else { ...
 *
 * @endcode
 * has the advantage that the conditional code is still parsed,  type-checked,
 * and analyzed for correctness in a release build,  while remaining every bit
 * as efficient as the equivalent #ifdef version would be, since the optimizer
 * has exactly the information it needs to omit the dead code from the release
 * build.
 */
inline bool isDebug()
{
    return !isRelease();
}

/**
 * True if STL container constructors have full allocator support.
 *
 * Unfortunately this cannot be written as an inline, since that
 * approach would alter the scope of the container being constructed.
 *
 * DZ and JB found that GCC 4.9.1 was missing allocator-aware
 * constructors for certain containers (list, basic_string, queue).
 * Apparently clang has the same problem (what version?).  For
 * now, we'll just do our best to accomodate all target compilation
 * environments.
 */
#if defined(__clang__) || \
    (defined(__GNUC__) && __GNUC__ < 6)
    // Known to be missing some constructors with allocator parameters.
    //
    // According to the gcc mailing list, this was not fixed until GCC 6
    // for std::list.
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=55409#c16
#   define STL_HAS_FULL_ALLOCATOR_SUPPORT 0
#else
    // Assumed to *not* have problems (but that may be incorrect).
#   define STL_HAS_FULL_ALLOCATOR_SUPPORT 1
#endif

/****************************************************************************/
}
/****************************************************************************/
#endif
/****************************************************************************/

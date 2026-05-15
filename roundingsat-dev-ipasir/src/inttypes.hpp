/***********************************************************************
Copyright (c) 2014-2020, Jan Elffers
Copyright (c) 2019-2021, Jo Devriendt
Copyright (c) 2020-2021, Stephan Gocht
Copyright (c) 2014-2024, Jakob Nordström
Copyright (c) 2022-2024, Andy Oertel
Copyright (c) 2024, Marc Vinyals

Parts of the code were copied or adapted from MiniSat.

MiniSat -- Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
           Copyright (c) 2007-2010  Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***********************************************************************/

#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#if WITHGMP
#include <boost/multiprecision/gmp.hpp>
#endif  // WITHGMP

namespace rs {

// This macro is adapted from Abseil
// https://github.com/abseil/abseil-cpp/blob/master/absl/base/config.h
//
// RS_HAVE_INTRINSIC_INT128
//
// Checks whether the __int128 compiler extension for a 128-bit integral type is
// supported.
//
// Note: __SIZEOF_INT128__ is defined by Clang and GCC when __int128 is
// supported, but we avoid using it in certain cases:
// * On Clang:
//   * Building using Clang for Windows, where the Clang runtime library has
//     128-bit support only on LP64 architectures, but Windows is LLP64.
// * On Nvidia's nvcc:
//   * nvcc also defines __GNUC__ and __SIZEOF_INT128__, but not all versions
//     actually support __int128.
#ifdef RS_HAVE_INTRINSIC_INT128
#error RS_HAVE_INTRINSIC_INT128 cannot be directly set
#elif defined(__SIZEOF_INT128__)
#if (defined(__clang__) && !defined(_WIN32)) ||           \
    (defined(__CUDACC__) && __CUDACC_VER_MAJOR__ >= 9) || \
    (defined(__GNUC__) && !defined(__clang__) && !defined(__CUDACC__))
#define RS_HAVE_INTRINSIC_INT128 1
#elif defined(__CUDACC__)
// __CUDACC_VER__ is a full version number before CUDA 9, and is defined to a
// string explaining that it has been removed starting with CUDA 9. We use
// nested #ifs because there is no short-circuiting in the preprocessor.
// NOTE: `__CUDACC__` could be undefined while `__CUDACC_VER__` is defined.
#if __CUDACC_VER__ >= 70000
#define RS_HAVE_INTRINSIC_INT128 1
#endif  // __CUDACC_VER__ >= 70000
#endif  // defined(__CUDACC__)
#endif  // RS_HAVE_INTRINSIC_INT128

#if WITHGMP
using int128 = boost::multiprecision::int128_t;  // NOTE: a bit slower than __int128, but plays nice with mpz_int
using bigint = boost::multiprecision::mpz_int;   // NOTE: requires GMP
#else
#if RS_HAVE_INTRINSIC_INT128
using int128 = __int128;
inline std::ostream& operator<<(std::ostream& os, const __int128& x) {
    // Outputs an int128 as a string.
  if (x < 0) return os << "-" << -x;
  if (x < 10) return os << (char)(x + '0');
  return os << x / 10 << (char)(x % 10 + '0');
}
#else
using int128 = boost::multiprecision::int128_t;
#endif // RS_HAVE_INTRINSIC_INT128
using bigint = boost::multiprecision::cpp_int;
#endif  // WITHGMP
using int256 = boost::multiprecision::int256_t;
using BigCoef = bigint;
using BigVal = bigint;

}  // namespace rs

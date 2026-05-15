/***********************************************************************
Based on IPASIR and IPAMIR.

Copyright (c) 2014, Tomas Balyo, Karlsruhe Institute of Technology.
Copyright (c) 2014, Armin Biere, Johannes Kepler University.

Copyright (c) 2021, Jeremias Berg, University of Helsinki.
Copyright (c) 2021, Matti Järvisalo, University of Helsinki.
Copyright (c) 2021, Andreas Niskanen, University of Helsinki.

Copyright (c) 2024, Jakob Nordström, Copenhagen University and Lund University.
Copyright (c) 2024, Andy Oertel, Lund University.
Copyright (c) 2024, Marc Vinyals, University of Auckland.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
***********************************************************************/

#ifndef ipasirpb_h_INCLUDED
#define ipasirpb_h_INCLUDED

#if defined(IPASIRPB_SHARED_LIB)
    #if defined(_WIN32) || defined(__CYGWIN__)
        #if defined(BUILDING_IPASIRPB_SHARED_LIB)
            #if defined(__GNUC__)
                #define IPASIRPB_API __attribute__((dllexport))
            #elif defined(_MSC_VER)
                #define IPASIRPB_API __declspec(dllexport)
            #endif
        #else
            #if defined(__GNUC__)
                #define IPASIRPB_API __attribute__((dllimport))
            #elif defined(_MSC_VER)
                #define IPASIRPB_API __declspec(dllimport)
            #endif
        #endif
    #elif defined(__GNUC__)
        #define IPASIRPB_API __attribute__((visibility("default")))
    #endif

    #if !defined(IPASIRPB_API)
        #if !defined(IPASIRPB_SUPPRESS_WARNINGS)
            #warning "Unknown compiler. Not adding visibility information to IPASIRPB symbols."
            #warning "Define IPASIRPB_SUPPRESS_WARNINGS to suppress this warning."
        #endif
        #define IPASIRPB_API
    #endif
#else
    #define IPASIRPB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// All functions return this.
// >= 0 means OK
// < 0 means error
typedef enum ipasirpb_return {
  IPASIRPB_OK = 0,
  IPASIRPB_UNKNOWN = 0,
  IPASIRPB_SAT = 10,
  IPASIRPB_UNSAT = 20,
  IPASIRPB_INCONSISTENT = 21,
  IPASIRPB_OPT = 30,
  // there is object to return
  IPASIRPB_NULL = -1,
  // call makes no sense (e.g. solution of an unsat formula)
  IPASIRPB_INVALID = -2,
  // this particular implementation does not support the call at this time
  IPASIRPB_NOT_SUPPORTED = -3,
  // the result would not fit in a 64-bit integer
  IPASIRPB_OVERFLOW = -4,
} ipasirpb_return;

// used in constraints and objectives
typedef enum ipasirpb_relation {
  IPASIRPB_MIN,
  IPASIRPB_MAX,
  IPASIRPB_EQ,
  IPASIRPB_GEQ,
  IPASIRPB_LEQ,
} ipasirpb_relation;

// should be easy to (de)serialise to/from Boost/GMP/Java/Python
// https://www.boost.org/doc/libs/1_85_0/libs/multiprecision/doc/html/boost_multiprecision/tut/import_export.html
// https://gmplib.org/manual/Integer-Import-and-Export
// https://docs.oracle.com/javase/8/docs/api/java/math/BigInteger.html#BigInteger-int-byte:A-
// https://peps.python.org/pep-0757/
typedef struct ipasirpb_bigint {
  char sign;
  char* magnitude;
  int64_t len;
} ipasirpb_bigint;

// default type for constraints and objectives
// literals are in dimacs format: d represents x_d and -d represents ~x_d = 1 - x_d
// (coeffs, lits, len) represents a sum of terms T = ∑ coeffs[i] · lits[i] for i in [0..len)
// rel is either an objective direction (min/max) or a comparison operator (=/≤/≥)
// if rel is an objective direction then this represents the objective T + rhs
// if rel is a comparison operator then this represents the constrainst T rel rhs
// RFC: should this include proof data such as IDs?
typedef struct ipasirpb_terms {
  ipasirpb_bigint* coeffs;
  int64_t* lits;
  int64_t len;
  ipasirpb_relation rel;
  ipasirpb_bigint rhs;
} ipasirpb_terms;

// fixed-width type for constraints and objectives
// cf ipasirpb_terms
typedef struct ipasirpb_terms64 {
  int64_t* coeffs;
  int64_t* lits;
  int64_t len;
  ipasirpb_relation rel;
  int64_t rhs;
} ipasirpb_terms64;

// Return the name and version of the solver implementing the API.
IPASIRPB_API char const* ipasirpb_signature ();
// Obtain a solver object, which can be used in other calls.
IPASIRPB_API void* ipasirpb_init ();
// Free solver memory, solver can no longer be used.
IPASIRPB_API void ipasirpb_release (void * solver);
// Set solver options, implementation defined.
// RFC: should value be a void* ?
IPASIRPB_API ipasirpb_return ipasirpb_set_option (void * solver, char const* option, char const* value);

// Calling conventions
// The user maintains ownership of any input parameters. The user is responsible for freeing input parameters, and may delete the parameters as soon as the call returns. The solver should make copies as needed.
// The solver maintains ownership of any output parameters. The solver is responsible for freeing outputs parameters, and may delete the parameters as soon as another API call is made. The user should make copies as needed.
// Most successful calls return 0
// Successful solve class return > 0
// Error calls return < 0
// A solver may return unsupported depending on previous calls. e.g. a solver might not support modifying the problem after the first call to solve.

// Add a constraint.
IPASIRPB_API ipasirpb_return ipasirpb_add64 (void * solver, ipasirpb_terms64 constr);
IPASIRPB_API ipasirpb_return ipasirpb_add (void * solver, ipasirpb_terms constr);
// Solve the problem. Assumptions is a list of literals of length len. To solve without assumptions, pass a null pointer and a length of 0.
  IPASIRPB_API ipasirpb_return ipasirpb_solve (void * solver, int64_t* assumptions, int64_t len, int seconds);
// Return the value of literal lit. Only valid if the last call to solve returned SAT or OPT.
// RFC: should the function return all literals at once?
IPASIRPB_API ipasirpb_return ipasirpb_litval (void * solver, int64_t lit, bool* value);
// Return a core. Only valid if the last call to solve returned INCONSISTENT. If there are multiple cores, each successive call will return a new core. Returns IPASIRPB_NULL once all cores have been exhausted.
// RFC: should the function return all cores at once?
IPASIRPB_API ipasirpb_return ipasirpb_get_core64 (void * solver, ipasirpb_terms64* core);
IPASIRPB_API ipasirpb_return ipasirpb_get_core (void * solver, ipasirpb_terms* core);

// Set the objective. Subsequent calls replace the objective.
IPASIRPB_API ipasirpb_return ipasirpb_set_obj64 (void * solver, ipasirpb_terms64 obj);
IPASIRPB_API ipasirpb_return ipasirpb_set_obj (void * solver, ipasirpb_terms obj);
// Get the primal bound on the objective. That is, the value of the best solution found so far.
// RFC: should this be the best solution or the last solution?
IPASIRPB_API ipasirpb_return ipasirpb_get_primal_bound64 (void * solver, int64_t* value);
IPASIRPB_API ipasirpb_return ipasirpb_get_primal_bound (void * solver, ipasirpb_bigint* value);
// Get the dual bound on the objective. That is, no solution can be strictly better than this bound.
IPASIRPB_API ipasirpb_return ipasirpb_get_dual_bound64 (void * solver, int64_t* value);
IPASIRPB_API ipasirpb_return ipasirpb_get_dual_bound (void * solver, ipasirpb_bigint* value);

  // ALBERT
IPASIRPB_API ipasirpb_return ipasirpb_set_periodic_function(void * solver, int (*f) (int x) );
IPASIRPB_API ipasirpb_return ipasirpb_decide(void * solver, int lit);
IPASIRPB_API ipasirpb_return ipasirpb_propagate(void * solver, bool* conflict);
IPASIRPB_API ipasirpb_return ipasirpb_assignedVars(void * solver, int* n);
IPASIRPB_API ipasirpb_return ipasirpb_is_true_lit(void * solver, int lit, bool* v);
IPASIRPB_API ipasirpb_return ipasirpb_is_false_lit(void * solver, int lit, bool* v);
IPASIRPB_API ipasirpb_return ipasirpb_backjump(void * solver, int levels);
IPASIRPB_API ipasirpb_return ipasirpb_assume_and_propagate(void * solver, int lit, bool* conflict);
#ifdef __cplusplus
} // closing extern "C"
#endif

#endif

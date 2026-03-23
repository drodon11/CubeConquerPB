#pragma once

#include<cassert>
#include<iostream>
#include <vector>
#include <numeric>

using namespace std;

#define TWOTOTHE30TH (long long)(1<<30)
#define TWOTOTHE31ST (((long long)(1))<<31)
#define debug 0
#define bdebug 0

template<class T>
T GCD(T a, T b) {   // new
  assert(a>=0); assert(b>=0);
  return std::gcd(a, b);
}

template<class T>
inline T divisionRoundedUp( T n, T c ) {
  assert( c>0 ); assert( n>0 );
  T d = n / c;
  if ( d*c != n ) d = d+1;
  return(d);
}

inline bool isOrdered(const vector<int>& v) {
  for (uint i = 1; i < v.size(); ++i) if (abs(v[i-1]) >= abs(v[i])) return false;
  return true;
}

// v is vector of <coeff,var>. Returns whether increasingly order by variable
inline bool isOrderedByIncreasingVariable(const vector<pair<int,int> >& v) {
  for (uint i = 1; i < v.size(); ++i) if (abs(v[i-1].second) >= abs(v[i].second)) return false;
  return true;
}

inline bool isOrderedByDecreasingCoefficient(const vector<pair<int,int> >& v) {
  for (uint i = 1; i < v.size(); ++i) if (v[i-1].first < v[i].first) return false;
  return true;
}


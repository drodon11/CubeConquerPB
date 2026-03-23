#pragma once

#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <iostream>
#include "Constraint.h"

using namespace std;

class PBConstraint;
class Clause;
class Model;

class WConstraint {

  vector<pair<int,int> > lhs; // pairs are <coeff,lit> with coeff > 0
  int rhs;                    // constraint is lhs >= rhs
  
 
 public:

  // posCoeffs[i] > 0 and r >= 0
  // creates posCoeffs*lits >= r
  WConstraint (const vector<int>& posCoeffs, const vector<int>& lits, int r);

  // b ==> creates tautology 0 >= 0, not(b) ==> creates contradiction 0 >= 1
  WConstraint (bool b = true);

  // pairs are <coeff,lit> with coef > 0 and r >= 0
  // creates l >= r
  WConstraint(const vector<pair<int,int> >& l, int r);

  WConstraint (const PBConstraint& pc);
  
  //  WConstraint (const Cardinality& c);

  WConstraint (const Clause& c);

  int  getSize           ( ) const;  // return number of variables
  int  getNumInts        ( ) const;
  int  getConstant       ( ) const;
  void setConstant       ( int c ); // c >= 0
  void clearLHS          ( );
  void reset             ( );
  void setLHS            ( const vector<pair<int,int>>& l );
  
  pair<int,int> getCoefficientLiteral(int var) const;
  int  getCoefficient    (int var) const; // coeff that goes with var or -var
  int  getLiteral        (int var) const; // return var, -var or zero
  
  pair<int,int> getCoefficientLiteralBinarySearch (int var) const;
  int  getCoefficientBinarySearch (int var) const; // coeff that goes with var or -var
  int  getLiteralBinarySearch(int var) const; // return var, -var or zero
  
  pair<int,int> getCoefficientLiteralBinarySearch (int var, int coef) const;
  int  getCoefficientBinarySearch (int var, int coef) const; // coeff that goes with var or -var
  int  getLiteralBinarySearch(int var, int coef) const; // return var, -var or zero
  
  int  getIthLiteral     (int i) const; // 0 <= i < getSize()
  int  getIthCoefficient (int i) const; // 0 <= i < getSize()
  void setIthLiteral     (int i, int lit); // 0 <= i < getSize()
  void setIthCoefficient (int i, int coeff); // 0 <= i < getSize()
  void addMonomial       (int coeff, int lit); // coeff > 0, var(lit) not in the constraint
  

  void sortByIncreasingCoefficient     ();
  void sortByDecreasingCoefficient     ();
  void sortByIncreasingVariable        ();
  void sortByFalseCoefficient          (const Model& m);
  void sortByModel                     (const Model& m);
  bool isOrderedByIncreasingVariable   ( ) const;
  bool isOrderedByDecreasingCoefficient ( ) const;

  void removeDuplicates ( ); // assumes ordered by increasing variable
  bool isClause ( ) const;
  //  bool isCardinality ( ) const;

  bool isInconsistent () const;
  void simplify ( );
  friend ostream& operator<<(ostream& os, const WConstraint& wc);
 private:

  int  getCoefficientBinarySearch (int var, int l, int r) const; // coeff that goes with var or -var
  pair<int,int> getCoefficientLiteralBinarySearch (int var, int l, int r) const;
  // For debugging
  bool coeffsStrictlyPositive(const vector<pair<int,int> >& l) const;
  
  int  maxCoefOfConstraint ( ) const;

};

#include "PBConstraint.h"
#include "Clause.h"
#include "Model.h"


inline WConstraint::WConstraint (const vector<int>& posCoeffs, const vector<int>& lits, int r)
  : rhs(r) {
  assert(posCoeffs.size() == lits.size());
  assert(r >= 0);
  
  for (uint i = 0; i < posCoeffs.size(); ++i) {
    assert(posCoeffs[i] > 0);
    lhs.push_back({posCoeffs[i],lits[i]});
  }
}


inline WConstraint::WConstraint (bool b) : rhs(not b) {
}

inline void WConstraint::reset () {
  lhs.clear();
  rhs = 0;
}

inline WConstraint::WConstraint(const vector<pair<int,int> >& l, int r) : lhs(l), rhs(r) {
  assert(coeffsStrictlyPositive(l));
  assert(r >= 0);
}

inline WConstraint::WConstraint(const PBConstraint& pc) {
  for (int i = 0; i < pc.getSize(); ++i) 
    lhs.push_back({abs(pc.getIthCoefficient(i)),pc.getIthLiteral(i)});
  rhs = pc.getConstant();
}  

inline WConstraint::WConstraint (const Clause& c) {
  for (int i = 0; i < c.getSize(); ++i) 
    lhs.push_back({1,c.getIthLiteral(i)});
  rhs = 1;
  
}

inline int WConstraint::getNumInts ( ) const {
  return 5 + 2*getSize();
}

inline int WConstraint::getSize( ) const {  // return number of variables
  return lhs.size();
}
  
inline int WConstraint::getConstant ( ) const {
  return rhs;
}

inline void WConstraint::setConstant ( int c ) { // c >= 0
  assert(c >= 0);
  rhs = c;
}

inline void WConstraint::clearLHS ( ) {
  lhs.clear();
}
inline void WConstraint::setLHS ( const vector<pair<int,int>>& l ) {
  assert(l.size() > 0);
  lhs = l;
}

inline pair<int,int> WConstraint::getCoefficientLiteral (int var) const {
  assert(var > 0);
  for (uint i = 0; i < lhs.size(); ++i)
    if (abs(lhs[i].second) == var) return lhs[i];
  return {0, 0};
}

inline int WConstraint::getCoefficient (int var) const { // coeff that goes with var or -var
  return getCoefficientLiteral(var).first;
}

inline int WConstraint::getLiteral (int var) const { // return var, -var or zero
  return getCoefficientLiteral(var).second;
}

inline pair<int,int> WConstraint::getCoefficientLiteralBinarySearch (int var) const {
  assert(var > 0);
  assert(isOrderedByIncreasingVariable());
  int l = 0;
  int r = lhs.size()-1;
  while (l <= r) {
    int m = (l+r)/2;
    int a = abs(lhs[m].second);
    if      (a < var) l = m+1;
    else if (a > var) r = m-1;
    else return lhs[m];
  }
  return {0,0};
}

inline pair<int,int> WConstraint::getCoefficientLiteralBinarySearch (int var, int coef) const {
  assert(var > 0);
  assert(isOrderedByDecreasingCoefficient());
  int l = 0;
  int r = lhs.size()-1;
  while (l <= r) {
    int m = (l+r)/2;
    int c = lhs[m].first; // coef
    if      (c < coef) r = m-1;
    else if (c > coef) l = m+1;
    else {
      int i = m;
      while (i >= l) {
        int x = abs(lhs[i].second); // lit
        if (x == var) return lhs[i];
        --i;
      }
      i = m + 1;
      while (i <= r) {
        int x = abs(lhs[i].second); // lit
        if (x == var) return lhs[i];
        ++i;
      }
      
      return {0,0};
    }
  }
  return {0,0};
}

inline int WConstraint::getCoefficientBinarySearch (int var) const {
  return getCoefficientLiteralBinarySearch(var).first;
}

inline int WConstraint::getLiteralBinarySearch (int var) const {
  return getCoefficientLiteralBinarySearch(var).second;
}

inline int WConstraint::getCoefficientBinarySearch (int var, int coef) const {
  return getCoefficientLiteralBinarySearch(var, coef).first;
}

inline int WConstraint::getLiteralBinarySearch (int var, int coef) const {
  return getCoefficientLiteralBinarySearch(var, coef).second;
}


inline int WConstraint::getIthLiteral(int i) const { // 0 <= i < getSize()
  assert( 0 <= i and i < getSize());
  return lhs[i].second;
}

inline int WConstraint::getIthCoefficient(int i) const { // 0 <= i < getSize()
  assert( 0 <= i and i < getSize());
  return lhs[i].first;
}

inline void WConstraint::setIthLiteral(int i, int lit) { // 0 <= i < getSize()
  assert( 0 <= i and i < getSize());
  lhs[i].second = lit;
}

inline void WConstraint::setIthCoefficient(int i, int coeff) { // 0 <= i < getSize()
  assert( 0 <= i and i < getSize());
  assert(coeff > 0);
  lhs[i].first = coeff;
} 

inline void WConstraint::addMonomial (int coeff, int lit) { // coeff > 0, var(lit) not in the constraint
  assert(getCoefficient(abs(lit)) == 0);
  assert(coeff > 0);
  lhs.push_back({coeff,lit});
}

inline bool WConstraint::isInconsistent() const {
    return rhs > 0 and lhs.size() == 0;
}

inline void WConstraint::sortByIncreasingCoefficient() {
  sort( lhs.begin(), lhs.end(),
         [](const pair<int,int> & m1, const pair<int,int> & m2) { return abs(m1.first) < abs(m2.first); } );
}

struct dec_coef_inc_var {
  bool operator () (pair<int,int> m1, pair<int,int> m2) const {
    return abs(m1.first) > abs(m2.first) || (abs(m1.first) == abs(m2.first) && abs(m1.second) < abs(m2.second));
  }
};

//inline void WConstraint::sortByDecreasingCoefficient() {
  //sort( lhs.begin(), lhs.end(), dec_coef_inc_var());
//}

inline void WConstraint::sortByDecreasingCoefficient() {
  sort( lhs.begin(), lhs.end(),
         [](const pair<int,int> & m1, const pair<int,int> & m2) { return m1.first > m2.first; } );
}

//inline void WConstraint::sortByDecreasingCoefficient() {
  //sort( lhs.begin(), lhs.end(),
         //[](const pair<int,int> & m1, const pair<int,int> & m2) {
           //return abs(m1.first) > abs(m2.first) || (abs(m1.first) == abs(m2.first) && abs(m1.second) < abs(m2.second));
          //} );
//}

//inline bool WConstraint::isOrderedByDecreasingCoefficient ( ) const {
  //return ::isOrderedByDecreasingCoefficient(lhs);
//}


inline void WConstraint::removeDuplicates ( ){
  assert(isOrderedByIncreasingVariable());
  uint i = 0;

  vector<pair<int,int> > newLhs;
  int newRhs = rhs;
  
  while (i < lhs.size()) {
    // Start block with new variable
    int v = abs(lhs[i].second);
    int posCoeffs = 0;
    int negCoeffs = 0;
    while (i < lhs.size() and abs(lhs[i].second) == v) {
      if (lhs[i].second > 0) posCoeffs += lhs[i].first;
      else                   negCoeffs += lhs[i].first;
      ++i;
    }
    // End block
    if (posCoeffs > negCoeffs) {
      newLhs.push_back({posCoeffs - negCoeffs, v});
      newRhs -= negCoeffs;
    }
    else if (posCoeffs < negCoeffs) {
      newLhs.push_back({negCoeffs - posCoeffs, -v});
      newRhs -= posCoeffs;
    }
    else newRhs -= posCoeffs;
  }
  lhs = newLhs;
  rhs = newRhs;
}

inline void WConstraint::sortByIncreasingVariable() {
  sort( lhs.begin(), lhs.end(),
         [](const pair<int,int> & m1, const pair<int,int> & m2) {
    return abs(m1.second) < abs(m2.second);
  } );
}

inline bool WConstraint::isOrderedByIncreasingVariable ( ) const {
  return ::isOrderedByIncreasingVariable(lhs);
}

inline bool WConstraint::isOrderedByDecreasingCoefficient ( ) const {
  return ::isOrderedByDecreasingCoefficient(lhs);
}

inline bool WConstraint::coeffsStrictlyPositive(const vector<pair<int,int> >& l) const {
  for (uint i = 0; i < l.size(); ++i) 
    if (l[i].first <= 0) return false;
  return true;
}

inline bool WConstraint::isClause ( ) const {
  if (getConstant() != 1) return false;
  for (int i = 0; i < getSize(); ++i) 
    if (getIthCoefficient(i) != 1) return false;
  return true;
}

inline int WConstraint::maxCoefOfConstraint( ) const {
  return ::maxCoefOfConstraint(*this);
}

inline void WConstraint::simplify() {
  ::simplify(*this);
}



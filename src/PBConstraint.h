#pragma once

#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <iostream>
#include "Constraint.h"

class WConstraint;

using namespace std;

static const int INTS_MEM = 7;

class PBConstraint {
  int *mem;
  // size[-1], rhs[-2], watchIdx[-3], numBackjump[-4], activity[-5], isInit[-6], LBD[-7]
  // mem[0] is <coeff1,x1,coeff2,x2,coeff3,x3,....>
  // mem[2*size()] starts number of propagations of every lit
           
 public:
  
  PBConstraint ( );
  PBConstraint (const PBConstraint& c);
  // PRE: lits are ordered by increasing variable, posCoeffs > 0, posCoeff.size() > 0,  r > 0, 
  PBConstraint (const vector<int>& posCoeffs, const vector<int>& lits, int r, bool isInit, int activity, int LBD);
  // PRE: in l, pairs are <coeff,lit> and are ordered by increasing variable
  // coeffs > 0, r > 0, l.size() > 0
  PBConstraint (const vector<pair<int,int> >& l, int r, bool isInit, int activity, int LBD); 
  // PRE: wc is ordered by increasing variable. wc is not a tautology or contradiction
  PBConstraint (const WConstraint& wc, bool isInit, int activity, int LBD);
  ~PBConstraint ();

  void freeMemory        ( );
  int  getSize           ( )       const;
  int  getNumInts        ( )       const;
  int  getConstant       ( )       const;
  int  getCoefficient    (int var) const; // coeff that goes with var or -var
  int  getLiteral        (int var) const; // return var, -var or zero
  int  getIthLiteral     (int i)   const;   // 0 <= i < getSize()
  int  getIthCoefficient (int i)   const;   // 0 <= i < getSize()
  int  getActivity       (     )   const;
  void setActivity       (int a);
  void increaseActivity  (int amount);
  bool isInitial         (     )   const;
  void setInitial        (bool b);
  int  getLBD            (     )   const;
  void setLBD            (int lbd);
  void setConstant (int co);
  
  bool isIthLitWatched         (int i)   const;
  void setIthLitWatched        (int i, bool w);
  int getMaxWIdx        (     )   const;  // optimization 1: the max idx of watched lits  
  void setMaxWIdx        (int i);  
  int getNumBackjump    (     )   const;  // optimization 1: check if there's already one backjump before propagating
  void setNumBackjump    (int i); 
  
  int  maxCoefOfConstraint( ) const;
  void simplify ( );
  friend ostream& operator<<(ostream& os, const PBConstraint& pc);

 private:

  // We do not allow assignments
  PBConstraint operator= (const PBConstraint& c) {assert(false); return *this; }

  void setSize(int s)        { mem[-1] = s;}
  void setIthCoefficient (int i, int value) {
    assert(value > 0);
    assert(i < getSize());
    assert(i >= 0);
    mem[2*i] = value;
  }

  template <class Constraint>
  friend
  void simplify(Constraint& cons);
};

#include "WConstraint.h"

inline PBConstraint::PBConstraint (const PBConstraint& c) {
  assert(c.mem != 0);
  mem = c.mem;
}

inline PBConstraint::PBConstraint ( ){
  mem = 0;
}

// PRE: lits are ordered by increasing variable, posCoeffs > 0, posCoeff.size() > 0,  r > 0, 
inline PBConstraint::PBConstraint (const vector<int>& posCoeffs, const vector<int>& lits, int r, bool isInit, int activity, int LBD) {
  assert(isOrdered(lits));
  assert(posCoeffs.size() == lits.size());
  assert(posCoeffs.size() > 0);
  assert(r > 0);

  mem = new int[2*posCoeffs.size()+INTS_MEM];
  mem += INTS_MEM;
  
  setSize((int)posCoeffs.size());
  setConstant(r);
  setMaxWIdx(0);
  setNumBackjump(0);
  setActivity(activity);
  setInitial(isInit);
  setLBD(LBD);
  
  for (uint i = 0; i < lits.size(); ++i) {
    assert(posCoeffs[i] > 0);
    mem[2*i] = posCoeffs[i];
    mem[2*i + 1] = lits[i];
  }
}


// PRE: in l, pairs are <coeff,lit> and are ordered by increasing variable
// coeffs > 0, r > 0, l.size() > 0
inline PBConstraint::PBConstraint (const vector<pair<int,int> >& l, int r, bool isInit, int activity, int LBD) {
  assert(l.size() > 0);
  assert(isOrderedByIncreasingVariable(l));
  assert(r > 0);
  
  mem = new int[2*l.size()+INTS_MEM];
  mem +=INTS_MEM;
  
  setSize((int)l.size());
  setConstant(r);
  setMaxWIdx(0);
  setNumBackjump(0);
  setActivity(activity);
  setInitial(isInit);
  setLBD(LBD);
  
  for (uint i = 0; i < l.size(); ++i) {
    assert(l[i].first > 0);
    mem[2*i] = l[i].first;
    mem[2*i + 1] = l[i].second;
  }
} 

// PRE: wc is ordered by increasing variable. wc is not a tautology or contradiction
inline PBConstraint::PBConstraint (const WConstraint& wc, const bool isInit, int activity, int LBD) {
  assert(wc.getSize() > 0);
  
  mem = new int[2*wc.getSize() + INTS_MEM];
  mem += INTS_MEM;
  
  setSize((int)wc.getSize());
  setConstant(wc.getConstant());
  setMaxWIdx(0);
  setNumBackjump(0);
  setActivity(activity);
  setInitial(isInit);
  setLBD(LBD);
  
  for (int i = 0; i < wc.getSize(); ++i) {
    mem[2*i] = wc.getIthCoefficient(i);
    mem[2*i + 1] = wc.getIthLiteral(i);
  }
}

inline PBConstraint::~PBConstraint () {}

inline void PBConstraint::freeMemory ( ) {
  if (mem) {
    mem -= INTS_MEM;
    delete[] mem;
  }
}

inline int PBConstraint::getSize ( )     const { assert(mem); return mem[-1]; }
inline int PBConstraint::getNumInts ( )     const { return INTS_MEM + 2*getSize();}
inline int PBConstraint::getConstant ( ) const { assert(mem); return mem[-2];}
inline void PBConstraint::setConstant (int co)  { mem[-2] = co; }
inline int PBConstraint::getCoefficient (int var) const {
  // coeff that goes with var or -var
  assert(var > 0);
  for (int i = 1; i < 2*getSize(); i+=2) {
    if (abs(mem[i]) == var) return mem[i-1];
  }
  return 0;
}

inline int PBConstraint::getLiteral (int var) const { 
  // return var, -var or zero
  assert(var > 0);
  for (int i = 1; i < 2*getSize(); i+=2) {
    if (abs(mem[i]) == var) return mem[i];
  }
  return 0;
}

inline int PBConstraint::getIthLiteral (int i) const {
  assert(i < getSize());
  assert(i >= 0);
  return mem[2*i + 1];
}

inline int PBConstraint::getIthCoefficient (int i) const {
  // if (i >= getSize() ) {    cout << endl << *this << endl;  }
  assert(i < getSize());
  assert(i >= 0);
  return mem[2*i];
}


inline int  PBConstraint::getActivity ( ) const         { assert(mem); return mem[-5];}
inline void PBConstraint::setActivity (int a)           { assert(mem); mem[-5] = a;}
inline void PBConstraint::increaseActivity (int amount) { assert(mem); mem[-5] += amount;}
inline bool PBConstraint::isInitial ( )   const         { assert(mem); return mem[-6];}
inline void PBConstraint::setInitial(bool b)            { assert(mem); mem[-6] = b;}
inline int  PBConstraint::getLBD (     )   const        { assert(mem); return mem[-7];}
inline void PBConstraint::setLBD (int lbd)              { assert(mem); mem[-7] = lbd;}

inline int  PBConstraint::getMaxWIdx ( )   const        { assert(mem); return mem[-3];}
inline void PBConstraint::setMaxWIdx (int i)            { assert(mem); mem[-3] = i;}
inline int PBConstraint::getNumBackjump ( ) const      { assert(mem); return mem[-4];}
inline void PBConstraint::setNumBackjump (int nc)      { assert(mem); mem[-4] = nc;  }

inline bool PBConstraint::isIthLitWatched (int i) const { 
  assert(i < getSize() and i >= 0); 
  return (mem[2*i] < 0);
}
inline void PBConstraint::setIthLitWatched (int i, bool w) { 
  assert(i < getSize() and i >= 0); 
  assert(isIthLitWatched(i) != w);
  mem[2*i] = -(mem[2*i]);
}

inline int PBConstraint::maxCoefOfConstraint( ) const {
  return ::maxCoefOfConstraint(*this);
}

inline void PBConstraint::simplify() {
  ::simplify(*this);
}



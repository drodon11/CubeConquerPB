#pragma once

#include <vector>
#include <iostream>
#include <iterator>

using namespace std;

class WConstraint;

class Clause {

  int* mem;
   // size[-1], watchIdx[-2], activity[-3], isInit[-4], LBD[-5]
  
 public:

  //class iterator : public std::iterator<std::bidirectional_iterator_tag,int*> {

    //int *itr;

  //public:
    //iterator (int* temp) : itr(temp) {}
    //iterator (const iterator& myitr) : itr(myitr.itr) {}
    //iterator& operator++()    {itr++; return *this;}//Pre-increment
    //iterator  operator++(int) {iterator tmp(*this); itr++; return tmp;}//Post-increment
    //bool operator== (const iterator& rhs) const {return itr == rhs.itr;}
    //bool operator!= (const iterator& rhs) const {return itr != rhs.itr;}
    //bool operator<= (const iterator& rhs) const {return itr <= rhs.itr;}
    //bool operator>= (const iterator& rhs) const {return itr >= rhs.itr;}
    //int  operator-  (const iterator& rhs) const {return itr - rhs.itr;}
    //int& operator* () {return *itr;}
    //int* operator->() {return  itr;}    
  //};
  
  class iterator {

    int *itr;

    public:
    
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type = int;
      using difference_type = int;
      using pointer = int*;
      using reference = int&;
    
      iterator (int* temp) : itr(temp) {}
      iterator (const iterator& myitr) : itr(myitr.itr) {}
      iterator& operator++()    {itr++; return *this;}//Pre-increment
      iterator  operator++(int) {iterator tmp(*this); itr++; return tmp;}//Post-increment
      bool operator== (const iterator& rhs) const {return itr == rhs.itr;}
      bool operator!= (const iterator& rhs) const {return itr != rhs.itr;}
      bool operator<= (const iterator& rhs) const {return itr <= rhs.itr;}
      bool operator>= (const iterator& rhs) const {return itr >= rhs.itr;}
      int  operator-  (const iterator& rhs) const {return itr - rhs.itr;}
      int& operator* () {return *itr;}
      int* operator->() {return  itr;}    
  };

  inline Clause ( );
  inline Clause (const Clause& cl); // quick, pointer copy
  inline Clause (const vector<int>& lits, bool isInit, int activity, int LBD); // two first lits will be watched
  inline Clause (const WConstraint& wc,   bool isInit, int activity, int LBD); // two first lits will be watched

  inline ~Clause() {}

 // size[-1], watchIdx[-2], activity[-3], isInit[-4], LBD[-5]
  inline void freeMemory        ( );
  inline int  getSize           ( )       const {assert(mem); return mem[-1]; }
  inline void  setSize          (int size)      {assert(mem); mem[-1] = size;}
  inline int  getIthLiteral     (int i)   const { return mem[i]; }  // 0 <= i < getSize()
  inline void setIthLiteral     (int i, int lit) { mem[i] = lit; }  // 0 <= i < getSize()
  inline int  getActivity       (     )   const { return mem[-3];}
  inline void setActivity       (int a)         { mem[-3] = a;}
  inline void increaseActivity  (int amount)    { mem[-3] += amount;}
  inline bool isInitial         (     )   const { return mem[-4];}
  inline void setInitial        (bool i)        { mem[-4] = i;}
  inline int  getLBD            (     )   const { return mem[-5];}
  inline void setLBD            (int LBD)       { mem[-5] = LBD;}
  inline int  getWatchIdx       (     )   const { return mem[-2];}
  inline void setWatchIdx       (int i)         { assert(i > 1); assert(i < getSize()); mem[-2] = i;}
  inline int  getNumInts        (     )   const { return getSize() + 5;}
  
  inline iterator begin()            const {return mem;}
  inline iterator firstNonWatched()  const {return mem+2;}
  inline iterator middleNonWatched() const {return mem+mem[-2];}
  inline iterator end()              const {return mem+mem[-1];}

  inline friend ostream& operator<<(ostream& os, const Clause& c) {
    for (auto& e:c) os << e << " ";
    os << "  [act = " << c.getActivity() << ", isInitial " << c.isInitial() << ", LBD " << c.getLBD() << "]";
    return os;
  }  

 //private:
 
  Clause operator= (const Clause& c) { mem = c.mem; return *this;}
  //Clause operator= (const Clause& c) { assert(false); return *this;}
 //private:
  inline int* longPointer() const;
};

#include "WConstraint.h"

inline Clause::Clause ( ) {}

inline Clause::Clause (const Clause& c) {
  assert(c.mem != 0);
  mem = c.mem;
}

inline Clause::Clause (const vector<int>& lits, bool isInit, int activity, int LBD) {
  // size[-1], watchIdx[-2], activity[-3], isInit[-4], LBD[-5]
  mem = new int[lits.size()+5];
  mem += 5;
  
  setSize((int)lits.size());
  setWatchIdx(2);
  setActivity(activity);
  setInitial(isInit);
  setLBD(LBD);
  
  for (uint i = 0; i < lits.size(); ++i) mem[i] = lits[i];
}

inline Clause::Clause (const WConstraint& wc, bool isInit, int activity, int LBD) { // two first lits will be watched
  assert(wc.isClause());
  vector<int> lits;
  for (int i = 0; i < wc.getSize(); ++i) lits.push_back(wc.getIthLiteral(i));
  *this = Clause(lits,isInit,activity,LBD);
}

inline void Clause::freeMemory ( ) {
  if (mem) {
    mem -= 5;
    delete[] mem;
  }
}


inline int* Clause::longPointer() const {
  assert(mem);
  return mem;
}



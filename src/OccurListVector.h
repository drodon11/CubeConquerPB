#pragma once

#include<iterator>
#include<iostream>
#include<vector>
#include<cassert>
using namespace std;

struct OccurListElem {
  int ctrId;
  int coefficient;
  OccurListElem(){}
  OccurListElem (int ctrNum, int coef):ctrId(ctrNum), coefficient(coef) {}
};


class OccurList {

  //class iterator : public std::iterator<std::forward_iterator_tag,OccurListElem*>
  //{
    //OccurListElem* itr; 
    
  //public :
    
    //iterator (OccurListElem* temp) : itr(temp) {}
    //iterator (const iterator& myitr) : itr(myitr.itr) {}
    //iterator& operator++()    {itr++; return *this;}//Pre-increment
    //iterator  operator++(int) {iterator tmp(*this); itr++; return tmp;}//Post-increment
    //bool operator== (const iterator& rhs) const {return itr == rhs.itr;}
    //bool operator!= (const iterator& rhs) const {return itr != rhs.itr;}
    //OccurListElem& operator* () {return *itr;}
    //OccurListElem* operator->() {return  itr;}
  //};
  
  
  class iterator {
    OccurListElem* itr; 
      
    public :
      using iterator_category = std::forward_iterator_tag;
      using value_type = OccurListElem;
      using difference_type = OccurListElem;
      using pointer = OccurListElem*;
      using reference = OccurListElem&;
      
      iterator (OccurListElem* temp) : itr(temp) {}
      iterator (const iterator& myitr) : itr(myitr.itr) {}
      iterator& operator++()    {itr++; return *this;}//Pre-increment
      iterator  operator++(int) {iterator tmp(*this); itr++; return tmp;}//Post-increment
      bool operator== (const iterator& rhs) const {return itr == rhs.itr;}
      bool operator!= (const iterator& rhs) const {return itr != rhs.itr;}
      OccurListElem& operator* () {return *itr;}
      OccurListElem* operator->() {return  itr;}
  };

  vector<OccurListElem> v;

 public:
  iterator begin() {if (v.size() != 0) return &v[0]; else return NULL;}
  iterator end() {if (v.size() != 0) return (&v[0]) + v.size(); else return NULL;}

 public:
  OccurList();
  inline int size();
  inline void addElem(const int ctrId, int coef);
  inline void addElems(const vector<OccurListElem>& elems);// pairs are <ctrId,coefficient>
  inline void freeMemory();
  inline void clear();  // can be removed
  inline void quickFlush();  // can be removed
  inline void setToEmpty();
};


inline OccurList::OccurList(): v(){}

inline void OccurList::addElem(const int ctrId, int coef){
  v.emplace_back(ctrId, coef);
}

inline void OccurList::addElems(const vector<OccurListElem>& elems) {
  v.reserve(v.size()+elems.size());
  for (uint i = 0; i < elems.size(); ++i)
    v.push_back(elems[i]);
}

inline void OccurList::freeMemory ( ){
  v.clear();
}
inline void OccurList::clear ( ) {
  v.clear();
}
inline void OccurList::quickFlush ( ) {
  v.clear();
}

inline int OccurList::size ( ){
  return v.size();
}

inline void OccurList::setToEmpty ( ){
  v.clear();
}




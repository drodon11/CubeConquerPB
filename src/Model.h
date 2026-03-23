#pragma once

#include <vector>
#include <cassert>
#include <cstdlib>
#include <iostream>

#include "Reason.h"
using namespace std;

class Model {
public:

  class StackElement {
  public:
    int lit;
    int dl;
    Reason reason;
    inline StackElement () { }
    inline StackElement (int lit1, int dl1) {
      dl  = dl1;
      lit = lit1;
      reason = Reason() ;
    }
    inline StackElement (int lit1, int dl1, const Reason& r) {
      dl  = dl1;
      lit = lit1;
      reason = r;
    }
  };

  vector<StackElement> modelStack;
  char * truthValueOfVar;  
  vector<int> lastPhaseOfVar; // value of v last time it was assigned
  vector<int> stackHeightOfVar;  
  int lastPropagated;
  int decisionLevel;
public:  
  Model(int nVars);
  int trailSize() const {return modelStack.size();}

  inline bool isTrueVar( int v)  const        { assert(v > 0); return truthValueOfVar[v]== 1; }
  inline bool isFalseVar(int v)  const        { assert(v > 0); return truthValueOfVar[v]==-1; }
  inline bool isUndefVar(int v)  const        { assert(v > 0); return truthValueOfVar[v]== 0; }

  inline bool isTrueLit( int lit) const    { return lit*truthValueOfVar[abs(lit)] > 0; }  // important: lit 0 is undefined
  inline bool isFalseLit(int lit) const    { return lit*truthValueOfVar[abs(lit)] < 0; }
  inline bool isUndefLit(int lit) const    { assert(abs(lit) > 0); assert(abs(lit) < lastPhaseOfVar.size()); return truthValueOfVar[abs(lit)]==0; }

  
  inline bool isUnit(int lit)    const        { assert(abs(lit) > 0); return (truthValueOfVar[abs(lit)] != 0 && getDLOfLit(lit) == 0); }
  inline bool isTrueUnit(int lit) const       { assert(abs(lit) > 0); return (isTrueLit(lit) && getDLOfLit(lit) == 0); }
  inline bool isFalseUnit(int lit) const      { assert(abs(lit) > 0); return (isFalseLit(lit) && getDLOfLit(lit) == 0); }
  
  inline void setTrue( int v)         { assert(v > 0);truthValueOfVar[v] = 1; }
  inline void setFalse(int v)         { assert(v > 0);truthValueOfVar[v] =-1; }
  inline void setUndef(int v)         { assert(v > 0); truthValueOfVar[v] = 0; }

  inline int getValue (int v) const    { assert(v > 0); assert( isTrueVar(v) or isFalseVar(v) ); 
    if (isTrueVar(v)) return 1; else return 0; }

  inline int getValueLit (int l) const { assert( isTrueLit(l) or isFalseLit(l) ); 
    if (isTrueLit(l)) return 1; else return 0;
  }
  
  inline string strValLit(int l) const {
    if (isTrueLit(l)) return "T"; 
    else if (isFalseLit(l)) return "F";
    else return "U";
  }
  inline int intValLit(int l) const {
    if (isTrueLit(l)) return 1; 
    else if (isFalseLit(l)) return -1;
    else return 0;
  }
  
  inline int getLastPhase(int v)  const                      { assert(v > 0); return lastPhaseOfVar[v];      }
  inline int currentDecisionLevel() const                    { if (modelStack.size()==0) return 0; 
                                                               else return modelStack[modelStack.size()-1].dl; }
  inline int heightOfTopElement() const                      { return modelStack.size()-1; }
  inline int getLitAtTop() const                             { return modelStack.back().lit; }
  inline int getVarAtTop() const                             { return abs(modelStack.back().lit); }
  inline int getHeightOfVar(int v) const                     { assert(v > 0); return stackHeightOfVar[v]; }
  inline int getVarAtHeight(int h) const                     { return abs(modelStack[h].lit);  }
  inline int getDLAtHeight(int h) const                      { return modelStack[h].dl;   }
  inline int getDLOfLit(int lit) const                       { return modelStack[getHeightOfVar(abs(lit))].dl;    }
  inline int getLitAtHeight(int h) const                     { return modelStack[h].lit;    }

  inline Reason getReasonAtHeight(int h) const     { return modelStack[h].reason; }
  inline Reason getReasonAtTop() const             { assert(modelStack.size()>0);
    return modelStack[modelStack.size()-1].reason; }
  inline Reason getReasonOfLit (int lit) const {
    assert(not isUndefLit(lit));
    int var = abs(lit);
    return modelStack[getHeightOfVar(var)].reason;
  }

  inline bool varIsOfCurrentDL( int v ) const                { assert(v >= 0); return getDLAtHeight(getHeightOfVar(v))==currentDecisionLevel(); }

  inline void flushLastPhase(){
    for (int i=0; i < (int)lastPhaseOfVar.size(); i++ ) lastPhaseOfVar[i] = -1;
  }
  void printStack() const;


  inline int getNextLitToPropagatePB()                         { if (lastPropagated == (int)modelStack.size() -1) return 0;
                                                               else return modelStack[++lastPropagated].lit; }
  inline bool areAllLitsPropagated( ) const                   { return lastPropagated == (int)modelStack.size() -1;}
  inline bool isLitPropagated ( int lit ) const {
    //PRE: lit belongs to model
    return lastPropagated >= getHeightOfVar(abs(lit));
  }
  
  void setTrueDueToDecision( int lit );
  void setTrueDueToReason( int lit, const Reason& r );
  int popAndUnassign();
};


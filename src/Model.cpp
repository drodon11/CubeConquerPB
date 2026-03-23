#include "Model.h"
using namespace std;

Model::Model(int nVars) {
  modelStack = vector<StackElement>(0);
  //    truthValueOfVar  = vector<int>( nVars + 1, 0  );
  truthValueOfVar  = new char [nVars+1];
  for( int i=0; i<=nVars; i++) truthValueOfVar[i]=0;
  lastPhaseOfVar   = vector<int>( nVars + 1, -1 );
  stackHeightOfVar = vector<int>( nVars + 1, -1 );
  lastPropagated = -1;
  decisionLevel = 0;
}

void Model::printStack() const {
  cout << endl << "MODEL STACK:   dl   lit   reasonConstraintNum" << endl;
  for (int i=0; i<(int)modelStack.size(); i++) {
    cout << "     "<< i << ":         " << getDLAtHeight(i) << "   x";
    if (getLitAtHeight(i)>0) 
      cout << abs(getLitAtHeight(i)) << "=1       ";
    else
      cout << abs(getLitAtHeight(i)) << "=0       ";
    cout << getReasonAtHeight(i) << endl;
  }
  cout << endl << endl;
}

void Model::setTrueDueToDecision( int lit ) {
  assert(isUndefLit(lit));
  ++decisionLevel;
  modelStack.push_back(StackElement(lit,decisionLevel));
  int var = abs(lit);
  if (lit>0) setTrue(var); else setFalse(var);
  stackHeightOfVar[var] = modelStack.size()-1;
}

void Model::setTrueDueToReason( int lit, const Reason& r) {
  assert(isUndefLit(lit));
  modelStack.push_back(StackElement(lit,decisionLevel,r));
  int var = abs(lit);
  if (lit>0) setTrue(var); else setFalse(var);
  stackHeightOfVar[var] = modelStack.size()-1;
}

int Model::popAndUnassign() { 
  assert ( modelStack.size() > 0 );
  int lit = modelStack.back().lit;
  int var = abs( lit );
  if (isTrueVar(var)) lastPhaseOfVar[var] = 1; else lastPhaseOfVar[var] = 0;
  setUndef(var);
  stackHeightOfVar[var] = -1;
  modelStack.pop_back();
  decisionLevel = modelStack.size()==0?0:modelStack[modelStack.size()-1].dl;
  
  int maxIdx = (int)modelStack.size()-1;
  if (lastPropagated > maxIdx)        lastPropagated = maxIdx;
  return lit;
}


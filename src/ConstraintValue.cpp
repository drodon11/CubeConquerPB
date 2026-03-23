#include "Solver.h"

bool Solver::constraintIsContradiction (const WConstraint & c) const {
  long long maxSum = 0;
  for (int i = 0; i < c.getSize(); ++i) { 
    int lit  = c.getIthLiteral(i);
    int coef = c.getIthCoefficient(i);
    if (!model.isFalseLit(lit) or model.getDLOfLit(lit) > 0) maxSum += coef; 
  }
  if (maxSum < c.getConstant()) return true;
  else return false;
}

bool Solver::constraintIsTrue ( const WConstraint & c) const { 
  assert(model.currentDecisionLevel() == 0);
  long long minSum = -c.getConstant();
  for (int i = 0; minSum < 0 && i < c.getSize(); ++i) { 
    int lit  = c.getIthLiteral(i);            
    int coef = c.getIthCoefficient(i);
    if (model.isTrueLit(lit)) minSum += abs(coef); 
  }
  if (minSum >= 0) return true;
  return false;
}

bool Solver::constraintIsFalse ( const WConstraint & c ) const {
  return ( slack(c) < 0 );
}

long long Solver::slack (const WConstraint & c) const {
  long long maxSum = -c.getConstant();
  for (int i = 0; i < c.getSize(); ++i) { 
    int lit  = c.getIthLiteral(i);            
    int coef = c.getIthCoefficient(i);
    if ( not model.isFalseLit(lit) ) maxSum += coef; 
  }
  return maxSum;
}

long long Solver::maxSumOfConstraintMinusRhsPropagated(const WConstraint & c) const {
  long long maxSum = -c.getConstant();
  for (int i = 0; i < c.getSize(); ++i) { 
    int lit  = c.getIthLiteral(i);            
    int coef = c.getIthCoefficient(i);
    if ( not model.isFalseLit(lit) or not model.isLitPropagated(lit)) maxSum += abs(coef); 
  }
  return maxSum;
}

bool Solver::clauseIsFalse (const Clause& cl) const {
    for (auto& lit:cl)
      if (not model.isFalseLit(lit)) return false;
    return true;
}

bool Solver::clauseIsTrue (const Clause& cl) const {
    for (auto& lit:cl)
      if (model.isTrueLit(lit)) return true;
    return false;
}

bool Solver::clauseHasTwoUnassignedWatches (const Clause& cl) const {
    return model.isUndefLit(cl.getIthLiteral(0)) 
      and model.isUndefLit(cl.getIthLiteral(1)); 
}

bool Solver::clausePropagates (const Clause& cl) const {
  int nUnassigned = 0;
  for (auto& lit:cl) {
    if (model.isTrueLit(lit)) return false;
    else if (model.isUndefLit(lit)) ++nUnassigned;
  }
  return nUnassigned == 1;    
}

bool Solver::constraintIsConflictingOrPropagating( const WConstraint &c ) {
  int maxUndef = 0;
  long long maxSum = -c.getConstant();
  for (int i = 0; i < c.getSize(); ++i) { 
    int lit  = c.getIthLiteral(i);            
    int coef = c.getIthCoefficient(i);
    if ( not model.isFalseLit(lit) ) maxSum += coef; 
    if ( model.isUndefLit(lit) and maxUndef < coef ) maxUndef = coef;
  }
  return maxSum - maxUndef < 0;
}

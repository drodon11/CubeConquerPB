#include "Solver.h"

void Solver::addBinaryClause (int lit1, int lit2) {
  if (lit1 > 0) positiveBinClauses[ lit1].push_back(lit2);
  else          negativeBinClauses[-lit1].push_back(lit2);
  if (lit2 > 0) positiveBinClauses[ lit2].push_back(lit1);
  else          negativeBinClauses[-lit2].push_back(lit1);

  if (lit1 > 0) { GeneralWatchListElem e(BINCLAUSE,lit2); positiveGenWatchList[ lit1].push_back(e); }
  else          { GeneralWatchListElem e(BINCLAUSE,lit2); negativeGenWatchList[-lit1].push_back(e); }
  if (lit2 > 0) { GeneralWatchListElem e(BINCLAUSE,lit1); positiveGenWatchList[ lit2].push_back(e); } 
  else          { GeneralWatchListElem e(BINCLAUSE,lit1); negativeGenWatchList[-lit2].push_back(e); }
  
}

void Solver::addClause ( const Clause & c) {
  clauses.push_back(c);
  int  firstWatched = c.getIthLiteral(0);
  int secondWatched = c.getIthLiteral(1);
  int        cached = c.getIthLiteral(c.getSize()-1);
  int      clauseId = clauses.size()-1;
  
  if (  firstWatched > 0) positiveWatchLists[  firstWatched].emplace_back(clauseId,cached);
  else                    negativeWatchLists[ -firstWatched].emplace_back(clauseId,cached);
  if ( secondWatched > 0) positiveWatchLists[ secondWatched].emplace_back(clauseId,cached);
  else                    negativeWatchLists[-secondWatched].emplace_back(clauseId,cached);

  if (  firstWatched > 0) { GeneralWatchListElem e(CLAUSE,cached,clauseId); positiveGenWatchList[  firstWatched].push_back(e); }
  else                    { GeneralWatchListElem e(CLAUSE,cached,clauseId); negativeGenWatchList[ -firstWatched].push_back(e); }
  if ( secondWatched > 0) { GeneralWatchListElem e(CLAUSE,cached,clauseId); positiveGenWatchList[ secondWatched].push_back(e); }
  else                    { GeneralWatchListElem e(CLAUSE,cached,clauseId); negativeGenWatchList[-secondWatched].push_back(e); }
  
  stats.numOfIntsInPbsAndClauses += ( c.getSize());  
}


void Solver::addPBConstraintCounter (const PBConstraint & c) {
  ++stats.numOfCounterCtr;
  int constraintId = (int)constraintsPB.size();
  const int size = c.getSize();
  long long slack = -c.getConstant() - c.getIthCoefficient(0);  // initialized to -k - maxcoef
  for (int i=0; i < size; i++) {
    int lit  = c.getIthLiteral(i);
    int coef = c.getIthCoefficient(i);
    int var = abs(lit);
    
    assert(coef > 0);
    if ( !model.isFalseLit(lit) or !model.isLitPropagated(lit) ) slack += coef; 
    if (lit > 0) positiveOccurLists[var].addElem(constraintId,coef);
    else         negativeOccurLists[var].addElem(constraintId,coef);

    if (lit > 0) { GeneralWatchListElem e(COUNTERPB,coef,constraintId); positiveGenWatchList[var].push_back(e);    }
    else         { GeneralWatchListElem e(COUNTERPB,coef,constraintId); negativeGenWatchList[var].push_back(e);    }
  }
  
  useCounter.push_back(true);
  sumOfWatches.push_back(slack);
  constraintsPB.push_back(c);

}

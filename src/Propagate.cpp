#include "Solver.h"

// Cadical version
void Solver::visitClauseWatchList (int lit) {
  
  vector<WatchListElem>& wl = (lit>0?positiveWatchLists[lit]:negativeWatchLists[-lit]);
  if (wl.size() == 0) return;
  
  const auto end = wl.end();
  auto itWL = wl.begin();
  auto itWL_kept = itWL;
  
  while (itWL != end) {
    WatchListElem& e = *itWL_kept++ = *itWL++;     // a=b=c     <==>    a=(b=c)    <==>    b = c; a = b;

    if (model.isTrueLit(e.cachedLit)) continue;
    
    Clause& cl = clauses[e.clauseId];
    int l0 = cl.getIthLiteral(0);
    int l1 = cl.getIthLiteral(1);
    const int otherLit = l0 ^ l1 ^ lit; 
    
    if (model.isTrueLit(otherLit)) { e.cachedLit = otherLit; continue; }
    
    const auto middle = cl.middleNonWatched();
    const auto end    = cl.end();
    auto k = middle;
    // Find replacement watch 'r' at position 'k' 
    assert(cl.getWatchIdx() > 1 && cl.getWatchIdx() <= cl.getSize());
    int r = 0;
    bool isFalse = true;
    while (k != end && (isFalse = model.isFalseLit(r = *k)) )
      k++;
    
    if (isFalse) {
      assert(r == 0 or model.isFalseLit(r));
      k = cl.firstNonWatched(); // the 3rd lit
      while (k != middle && (isFalse = model.isFalseLit(r = *k)) )
        k++;
    }
    cl.setWatchIdx(k - cl.begin());
    
    if (not isFalse) { //we have found *k to be reselected
      if (model.isTrueLit(r)) e.cachedLit = r;  // Replacement satisfied,
      else {  // Found new unassigned replacement literal to be watched.
        assert(model.isUndefLit(r));
        cl.setIthLiteral(0, otherLit);
        cl.setIthLiteral(1, r);
        *k = lit;
        if (r > 0) positiveWatchLists[r].emplace_back(e.clauseId, lit);
        else       negativeWatchLists[-r].emplace_back(e.clauseId, lit);
        itWL_kept--;
      }
    }
    else if (model.isUndefLit(otherLit)) {
      strat.reportPropagationInClause();
      setTrueDueToReason(otherLit,Reason(e.clauseId,Reason::CLAUSE));
    }
    else {
      assert(model.isFalseLit(otherLit));
      conflict = true;
      typeConflict = CONFLICT_CLAUSE; 
      clauseConflictNum = e.clauseId;
      break;
    }
  }
  
  if (itWL_kept != itWL) {
    while(itWL != end) 
      *itWL_kept++ = *itWL++;
    wl.resize(itWL_kept - wl.begin());  // numElems, keep the allocatedInts
  }
}


void Solver::visitPBCounterLists (int lit) {   // lit has just become false
  int nVisited = 0; 
  int var = abs(lit);
  
  for (GeneralWatchListElem& e: (lit > 0?positiveGenWatchList[var]:negativeGenWatchList[var])) {
    if (e.isCounterPB()) {
      ++nVisited;
      int constraintId = e.constraintId();
      int coef         = e.coef(); 
      long long & SNF = sumOfWatches[constraintId];
      SNF -= coef;
      if (SNF < 0 ) {
	int maxCoef = constraintsPB[constraintId].getIthCoefficient(0); 
	int wslk = SNF + maxCoef; 
	if (wslk < 0) {	conflict = true; typeConflict = CONFLICT_PB; constraintConflictNum = constraintId; break; }
	assert(wslk < maxCoef);
	propagatePBCtrCounter(constraintId, wslk);
      }
    }
  }
  
  if (conflict) {  // leave sumOfWatches of the constraints already visited in this watchlist as they were:
    int nVisited2 = 0;
    for (GeneralWatchListElem& e: (lit > 0?positiveGenWatchList[var]:negativeGenWatchList[var])) {
      if (e.isCounterPB()) {
	int constraintId = e.constraintId();
	int coef         = e.coef();
	sumOfWatches[constraintId] += coef;
	++nVisited2;
	if (nVisited2 == nVisited) break;
      }
    }
  }  
  
}


void Solver::visitBinClause (int lit){  // lit has just become false

  vector<GeneralWatchListElem>& wl = (lit > 0?positiveGenWatchList[lit]:negativeGenWatchList[-lit]);
  for (auto& e:wl) {
    if (e.isBinClause()) {
      int otherLit = e.otherLit();
      if (model.isFalseLit(otherLit)) {
	conflict = true;
	typeConflict = CONFLICT_BIN_CLAUSE;
	binClauseConflict = {lit,otherLit};
	return;
      }
      else if (model.isUndefLit(otherLit)) {
	setTrueDueToReason(otherLit,Reason(lit,Reason::BIN_CLAUSE));
	strat.reportPropagationInBinClause();
      }
    }
  }

}


void Solver::propagate () {
  assert(!conflict);
  while (true) {
    if (model.areAllLitsPropagated()) { /*checkAllConstraintsPropagated(); */ return; }
    int lit = model.getNextLitToPropagatePB();
    
    assert(lit != 0);      
    visitBinClause(-lit);       if (conflict) { --model.lastPropagated; return; }
    visitClauseWatchList(-lit); if (conflict) { --model.lastPropagated; return; }
    visitPBCounterLists(-lit);  if (conflict) { --model.lastPropagated; return; }      
  }
}

// BELOW THIS POINT DEBUGGING FUNCTIONS

void Solver::checkPropagatedPBs ( PBConstraint& c, int ctrId ) {
  assert(!conflict);
  assert(model.lastPropagated == model.trailSize()-1);
  const int size = c.getSize();
  long long sumMinusRHSCurrent = -c.getConstant();
  for (int i = 0; i < size; ++i) { 
    int lit  = c.getIthLiteral(i);
    int coef = abs(c.getIthCoefficient(i));
    if (not model.isFalseLit(lit)) sumMinusRHSCurrent += coef;
  }
  
  if (sumMinusRHSCurrent < 0) {  // Check it is not a conflict
    cout << "PB " << ctrId << " is conflicting!!  sumMinusRHSCurrent " << sumMinusRHSCurrent << ", snf+maxCoef " << sumOfWatches[ctrId] + abs(c.getIthCoefficient(0)) << ", maxCoef " << abs(c.getIthCoefficient(0)) << " nDecs: " << stats.numOfDecisions << " nConfs: " << stats.numOfConflicts << ", dl " << model.currentDecisionLevel() << ", obj_num " << obj_num << endl;
    
    cout << endl << endl;
    
    long long sumMinusRHSCurrent = -c.getConstant() - abs(c.getIthCoefficient(0));
    for (int i = 0; i < size; ++i) { 
      int lit  = c.getIthLiteral(i);
      int coef = c.getIthCoefficient(i);
      if (coef < 0 and !model.isFalseLit(lit)) sumMinusRHSCurrent += abs(coef);
    }
    cout << "watched slack = " << sumMinusRHSCurrent << ", saved sumOfWatches " <<  sumOfWatches[ctrId] << endl << endl;
    writeConstraint(c);
    exit(0);
  }
  
  if (!useCounter[ctrId]) {
    sumMinusRHSCurrent = -c.getConstant();
    for (int i = 0; i < size; ++i) { 
      int lit  = c.getIthLiteral(i);
      int coef = c.getIthCoefficient(i);
      if (coef < 0 and !model.isFalseLit(lit)) sumMinusRHSCurrent += abs(coef);
    }
  }
  if (sumMinusRHSCurrent - abs(c.getIthCoefficient(0)) != sumOfWatches[ctrId]) {
    cout << "PB " << ctrId << " slackMC error!!  sumMinusRHSCurrent " << sumMinusRHSCurrent << ", current slackMC " << sumMinusRHSCurrent - abs(c.getIthCoefficient(0)) << ", saved sumOfWatches " << sumOfWatches[ctrId] << ", maxCoef " << abs(c.getIthCoefficient(0)) << " nDecs: " << stats.numOfDecisions << " nConfs: " << stats.numOfConflicts << ", dl " << model.currentDecisionLevel() << ", obj_num " << obj_num << endl;
    exit(0);
  }

  for (int i = 0; i < size; ++i) { 
    int lit  = c.getIthLiteral(i);
    int coef = abs(c.getIthCoefficient(i));
    if ( model.isUndefLit(lit) ) {
      if (sumMinusRHSCurrent - coef < 0) {
        
        cout << endl << "PB "  << ctrId << " not propagate all, sumMinusRHSCurrent " << sumMinusRHSCurrent << ", maxCoef " << abs(c.getIthCoefficient(0)) << ", coef " << coef << ", i = " << i << ", isInitial? " << constraintsPB[ctrId].isInitial() << flush << endl;
        cout << " nDecs: " << stats.numOfDecisions << " nConfs: " << stats.numOfConflicts << endl;
        cout << endl << "real wslk-maxCoef = " << (sumMinusRHSCurrent - abs(c.getIthCoefficient(0))) << ", but we have " << sumOfWatches[ctrId] << flush << endl;
        cout << " nDecs: " << stats.numOfDecisions << " nConfs: " << stats.numOfConflicts << ", dl " << model.currentDecisionLevel() << endl;
        exit(0);
      }
      assert(sumMinusRHSCurrent - coef >= 0);
    }    
  }
}

void Solver::checkAllConstraintsPropagated() {
  for ( int i = 1; i <= numVars; i++ ){
    if (model.isFalseLit(i)) {
      vector<int>& wl = positiveBinClauses[i];
      if (wl.empty()) continue;
      
      auto itWL = wl.begin();
      while (itWL != wl.end()) {
      assert(model.isTrueLit(*itWL));
      ++itWL;
      }
    }
    
    if (model.isFalseLit(-i)) {
      vector<int>& wl2 = negativeBinClauses[i];
      if(wl2.empty()) continue;
      auto itWL = wl2.begin();
      while (itWL != wl2.end()) {
      assert(model.isTrueLit(*itWL));
      ++itWL;
      }
    }
  }
  
  for(uint i = 0; i < clauses.size(); ++i ){
    Clause cl = clauses[i];
    //int lit_undef = 0;
    int numFalseLit = 0; 
    bool isTrue = false;
    for(int j = 0; j < cl.getSize(); ++j){
      int lit = cl.getIthLiteral(j);
      if(model.isTrueLit(lit)) {isTrue = true; break;}
      else if(model.isFalseLit(lit)) ++numFalseLit;
      //else lit_undef = lit;
    }
    if(isTrue) continue;
    if (numFalseLit == cl.getSize()-1) {
      for(int j = 0; j < cl.getSize(); ++j){
        int lit = cl.getIthLiteral(j);
        cout << lit;
        if (model.isTrueLit(lit)) cout << "(T" << model.getDLOfLit(lit) << ") ";
        else if (model.isFalseLit(lit)) cout << "(F" << model.getDLOfLit(-lit) << ") ";
        else cout << "(U)";      
      }
      cout << "[isInit " << cl.isInitial() << "]" << endl;
    }
    assert(numFalseLit != cl.getSize()-1);
    assert(numFalseLit != cl.getSize());
  }

  for(uint i = 0; i < constraintsPB.size(); ++i){
    PBConstraint& c = constraintsPB[i];
    checkPropagatedPBs(c, i);
  }  
  //cout << "done" << endl << flush;
}

void Solver::propagatePBCtrCounter ( const int ctrId, const long long wslk ) {
  PBConstraint& c = constraintsPB[ctrId];
  const int size = c.getSize();
  assert(sumOfWatches[ctrId] < 0); // propagating
  int idx = 0;
  if (c.getNumBackjump() < stats.numOfBackjump)
    c.setNumBackjump(stats.numOfBackjump);
  else idx = c.getMaxWIdx();
  
  while (idx < size and c.getIthCoefficient(idx) > wslk) {
    int lit = c.getIthLiteral(idx);
    if (model.isUndefLit(lit)) {
      strat.reportPropagationInPBCounter();
      setTrueDueToReason(lit,Reason(ctrId,Reason::PB_CONSTRAINT));
    }
    ++idx;
  }
  c.setMaxWIdx(idx);
}

// Why is it called initial????
void Solver::propagateInitialConstraintCounter (const int ctrId) {
  const long long& wslkMC = sumOfWatches[ctrId];
  if(wslkMC < 0) {
    long long wslk = wslkMC + constraintsPB[ctrId].getIthCoefficient(0); 
    if(wslk < 0) {
      conflict = true; typeConflict = CONFLICT_PB; constraintConflictNum = ctrId;
      return;
    }
    PBConstraint& c = constraintsPB[ctrId];
    assert(wslk < c.getIthCoefficient(0));
    int size = c.getSize();
    int idx = 0; 
    while (idx < size and c.getIthCoefficient(idx) > wslk) {
      int lit = c.getIthLiteral(idx);
      if (model.isUndefLit(lit)) {
        strat.reportPropagationInPBCounter();
        setTrueDueToReason(lit,Reason(ctrId,Reason::PB_CONSTRAINT));
      }
      ++idx;
    }
    c.setMaxWIdx(idx);
    c.setNumBackjump(stats.numOfBackjump);
  }
}

void Solver::checkObjectiveIsConflictingOrPropagating ( const int ctrId) {
  assert(!conflict);
  const long long& wslkMC = sumOfWatches[ctrId];
  if (wslkMC >= 0) return;
 
  PBConstraint& c = constraintsPB[ctrId];
  long long wslk = wslkMC + abs(c.getIthCoefficient(0)); 
  if(wslk < 0) {
    conflict = true; typeConflict = CONFLICT_PB; constraintConflictNum = ctrId;
    return;
  }
  
  int size = c.getSize();
  int idx = 0; 
  while (idx < size and abs(c.getIthCoefficient(idx)) > wslk) {
    int lit = c.getIthLiteral(idx);
    if (model.isUndefLit(lit)) {
      strat.reportPropagationInPBCounter();
      setTrueDueToReason(lit,Reason(ctrId,Reason::PB_CONSTRAINT));
    }
    ++idx;
  }
  c.setMaxWIdx(idx);
  c.setNumBackjump(stats.numOfBackjump);
}

void Solver::checkClausesPropagated ( ) const {
  for (uint i = 0; i < clauses.size(); ++i) {
    if (not clauseIsTrue(clauses[i]) and not clauseHasTwoUnassignedWatches(clauses[i])) {
      cout << "Clause " << clauses[i] << " not properly watched" << endl;
      printConstraint(WConstraint(clauses[i]));
      assert(false);
    }
    if (clauseIsFalse(clauses[i]) or clausePropagates(clauses[i])) {
      cout << "Clause " << clauses[i] << " not propagated" << endl;
      printConstraint(WConstraint(clauses[i]));
      assert(false);
    }
  }
}

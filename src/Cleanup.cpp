#include "Solver.h"

void Solver::cleanupPBConstraints (vector<ConstraintCleanup>& pbs, vector<ConstraintCleanup>& cls, vector<pair<int,int> >& binClauses, vector<bool>& ctrIsRemoved, uint newestObjectiveFunction) {
  
  for ( uint k = 0; k < constraintsPB.size(); ++k) {
    PBConstraint& c = constraintsPB[k];
    if (k == newestObjectiveFunction) { // we don't remove obj ctr and its units, to be reused when a new solution is found
      obj_num = pbs.size(); 
      objectiveFunctions.push_back(pbs.size());
      pbs.emplace_back(WConstraint(c), c.isInitial(),strat.reduceActivityOfPBInCleanup(c.getActivity()),c.getLBD());
      c.freeMemory();
      continue;
    }
    
    if (not ctrIsRemoved[k]) {
      WConstraint wc;
      // Now remove lits that are defined at dl0, adapting the constant:
      int constant = c.getConstant(); 
      for (int i = 0; i < c.getSize(); ++i) {
        int lit  = c.getIthLiteral(i);
        int coef = abs(c.getIthCoefficient(i));
        if (model.isUndefLit(lit)) wc.addMonomial(coef,lit);
        else if ( model.isTrueLit(lit) ) constant -= coef;
      }
      
      assert(constant>0); // otherwise c is tautology
      wc.setConstant(constant);
      assert( not constraintIsFalse(wc) );   // otherwise: undetected conflict before calling doCleanup
      assert( wc.getSize() > 1 );            // otherwise c would have been a bound already propagated at dl 0

      wc.simplify();
      
      if (wc.getConstant() == 1) {
        assert(wc.isClause());
        if (wc.getSize() > 2) {
          cls.emplace_back(wc,c.isInitial(),strat.reduceActivityOfClauseInCleanup(c.getActivity()),c.getLBD());
        }
        else {
          assert(wc.getSize() == 2);
          int lit1 = wc.getIthLiteral(0);
          int lit2 = wc.getIthLiteral(1);
          if (abs(lit1) > abs(lit2)) swap(lit1,lit2);
          binClauses.emplace_back(lit1,lit2);
        }
      }
      else {
        if (k == newestObjectiveFunction) {obj_num = pbs.size(); objectiveFunctions.push_back(pbs.size());}
        pbs.emplace_back(wc,c.isInitial(),strat.reduceActivityOfPBInCleanup(c.getActivity()),c.getLBD());
      }
    }
    c.freeMemory();
  }
}


void Solver::cleanupClauses (vector<ConstraintCleanup>& cls, vector<pair<int,int> >& binClauses, vector<bool>& ctrIsRemoved) {
  //  uint numCtr = constraintsPB.size() + cardinalities.size() - 1;
  uint numCtr = constraintsPB.size() - 1;
  
  for ( Clause c : clauses ) {
    ++numCtr; assert(numCtr < ctrIsRemoved.size());
    
    if (not ctrIsRemoved[numCtr]) {
      WConstraint wc;
      // Now remove lits that are defined at dl0, adapting the constant:
      for (auto& lit:c) {
        if (model.isUndefLit(lit)) wc.addMonomial(1,lit);
        assert(not model.isTrueLit(lit));
      }
      
      wc.setConstant(1);
      assert( not constraintIsFalse(wc) );   // otherwise: undetected conflict before calling doCleanup
      assert( wc.getSize() > 1 );            // otherwise c would have been propagated at dl 0
      
      if (wc.getSize() == 2) {
        assert(wc.getSize() == 2);
        int lit1 = wc.getIthLiteral(0);
        int lit2 = wc.getIthLiteral(1);
        if (abs(lit1) > abs(lit2)) swap(lit1,lit2);
        binClauses.emplace_back(lit1,lit2);
      }
      else {
        cls.emplace_back(wc,c.isInitial(),c.getActivity()/2,c.getLBD());
      }
    }
    
    c.freeMemory();
  }
}

void Solver::cleanupBinaryClauses ( vector<pair<int,int> >& binClauses ){
  for (int v = 1; v <= stats.numOfVars; ++v) 
    if (model.isUndefLit(int(v))) 
      for (auto& lit2:positiveBinClauses[v]) 
        if (model.isUndefLit(lit2) and v < abs(lit2)) binClauses.emplace_back(v,lit2);

  for (int v = 1; v < stats.numOfVars; ++v) 
    if (model.isUndefLit(int(v))) 
      for (auto& lit2:negativeBinClauses[v]) 
        if (model.isUndefLit(lit2) and v < abs(lit2)) binClauses.emplace_back(-v,lit2);  
}



void Solver::doCleanup () {
  struct Triple{
    int LBD;
    int act;
    uint id;    
    Triple(){}
    Triple(int lbd, int a, uint i):LBD(lbd),act(a),id(i){}
  };
  static vector<Triple> LBDAct;
  LBDAct.clear();
  vector<bool> ctrIsRemoved(constraintsPB.size() + clauses.size(), false);
  
  // Mark old objective functions to be deleted
  int newestObjectiveFunction = -1;
  if (objectiveFunctions.size() > 0) newestObjectiveFunction = objectiveFunctions.back();
  for (int i = 0; i < int(objectiveFunctions.size()) - 1; ++i) {
    assert(objectiveFunctions[i] >= 0);
    assert(objectiveFunctions[i] < ctrIsRemoved.size());
    ctrIsRemoved[objectiveFunctions[i]] = true;
  }
  objectiveFunctions.clear();
  assert(newestObjectiveFunction == -1 or newestObjectiveFunction == obj_num);
  
  size_t totalLearnts = 0;
  size_t promisingLearnts = 0;
  uint numPBLems = 0;
  
  for (uint i = 0; i < constraintsPB.size(); ++i) {
    if((int)i == obj_num) continue; 
    if (not ctrIsRemoved[i]) {
      PBConstraint& c = constraintsPB[i];
      long long counter = -c.getConstant();
      int size = c.getSize();
      for (int j = 0; counter < 0 and j < size; ++j)
        if (model.isTrueLit(c.getIthLiteral(j))) counter += abs(c.getIthCoefficient(j));
      if (counter >= 0) {
        ctrIsRemoved[i] = true; 
        continue;
      }
      if (not c.isInitial()) {
        ++numPBLems;
        int LBD  = c.getLBD();
        if (size > 2 && LBD > 2) LBDAct.push_back({LBD, c.getActivity(), i});
        if (size <= 2 || LBD <= 3) ++promisingLearnts;
        ++totalLearnts;
      }
    }
  }
  
  uint numCtr = constraintsPB.size()-1;
  
  for (uint i = 0; i < clauses.size(); ++i) {
    ++numCtr;
    Clause& c = clauses[i];
    bool isClauseTrue = false;
    for (auto& lit:c)
      if ( model.isTrueLit(lit) ) {isClauseTrue = true;break;}
    if (isClauseTrue) {ctrIsRemoved[numCtr] = true; continue;}
    if (not c.isInitial()) {
      int LBD  = c.getLBD();
      int size = c.getSize();
      if (LBD > 2) LBDAct.push_back({LBD, c.getActivity(), numCtr});
      if (size <= 2 || LBD <= 3) ++promisingLearnts;
      ++totalLearnts;
    }
  }
  assert(numCtr < ctrIsRemoved.size());
  
  if (promisingLearnts > totalLearnts / 2)
    nconfl_to_reduce += 10 * 100;
  else
    nconfl_to_reduce += 100;
  
  shuffle(LBDAct.begin(), LBDAct.end(), default_random_engine(stats.numOfCleanUps));
  
  std::sort(LBDAct.begin(), LBDAct.end(), [](Triple& x, Triple& y) {
    return x.LBD > y.LBD || (x.LBD == y.LBD && x.act < y.act);
  });
  
  size_t numDelete = min(totalLearnts/2, LBDAct.size());  // delete worest 75% of lemmas overall
  uint numRemovedPBLemmas = 0;
  for (size_t i = 0; i < numDelete; ++i) {
    ctrIsRemoved[LBDAct[i].id] = true; 
    if (LBDAct[i].id < constraintsPB.size()) ++numRemovedPBLemmas;
  }

  vector<ConstraintCleanup> tempConstraints;
  vector<ConstraintCleanup> tempClauses;
  vector<pair<int,int> > tempBinaryClauses; // abs(first) < abs(second)
  
  cleanupPBConstraints(tempConstraints,tempClauses,tempBinaryClauses,ctrIsRemoved, newestObjectiveFunction);
  cleanupClauses(tempClauses,tempBinaryClauses, ctrIsRemoved);
  cleanupBinaryClauses(tempBinaryClauses);
  stats.numOfIntsInPbsAndClauses = 0;
  
  for ( int i = 0; i <= numVars; i++ ) positiveOccurLists[i].clear();
  for ( int i = 0; i <= numVars; i++ ) negativeOccurLists[i].clear();
  
  for ( int i = 0; i <= numVars; i++ ) positiveWatchLists[i].clear();
  for ( int i = 0; i <= numVars; i++ ) negativeWatchLists[i].clear();

  for ( int i = 0; i <= numVars; i++ ) positiveBinClauses[i].clear();
  for ( int i = 0; i <= numVars; i++ ) negativeBinClauses[i].clear();
  
  for ( int i = 0; i <= numVars; i++ ) positiveGenWatchList[i].clear();
  for ( int i = 0; i <= numVars; i++ ) negativeGenWatchList[i].clear();

  sumOfWatches.clear();
  constraintsPB.clear();
  clauses.clear();
  useCounter.clear();
  stats.numOfBackjump = 0;
  
  int newPBs, newCls, numBins;
  newPBs = newCls = numBins = 0;
  
  buildBinaryClauses(tempBinaryClauses);

  for (uint i = 0; i < tempClauses.size(); ++i) {
    clauses.push_back(Clause(tempClauses[i].wc,tempClauses[i].isInit,tempClauses[i].activity,tempClauses[i].LBD));
    stats.numOfIntsInPbsAndClauses += clauses.back().getNumInts();
    if (not tempClauses[i].isInit) ++newCls;
  }
  buildWatchLists();
  
  for (uint i = 0; i < tempConstraints.size(); ++i) {
    PBConstraint pc(tempConstraints[i].wc,tempConstraints[i].isInit,tempConstraints[i].activity, tempConstraints[i].LBD);
    
    long long wslk; int numWatches; bool useC; 
    int maxCoef = pc.getIthCoefficient(0);
    minNumWatchesCleanup(tempConstraints[i].wc, wslk, numWatches);
    
    // COUNTER (all counter-based propagation for the moment)
    useC = true;
    wslk = slack(tempConstraints[i].wc) - maxCoef;
    ++stats.numOfCounterCtr;
    
    useCounter.push_back(useC);
    sumOfWatches.push_back(wslk);
    constraintsPB.push_back(pc);
    
    stats.numOfIntsInPbsAndClauses += pc.getNumInts();
    if (not tempConstraints[i].isInit) {++newPBs;}
  }
  
  buildOccurListsAndPBWatches();
  
  strat.reportNewPBClausesDatabase(constraintsPB.size() - newPBs, clauses.size() - newCls, tempBinaryClauses.size(), newPBs, newCls);
}

void Solver::buildOccurListsAndPBWatches ( ) {
  for (uint constraintId = 0; constraintId < constraintsPB.size(); ++constraintId) {
    PBConstraint& pc = constraintsPB[constraintId];
    for (int j = 0; j < pc.getSize(); ++j) {
      int lit = pc.getIthLiteral(j);
      int var = abs(lit);
      int coef = pc.getIthCoefficient(j);
      
      if (lit > 0) positiveOccurLists[var].addElem(constraintId,coef);
      else         negativeOccurLists[var].addElem(constraintId,coef);

      if (lit > 0) { GeneralWatchListElem e(COUNTERPB,coef,constraintId); positiveGenWatchList[var].push_back(e); }
      else         { GeneralWatchListElem e(COUNTERPB,coef,constraintId); negativeGenWatchList[var].push_back(e); }
      
    }
  }
}

void Solver::buildWatchLists ( ) {
  for (uint i = 0; i < clauses.size(); ++i) {
    Clause& cl = clauses[i];
    int firstWatched = cl.getIthLiteral(0);
    int secondWatched = cl.getIthLiteral(1);
    int cached = cl.getIthLiteral(cl.getSize()-1);
    
    if ( firstWatched > 0) positiveWatchLists[  firstWatched].emplace_back(i,cached);
    else                   negativeWatchLists[ -firstWatched].emplace_back(i,cached);
    if (secondWatched > 0) positiveWatchLists[ secondWatched].emplace_back(i,cached);
    else                   negativeWatchLists[-secondWatched].emplace_back(i,cached);

    if ( firstWatched > 0) { GeneralWatchListElem e(CLAUSE,cached,i); positiveGenWatchList[  firstWatched].push_back(e); }
    else                   { GeneralWatchListElem e(CLAUSE,cached,i); negativeGenWatchList[ -firstWatched].push_back(e); }
    if (secondWatched > 0) { GeneralWatchListElem e(CLAUSE,cached,i); positiveGenWatchList[ secondWatched].push_back(e); }
    else                   { GeneralWatchListElem e(CLAUSE,cached,i); negativeGenWatchList[-secondWatched].push_back(e); }

  }
}

void Solver::buildBinaryClauses (const vector<pair<int,int> >& binClauses) {
  for (auto& c:binClauses) {
    int lit1 = c.first;
    int lit2 = c.second;
    if (lit1 > 0) positiveBinClauses[ lit1].push_back(lit2);  
    else          negativeBinClauses[-lit1].push_back(lit2);  
    if (lit2 > 0) positiveBinClauses[ lit2].push_back(lit1);  
    else          negativeBinClauses[-lit2].push_back(lit1);

    if (lit1 > 0) { GeneralWatchListElem e(BINCLAUSE,lit2); positiveGenWatchList[ lit1].push_back(e); }
    else          { GeneralWatchListElem e(BINCLAUSE,lit2); negativeGenWatchList[-lit1].push_back(e); }
    if (lit2 > 0) { GeneralWatchListElem e(BINCLAUSE,lit1); positiveGenWatchList[ lit2].push_back(e); } 
    else          { GeneralWatchListElem e(BINCLAUSE,lit1); negativeGenWatchList[-lit2].push_back(e); }
    
  }
}

void Solver::minNumWatchesCleanup (const WConstraint & c, long long& wslk, int& numWatches) {
  wslk = -c.getConstant() - c.getIthCoefficient(0);
  int size = c.getSize();
  for (numWatches = 0; wslk < 0 and numWatches < size; ++numWatches) {
    int lit = c.getIthLiteral(numWatches);
    int coef = c.getIthCoefficient(numWatches);
    if (not model.isFalseLit(lit)) wslk += coef;
  }
}


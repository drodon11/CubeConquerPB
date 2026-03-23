#include "Solver.h"

int Solver::getNextDecisionVar() { 
  int v = maxHeap.consultMax();
  if (v==0) return v; 
  int ctr=0;
  if (strat.randomDecisionCondition()) { // take random decision
    strat.reportRandomDecision();
    while ( ctr < 200 and not model.isUndefVar(v) ) { v = rand() % numVars + 1; ctr++; }
    if ( model.isUndefVar(v) )  return v; 
  }
  v = maxHeap.consultMax();
  while ( v != 0 and not model.isUndefVar(v)) { maxHeap.removeMax(); v=maxHeap.consultMax(); }
  //if (v != 0) maxHeap.reduceScore(v);
  return v;
}

void Solver::takeDecisionForVar (int decVar) {  
  assert( not conflict );    assert( model.isUndefVar(decVar) );
  
  vector<DecPolarity>& pols = strat.decisionPolarities[decVar];
  
  int pol = -1;
  for (uint i = 0; i < pols.size(); ++i) {
    if      (pols[i] == OBJECTIVE)      pol = bestPolarityForVarInObjective[decVar];
    else if (pols[i] == LAST_PHASE)     pol = model.getLastPhase(decVar);
    else if (pols[i] == LAST_SOLUTION) {
      if (stats.numOfSolutionsFound != 0 and stats.numOfConflictsSinceLastSolution < strat.NUM_CONFLICTS_CLOSE_TO_SOLUTION)  // default 1000
        pol = lastSolution[decVar];
    }
    else if (pols[i] == POSITIVE) pol = 1;
    else if (pols[i] == NEGATIVE) pol = 0;
    else if (pols[i] == INITIAL_SOLUTION) {
      if (stats.numOfConflicts <= (uint)strat.NUM_CONFLICTS_CLOSE_TO_SOLUTION)
        pol = initialInputSolution[decVar];
    }
    else if (pols[i] == RANDOM) {break;}
    else {cout << "Non-existent polarity " << pols[i] << " for var " << decVar << endl;exit(1);}

    if      (pol == 1) {setTrueDueToDecision( decVar); return;}
    else if (pol == 0) {setTrueDueToDecision(-decVar); return;}
  }

  // Random polarity
  decVar = (rand()%2?decVar:-decVar);
  setTrueDueToDecision(decVar);
}

void Solver::increaseScoresOfVars (const WConstraint& constraint) {
  for (int i = 0; i < constraint.getSize(); ++i) {
    int lit = constraint.getIthLiteral(i);
    increaseActivityScoreOfVar(abs(lit));
  }
}

void Solver::computeBestPolarityForVarInObjectiveFunction ( ) {
  // Note that objective function is maximize and all coeffs are positive
  bestPolarityForVarInObjective = vector<int>(stats.numOfVars+1,-1);
  for (auto& coeffLit:objective) {
    assert(coeffLit.first > 0);
    int lit   = coeffLit.second;
    if   (lit > 0) bestPolarityForVarInObjective[lit]  = 1;
    else           bestPolarityForVarInObjective[-lit] = 0;
  }
}

void Solver::increaseActivityScoreOfVar(int var) { 
  bool overFlow = maxHeap.increaseValueBy( var, strat.increaseFactorInDecision(var));
  if ( overFlow ) { cout << "O" << flush; strat.scoreBonus = strat.INITIAL_SCORE_BONUS; maxHeap.reset(); }
}

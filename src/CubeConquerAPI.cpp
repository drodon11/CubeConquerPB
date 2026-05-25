#include "Solver.h"



// int assignedVars( ) const {
//   if (roundingsat) {}
//   else if (barcelogic) { }
//   ///
// }


void Solver::goodClauses ( ) const {
  // Unit clauses
  for (int v = 1; v <= numVars; ++v) {
    if (model.isTrueUnit(v)) cout << v << endl;
    else if (model.isFalseUnit(v)) cout << -v << endl;
  }

  for (auto c : learnedShortConstraints)
    cout << c << endl;
}

vector<WConstraint> Solver::collectGoodClauses ( ) const {
  return learnedShortConstraints;
}


// Returns number of non-satisfied constraints (including bins, clauses and general PBs)
// Should have a look at which measures the paper mentions
int Solver::reducedFormulaSize ( ) const {
  assert(not conflict);
  int total = 0;

  // Bin clauses
  for (int v = 1; v <= numVars; ++v) { 
    if (not model.isTrueVar(v)) { // otherwise clauses v OR lit are all satisfied
      for (int lit :  positiveBinClauses[v]) { // Look for bin clauses of the form v OR lit
	if (model.isTrueLit(lit) or abs(lit) < v) continue; // v OR lit will be counted when traversing bin clause list of lit
	++total;
      }
    }
    if (not model.isFalseVar(v)) { // otherwise clauses -v OR lite are all satisfied
      for (int lit : negativeBinClauses[v]) { // Look for bin clauses of the form -v OR lit
	if (model.isTrueLit(lit) or abs(lit) < v) continue; // -v OR lit will be counted when traversing bin clause list of lit
	++total;
      }
    }
  }  // Finish counting bin clauses

  // Clauses
  for (const Clause& c : clauses) {
    if (not clauseIsTrue(c)) ++total;
  }

  // Constraints
  for (const PBConstraint& ctr : constraintsPB) {
    if (not constraintIsTrue(ctr)) ++total;
  }
  return total;
}

int Solver::assignedVars ( ) const {
  return model.trailSize();
}

bool Solver::isTrueLit (int lit) const {
  return model.isTrueLit(lit);
}

bool Solver::isFalseLit (int lit) const {
  return model.isFalseLit(lit);
}

bool Solver::isUndefLit (int lit) const {
  return model.isUndefLit(lit);
}

bool Solver::assumeAndPropagate (int lit) {
  assert(isUndefLit(lit));
  assert(not conflict);
  setTrueDueToDecision(lit);
  propagate();
  int h = model.getHeightOfVar(abs(lit));
  for (int i = h; i <= model.heightOfTopElement(); ++i) {
    //cout << "Prop " << model.getLitAtHeight(i) <<endl;
  }
  return not conflict;
}

void Solver::backtrack (int nLevels) {
  if (nLevels == 0) return;
  int currentLevel = model.currentDecisionLevel();
  int goal = currentLevel - nLevels;
  backjumpToDL(goal);
  conflict = false; // We can do this because if we only follow this API, a conflict will occur only at the last assumeAndPropagate call
}

#include "Solver.h"

void Solver::conflictAnalysis ( ) {
  strat.increaseActivityScoreBonus();
  if (typeConflict == CONFLICT_PB) {
    constraintsPB[constraintConflictNum].increaseActivity(strat.ACTIVITY_BONUS_FOR_PBS);
    conflictAnalysisAndBackjump(WConstraint(constraintsPB[constraintConflictNum]));
    ++stats.numOfPBConstraintsInConflicts;
  }
  else if (typeConflict == CONFLICT_CLAUSE) {
    clauses[clauseConflictNum].increaseActivity(strat.ACTIVITY_BONUS_FOR_CLAUSES);
    conflictAnalysisAndBackjump(WConstraint(clauses[clauseConflictNum]));
    ++stats.numOfClausesInConflicts;
  }
  else if (typeConflict == CONFLICT_BIN_CLAUSE){
    WConstraint temp = WConstraint( { {1,binClauseConflict.first}, {1,binClauseConflict.second} }, 1);
    conflictAnalysisAndBackjump(temp);
    ++stats.numOfBinClausesInConflicts;
  }
  else assert(false);
}

void Solver::removeUnits(WConstraint& c) {
  vector<pair<int,int> > newCtr;
  int constant = c.getConstant(); 
  int size     = c.getSize();
  for(int i = 0; i < size; i++) {
    int lit = c.getIthLiteral(i); int coef = c.getIthCoefficient(i);
    if (!model.isUndefLit(lit) and model.getDLOfLit(lit) == 0){
      if (model.isTrueLit(lit)) constant -= coef;
    }
    else newCtr.push_back({coef, lit});
  }
  c.setLHS(newCtr);
  c.setConstant(constant);
}

void Solver::conflictAnalysisAndBackjump (const WConstraint& falsifiedCtr) {
    assert( model.currentDecisionLevel() > 0 );
    WConstraint rCtr; // (r)easonConstraint
    WConstraint cCtr = falsifiedCtr; // (c)onflictingConstraint
    cCtr.sortByIncreasingVariable();
    
    Reason rCtrReason;
    long long slackCCtr = slack(cCtr); // slack (C)onflicting Constraint
    assert( slackCCtr < 0);
    int confVar = 0;
    int litInReasonCtr = 0,     coefInReasonCtr = 0;
    int litInConflictingCtr = 0, coefInConflictingCtr = 0;

    while ( true ) {    // while no backjump takes place
        rCtr.reset();
        assert(slackCCtr < 0);

        while ( slackCCtr < 0 ) { // Pop literals from model until slack becomes positive
            if ( model.currentDecisionLevel() == 0 ) { // 1599*.lp
	      //cout << "it's still conflicting after popping all literals in trail........" << endl;
	      return;
            }
            assert( slackCCtr == slack(cCtr) );
            rCtrReason = model.getReasonAtTop();
            litInReasonCtr = model.getLitAtTop();
            assert(model.getDLOfLit(litInReasonCtr) > 0);
            assert(model.getReasonOfLit(litInReasonCtr) == rCtrReason);
            confVar = abs( litInReasonCtr );
            assert(cCtr.isOrderedByIncreasingVariable());

            pair<int,int> coefLitConflictingCtr = cCtr.getCoefficientLiteralBinarySearch(confVar);
            coefInConflictingCtr = coefLitConflictingCtr.first;
            litInConflictingCtr = coefLitConflictingCtr.second;

            if ( coefInConflictingCtr != 0 and model.isFalseLit(litInConflictingCtr) )  {
                assert(litInConflictingCtr == -litInReasonCtr);
                slackCCtr += coefInConflictingCtr;
            }
            popAndUnassign();
        }

        // We have identified confVar as the variable to be eliminates via a cut
        // between rCtr and cCtr
        assert(slack(cCtr) >= 0);

        if (rCtrReason.isConstraint()) {
            PBConstraint& pb = constraintsPB[rCtrReason.getCtrId()];
            pb.increaseActivity(strat.ACTIVITY_BONUS_FOR_PBS);
            rCtr = WConstraint(pb);
            rCtr.sortByIncreasingVariable();
            ++stats.numOfPBConstraintsInConflicts;
        }
        else if (rCtrReason.isClause()) {
            Clause& clause = clauses[rCtrReason.getClauseNum()];
            clause.increaseActivity(strat.ACTIVITY_BONUS_FOR_CLAUSES);
            rCtr = WConstraint(clause);
            rCtr.sortByIncreasingVariable();
            ++stats.numOfClausesInConflicts;
        }
        else if (rCtrReason.isBinClause()) {
            rCtr = WConstraint({ {1,litInReasonCtr}, {1,rCtrReason.getOtherBinLit()} },1);
            rCtr.sortByIncreasingVariable();
            ++stats.numOfBinClausesInConflicts;
        }
        else {
            assert(rCtrReason.isUnitOrDecision());
            cout << "Error: Reason Constraint is not a PBConstraint nor a Clause" << endl;
            exit(-1);
        }


        coefInReasonCtr = rCtr.getCoefficientBinarySearch(confVar);
        assert(coefInReasonCtr > 0); // found the litInReasonCtr in the rCtr
        assert(rCtr.getLiteralBinarySearch(confVar) == litInReasonCtr);
        assert(model.isUndefLit(litInReasonCtr));
        assert(slack(rCtr) < coefInReasonCtr); // should be propagating
        assert(litInConflictingCtr == -litInReasonCtr);



        WConstraint cut;
        bool clash = false;
        bool applyCutAgain = true;
        bool isInconsistentCut;
        bool overflow = applyCut( confVar, cCtr, rCtr, cut, clash, isInconsistentCut );
        bool enoughCut = not overflow and slack(cut) < 0;

        if (enoughCut) {
            applyCutAgain = false;
            assert(cut.getConstant() > 0); //We have learned a tautology which should not be the case on Conflict Analysis

	    // cout << "Apply cut to eliminate " << confVar << " between:" << endl;
	    // printConstraint(cCtr);
	    // printConstraint(rCtr);
	    // cout << "Gives ";
	    // printConstraint(cut);	    
        }

        else {
            if (rCtr.getSize()  > cCtr.getSize()) { // WHY do we choose to fix the longest one?
                fixRoundingProblemSAT(litInReasonCtr,rCtr);
            }
            else {
                fixRoundingProblemSAT(litInConflictingCtr, cCtr );
            }
        }

        assert(cCtr.isOrderedByIncreasingVariable());
        assert(rCtr.isOrderedByIncreasingVariable());
        assert(slack(cCtr) >= 0);

        //We are doing a cut with a clause. In case that we get Overflow with that we are going to get an Overflow with every constraint 
        // since all coefficients should be >= 1
        if (applyCutAgain) {
	  overflow = applyCut( confVar, cCtr, rCtr, cut, clash, isInconsistentCut );
	  // cout << "Apply cut (AGAIN) to eliminate " << confVar << " between:" << endl; 
	  // printConstraint(cCtr);
	  // printConstraint(rCtr);
	  // cout << "Gives ";
	  // printConstraint(cut);
	  if (overflow) {
	    fixRoundingProblemSAT(litInReasonCtr,rCtr);
	    fixRoundingProblemSAT(litInConflictingCtr, cCtr );
	    overflow = applyCut( confVar, cCtr, rCtr, cut, clash, isInconsistentCut );
	    assert(not overflow);
	  }
	}
        assert(overflow or constraintIsFalse(cut)); 
        increaseScoresOfVars(rCtr);
        assert( not overflow );
        assert( constraintIsFalse(cut) );

        // ===================    end cut
        if (cut.isInconsistent()) {
            assert(conflict);
            backjumpToDL(0);
            assert(conflict);
            return;
        }

        cCtr = cut;
        int dlToBackjumpTo = clash?lowestDLAtWhichConstraintPropagatesOrConflicting(cCtr):-1;
	
        if (dlToBackjumpTo != -1) {  // backjump!
            if (cCtr.isClause()) {  // if the conflicting is clause 	      

	      if (cCtr.getSize() <= 3) learnedShortConstraints.push_back(cCtr);
	      assert(cCtr.getSize() > 0);
                vector<int> lemma;
                int posUIP = -1; int numUIP = 0; int maxDL = -1;

                for (int i = 0; i < cCtr.getSize(); ++i) {
                    int lit = cCtr.getIthLiteral(i);
                    assert(model.isFalseLit(lit));

                    if (model.getDLOfLit(lit) == maxDL)     {posUIP = i; ++numUIP;}
                    else if (model.getDLOfLit(lit) > maxDL) {posUIP = i; numUIP = 1; maxDL = model.getDLOfLit(lit);}
                    lemma.push_back(lit);
                }

		if (numUIP != 1) {
		  assert(numUIP > 1); // This can only happen if clause is conflicting at some previous DL
		                      // (due to lowestDLAtWhichClausePropagatesOrConflicting)
		  assert(dlToBackjumpTo < model.currentDecisionLevel());
		  backjumpToDL(dlToBackjumpTo);
		  assert(slack(cCtr) < 0);
		  slackCCtr = slack(cCtr);
		  continue;
		}
                assert(numUIP == 1);
                swap(lemma[0],lemma[posUIP]);
                lemmaShortening(lemma);
                int LBD = computeLBD(lemma);
                backjumpToDL(dlToBackjumpTo);

                conflict = false;

                if (lemma.size() == 1) {  // lemma is unit
		  if (model.isFalseLit(lemma[0])) { // Inconsistent unit clause
		    assert(model.currentDecisionLevel() == 0);
		    conflict = true;
		    return;
		  }
		  else
		    setTrueDueToReason(lemma[0],noReason());
                }
                else if (lemma.size() == 2) {  // lemma is binary clause
                    addBinaryClause(lemma[0],lemma[1]);
                    strat.reportLearnBinClause();
                    setTrueDueToReason(lemma[0],Reason(lemma[1],Reason::BIN_CLAUSE));
                }
                else {  // lemma is clause
                    Clause cl(lemma,false,strat.NEW_CONSTRAINT_ACTIVITY,LBD);
                    addClause(cl);
                    strat.reportLearnClause(lemma.size());
                    setTrueDueToReason(lemma[0],Reason(clauses.size()-1, Reason::CLAUSE));
                }
                return;
            }
            else {  // if the conflicting is a PBConstraint 
                assert( dlToBackjumpTo < model.currentDecisionLevel() );

                backjumpToDL(dlToBackjumpTo);
                assert( constraintIsConflictingOrPropagating( cCtr ) );

                slackCCtr = slack(cCtr);

                if ( dlToBackjumpTo == 0 and slackCCtr < 0 ) {cout << "The PB lemma is still conflict at dl 0" << endl; return;} // still conflict, and at dl0

                removeUnits(cCtr);
                assert(slackCCtr == slack(cCtr));

                if (slackCCtr < 0) { // We have backjumped and constraint is still conflicting
                    continue;
                }

                // We know cCtr propagates
                increaseScoresOfVars(cCtr);
                conflict = false;
                int LBD = computeLBD(cCtr);
                cCtr.sortByDecreasingCoefficient();

                addAndPropagatePBConstraint(cCtr, false,strat.NEW_CONSTRAINT_ACTIVITY,LBD, false);
		if (cCtr.getSize() <= 3) learnedShortConstraints.push_back(cCtr);

                strat.reportLearnPB(cCtr.getSize());
                return;
            }
        } // END: backjump
        slackCCtr = slack(cCtr);
    }
}

int Solver::lowestDLAtWhichClausePropagatesOrConflicting (const WConstraint & c) const {
  // PRE: c is false in current assignment
  // CAREFUL: we cannot guarantee that there are literals set at the last DL
  assert(c.isClause());
  int maxDL = -1;
  int secondMaxDL = -1;
  int nMax = 0;
  for (int i = 0; i < c.getSize(); ++i) {
    assert(model.isFalseLit(c.getIthLiteral(i)));
    // if (not model.isFalseLit(c.getIthLiteral(i))) return -1;
    // else {
      int dl = model.getDLOfLit(c.getIthLiteral(i));
      if (dl > maxDL) {secondMaxDL = maxDL; maxDL = dl; nMax = 1;}
      else if (dl == maxDL) ++nMax;
      else if (dl > secondMaxDL) secondMaxDL = dl;
      //}
  }
  
  if (nMax == 1) {
    if (secondMaxDL == -1) secondMaxDL = 0;
    return secondMaxDL;
  }
  else if (maxDL != model.currentDecisionLevel()) {
    // Found a clause that is conflicting at some previous level
    return maxDL; // clause conflicting at maxDL
  }
  else return -1;
}

int Solver::lowestDLAtWhichConstraintPropagatesOrConflicting (const WConstraint & c) const {
  assert(constraintIsFalse(c) );
  
  if (c.isClause()) return lowestDLAtWhichClausePropagatesOrConflicting(c);
  
  struct Triple{
    int coeff;
    int lit;
    int dl;    
    Triple(){}
    Triple(int c, int l, int d):coeff(c),lit(l),dl(d){}
  };
  
  static vector<Triple> coeffLitDL; // contains initially true/false literals
  coeffLitDL.clear();
  long long int slack = -c.getConstant();
  int maxUnassigned = 0; 
  for (int i = 0; i < c.getSize(); ++i) {
    int l = c.getIthLiteral(i);
    int coeff = c.getIthCoefficient(i);
    if (not model.isFalseLit(l)) slack += coeff;
    if (model.isUndefLit(l)) maxUnassigned = max(maxUnassigned,coeff);
    else coeffLitDL.push_back({coeff,l,model.getDLOfLit(l)});
  }

  // Sort from largest to smallest
  sort(coeffLitDL.begin(),coeffLitDL.end(),
       [](const Triple& t1, const Triple& t2){return t1.dl > t2.dl;});
  coeffLitDL.push_back({0,0,0}); // to make sure next dl always exists
  
  assert(slack - maxUnassigned < 0);
  int bestSoFar = coeffLitDL[0].dl;
  // Note that if the constraint never propagates but is conflicting at a DL lower than
  // the current one, bestSoFar will not be updated and we will detect it to be conflicting
  // at some previous DL

  // The idea is to simulate how the slack evolves after backjumping over decision levels
  // and check the lowest where it propagates
  for (uint i = 0; i < coeffLitDL.size() - 1; ++i) {
    if (model.isFalseLit(coeffLitDL[i].lit)) slack += coeffLitDL[i].coeff;
    maxUnassigned = max(maxUnassigned,coeffLitDL[i].coeff);
    // If we finish the decision level and slack - maxUnassigned < 0 then propagate
    if (coeffLitDL[i].dl != coeffLitDL[i+1].dl and slack - maxUnassigned < 0) {
      bestSoFar = coeffLitDL[i+1].dl;      
    }
  }
  
  if (bestSoFar == model.decisionLevel) return -1; // backjump to model.decisionLevel means no BJ
  else return bestSoFar;
}



void Solver::lemmaShorteningAuxFunction (int lit, vector<bool>& isMarked, vector<int>& lemma, int& lastMarkedInLemma, bool filterOutDLZero) {
  assert(not model.isUndefLit(lit));
  int v = abs(lit);
  if (isMarked[v]) return;
  if (filterOutDLZero and model.getDLOfLit(v) == 0) return;
  isMarked[v] = true;
  lemma.push_back(lit);
  ++lastMarkedInLemma;
}

Model *mod;
// PRE: UIP is first in lemma
void Solver::lemmaShortening (vector<int>& lemma){   
  /* Try to mark more intermediate variables, with the goal to minimize
   * the conflict clause.  This is a BFS from already marked variables
   * backward through the implication graph.  It tries to reach other marked
   * variables.  If the search reaches an unmarked decision variable or a
   * variable assigned below the minimum level of variables in the first uip
   * learned clause, then the variable from which the BFS is started is not
   * redundant.  Otherwise the start variable is redundant and will
   * eventually be removed from the learned clause.
   */
      
  if (lemma.size() <= 1) return;

  vector<int> lemmaToLearn;

  static vector<bool> isMarked; isMarked.resize(numVars+1,false);

  if (model.currentDecisionLevel() == 0) return;
 
  //First of all, mark all lits in original lemma.
  for (uint i = 0; i < lemma.size(); ++i) isMarked[abs(lemma[i])] = true;

  int originalSizeLemma = lemma.size();

  //Order lemma's lits from most recent to oldest 
  mod = &model;
  sort(lemma.begin()+1,lemma.end(),[](int lit1, int lit2) {return mod->getDLOfLit(lit1) > mod->getDLOfLit(lit2);}); // 1UIPs is not assigned ==> hence we cannot for its DL
  
  int lowestDL = model.getDLOfLit(lemma.back()); // lowestDL in lemma
  static vector<int> lemmaShortened; lemmaShortened.clear();

  //Go through the lits in lemma, and test if they're redundant
  for (int i=0; i < originalSizeLemma; ++i ){
    int lit = lemma[i];
    bool litIsRedundant=true;
    assert(isMarked[abs(lit)]);

    //Begins test to see if literal is redundant
    if (i == 0 or model.isUndefLit(lit) or model.getReasonOfLit(lit).isUnitOrDecision())      
      litIsRedundant=false;
    else if (model.getDLOfLit(lit) != 0) {
      //We add reasons' lits at the end of the lemma
      Reason r = model.getReasonOfLit(lit);
      int lastMarkedInLemma = originalSizeLemma;
      if (r.isBinClause())
        lemmaShorteningAuxFunction(r.getOtherBinLit(),isMarked,lemma,lastMarkedInLemma,true);
      else if (r.isClause()) {
        Clause& c = clauses[r.getClauseNum()];
        for (int j = 0; j < c.getSize(); ++j) {
          lemmaShorteningAuxFunction(c.getIthLiteral(j),isMarked,lemma,lastMarkedInLemma,true);
        }
      }
      // else if (r.isCardinality()){
      //   const Cardinality& c = cardinalities[r.getCardinalityNum()];
      //   for (int j = 0; j < c.getSize(); ++j) { // Add all false lits plus lit
      //     int litInCard = c.getIthLiteral(j);
      //     if (model.isFalseLit(litInCard) or abs(litInCard) == abs(lit)) {
      //       lemmaShorteningAuxFunction(litInCard,isMarked,lemma,lastMarkedInLemma,true);
      //     }
      //   }
      // }
      else if (r.isConstraint()){
        const PBConstraint& c = constraintsPB[r.getCtrId()];
        for (int j = 0; j < c.getSize(); ++j) { // Add all false lits plus lit
          int litInPB = c.getIthLiteral(j);
          if (model.isFalseLit(litInPB) or abs(litInPB) == abs(lit)) {
            lemmaShorteningAuxFunction(litInPB,isMarked,lemma,lastMarkedInLemma,true);
          }
        }
      }
      else assert(false);
    
      //test added literals and subsequent ones....
      int testingIndex = originalSizeLemma;
      while (testingIndex < lastMarkedInLemma){
        int testingLit = lemma[testingIndex];
        assert(isMarked[abs(testingLit)]);
        assert(model.getDLOfLit(lit) != 0);
        if ( model.getDLOfLit(testingLit) < lowestDL or //has lower dl
             model.getReasonOfLit(testingLit).isUnitOrDecision()) { //is decision
          //test fails
          litIsRedundant=false;
          break;
        }

        // the three true in AuxFunction were false in the previous SAT solver
        // it seems to me that this is stronger as we can ignore dl-zero literals
        Reason r = model.getReasonOfLit(testingLit);
        if (r.isBinClause())
          lemmaShorteningAuxFunction(r.getOtherBinLit(),isMarked,lemma,lastMarkedInLemma,true);
        else if (r.isClause())  {
          Clause& c = clauses[r.getClauseNum()];
          for (int j = 0; j < c.getSize(); ++j)
            lemmaShorteningAuxFunction(c.getIthLiteral(j),isMarked,lemma,lastMarkedInLemma,true);
        }
        else if (r.isConstraint()) {
          const PBConstraint& c = constraintsPB[r.getCtrId()];
          for (int j = 0; j < c.getSize(); ++j) { // Add all false lits plus lit
            int litInPB = c.getIthLiteral(j);
            if (model.isFalseLit(litInPB) or abs(litInPB) == abs(testingLit)) {
              lemmaShorteningAuxFunction(litInPB,isMarked,lemma,lastMarkedInLemma,true);
            }
          }
        } 
        else assert(false);
        
        ++testingIndex;
      }
      assert(testingIndex != lastMarkedInLemma or litIsRedundant);
      //Clean tested literals
      while ( lastMarkedInLemma > originalSizeLemma) {
        --lastMarkedInLemma;
        isMarked[abs(lemma[lastMarkedInLemma])] = false;
        lemma.pop_back();
      }
    }

    //Add (or not) litOfLemma to lemmaToLearn
    if (litIsRedundant) {
      lemma[i] = 0;
      isMarked[abs(lit)] = false;
    }
    else lemmaToLearn.push_back(lit);
  }

  lemma.clear();
  for (uint i = 0; i < lemmaToLearn.size(); ++i){
    lemma.push_back(lemmaToLearn[i]);
    isMarked[abs(lemmaToLearn[i])] = false;
  }

  strat.reportLemmaShortening(originalSizeLemma,lemma.size());
  // First lit is UIP
  // Second lit indicates where to Backjump
}  




/* for the moment, just generate the clause:  sum >= 1
   where sum is subset of the false lits in c plus the lit of x.
   We know that the sum of the coefficients in "sum" is large enough to justify the propagation/conflict
*/

void Solver::fixRoundingProblemMinSAT (int l, WConstraint & c) const{
  // intentar minimitzar el nombre de lits a "sum" (segurament caldrà ordenar primer per coeff (gran a petit) pero al final c ha d'estar ordenat per variable...

  // potser es pot intentar ordenar de manera que primer apareguin tots els falsos (i dins d'aquests ordenar per coefficient de gran a petit) per intentar minimitzar el nombre de literals que visites quan busques una sum prou gran

  
}
void Solver::fixRoundingProblemSAT (int l, WConstraint & c) const {
  
  // Literal l has just been popped from the assignment in conflict analysis
  // Assertions there guarantee that the DL of l was > 0
  
  WConstraint w2;
  int x = abs(l);
  long long int sumTotalMinusRhs = -c.getConstant();
  int coeffL = 0; // coefficient of l in c
  for (int i = 0; i < c.getSize(); ++i) { 
    sumTotalMinusRhs += c.getIthCoefficient(i);
    int var = abs(c.getIthLiteral(i));
    if (var == x) {
      coeffL = c.getIthCoefficient(i);
    }
  }  
  

  assert(sumTotalMinusRhs >= 0); // otherwise c in an unsatisfiable constraint
  
  sumTotalMinusRhs -= coeffL;
  assert(sumTotalMinusRhs >= 0); // otherwise literal l would have been propagatet at DL zero
  // and we only call this function on literals l that have been asserted at DL > 0 (when a conflict is found)


  //c.sortByFalseCoefficient(model);
  // Here we will add l to the new constraint "c" for sure
  // We do not add it right at the beginning because we want to keep "c" sorted by variable
  int i = 0;
  bool added = false;  
  while (sumTotalMinusRhs >= 0 or not added) {
    int lit  = c.getIthLiteral(i);            
    if (model.isFalseLit(lit) and abs(lit) != x and sumTotalMinusRhs >=0) {
      w2.addMonomial(1,lit);
      sumTotalMinusRhs -= c.getIthCoefficient(i);
    }
    else if (abs(lit) == x) {
      w2.addMonomial(1,c.getIthLiteral(i));
      added = true;
    }
    ++i;
  }

  w2.setConstant(1);
  c = w2;
  //c.sortByIncreasingVariable();
}

//// Returns whether an overflow or a tautology has occurred
bool Solver::applyCut ( int var, const WConstraint & c1, const WConstraint & c2, WConstraint & cut, bool& clash, bool& isInconsistentCut ) {
  assert(c1.isOrderedByIncreasingVariable());
  assert(c2.isOrderedByIncreasingVariable());

  // Next check somehow assumes that the largest coefficient in c1 is <= c1.getConstant and similar for c2
  // But this requires saturation/shaving to be applied (maybe add an assertion?)
  
  if ((long long)(c1.getConstant()) * c2.getConstant() < TWOTOTHE30TH) {
    // assume: c1*c2 < 2^30
    // when appling a cut, the largest coefficient/rhs that we can construct is:
    // a) RHS: at most it will be c1*m1 + c2*m2 (where m1, m2 are the multipliers)
    //         since m1 <= coeffs and coeffs <= rhs
    //         c1*m1 + c2*m2 <= c1*c2 + c2*c1 = 2*c1*c2 < 2*(2^30) = 2^31 --> fits in an integer
    // b) COEFF: coeff1*m1 + coeff2*m2 <= c1*m1 + c2*m2 < 2^31
    strat.reportIntCut();
    return applyCut<int>(var,c1,c2,cut, clash, isInconsistentCut);
  }
  else {
    // the range of an int is [-2^31, 2^31 - 1]
    // the range of a long long is [-2^63, 2^63 - 1]
    // c1.getConstant() is in [1, 2^31 - 1]
    // c2.getConstant() is in [1, 2^31 - 1]
    // Therefore, since any coefficient should be at most c1/c2: we know that all coefficients are <= 2^31 - 1
    // Hence: the multipliers for each constraint will be at most 2^31 - 1, which means that any coefficient of the resulting constraint will be at most:
    // largest_coeff * largest_multiplier + largest_coeff * largest_multiplier
    // which is at most: (2^31 - 1) * (2^31 - 1) + (2^31 - 1) * (2^31 - 1) =
    // 2*[(2^31 - 1) * (2^31 - 1)]
    // 2*[(2^62 - 2^31 - 2^31 + 1)]
    // 2*[2^62 - 2^32 + 1]
    // [2*63 - 2^33 + 2] < 2^63 - 1 ==> it should fit in a long long
    strat.reportLongIntCut(); 
    return  applyCut<long long>(var,c1,c2,cut,clash, isInconsistentCut);
  }
}


template<class T>
bool Solver::applyCut ( int var, const WConstraint & c1, const WConstraint & c2, WConstraint & cut, bool& clash, bool& isInconsistentCut ) {
  assert(c1.isOrderedByIncreasingVariable());
  assert(c2.isOrderedByIncreasingVariable());
  clash = false;

  int size1 = c1.getSize(); // conf false ctr
  int size2 = c2.getSize(); // cut reason
  int a1    = c1.getCoefficientBinarySearch(var); 
  int a2    = c2.getCoefficientBinarySearch(var);
  assert(c1.getLiteralBinarySearch(var) == - c2.getLiteralBinarySearch(var)); 
  
  assert(a1 != 0);
  assert(a2 != 0);
  int g    = GCD<int>( a1, a2 );
  T newConstant=0;
  T k1 = a2 / g; // type long long to force long long type of expressions k1 * ... below
  T k2 = a1 / g; // example: a1=12, a2=18, ====> gcd=6, k1=3, k2=2
  // We will construct k1*c1 + k2*c2
  static vector<T> newCoeffs; newCoeffs.clear();
  static vector<int> newLits; newLits.clear();
  int i1 = 0;    int i2 = 0;
  while (i1 < size1 and i2 < size2) { // sum using the fact that they are ordered by increasing var
    int lit1 =c1.getIthLiteral(i1);  int coef1=c1.getIthCoefficient(i1); 
    int lit2 =c2.getIthLiteral(i2);  int coef2=c2.getIthCoefficient(i2);
    if      (abs(lit1)<abs(lit2)) { newCoeffs.push_back( k1 * coef1 ); newLits.push_back(lit1); ++i1; } 
    else if (abs(lit1)>abs(lit2)) { newCoeffs.push_back( k2 * coef2 ); newLits.push_back(lit2); ++i2; } 
    else { // both i1 and i2 point to the same var, but this var different from the var we want to eliminate
      if (abs(lit1) != var) clash = true;
      T lCoef;
      if ( lit1 != lit2 ) { // same var, different signs
        if (k1 * coef1 >= k2 * coef2 ) {  
          newConstant -= k2*coef2;                   // this is the amount that is cancelled out
          lCoef = k1 * coef1 - k2 * coef2;           // this is the amount that remains
          if (lCoef != 0) { newCoeffs.push_back(lCoef); newLits.push_back(lit1); } 
        } else {
          newConstant -= k1*coef1;                   // this is the amount that is cancelled out
          lCoef = k2 * coef2 - k1 * coef1;           // this is the amount that remains
          if (lCoef != 0) { newCoeffs.push_back(lCoef); newLits.push_back(lit2); }
        }
      } else { // same var, same sign
        lCoef = k2 * coef2 + k1 * coef1; 
        newCoeffs.push_back(lCoef); newLits.push_back(lit1);
      }
      ++i1; ++i2;
    }
  }
  while (i1 < size1) { // the other one finished first
    int lit1=c1.getIthLiteral(i1);  int coef1=c1.getIthCoefficient(i1);
    newCoeffs.push_back( k1 * coef1 ); newLits.push_back(lit1); ++i1; } 
  while (i2 < size2) { 
    int lit2=c2.getIthLiteral(i2);  int coef2=c2.getIthCoefficient(i2);
    newCoeffs.push_back( k2 * coef2 ); newLits.push_back(lit2); ++i2; } 
  newConstant += k1 * c1.getConstant() + k2 * c2.getConstant();

  if (newConstant<=0) {
    //cout << endl <<"applyCut found a tautology " << endl; 
    return true;
  }
  
  if (newCoeffs.size()==0) { // inconsistency:  0 >= 1
    cut = WConstraint(false); 
    isInconsistentCut = true;
    //cout << endl << "applyCut found inconsistency cut:  0 >= 1" << endl; 
    return false; 
  } 

#ifndef NDEBUG
  for (auto x: newCoeffs) if (x < 0) {cout << "OF" << endl; exit(1);} // This should never happen! (but just in case)
#endif
  // normalization:  compute gcd of all coeffs that are smaller or equal than the constant
  T gcdV = -1;
  for (uint i = 0; i < newCoeffs.size(); ++i) {
    if (newCoeffs[i] <= newConstant) {
      if (gcdV==-1) gcdV = newCoeffs[i]; 
      else gcdV = GCD<T>( gcdV, newCoeffs[i] );
    }
  }
  if (gcdV == -1) gcdV = newConstant; // in this case all newCoefs are larger than newConstant [this will give a clause]
  newConstant = divisionRoundedUp<T>( newConstant, gcdV );  
  if ( newConstant > TWOTOTHE30TH ) {++stats.numConstantOverflow; return true;} // constraint does not fit in the type Constraint (which assumes rhs to be int) 
  
  static vector<int> newCoeffsInt; newCoeffsInt.clear();
  for (uint i = 0; i < newCoeffs.size(); ++i) {
    newCoeffs[i] = divisionRoundedUp<T>(newCoeffs[i],gcdV);
    if (newCoeffs[i] > newConstant) newCoeffs[i] = newConstant;  // shaving
    newCoeffsInt.push_back((int)newCoeffs[i]);
    
  }
  assert(int(newConstant) >= 0);

  cut = WConstraint(newCoeffsInt, newLits, (int)newConstant);
  assert(cut.getSize() > 0);
  return false;
}

int  Solver::computeLBD (const WConstraint& c) const {
  static unordered_set<int> DLs; DLs.clear();
  for (int i = 0; i < c.getSize(); ++i) {  
    int lit = c.getIthLiteral(i);
    if (not model.isUndefLit(lit)) {
      int DL = model.getDLOfLit(lit);
      if (DL > 0) DLs.insert(DL);
    }
  }
  return DLs.size();
}

int  Solver::computeLBD (const vector<int>& c) const {
  static unordered_set<int> DLs; DLs.clear();
  for (uint i = 0; i < c.size() ;++i)  
    if (not model.isUndefLit(c[i])) {
      int DL = model.getDLOfLit(c[i]);
      if (DL > 0) DLs.insert(DL);
    }
  return DLs.size();
}

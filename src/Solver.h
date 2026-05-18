#pragma once

#include <vector>
#include <queue>
#include <string>
#include <iostream>
#include <limits.h>
#include <fstream>

#include "PBConstraint.h"
#include "WConstraint.h"
#include "Strategy.h"
#include "MaxHeap.h"
#include "Model.h"
#include "OccurListVector.h"
#include "Clause.h"
#include "Reason.h"
#include "Statistics.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include <iomanip>
#include <bits/stdc++.h>

using namespace std;

class Solver {
 public:
  typedef enum {
    INFEASIBLE,
    NO_SOLUTION_FOUND,
    SOME_SOLUTION_FOUND,
    OPTIMUM_FOUND
  } StatusSolver;

 private:
  int numVars;
  bool conflict;
  static const int CONFLICT_CLAUSE      = 1;
  static const int CONFLICT_PB          = 2;
  static const int CONFLICT_BIN_CLAUSE  = 3;
  static const int CONFLICT_CARD  = 4;

  static const int BINCLAUSE  = 0;
  static const int CLAUSE     = 1;
  static const int CARD       = 2;
  static const int COUNTERPB  = 3;
  static const int WATCHEDPB  = 4;
  
  int typeConflict;
  int constraintConflictNum;
  int clauseConflictNum;
  Clause clauseConflict;
  pair<int,int> binClauseConflict;

  // TODO: move somewhere else
  // This controls the restart and the database reduction frequency
  long long nconfl_to_reduce = 2000;
  long long nconfl_to_restart = 0; // Think why this is zero

  // ---------------------------------------------------
  struct ConstraintCleanup {
    WConstraint wc;
    bool isInit;
    int activity;
    int LBD;
    ConstraintCleanup(const WConstraint& c, bool i, int a, int lbd):wc(c),isInit(i),activity(a),LBD(lbd){}
  };
  


  // //RN
  // int32_t iID; // internal ID, the idx of slack in slackMCoefV
  // int idx;  // >=0: index of watched literal (counting/watched propagation). <0: blocked literal (clausal propagation).
  // struct Header {
  //   unsigned cSize      : 2;  // the value means coefOrLit is: 0 (coef), 1 (SIGN_PTR_64), 2 (SIGN_PTR_128), 3 (SIGN_PTR_BIGINT)
  //   long long coefOrLit : 62; // the value is eigher coef, or pointer to different sizes
  // } header;


  
  class GeneralWatchListElem { //RN  
  public:
    int typeField;             //    BINCLAUSE    CLAUSE     CARD       COUNTERPB       WATCHEDPB
    int intField1;             //    lit          lit        -          coef            coef                                
    int intField2;             //    -            clauseID   constrID   constrID        constrID                                              
    int intField3;             //    -            -          -          -               indexOfMonomial

    GeneralWatchListElem (){}
    GeneralWatchListElem (int typeF, int i1 ): typeField(typeF), intField1(i1) {
      assert( typeF==BINCLAUSE );
    }
    GeneralWatchListElem (int typeF, int i1, int i2 ): typeField(typeF), intField1(i1), intField2(i2) {
      assert( typeF==CLAUSE     or
	      typeF==CARD       or
	      typeF==COUNTERPB );
    }
    GeneralWatchListElem (int typeF, int i1, int i2, int i3): typeField(typeF), intField1(i1), intField2(i2), intField3(i3) {
      assert( typeF==WATCHEDPB );
    }
    
    inline bool isBinClause ()       const { return typeField==BINCLAUSE; }
    inline bool isClause ()          const { return typeField==CLAUSE   ; }
    inline bool isCard ()            const { return typeField==CARD     ; }
    inline bool isCounterPB ()       const { return typeField==COUNTERPB; }
    inline bool isWatchedPB ()       const { return typeField==WATCHEDPB; }
    
    inline int  otherLit()           const { assert(isBinClause());                               return intField1; }
    inline int  cachedLit()          const { assert(isClause());                                  return intField1; }
    inline int  coef()               const { assert(isCounterPB() or isWatchedPB() );             return intField1; }

    inline int  clauseId()           const { assert(isClause());                                  return intField2; }
    //    inline int  constraintId()       const { assert(isCard() or isCounterPB() or isWatchedPB() ); return intField2; }
    inline int  constraintId()       const { assert(isCounterPB()); return intField2; }

    inline int  indexOfMonomial()    const { assert(isWatchedPB());                               return intField3; }
    
  };
  

  class WatchListElem { // for clauses
  public:
    int clauseId;
    int cachedLit;
    WatchListElem(){}
    WatchListElem (int id, int lit): clauseId(id), cachedLit(lit) {}
    friend ostream& operator << (ostream& os, const WatchListElem& m) {
      os << "(clNum " << m.clauseId << ",cachedLit " << m.cachedLit << ")";
      return os;
    }
  };
  


  class PBWatchElem {
  public:
    int ctrId;
    int coef;
    int idx;
    PBWatchElem(){}
    PBWatchElem (int ctrNum, int c, int idx):ctrId(ctrNum), coef(c), idx(idx) {}
  };


  // The following are all data structures needed for propagation;
  // watchlists of all types
  vector<vector<GeneralWatchListElem> > positiveGenWatchList;
  vector<vector<GeneralWatchListElem> > negativeGenWatchList;
  
  vector<vector<WatchListElem> > positiveWatchLists;
  vector<vector<WatchListElem> > negativeWatchLists;
  
  vector<vector<int> > positiveBinClauses;
  vector<vector<int> > negativeBinClauses;

  vector<vector<PBWatchElem> > positivePBWatches;
  vector<vector<PBWatchElem> > negativePBWatches;
  
  vector<OccurList> positiveOccurLists;
  vector<OccurList> negativeOccurLists;
  
  vector<PBConstraint> constraintsPB;
  vector<Clause> clauses;
  vector<bool> useCounter; // not used for now, because no watches available
  vector<long long> sumOfWatches; // TO DO: this should be called slack?
  
 public:
  Statistics stats;
  
 private:
  Strategy               strat;
  Model                  model;
  MaxHeap                maxHeap; // for VSIDS-like decision heuristic
  vector<int>            bestPolarityForVarInObjective; // 1 --> pos, 0 --> neg, -1 --> none
  bool                   feasibility;
  bool                   minimizing; // whether original problem was minimizing or not
                                     // this is only used to write cost or -cost
public:
  vector<pair<int,int>>  objective; // obj function to maximize, after removing negative coefficients
                                    // repeated lits/vars and unit lits. Pairs are <coeff,lit>
private:
  long long int          addedConstantToObjective; // obj function is: min objective + addedConstantToObjective

  vector<string>         varNames; // internal id to original string variable
  map<string,int>        stringVar2Num; // original string to original varNum

  vector<uint>           objectiveFunctions;  // store the idx of objective PBConstraint in constraintsPB
  int                    obj_num;

  vector<bool>           lastSolution;
  vector<int>            initialInputSolution; // for heuristic
  StatusSolver           status;
  
  int                    timeLimit;
  bool                   BT0; // BT to zero after each solution found

  int (*periodic_function)(int x);
  void (*import_external_constraints)(Solver * );
  
public: // all in Solver.cpp
  
  Solver(int nVars, clock_t beginTime);  

  // Declare input problem
  void addObjectiveFunction        (bool minimize, const vector<int>& coeffs, const vector<int>& vars);
  void addAndPropagatePBConstraint (WConstraint & c, const bool isInitial, int activity, const int LBD, bool isObj = false);
  void addVarName                  (int varNum, const string& varName);

  // Solve and retrieve info about solving process
  void   solve (int tlimit);
  void   set_periodic_function(int (*f) (int x) );
  void   set_import_external_constraints_procedure(void (*f) (Solver *) );
  int    cost_best_solution ( ) const;
  
  StatusSolver currentStatus ( ) const;
  void    printStats() const;
  double  real_time () const;
  double  process_time () const;
  double  absolute_real_time () const;
  double  absolute_process_time () const;
  uint64_t maximum_resident_set_size ();
  uint64_t current_resident_set_size ();
  
  // THIS NEEDS TO BE REVISITED
  void readStrategy        (const string& fileStrategy);
  void readDecision        (const string& filePolarity);
  void readInitialSolution (const string& fileName);
  void checkInitialInputSolutionNeeded() const;
  void setBT0              (bool up);

 private:

  // Mapping between lit and positive integer (useful for indexing vectors)
  // 1 --> 1, -1 --> 2, 2 --> 3, -2 --> 4
  int lit2id(int lit) { return lit > 0 ? 2*lit - 1 : 2*(-lit); }
  int id2lit(int id)  { return id%2    ? id/2 + 1  : -id/2 ;   }
  int maxLitId ( )    { return 2*stats.numOfVars;}
  int minLitId ( )    { return 1;}
  
  // Solver.cpp
  bool   timeout() const;
  void   backjumpToDL(int dl);
  int    popAndUnassign();
  void   setTrueDueToDecision( int lit );
  void   setTrueDueToReason( int lit, const Reason& r);
  double luby (double y, int idx);
  void   updateStatusConflictAtDLZero ( );
  void   writeOccurLists ( );
  void   writeWatchLists ( );
  void   writeConstraint (const PBConstraint& c);
  void   printConstraintsPB() const;
  void   printConstraint (const PBConstraint& c) const;
  void   printConstraint (const WConstraint& c) const;

  // Branching.cpp
  int  getNextDecisionVar();
  void takeDecisionForVar (int decVar);
  void increaseScoresOfVars (const WConstraint& constraint);
  void computeBestPolarityForVarInObjectiveFunction ( );
  void increaseActivityScoreOfVar(int var);

  // Cleanup.cpp
  void cleanupPBConstraints (vector<ConstraintCleanup>& pbs, vector<ConstraintCleanup>& cls, vector<pair<int,int> >& binClauses, vector<bool>& ctrIsRemoved, uint newestObjectiveFunction);
  void cleanupClauses (vector<ConstraintCleanup>& cls, vector<pair<int,int> >& binClauses, vector<bool>& ctrIsRemoved);
  void cleanupBinaryClauses ( vector<pair<int,int> >& binClauses );
  void doCleanup ();
  void buildOccurListsAndPBWatches ( );
  void buildWatchLists ( );
  void buildBinaryClauses (const vector<pair<int,int> >& binClauses);
  void minNumWatchesCleanup (const WConstraint & c, long long& wslk, int& numWatches);

  // ConflictAnalysis.cpp
  void conflictAnalysis ( );
  void removeUnits(WConstraint& c);
  void conflictAnalysisAndBackjump (const WConstraint& falsifiedCtr);
  int  lowestDLAtWhichClausePropagatesOrConflicting (const WConstraint & c) const;
  int  lowestDLAtWhichConstraintPropagatesOrConflicting (const WConstraint & c) const;
  void lemmaShorteningAuxFunction (int lit, vector<bool>& isMarked, vector<int>& lemma, int& lastMarkedInLemma, bool filterOutDLZero);
  void lemmaShortening (vector<int>& lemma);
  void fixRoundingProblemMinSAT (int l, WConstraint & c) const;
  void fixRoundingProblemSAT (int l, WConstraint & c) const;
  bool applyCut ( int var, const WConstraint & c1, const WConstraint & c2, WConstraint & cut, bool& clash, bool& isInconsistentCut );
  template<class T>
  bool applyCut ( int var, const WConstraint & c1, const WConstraint & c2, WConstraint & cut, bool& clash, bool& isInconsistentCut );
  int  computeLBD (const WConstraint& c) const;
  int  computeLBD (const vector<int>& c) const;

  // ConstraintValue.cpp (revisit them)
  bool constraintIsContradiction ( const WConstraint& c ) const;
  bool constraintIsTrue          ( const WConstraint& c ) const;
  bool constraintIsFalse         ( const WConstraint& c ) const;
  long long slack (const WConstraint & c) const;
  long long maxSumOfConstraintMinusRhsPropagated(const WConstraint& c) const;
  bool clauseIsFalse (const Clause& cl) const;
  bool clauseIsTrue (const Clause& cl) const;
  bool clauseHasTwoUnassignedWatches (const Clause& cl) const;
  bool clausePropagates (const Clause& cl) const;
  bool constraintIsConflictingOrPropagating( const WConstraint &c );

public:
  // CubeConquerAPI.cpp
  int  reducedFormulaSize ( )       const;
  int  assignedVars       ( )       const;
  bool isTrueLit          (int lit) const;
  bool isFalseLit         (int lit) const;
  bool isUndefLit         (int lit) const;

  bool assumeAndPropagate (int lit); // lit must be undef. Returns false if conflict found, true otherwise
  void backtrack          (int nLevels);
  
private:
  // DBAddition.cpp
  void addBinaryClause        ( int lit1, int lit2);
  void addClause              ( const Clause & c);
  void addPBConstraintCounter ( const PBConstraint & c);

  // MaxHeap.cpp [pensar si moure tot aqui]

  // Model.cpp [pensar si moure tot aqui]

  // Parser.cpp [pensar si moure tot aqui]
  
  // PBConstraint

  // Propagate.cpp
  void visitClauseWatchList (int lit);
  void visitPBCounterLists (int lit);
  void visitBinClause (int lit);
  void propagate ();
  void checkPropagatedPBs ( PBConstraint& c, int ctrId );
  void checkAllConstraintsPropagated();
  void propagatePBCtrCounter ( const int ctrId, const long long wslk );
  void propagateInitialConstraintCounter (const int ctrId);
  void checkObjectiveIsConflictingOrPropagating ( const int ctrId);
  void checkClausesPropagated ( ) const;
  
  // Strategy.cpp [pensar si moure tot aqui]

  // WConstraint.cpp [pensar si moure tot aqui]
  
  };
  


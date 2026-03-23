#pragma once

#include <vector>
#include <map>
#include "Statistics.h"

// Used for Luby
#define isZeroOrPowerOfTwo(x)  ( !(((x)-1) & (x)) )

typedef enum {
  OBJECTIVE,
  POSITIVE,
  NEGATIVE,
  LAST_PHASE,
  LAST_SOLUTION,
  INITIAL_SOLUTION,
  RANDOM
} DecPolarity;


class Strategy {

public:

  // STATIC INPUT PARAMETERS
  // ---- Decision heuristic
  double INITIAL_SCORE_BONUS; 
  double SCORE_BONUS_INFLATION_FACTOR; 
  int    ONE_RANDOM_DECISION_OUT_OF;
  vector<DecPolarity> DEFAULT_POLARITY;
  int NUM_CONFLICTS_CLOSE_TO_SOLUTION;
  int NUM_CONFLICTS_CLOSE_TO_INITIAL_SOLUTION;
  
  // ---- Restarts
  bool   USE_LUBY;
  int    NO_RESTARTS_BELOW_THIS_DL; 
  int    INITIAL_INNER_RESTART_BOUND;
  float  INNER_RESTART_BOUND_INFLATION_FACTOR;
  float  OUTER_RESTART_BOUND_INFLATION_FACTOR;
  int    LUBY_MULTIPLIER;
  
  // ---- Cleanups
  int    INITIAL_NUM_OF_NEW_CONSTRAINTS_FOR_NEXT_CLEANUP;
  double CONSTRAINTS_FOR_CLEANUP_INFLATION_FACTOR;
  int    ACTIVITY_BONUS_FOR_PBS;
  int    ACTIVITY_BONUS_FOR_CARDS;
  int    ACTIVITY_BONUS_FOR_CLAUSES;
    
  // ---- Lemma learning
  int    NEW_CONSTRAINT_ACTIVITY;
  bool   SAT_CONFLICT_ANALYSIS;
  
  
  // DYNAMIC PARAMETERS
  // ---- Decision heuristic
  double scoreBonus;
  vector<vector<DecPolarity> > decisionPolarities;
  vector<int>                  multFactorInDecisionIncrease;
  
  // ---- Restarts
  int    innerRestartBound; 
  int    outerRestartBound;
  int    lubyCurrentNumber;

  
  // ---- Cleanups
  int    numOfNewConstraintsForNextCleanup;



  // ********** TEMP
  int windowTrailSize = 5000;
  vector<int> trailSizes;
  int idxInWindows;
  double movingAverageTrailSizes;
  
  Statistics& stats;

public:
  inline Strategy(Statistics& st, int nVars):
    stats(st)
  {
    // STATIC INPUT PARAMETERS
    // -- Decision heuristic
    INITIAL_SCORE_BONUS                             = 1.0;
    SCORE_BONUS_INFLATION_FACTOR                    = 1.03;    // 1.03
    ONE_RANDOM_DECISION_OUT_OF                      = 100;    
    DEFAULT_POLARITY                                = {OBJECTIVE,LAST_PHASE};
    NUM_CONFLICTS_CLOSE_TO_SOLUTION                 = 1000;
    NUM_CONFLICTS_CLOSE_TO_INITIAL_SOLUTION         = 1000;
    
    // -- Restarts
    USE_LUBY                                        = true;
    NO_RESTARTS_BELOW_THIS_DL                       = 3;    
    INITIAL_INNER_RESTART_BOUND                     = 250; // 5000
    INNER_RESTART_BOUND_INFLATION_FACTOR            = 1.03;  // 1.03
    OUTER_RESTART_BOUND_INFLATION_FACTOR            = 1.03;  // 1.03
    LUBY_MULTIPLIER                                 = 100;    
    // -- Cleanups
    INITIAL_NUM_OF_NEW_CONSTRAINTS_FOR_NEXT_CLEANUP = 5000; // 4000    
    CONSTRAINTS_FOR_CLEANUP_INFLATION_FACTOR        = 1.03;      // 1.03
    ACTIVITY_BONUS_FOR_PBS                          = 3;
    ACTIVITY_BONUS_FOR_CARDS                        = 3;
    ACTIVITY_BONUS_FOR_CLAUSES                      = 3;    
    // -- Lemma learning
    NEW_CONSTRAINT_ACTIVITY                         = 5;  // 8
    SAT_CONFLICT_ANALYSIS                           = true;
    
    // DYNAMIC PARAMETERS
    // -- Decision heuristic
    scoreBonus = INITIAL_SCORE_BONUS;
    decisionPolarities =           vector<vector<DecPolarity> >(nVars+1,DEFAULT_POLARITY);
    multFactorInDecisionIncrease = vector<int>        (nVars+1,1);
    
    // -- Restarts
    innerRestartBound = INITIAL_INNER_RESTART_BOUND;   
    outerRestartBound = INITIAL_INNER_RESTART_BOUND;
    lubyCurrentNumber = 1;
    
    // -- Cleanups
    numOfNewConstraintsForNextCleanup = INITIAL_NUM_OF_NEW_CONSTRAINTS_FOR_NEXT_CLEANUP;  

    trailSizes = vector<int>(windowTrailSize,0);
    movingAverageTrailSizes = 0;
    idxInWindows = 0;
  }

  inline void reportLemmaShortening (int originalSize, int shortenedSize) {
    ++stats.numOfLemmaShortenings;
    stats.sumOfPercReductionsInLemmaShortenings += double(originalSize-shortenedSize)/originalSize;
  }

  inline void reportInitialSizes (int PBs, int clauses, int binary) {
    stats.numOfInitialPBConstraints = PBs;
    stats.numOfInitialClauses = clauses;
    stats.numOfInitialBinClauses = binary;
  }

  inline void reportSolutionFound (  long long int value ) {
    assert(stats.numOfSolutionsFound == 0  or value < stats.costOfBestSolution);
    ++stats.numOfSolutionsFound;
    stats.costOfBestSolution = value;
    stats.numOfConflictsSinceLastSolution = 0;
  }
  
  inline void reportRestart ( ) {
    stats.numOfDecisionsSinceLastRestart = 0;
    stats.numOfConflictsSinceLastRestart = 0;
    ++stats.numOfRestarts;
    //if (USE_LUBY) {
    //lubyCurrentNumber = computeIthLubyNumber(stats.numOfRestarts);
    //}
    //else{
    //if (innerRestartBound >= outerRestartBound) { 
    //outerRestartBound = (int) (outerRestartBound * OUTER_RESTART_BOUND_INFLATION_FACTOR); 
    //innerRestartBound = INITIAL_INNER_RESTART_BOUND;
    //} else { 
    //innerRestartBound = (int) (innerRestartBound * INNER_RESTART_BOUND_INFLATION_FACTOR); 
    //}
    //}
  }
  
  inline void reportCleanup (  ) {
    ++stats.numOfCleanUps;
    //numOfNewConstraintsForNextCleanup = 
    //(int) (numOfNewConstraintsForNextCleanup * CONSTRAINTS_FOR_CLEANUP_INFLATION_FACTOR);
  }

  inline void reportNewPBClausesDatabase ( int initPBs, int initCls, int bins, int newPBs, int newCls ){
    stats.numOfSurvivingInitPBConstraints = initPBs;
    stats.numOfSurvivingInitClauses = initCls;
    stats.numOfBinClauses = bins;
    stats.numOfNewPBConstraints = newPBs;
    stats.numOfNewClauses = newCls;
  }

  inline void reportConflict ( int trailSize ){
    ++stats.numOfConflicts;
    ++stats.numOfConflictsSinceLastRestart;
    ++stats.numOfConflictsSinceLastSolution;
    movingAverageTrailSizes = (movingAverageTrailSizes*windowTrailSize - trailSizes[idxInWindows] + trailSize)/windowTrailSize;
    trailSizes[idxInWindows] = trailSize;
    idxInWindows = (idxInWindows+1)%windowTrailSize;
  }

  inline void reportDecision ( int decisionLevel ){
    ++stats.numOfDecisionsSinceLastRestart;
    ++stats.numOfDecisions;
    //++stats.numOfConflictsSinceLastSolution;
  }

  inline void reportLongIntCut ( ){
    ++stats.numOfLongIntCuts;
  }

  inline void reportIntCut ( ){
    ++stats.numOfIntCuts;
  }

  inline void reportLearnBinClause ( ){
    ++stats.numOfBinClauses;
    ++stats.numOfTotalLearnedBinClauses;
  }

  inline void reportLearnClause ( int size ) {
    ++stats.numOfNewClauses;
    ++stats.numOfTotalLearnedClauses;
    stats.numOfLearnedLitsInClauses += size;
  }

  inline void reportLearnPB ( int size ) {
    ++stats.numOfNewPBConstraints;
    ++stats.numOfTotalLearnedPBConstraints;
    stats.numOfLearnedLitsInPbs += size;
  }

  inline void reportPropagationInPB ( ){
    ++stats.numOfPropagationsPB;
  }
  inline void reportPropagationInPBCounter ( ){
    ++stats.numOfPropagationsPBCounter;
  }
  
  inline void reportPropagationInPBWatch ( ){
    ++stats.numOfPropagationsPBWatch;
  }

  inline void reportPropagationInClause ( ){
    ++stats.numOfPropagationsClauses;
  }

  inline void reportPropagationInBinClause ( ){
    ++stats.numOfPropagationsBinClauses;
  }

  inline void reportRandomDecision ( ) {}

  inline void increaseActivityScoreBonus() {
    scoreBonus *= SCORE_BONUS_INFLATION_FACTOR;
  }

  inline int computeIthLubyNumber (uint i){
    if(isZeroOrPowerOfTwo(i+1)){
      return((uint)pow(2,log2(i+1)-1));
    } else{
      uint l = (int)log2(i);
      uint p = (int)pow((float)2,(int)l);
      return(computeIthLubyNumber(i+1-p));
    }
  }

  inline bool randomDecisionCondition ( ) {
    return stats.numOfDecisions % ONE_RANDOM_DECISION_OUT_OF == 0 and stats.numOfDecisions < 100000;
  }

  inline double increaseFactorInDecision (uint var) {
    return multFactorInDecisionIncrease[var]*scoreBonus;
  }
  
  inline bool restartCondition(int decisionLevel) {    
    if (USE_LUBY)
      return stats.numOfConflictsSinceLastRestart >= lubyCurrentNumber*LUBY_MULTIPLIER and decisionLevel > NO_RESTARTS_BELOW_THIS_DL;
    else
      return stats.numOfConflictsSinceLastRestart >= innerRestartBound and decisionLevel > NO_RESTARTS_BELOW_THIS_DL;
  }
  
  //inline bool cleanupCondition ( ) {
  //  static long long int maxInts = (long long uint)(1)<<27; // 0.25GB of data (4 bytes =  1 int)
  //  bool toReturn = stats.numOfNewPBConstraints + stats.numOfNewCardinalities + stats.numOfNewClauses >= numOfNewConstraintsForNextCleanup or stats.numOfIntsInPbsAndClauses >= maxInts;
  //  return toReturn;
  //}

  inline bool keepLearnedPBConstraint (const PBConstraint& c ) {
    //    return c.isInitial() or c.getLBD() <= 20;
    return c.isInitial() or c.getSize() <= 3 or c.getActivity() > 0;
  }

  inline bool keepLearnedClause (const Clause& c ) {
    //return c.isInitial() or c.getLBD() <= 4;
    return c.isInitial() or c.getSize() <= 3 or c.getActivity() > 0;    
  }

  inline int reduceActivityOfClauseInCleanup (int act) { return act/2;}
  inline int reduceActivityOfPBInCleanup     (int act) { return act/2;}
  inline int reduceActivityOfCardInCleanup   (int act) { return act/2;}

  inline void read (const string& fileName) {
    cout << "Reading strategy file " << fileName << endl;
    fstream input(fileName.c_str(), fstream::in);
    if (not input) {cout << "Strategy file " << fileName << " not recognized" << endl;exit(1);}
    string token;
    while (input >> token) {
      if (token == "INITIAL_SCORE_BONUS") {
	input >> INITIAL_SCORE_BONUS;
      }
      else if (token == "SCORE_BONUS_INFLATION_FACTOR") {
	input >> SCORE_BONUS_INFLATION_FACTOR;
      }      
      else if (token == "ONE_RANDOM_DECISION_OUT_OF") {
	input >> ONE_RANDOM_DECISION_OUT_OF;
      }
      else if (token == "NUM_CONFLICTS_CLOSE_TO_SOLUTION") {
	input >> NUM_CONFLICTS_CLOSE_TO_SOLUTION;
      }
      else if (token == "NUM_CONFLICTS_CLOSE_TO_INITIAL_SOLUTION") {
	input >> NUM_CONFLICTS_CLOSE_TO_INITIAL_SOLUTION;
      }
      else if (token == "USE_LUBY"){
	input >> USE_LUBY;
      }
      else if (token == "NO_RESTARTS_BELOW_THIS_DL"){
	input >> NO_RESTARTS_BELOW_THIS_DL;
      }      
      else if (token == "INITIAL_INNER_RESTART_BOUND"){
	input >> INITIAL_INNER_RESTART_BOUND;
      }      
      else if (token == "INNER_RESTART_BOUND_INFLATION_FACTOR"){
	input >> INNER_RESTART_BOUND_INFLATION_FACTOR;
      }
      else if (token == "OUTER_RESTART_BOUND_INFLATION_FACTOR"){
	input >> OUTER_RESTART_BOUND_INFLATION_FACTOR;
      }
      else if (token == "LUBY_MULTIPLIER"){
	input >> LUBY_MULTIPLIER;
      }        
      else if (token == "INITIAL_NUM_OF_NEW_CONSTRAINTS_FOR_NEXT_CLEANUP"){
	input >> INITIAL_NUM_OF_NEW_CONSTRAINTS_FOR_NEXT_CLEANUP;
      }
      else if (token == "CONSTRAINTS_FOR_CLEANUP_INFLATION_FACTOR"){
	input >> CONSTRAINTS_FOR_CLEANUP_INFLATION_FACTOR;
      }
      else if (token == "ACTIVITY_BONUS_FOR_PBS"){
	input >> ACTIVITY_BONUS_FOR_PBS;
      }
      else if (token == "ACTIVITY_BONUS_FOR_CARDS"){
	input >> ACTIVITY_BONUS_FOR_CARDS;
      }
      else if (token == "ACTIVITY_BONUS_FOR_CLAUSES"){
	input >> ACTIVITY_BONUS_FOR_CLAUSES;
      }        
      else if (token == "NEW_CONSTRAINT_ACTIVITY"){
	input >> NEW_CONSTRAINT_ACTIVITY;
      }
      else if (token == "SAT_CONFLICT_ANALYSIS") {
	input >> SAT_CONFLICT_ANALYSIS;
      }
      else {
	cout << "Token \"" << token << "\" in strategy file not recognized" << endl;
	exit(1);
      }
    }    
  }

  inline void readPolarityAndFactor (fstream& in, vector<DecPolarity>& pols, int fact) {
    string token;
    while (in >> token and token != "#") {
      if      (token == "obj") pols.push_back(OBJECTIVE);
      else if (token == "pos") pols.push_back(POSITIVE);
      else if (token == "neg") pols.push_back(NEGATIVE);
      else if (token == "lastSol") pols.push_back(LAST_SOLUTION);
      else if (token == "lastPha") pols.push_back(LAST_PHASE);
      else if (token == "initialSol") pols.push_back(INITIAL_SOLUTION);
      else if (token == "rand")    pols.push_back(RANDOM);
      else {cout << "Polarity " << token << " in decision strategy file not allowed" << endl;exit(1);}
    }
    in >> fact;
  }
  
  inline void readDecisionStrategy (const string& fileName, map<string,int>& string2VarNum) {
    cout << "Reading decision strategy file " << fileName << endl;
    fstream input(fileName.c_str(), fstream::in);
    if (not input) {cout << "Decision strategy file " << fileName << " not recognized" << endl;exit(1);}
    string token;
    input >> token; assert(token == "default");
    vector<DecPolarity> polarity;
    int multFactor = 1;
    readPolarityAndFactor(input,polarity,multFactor);
    decisionPolarities =           vector<vector<DecPolarity> > (stats.numOfVars+1,polarity);
    multFactorInDecisionIncrease = vector<int>          (stats.numOfVars+1,multFactor);

    string nVar;
    while (input >> nVar) {
      if (string2VarNum.count(nVar) != 1) {
	cout << "Var " << nVar << " in decisionStrategy file not a declared variable name" << endl;
	readPolarityAndFactor(input,polarity,multFactor);
      }
      else {
	int numVar = string2VarNum[nVar];
	assert(numVar >=1); assert(numVar <= stats.numOfVars);
	polarity.clear();
	readPolarityAndFactor(input,polarity,multFactor);
	decisionPolarities[numVar] = polarity;
	multFactorInDecisionIncrease[numVar] = multFactor;
      }
    }
  }
  
  inline void print ( ) {
    cout << "INITIAL_SCORE_BONUS                              " << INITIAL_SCORE_BONUS << endl;
    cout << "SCORE_BONUS_INFLATION_FACTOR                     " << SCORE_BONUS_INFLATION_FACTOR << endl;
    cout << "ONE_RANDOM_DECISION_OUT_OF                       " << ONE_RANDOM_DECISION_OUT_OF << endl;
    cout << "NUM_CONFLICTS_CLOSE_TO_SOLUTION                  " << NUM_CONFLICTS_CLOSE_TO_SOLUTION << endl;
    cout << "NUM_CONFLICTS_CLOSE_TO_INITIAL_SOLUTION          " << NUM_CONFLICTS_CLOSE_TO_INITIAL_SOLUTION << endl;
    cout << "USE_LUBY                                         " << USE_LUBY << endl;
    cout << "NO_RESTARTS_BELOW_THIS_DL                        " << NO_RESTARTS_BELOW_THIS_DL << endl;
    cout << "INITIAL_INNER_RESTART_BOUND                      " << INITIAL_INNER_RESTART_BOUND << endl;
    cout << "INNER_RESTART_BOUND_INFLATION_FACTOR             " << INNER_RESTART_BOUND_INFLATION_FACTOR << endl;
    cout << "OUTER_RESTART_BOUND_INFLATION_FACTOR             " << OUTER_RESTART_BOUND_INFLATION_FACTOR << endl;
    cout << "LUBY_MULTIPLIER                                  " << LUBY_MULTIPLIER << endl;
    cout << "INITIAL_NUM_OF_NEW_CONSTRAINTS_FOR_NEXT_CLEANUP  " << INITIAL_NUM_OF_NEW_CONSTRAINTS_FOR_NEXT_CLEANUP << endl;
    cout << "CONSTRAINTS_FOR_CLEANUP_INFLATION_FACTOR         " << CONSTRAINTS_FOR_CLEANUP_INFLATION_FACTOR << endl;
    cout << "ACTIVITY_BONUS_FOR_PBS                           " << ACTIVITY_BONUS_FOR_PBS << endl;
    cout << "ACTIVITY_BONUS_FOR_CARDS                         " << ACTIVITY_BONUS_FOR_CARDS << endl;
    cout << "ACTIVITY_BONUS_FOR_CLAUSES                       " << ACTIVITY_BONUS_FOR_CLAUSES << endl;
    cout << "NEW_CONSTRAINT_ACTIVITY                          " << NEW_CONSTRAINT_ACTIVITY << endl;
    cout << "SAT_CONFLICT_ANALYSIS                            " << SAT_CONFLICT_ANALYSIS << endl;
  }
};



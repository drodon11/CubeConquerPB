#pragma once

#include <iostream>
class Statistics {

 public:
  int           numOfVars;

  int           numOfSolutionsFound;
  long long int costOfBestSolution, lastSubtractConstant;

  uint           numOfDecisions;
  uint           numOfConflicts;
  uint           nPBLemmas;
  uint           nClauseLemmas;
  int            numOfBackjump;


  uint           numOfRestarts;
  uint           numOfCleanUps;
  long long int numOfPropagationsPB;
  long long int numOfPropagationsClauses;
  long long int numOfPropagationsBinClauses;
  long long int numOfPropagationsPBCounter;
  long long int numOfPropagationsPBWatch;
  
  long long int numOfCounterCtr = 0;
  long long int numConstantOverflow = 0;
  
  unsigned long long int numGLPB = 0, numGLPBWatch = 0;
  unsigned long long int numCheckWatchidx = 0;
  unsigned long long int numGLclauses = 0;
  unsigned long long int numGLcards = 0;
  
  int           numOfIntCuts;
  int           numOfLongIntCuts;

  int           numOfLemmaShortenings;
  double        sumOfPercReductionsInLemmaShortenings;

  int           numOfNewPBConstraints;
  int           numOfNewClauses;
  int           numOfBinClauses;

  int           numOfSurvivingInitPBConstraints;
  int           numOfSurvivingInitClauses;

  int           numOfInitialPBConstraints; // when we start the search
  int           numOfInitialCardinalities; // when we start the search
  int           numOfInitialClauses;       // when we start the search
  int           numOfInitialBinClauses;    // when we start the search

  int           numOfTotalLearnedPBConstraints;
  int           numOfTotalLearnedClauses;
  int           numOfTotalLearnedBinClauses;

  long long int numOfIntsInPbsAndClauses;

  long long int numOfLearnedLitsInPbs;
  long long int numOfLearnedLitsInClauses;

  int           numOfConflictsSinceLastRestart;
  int           numOfDecisionsSinceLastRestart;
  int           numOfConflictsSinceLastSolution;

  clock_t       startSolvingTime;
  clock_t       lastTimeWhenStatisticsPrinted;

  int           numOfPBConstraintsInConflicts;
  int           numOfCardinalitiesInConflicts;
  int           numOfClausesInConflicts;
  int           numOfBinClausesInConflicts;

  uint          numOfCuts;
  uint          numOfCutsCoefGOne;
  uint          numOfCutsCoefGOneNoSAT;
  
  struct { double process, real; } time;

  Statistics (clock_t beginTime, int nVars) :

    numOfVars(nVars),
    numOfSolutionsFound(0),
    numOfDecisions(0),
    numOfConflicts(0),
    numOfBackjump(0),

    numOfRestarts(0),
    numOfCleanUps(0),
    numOfPropagationsPB(0),
    numOfPropagationsClauses(0),
    numOfPropagationsBinClauses(0),
    numOfPropagationsPBCounter(0),
    numOfPropagationsPBWatch(0),
    numOfIntCuts(0),
    numOfLongIntCuts(0),

    numOfLemmaShortenings(0),
    sumOfPercReductionsInLemmaShortenings(0),

    numOfNewPBConstraints(0),
    numOfNewClauses(0),
    numOfBinClauses(0),

    numOfInitialPBConstraints(0),
    numOfInitialCardinalities(0),
    numOfInitialClauses(0),
    numOfInitialBinClauses(0),

    numOfTotalLearnedPBConstraints(0),
    numOfTotalLearnedClauses(0),
    numOfTotalLearnedBinClauses(0),

    numOfIntsInPbsAndClauses(0),

    numOfLearnedLitsInPbs(0),
    numOfLearnedLitsInClauses(0),

    numOfConflictsSinceLastRestart(0),
    numOfDecisionsSinceLastRestart(0),
    numOfConflictsSinceLastSolution(0),

    startSolvingTime(beginTime),
    lastTimeWhenStatisticsPrinted(beginTime),

      numOfPBConstraintsInConflicts(0),
      numOfCardinalitiesInConflicts(0),
      numOfClausesInConflicts(0),
      numOfBinClausesInConflicts(0),

      numOfCuts(0),
      numOfCutsCoefGOne(0),
      numOfCutsCoefGOneNoSAT(0)
  
    {}

  inline void print ( ) const {
    cout << endl;
    cout << "Solutions found:          " << numOfSolutionsFound << endl;
    if (numOfSolutionsFound)
      cout << "Cost of best solution:    " << costOfBestSolution << endl;
    cout << "Decisions:                " << numOfDecisions << endl;
    cout << "Conflicts:                " << numOfConflicts << endl;
    cout << "Restarts:                 " << numOfRestarts << endl;
    cout << "CleanUps:                 " << numOfCleanUps << endl;  // numOfCleanUps +1 for rs strategy
    cout << "Initial PBs:              " << numOfInitialPBConstraints << endl;
    //cout << "Initial Cards:            " << numOfInitialCardinalities << endl;
    //cout << "Initial Clauses:          " << numOfInitialClauses << endl;
    //cout << "Initial BinClauses:       " << numOfInitialBinClauses << endl;
    cout << "Final PBs:                " << numOfSurvivingInitPBConstraints + numOfNewPBConstraints << endl;
    //cout << "Final Cards:              " << numOfSurvivingInitCardinalities + numOfNewCardinalities << endl;
    cout << "Final Clauses:            " << numOfSurvivingInitClauses + numOfNewClauses << endl;
    cout << "Final BinClauses:         " << numOfBinClauses << endl;
    cout << "Final learned PBs:        " << numOfNewPBConstraints << endl;
    //cout << "Final learned Cards:      " << numOfNewCardinalities << endl;
    cout << "Final learned Clauses     " << numOfNewClauses << endl;
    cout << "Total learned PBs:        " << numOfTotalLearnedPBConstraints << endl;
    //cout << "Total learned Cards:      " << numOfTotalLearnedCardinalities << endl;
    cout << "Total learned Clauses:    " << numOfTotalLearnedClauses << endl;
    cout << "Total learned BinClauses: " << numOfTotalLearnedBinClauses << endl;
    cout << "Avg. size of learned PBs: " << double(numOfLearnedLitsInPbs)/numOfTotalLearnedPBConstraints << endl;
    //cout << "Avg. size of learned Cards: " << double(numOfLearnedLitsInCards)/numOfTotalLearnedCardinalities << endl;
    cout << "Avg. size of learned cls: " << double(numOfLearnedLitsInClauses)/numOfTotalLearnedClauses << endl;
    cout << "avg. %red. lemma short.:  " << sumOfPercReductionsInLemmaShortenings/numOfLemmaShortenings*100 << endl;
    cout << "Long-int cuts:            " << numOfLongIntCuts << endl;
    cout << "Integer cuts:             " << numOfIntCuts << endl;
    //cout << "avg. num resolutions/conf:" << numOfResolutions/double(numOfPurelySATConflictAnalysis) << endl;
    cout << "avg. num cuts/conf:       " << (numOfIntCuts + numOfLongIntCuts)/double(numOfConflicts) << endl;
    
    long long numOfPropagationsPBTotal = numOfPropagationsPBCounter + numOfPropagationsPBWatch;
    double totalProps = numOfPropagationsPBTotal + numOfPropagationsClauses + numOfPropagationsBinClauses;
    
    //cout << "%Prop in PBs:            " << numOfPropagationsPB/totalProps*100 << endl;
    //cout << "%Prop in Cards:          " << numOfPropagationsCards/totalProps*100 << endl;
    //cout << "%Prop in Clauses:        " << numOfPropagationsClauses/totalProps*100 << endl;
    //cout << "%Prop in BinClauses:     " << numOfPropagationsBinClauses/totalProps*100 << endl;
    cout << "Props in PBs:            " << numOfPropagationsPBTotal << " ( %Counter: " << (double)numOfPropagationsPBCounter/numOfPropagationsPBTotal*100 
           << "% ,%Watch: " << (double)numOfPropagationsPBWatch/numOfPropagationsPBTotal*100 << "% )"<< endl;
    
    //cout << "Props in Cards:          " << numOfPropagationsCards     << endl;
    cout << "Props in Clauses:        " << numOfPropagationsClauses     << endl;
    cout << "Props in BinClauses:     " << numOfPropagationsBinClauses  << endl;
    cout << "Props:                   " << totalProps                   << endl;
    
    //cout << "numGLPB:                 " << numGLPB << endl;          
    //cout << "numLoadPB:                 " << (numLoadPBCounter + numLoadPBWatch) << " (counter " << numLoadPBCounter << " ,watch " << numLoadPBWatch << " )" << endl;        
    //cout << "numGLPBWatch:            " << numGLPBWatch << endl;          
    //cout << "numGLcards:              " << numGLcards << endl;  
    //cout << "numGLclauses:            " << numGLclauses << endl << endl;  
    
    //cout << "numOfWatchedCtrs:        " << numOfWatchedCtrs << " ( " << (double)numOfWatchedCtrs/(numOfWatchedCtrs+numOfCounterCtr)*100 << "% )" << endl;        
    //cout << "numOfCounterCtr:         " << numOfCounterCtr << " ( " << (double)numOfCounterCtr/(numOfWatchedCtrs+numOfCounterCtr)*100 << "% )" << endl;        
    cout << "numOfCounterCtr:         " << numOfCounterCtr << endl;
    //Substract because when numOfCutsCoefGOne does an extra cut
    cout << "Number of cuts:         " << numOfCuts - numOfCutsCoefGOne << endl;
    cout << "Number of cuts both greater than one:         " << numOfCutsCoefGOne<< endl;
    cout << "Number of cuts both greater than one and INSAT cut:         " << numOfCutsCoefGOneNoSAT<< endl;
    
    cout << "#over TWOTOTHE30TH:                 " << numConstantOverflow << endl;  
  }

  inline void shortPrint ( ) {
    static int n = 0;
    ++n;
    numOfPropagationsPB = numOfPropagationsPBCounter + numOfPropagationsPBWatch;
    //    if (n%10 == 0) {print();return;}
    lastTimeWhenStatisticsPrinted = clock();
    cout << endl << double(lastTimeWhenStatisticsPrinted/CLOCKS_PER_SEC) << "s. Decs: " << numOfDecisions
   //<< ". confs: " << numOfConflicts << "(" << numOfPurelySATConflictAnalysis/double(numOfConflicts)*100 << "% SAT)"
   << ". init PBs: " << numOfSurvivingInitPBConstraints
   << ". init cls: " << numOfSurvivingInitClauses
   << ". PB lemmas: " << numOfNewPBConstraints
   << ". cl lemmas: " << numOfNewClauses
   << ". bin cls: " << numOfBinClauses
   << ". PB  conf: " << numOfPBConstraintsInConflicts
   //<< ". card  conf: " << numOfCardinalitiesInConflicts
   << ". cl  conf: " << numOfClausesInConflicts
   << ". bin conf: " << numOfBinClausesInConflicts
   << ". %props PB: " << double(numOfPropagationsPB)/(numOfPropagationsPB+numOfPropagationsClauses+numOfPropagationsBinClauses) * 100  << "%"
   << ". %props cl: " << double(numOfPropagationsClauses)/(numOfPropagationsPB+numOfPropagationsClauses+numOfPropagationsBinClauses) * 100  << "%";
    if (numOfSolutionsFound > 0) cout << ". incumbent: " << costOfBestSolution << "  ";
    else                 cout << ". incumbent: -  ";
  }
};



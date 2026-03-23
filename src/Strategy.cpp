class Strategy {
public:
  double initialScoreBonus; // intern
  double scoreBonusInflationFactor;  // intern
  int noRestartsBelowThisDL; // intern
  int numOfNewConstraintsForNextCleanup; // intern
  double constraintsForCleanupInflationFactor; // intern
  int numOfDecisionsSinceLastRestart; // extern
  int numOfDecisionsSinceLastCleanup; // extern
  int numOfConflictsSinceLastRestart; // extern
  int newConstraintActivity;  // intern
  float innerRestartBoundInflationFactor; // intern
  float outerRestartBoundInflationFactor; // intern
  int innerRestartBound; // intern
  int outerRestartBound; // intern

  Strategy() {
    innerRestartBoundInflationFactor = 1.01;  // 1.03
    outerRestartBoundInflationFactor = 1.01;  // 1.03
    innerRestartBound = 250;   // 5000
    outerRestartBound = 250;   // 5000
    numOfDecisionsSinceLastRestart = 0;
    numOfConflictsSinceLastRestart = 0;
    numOfNewConstraintsForNextCleanup = 5000;  // 4000
    initialScoreBonus = 1.0;
    scoreBonusInflationFactor = 1.02;    // 1.03
    noRestartsBelowThisDL = 3;
    constraintsForCleanupInflationFactor = 1.03;      // 1.03
    newConstraintActivity=5;  // 8
  }

  bool restartCondition(int decisionLevel) { 
    if ( numOfConflictsSinceLastRestart >= innerRestartBound and decisionLevel > noRestartsBelowThisDL) { 
      if (innerRestartBound >= outerRestartBound) { 
	outerRestartBound = (int) (outerRestartBound * outerRestartBoundInflationFactor); 
	innerRestartBound = 250; 
      } else { 
	innerRestartBound = (int) (innerRestartBound * innerRestartBoundInflationFactor); 
      } 
      numOfConflictsSinceLastRestart = 0;
      return(true); 
    } 
    return(false); 
  }

  bool cleanupCondition( int numNewConstraints ) {
    if (numNewConstraints >= numOfNewConstraintsForNextCleanup) {
      numOfNewConstraintsForNextCleanup = 
	(int) (numOfNewConstraintsForNextCleanup * constraintsForCleanupInflationFactor);
      return true;
    }
    return false;
  }
};


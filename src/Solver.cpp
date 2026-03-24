#include <fstream>
#include <stack>
#include <unordered_set>
#include <unistd.h>

#include "Solver.h"
#include "Functions.h"


using namespace std;

// *********************************
// ********* PUBLIC ****************
// *********************************

Solver::Solver(int nVars, clock_t beginTime) : 
  numVars(nVars), 
  conflict(false),
  
  positiveGenWatchList(nVars+1),
  negativeGenWatchList(nVars+1),

  positiveWatchLists(nVars+1),
  negativeWatchLists(nVars+1),

  positiveBinClauses(nVars+1),
  negativeBinClauses(nVars+1),
  
  positiveOccurLists(nVars+1),
  negativeOccurLists(nVars+1),
  
  stats(beginTime,nVars),
  strat(stats,nVars), 
  model(nVars), 
  maxHeap(nVars),
  obj_num(-1),
  lastSolution(nVars+1,false),
  status(NO_SOLUTION_FOUND),
  timeLimit(0),
  BT0(false)
{
  bestPolarityForVarInObjective = vector<int> (nVars+1,-1);
  addedConstantToObjective = 0;
  varNames = vector<string> (nVars+1);
  stats.time.real = absolute_real_time ();
  stats.time.process = absolute_process_time ();
}

// ------------------------- DECLARE INPUT PROBLEM ----------------------------------------------

void Solver::addObjectiveFunction (bool minimize, const vector<int>& coeffs, const vector<int>& vars) {
  assert(objective.size() == 0);
  assert(addedConstantToObjective == 0);
  assert(coeffs.size() == vars.size());

  minimizing = minimize;
  

  // Add to objective, only changing sign if minimizing
  for (uint i = 0; i < coeffs.size(); ++i) {
    if (coeffs[i] == 0) continue;
    assert(vars[i] > 0);
    if (minimize) objective.push_back({-coeffs[i],vars[i]});
    else          objective.push_back({ coeffs[i],vars[i]});
  }

  // First remove negative coeffs
  for (auto& p : objective) {
    if (p.first < 0) {
      addedConstantToObjective += p.first;
      p.first = -p.first; // sign coeff
      p.second = -p.second; // sign lit
    }
  }
  
  if (abs(addedConstantToObjective) >= INT_MAX) {cout << "LARGE addedConstantToObjective = " << addedConstantToObjective << ", obj constant will be " << int(-addedConstantToObjective) << endl; exit(1);}


  WConstraint wc(objective,-addedConstantToObjective);
  wc.sortByIncreasingVariable();
  wc.removeDuplicates();
  wc.sortByDecreasingCoefficient();
  addedConstantToObjective = -wc.getConstant();  // in case it will be changed when removing duplicates

  // remove units (there might be some input unit literals) in wc and copy back to objective
  objective.clear();
  for (int i = 0; i < wc.getSize(); ++i) {
    int lit  = wc.getIthLiteral(i);
    int coef = wc.getIthCoefficient(i);
    assert(coef > 0);
    if (model.isUndefLit(lit))      objective.emplace_back(coef, lit);
    else if (model.isTrueLit(lit))  addedConstantToObjective += coef;
  }

  if (abs(addedConstantToObjective) > INT_MAX) {cout << "Initializing objective: Too LARGE new obj rhs = " << addedConstantToObjective << endl; exit(0);}

  // cout << "objective is" << endl << "Maximize: ";
  // for (auto p : objective) cout << p.first << " * v" << p.second << " ";
  // cout << "+ " << addedConstantToObjective << endl;
  
  // Compute best polarity for each variable according to objective function
  computeBestPolarityForVarInObjectiveFunction();
}
  
void Solver::addAndPropagatePBConstraint (WConstraint & c, const bool isInitial, int activity, const int LBD, bool isObj) {
  if (!isObj) c.simplify();  
  PBConstraint pc(c,isInitial,activity,LBD);  // maxCoef is the first one
  addPBConstraintCounter(pc);
  propagateInitialConstraintCounter(constraintsPB.size()-1);
  stats.numOfIntsInPbsAndClauses += pc.getNumInts();
}

void Solver::addVarName (int varNum, const string& varName) {
    varNames[varNum]=varName;
    stringVar2Num[varName] = varNum;
}


// ------------------------- SOLVE AND RETRIEVE INFO ABOUT SOLVING PROCESS ---------------------

void Solver:: set_periodic_function(int (*f) ( ) ) {
  periodic_function = f;
}

void Solver::solve (int tlimit) {  
  bool feasibilityProblem = (objective.size() == 0);
  timeLimit = tlimit;
  cout.setf(ios::fixed);
  cout.precision(2);
  // cout << "solve.....BT0 " << BT0 << endl;
  // cout << "init stats.numOfSolutionsFound = " << stats.numOfSolutionsFound << endl;

  // Might be conflicting because of (incomplete) propagation of input constraints
  if (conflict) {updateStatusConflictAtDLZero(); return; }
  propagate();
  if (conflict) {updateStatusConflictAtDLZero(); return; }

  // This is a template whose rhs will be increase to enforce better and better solutions
  WConstraint solutionImprovingCtr = WConstraint(objective,0);

  strat.reportInitialSizes(constraintsPB.size(), clauses.size(),stats.numOfBinClauses);
  //cout << "initial num units: " << model.trailSize() << endl << endl;

  // When problem is read, all constraints are stored as PB constraints (even bins and clauses)
  // The doCleanup call will classify each constraint properly
  doCleanup();
  
  while (true) {
    if (!conflict) propagate();
    
    while (conflict) {
      --nconfl_to_restart;
      strat.reportConflict(model.trailSize());
      if (model.currentDecisionLevel() == 0) {
        //cout << endl << "conflict at dl 0" << ", nDecs: " << stats.numOfDecisions << " nConfs: " << stats.numOfConflicts << endl;
        updateStatusConflictAtDLZero();
        return;
      }
      
      conflictAnalysis(); // updates variable "conflict"
      
      if (!conflict) propagate();
    }
    
    if (nconfl_to_restart <= 0) { // Using luby
      backjumpToDL(0);
      strat.reportRestart(); 
      double rest_base = luby(2, stats.numOfRestarts);
      nconfl_to_restart = (long long)rest_base * 100;
      //cout << "R" << flush;
      maxHeap.reset();
    }
    
    if (stats.numOfConflicts >= (stats.numOfCleanUps + 1) * nconfl_to_reduce) {
      // Cleanup based on conflicts, not on constraint database siza
      backjumpToDL(0);
      strat.reportCleanup();
      //cout << "C" << stats.numOfCleanUps << " " << flush;  
      doCleanup();                 assert( not conflict );
      while (stats.numOfConflicts >= stats.numOfCleanUps * nconfl_to_reduce) nconfl_to_reduce += 100;
    }
    
    if (timeout()) return;

    if (periodic_function()) return;
    
    int decVar = getNextDecisionVar();
    
    if ( decVar == 0 ) { // all vars assigned and no conflict: solution found
      if (feasibilityProblem) { cout<<endl<<endl<<"Feasibility Proved"<<endl; status = OPTIMUM_FOUND; return; }
      // Compute cost of current solution and store it
      long long int bestSoFar = addedConstantToObjective;
      for ( pair<int,int>& p : objective) bestSoFar += p.first * model.getValueLit( p.second );  
      long long int originalCost = (minimizing ? -bestSoFar : bestSoFar);
      strat.reportSolutionFound(originalCost);
      status = SOME_SOLUTION_FOUND;
      for (int i=1; i<=numVars; ++i) {assert(model.getValue(i) != -1); lastSolution[i] = model.getValue(i);  }

      // Compute rhs of solution improving constraint
      long long int rhs = (bestSoFar - addedConstantToObjective) + 1;
      assert(rhs >= 0 and abs(rhs) < INT_MAX);
      if (rhs < 0 or abs(rhs) > INT_MAX) {cout << "Too LARGE new obj rhs = " << rhs << endl; exit(0);}
      
      cout << "BestSoFar: " << originalCost << ", nSolu= " << stats.numOfSolutionsFound << " nDecs: " << stats.numOfDecisions << " nConfs: " << stats.numOfConflicts  << endl;
      
      solutionImprovingCtr.setConstant(rhs);
      
      if (constraintIsContradiction(solutionImprovingCtr)) {
	cout << "obj ctr is contradict, DONE! " << endl; backjumpToDL(0); conflict = true; updateStatusConflictAtDLZero(); return;
      }
      
      assert(useCounter.size() == constraintsPB.size());
      
      if (BT0) backjumpToDL(0);
      
      if (stats.numOfSolutionsFound > 1) { // Update constraint that forces a better solution to be found
        assert(obj_num != -1);
        PBConstraint& obj = constraintsPB[obj_num];
        assert(obj.getSize() == solutionImprovingCtr.getSize());
        sumOfWatches[obj_num] += (obj.getConstant() - rhs);
        obj.setConstant(rhs);
      }
      
      if (model.currentDecisionLevel() != 0) {
        int dlToBackjumpTo = lowestDLAtWhichConstraintPropagatesOrConflicting( solutionImprovingCtr );
        if ( dlToBackjumpTo != -1 ) {
          assert( dlToBackjumpTo < model.currentDecisionLevel() );
          backjumpToDL(dlToBackjumpTo);
        }
        assert(constraintIsConflictingOrPropagating(solutionImprovingCtr));
      }
      
      if (stats.numOfSolutionsFound == 1) { // First found solution
        objectiveFunctions.push_back(constraintsPB.size()); // New objective function, // objective is already sorted
        obj_num = (int)constraintsPB.size();
        addAndPropagatePBConstraint(solutionImprovingCtr, true,0,0, true); // if only one obj constraint, don't simplify it.
      }
      else {
        assert(stats.numOfSolutionsFound > 1 and obj_num != -1 and obj_num < (int)constraintsPB.size());
        assert(constraintsPB[obj_num].getConstant() == rhs);
	//        if (!useCounter[obj_num]) watchMoreLitsInPB(obj_num);
        checkObjectiveIsConflictingOrPropagating(obj_num);
      }
      
      continue;
    }  // END new solution found
    
    strat.reportDecision(model.currentDecisionLevel());
    takeDecisionForVar(decVar);
  }
}

Solver::StatusSolver Solver::currentStatus ( ) const {
  return status;
}

void Solver::printStats() const {
  stats.print();
}

double Solver::real_time ( ) const {
  return absolute_real_time () - stats.time.real;
}

double Solver::process_time ( ) const {
  return absolute_process_time () - stats.time.process;
}


uint64_t Solver::maximum_resident_set_size () {
  struct rusage u;
  if (getrusage (RUSAGE_SELF, &u)) return 0;
  return ((uint64_t) u.ru_maxrss) << 10;
}

uint64_t Solver::current_resident_set_size () {
  char path[40];
  sprintf (path, "/proc/%" PRId64 "/statm", (int64_t) getpid ());
  FILE * file = fopen (path, "r");
  if (!file) return 0;
  uint64_t dummy, rss;
  int scanned = fscanf (file, "%" PRIu64 " %" PRIu64 "", &dummy, &rss);
  fclose (file);
  return scanned == 2 ? rss * sysconf (_SC_PAGESIZE) : 0;
}

double Solver::absolute_real_time () const {
  struct timeval tv;
  if (gettimeofday (&tv, 0)) return 0;
  return 1e-6 * tv.tv_usec + tv.tv_sec;
}

double Solver::absolute_process_time () const {
  double res;
  struct rusage u;
  if (getrusage (RUSAGE_SELF, &u)) return 0;
  res = u.ru_utime.tv_sec + 1e-6 * u.ru_utime.tv_usec;  // user time
  res += u.ru_stime.tv_sec + 1e-6 * u.ru_stime.tv_usec; // + system time
  return res;
}


// ------------------------- THIS NEEDS TO BE REVISITED -----------------------------------------

void Solver::readStrategy (const string& fileStrategy) {
  strat.read(fileStrategy);
}

void Solver::readDecision (const string& fileDecision) {
  strat.readDecisionStrategy(fileDecision,stringVar2Num);
}
 
void  Solver::readInitialSolution(const string& fileName) {
  cout << "Reading initial solution file " << fileName << endl;
  fstream in(fileName.c_str(), fstream::in);
  if (not in) {cout << "Initial solution file " << fileName << " not recognized" << endl;exit(1);}

  string var;
  int value;
  string aux;
  initialInputSolution.resize(numVars+1);

  while (in >> var >> aux >> value >> aux) {
    int varNum = stringVar2Num[var];
    assert(varNum > 0);
    assert(varNum < int(initialInputSolution.size()));
    initialInputSolution[varNum] = value;
  }
}

void Solver::checkInitialInputSolutionNeeded() const {
  for (int v = 1; v <= numVars; ++v) {
    const vector<DecPolarity>& pols = strat.decisionPolarities[v];
    for (auto& p:pols)
      if ( p == INITIAL_SOLUTION and initialInputSolution.size() == 0) {
        cout << "ERROR: Some polarity depends on initialSolution but this has not been read" << endl;
        exit(1);
      }
  }
}

void Solver::setBT0 (bool up) {
  BT0 = up;
}

// *********************************
// ********* PRIVATE ***************
// *********************************

bool Solver::timeout() const {
  if ( timeLimit and process_time() >=  timeLimit) {
    cout << endl << endl << "Time limit exceeded." << endl;
    return true;
  }
  return false;
}

void Solver::backjumpToDL(int dl) { 
  assert( model.currentDecisionLevel()>=dl and dl>=0 );
  ++stats.numOfBackjump;
  while ( model.currentDecisionLevel() > dl) popAndUnassign();
}


int Solver::popAndUnassign() {
  int lit = model.getLitAtTop();
  int var = abs(lit);
  
  if (model.isLitPropagated(lit)) { // the sumOfWatches has already been updated when visiting the occurlist
    for (OccurListElem& e: (lit > 0? negativeOccurLists[var]:positiveOccurLists[var]))
      sumOfWatches[e.ctrId] += e.coefficient;
  }  
  model.popAndUnassign();
  maxHeap.insertElement(var);  
  return(lit);
}

void Solver::setTrueDueToDecision( int lit ) { 
  model.setTrueDueToDecision(lit); assert(not conflict); 
}

void Solver::setTrueDueToReason( int lit, const Reason& r) {
  model.setTrueDueToReason(lit,r);
}


double Solver::luby (double y, int idx) {
  // Find the finite subsequence that contains index 'i', and the
  // size of that subsequence:
  int size, seq;
  for (size = 1, seq = 0; size < idx + 1; seq++, size = 2 * size + 1) {
  }
  while (size != idx + 1) {
    size = (size - 1) >> 1;
    --seq;
    assert(size != 0);
    idx = idx % size;
  }
  return std::pow(y, seq);
}


void Solver::updateStatusConflictAtDLZero ( ) {
  assert(model.currentDecisionLevel() == 0);
  assert(conflict);
  if (stats.numOfSolutionsFound == 0) {status = INFEASIBLE; return;}
  double cost=stats.costOfBestSolution;
  if (not minimizing) cost=-cost;
  if (cost>=0) printf("\nMIP - Integer optimal solution:  Objective =  %1.10e\n",cost);
  else         printf("\nMIP - Integer optimal solution:  Objective = %1.10e\n",cost);
  status = OPTIMUM_FOUND;
}

void Solver::writeOccurLists ( ) {
  for (int v = 1;  v <= numVars; ++v) {
    cout << "Pos[" << v << "]:=";
    for (auto& e:positiveOccurLists[v]) {
      cout << "(" << e.ctrId << "," << e.coefficient << ") ";
      assert(e.ctrId < int(constraintsPB.size()));
    }
    cout << endl;
    cout << "Neg[" << v << "]:=";
    for (auto& e:negativeOccurLists[v]) {
      cout << "(" << e.ctrId << "," << e.coefficient << ") ";
      assert(e.ctrId < int(constraintsPB.size()));     
    }
    cout << endl;
  }
}

void Solver::writeWatchLists ( ) {
  for (int v = 1;  v <= numVars; ++v) {
    cout << "PosWatch[" << v << "]" << endl;
    for (auto& e : positiveWatchLists[v]) cout << e << endl;
    cout << endl;

    cout << "NegWatch[" << v << "]";
    for (auto& e : negativeWatchLists[v]) cout << e << endl;
    cout << endl;
  }
  cout << endl;
}

void Solver::writeConstraint (const PBConstraint& c) {
  for(int i = 0; i < c.getSize(); ++i) {
    int lit  = c.getIthLiteral(i);
    int coef = c.getIthCoefficient(i);
    cout << coef << "*x"<< lit << "["<< model.strValLit(lit);  // T/F/U
    cout << ",l" << (model.isUndefLit(lit)?(-1):model.getDLOfLit(lit));
    cout << "] ";
  }
  cout << "  >= " << c.getConstant() << ", isInitial " << c.isInitial() 
  << ", size " << c.getSize() << " ]" << endl << flush;
}

void Solver::printConstraintsPB() const { // for debugging only
  cout << "Pseudo-Boolean Constraints: " << endl;
  for (uint i=0; i<constraintsPB.size(); i++) 
    cout << i << ": " << constraintsPB[i] << endl;    
}

void Solver::printConstraint (const PBConstraint& c) const {
  printConstraint(WConstraint(c));
}

void Solver::printConstraint (const WConstraint& c) const {
  for (int i = 0; i < c.getSize(); ++i) {
    int lit = c.getIthLiteral(i);
    int coef = c.getIthCoefficient(i);
    cout << coef << "*" << (lit<0?"-":"") << "x" << abs(lit);
    if (model.isUndefLit(lit)) cout << "U ";
    else if (model.isTrueLit(lit)) cout << "T" << model.getDLOfLit(lit) << " ";
    else cout << "F" << model.getDLOfLit(-lit) << " ";
  }
  if (c.getSize() == 0) cout << 0;
  cout << " >= " << c.getConstant()  << endl;
}

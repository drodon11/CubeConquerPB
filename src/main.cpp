
#include "Parser.h"
#include "MaxHeap.h"
#include "Model.h"
#include "WConstraint.h"
#include "Strategy.h"
#include "Functions.h"
#include "Solver.h"

using namespace std;


char* inputReadCommandLineArg(int argc, char** argv, const char* name) 
// Retrieves a command line argument by its name
{
  int argIndex;
  char* argName;

  argIndex = 1;  // skip executable name
  while(argIndex < argc) {
    if ( argv[argIndex][0] != '-' )
      argIndex++;
    else {
      argName = &argv[argIndex][1];
      if (*argName=='-') argName++;   // allow --name
      if (strcmp(argName,name)==0) {  // found the sought arg
        if (string(name) == "help") return argName;
        argIndex++;
        if (argIndex < argc)
          return(argv[argIndex]);
        else
          return(NULL);
      }
      argIndex = argIndex + 2;
    }
  }
  return NULL;
}


// Funcio que es crida periodicament dins solver

extern "C" int terminate_cb( ) {
  // static int x = 0;
  // ++x;
  // printf("Crido a la funcio periodica\n");
  // if (x < 10) return 0;
  // else return 1;
  return 0;
}

void showUsage(char* exec) {
  cout << exec << " [-help] [-seed int] [-tlimit int] [-bt0 bool] [-d decisionsLimit] [-c conflictsLimit] [-strategy file.txt] [-decision file.txt] [-iniSol file.txt]  *.lp/opb" << endl << endl;
  
  cout << "strategy --> specify the search strategies if it exists (default empty)" << endl;
  cout << "decision --> specify the decision literals if it exists (default empty)" << endl;
  cout << "iniSol   --> specify the initial solution if it exists  (default empty)" << endl;
  cout << "d        --> (integer) the decison limit" << endl;
  cout << "c        --> (integer) the conflict limit" << endl;
  cout << "seed     --> (integer) the seed used by solver for decision making" << endl;
  cout << "tlimit   --> (double)  the time limit 't' (t >= 0, 0:unlimited(default))" << endl;
  cout << "bt0      --> (bool)    always backjump to decision level 0 when a new solution is found or not (default is true)" << endl;
}

Solver* s;
clock_t beginTime;


void printFinalMessage (Solver::StatusSolver ans) {

  if      (ans == Solver::INFEASIBLE) cout << "RESULT: Infeasible" << endl;
  else if (ans == Solver::NO_SOLUTION_FOUND) cout << "RESULT: No_solution_found" << endl;
  else if (ans == Solver::SOME_SOLUTION_FOUND) cout << "RESULT: Some_solution_found" << endl;
  else if (ans == Solver::OPTIMUM_FOUND) cout << "RESULT: Optimum_found" << endl;
  else {cout << "Unexpected answer from solver." << endl; exit(1);}

  cout << "TIME:                  " << double(clock()-beginTime)/CLOCKS_PER_SEC << " s " << endl; 
  cout << "Process time:                  " << s->process_time() << endl;  
  cout << "Real    time:                  " << s->real_time() << endl;   
}

void finalMessage ( ){
  s->printStats(); cout << endl << endl;
  Solver::StatusSolver ans = s->currentStatus();
  printFinalMessage(ans);  
}

void terminateSolver(int sig){ // can be called asynchronously
  finalMessage();
  exit(1);
}


// Currently, since size is num non-satisfied constraints we might need to assign
// a sequence of lits that do not change the size and then find one that indeed
// reduces the size
int pickBestLit (Solver& solver) {
  int vars = solver.stats.numOfVars;
  int minSize = solver.reducedFormulaSize();
  int bestLit = 0;
  int s = 0;

  // Coses a canviar:
  // * A l'article deia que calia escollir una variable v tal que tant v com -v redueixi bastant
  // * Si una de les dues polaritats dona un conflicte aleshores aquesta variables hauria de tenir puntuacio maxima
  //   perque de fet la polaritat que dona conflicte no s'ha ni de considerar (pensar com gestionem aixo)

  // * S'ha de canviar la nocio de reducedFormulaSize
  
  for (int v = 1; v <= vars; ++v) {
    if (solver.isUndefLit(v)) {
      bool ok = solver.assumeAndPropagate(v);
      if (ok) {
	s = solver.reducedFormulaSize();
	if (s < minSize) { minSize = s; bestLit = v;}
      }
      solver.backtrack(1);
      
      ok = solver.assumeAndPropagate(-v);
      if (ok) {
	s = solver.reducedFormulaSize();
	if (s < minSize) { minSize = s; bestLit = -v;}
      }
      solver.backtrack(1);      
    }
  }
  cout << "Best lit " << bestLit << " with size " << minSize << endl;
  return bestLit;
}

void cube_conquer (Solver& solver) {
  cout << "Te " << solver.stats.numOfVars << " variables" << endl;
  cout << solver.reducedFormulaSize() << " formula size" << endl;
  cout << solver.assignedVars()  << " assigned vars" << endl;

  int lit = pickBestLit(solver);
  while (lit != 0){
    bool ok = solver.assumeAndPropagate(lit);
    if (not ok) cout << "ERROR en propagar " << lit << endl;
    lit = pickBestLit(solver);
  }
  
  cout << "Final size is " << solver.reducedFormulaSize() << endl;
  cout << "Final assigned vars is " << solver.assignedVars() << endl;
}

int main (int argc, char *argv[]) {  
  char* arg;
  if (argc <= 1) {
    showUsage(argv[0]);
    exit(0);
  }

  //  srand ( seed );
  string filename = argv[argc-1];
  cout << endl << endl << "Input problem:  " <<  filename << endl << endl;

  int seed = 1;
  arg = inputReadCommandLineArg(argc,argv,"seed");
  if (arg) seed = atoi(arg);
  
  int tlimit = 0;
  arg = inputReadCommandLineArg(argc,argv,"tlimit");
  if (arg) tlimit = atoi(arg);

  srand ( seed );
  Parser parser;
  PBProblem problem;
  if (filename.size() >= 3 and filename.substr(filename.size()-3) == ".lp")
    problem = parser.readLP(filename);
  else if (filename.size() >= 4 and filename.substr(filename.size()-4) == ".opb")
    problem = parser.readOPB(filename);
  else {cout << "This version of intsat only admits (very naive) .lp or .opb input format." << endl;    exit(1);}

  assert(problem.objCoeffs.size() == problem.objVars.size());
  problem.writeObjectiveFunction();
  
  int numVars = parser.numVars();
  int numConstraints = problem.constraints.size();
  cout << "read "<< numVars << " variables in " << numConstraints << " constraints.   numVarsInOBJ " << problem.objCoeffs.size() << endl;
  beginTime=clock();

  if (debug) {  cout << "create solver: " << endl;      }

  Solver solver(numVars,beginTime);
  s = &solver;
  signal(SIGINT, terminateSolver);
  //  solver.setBT0(bt0);
  
  for (int varNum=1; varNum<=numVars; varNum++) // Adding var names to the solver (useful for debugging)
    solver.addVarName( varNum, parser.var2string(varNum) );
    
  cout << "took time " << double((clock() - beginTime)/CLOCKS_PER_SEC) << "s" << endl;
  
  if (debug) {  cout << "varnames added " << endl;      }

  for (int i = 0; i < (int)problem.constraints.size(); i++) {
    problem.constraints[i].sortByIncreasingVariable();
    problem.constraints[i].removeDuplicates();
    problem.constraints[i].sortByDecreasingCoefficient();
    solver.addAndPropagatePBConstraint(problem.constraints[i],true,0,0); 
  }
  solver.addObjectiveFunction(problem.minimizing, problem.objCoeffs, problem.objVars);

  cout << "constraints and objective added, took " << double((clock() - beginTime)/CLOCKS_PER_SEC) << "s" << endl;

  solver.set_periodic_function(terminate_cb);

  //cube_conquer(solver);
  solver.solve(tlimit);

  
  uint64_t m = solver.maximum_resident_set_size ();
  cout << endl;
  printf("maximum resident set size of process:    %12.2f    MB", m/(double)(1l<<20));

  finalMessage();
  exit(0);

  
}


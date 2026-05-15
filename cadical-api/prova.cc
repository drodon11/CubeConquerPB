#include "cadical-rel-3.0.0/src/cadical.hpp"  // header de CaDiCaL
#include <cassert>
#include <iostream>

using namespace std;


int assignedVars (CaDiCaL::Solver* solver) {
  return solver->assignedVars();
}

bool isTrueLit (CaDiCaL::Solver* solver, int lit)  {
  return solver->value(lit) == 1;
}

bool isFalseLit (CaDiCaL::Solver* solver, int lit)  {
  return solver->value(lit) == 0;
}

bool isUndefLit (CaDiCaL::Solver* solver, int lit)  {
  return solver->value(lit) == -1;
}

bool assumeAndPropagate (CaDiCaL::Solver* solver,int lit) {
  solver->search_assume_decision(lit);
  return solver->propagateX();
}

void backtrack(CaDiCaL::Solver* solver, int nLevels) {
  solver->backtrackX(nLevels);
}

int value (CaDiCaL::Solver* solver, int lit) {
  return solver->value(lit);
}

// This is the termination function
class MyTerminator : public CaDiCaL::Terminator {
public:
    // This function is called periodically by the solver
    bool terminate() override {
      cout << "Check for termination" << endl;
      return false;
    }
};


void testSolveUNSAT() {
    // Inicializar solver IPASIR
    CaDiCaL::Solver* solver = new CaDiCaL::Solver;
    MyTerminator *terminator = new MyTerminator();
    solver->connect_terminator(terminator); // Conecting the termination function

    solver->resize(6);

    solver->clause(-3,6);
    solver->clause(-3,-6); // These two imply -3

    solver->clause(3,4);
    vector<int> clause = {3,-4};
    solver->clause(clause); // These two imply 3
    
    int res = solver->solve();
    if (res == 10) cout << "SATISFIABLE" << endl;
    else if (res == 20) cout << "UNSATISFIABLE" << endl;
    else cout << "UNKNOWN" << endl;
    

    delete solver;

}

void testSolveSAT() {
    // Inicializar solver IPASIR
    CaDiCaL::Solver* solver = new CaDiCaL::Solver;
    MyTerminator *terminator = new MyTerminator();
    solver->connect_terminator(terminator);

    solver->resize(6);

    solver->clause(-3,6);
    solver->clause(3,2);
    solver->clause(-3,4);
    vector<int> clause = {-3,-4};
    solver->clause(clause);
    
    int res = solver->solve();
    if (res == 10) cout << "SATISFIABLE" << endl;
    else if (res == 20) cout << "UNSATISFIABLE" << endl;
    else cout << "UNKNOWN" << endl;
    
    for (int v = 1; v <= 6 ; ++v)
      cout << v << " --> " << value(solver,v) << endl;
    
    delete solver;

}

void testAPI() {
    CaDiCaL::Solver* solver = new CaDiCaL::Solver;

    solver->resize(6);

    solver->clause(5,6);
    solver->clause(2,3,4);
    solver->clause(3,-4);

    assumeAndPropagate(solver,-5);
    
    cout << "========" << endl;
    cout << "1 --> " << value(solver,1) << endl;
    cout << "2 --> " << value(solver,2) << endl;
    cout << "3 --> " << value(solver,3) << endl;
    cout << "4 --> " << value(solver,4) << endl;
    cout << "5 --> " << value(solver,5) << endl;
    cout << "6 --> " << value(solver,6) << endl;
    cout << "Size " << assignedVars(solver) << endl;
    
    assumeAndPropagate(solver,-2);
    bool ok = assumeAndPropagate(solver,-3);
    cout << "OK " << ok << endl;

    backtrack(solver,1);
    ok = assumeAndPropagate(solver,3);
    cout << "========" << endl;
    cout << "1 --> " << value(solver,1) << endl;
    cout << "2 --> " << value(solver,2) << endl;
    cout << "3 --> " << value(solver,3) << endl;
    cout << "4 --> " << value(solver,4) << endl;
    cout << "5 --> " << value(solver,5) << endl;
    cout << "6 --> " << value(solver,6) << endl;    
    cout << "Size " << assignedVars(solver) << endl;
    
    delete solver;
}

int main ( ) {
  //  testSolveUNSAT();
  //testSolveSAT();
  testAPI();
}

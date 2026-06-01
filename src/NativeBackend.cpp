#include "NativeBackend.h"

using namespace std;

// Functions defined in cubePB.cpp that NativeBackend needs
string get_var_name(Parser& parser, int varNum, bool sat);
void add_objective_bound_constraint(Solver& solver, PBProblem& problem, int bestCost);
extern "C" int terminate_cb(int x);
extern "C" void import_external_constraints(Solver* solver);

NativeBackend::NativeBackend(Parser& parser_, int nVars_, bool sat_)
    : parser(parser_),
      nVars(nVars_),
      sat(sat_),
      beginTime(clock()),
      solver(nVars_, beginTime) {
    solver.setBT0(true);
    solver.set_periodic_function(terminate_cb);
    solver.set_import_external_constraints_procedure(import_external_constraints);

    // Store variable names for output/debugging
    for (int varNum = 1; varNum <= nVars; ++varNum) {
        solver.addVarName(varNum, get_var_name(parser, varNum, sat));
    }
}

void NativeBackend::addConstraint(WConstraint c, bool isInitial) {
    c.sortByIncreasingVariable();
    c.removeDuplicates();
    c.sortByDecreasingCoefficient();
    solver.addAndPropagatePBConstraint(c, isInitial, 0, 0);
}

vector<WConstraint> NativeBackend::goodClauses() {
    return solver.collectGoodClauses();
}

void NativeBackend::addBaseProblem(PBProblem& problem) {
    for (int i = 0; i < (int)problem.constraints.size(); ++i) {
        addConstraint(problem.constraints[i], true);
    }

    solver.addObjectiveFunction(problem.minimizing, problem.objCoeffs, problem.objVars);
}

void NativeBackend::addLearnedConstraints(const vector<WConstraint>& constraints) {
    for (const WConstraint& c : constraints) {
        addConstraint(c, true);
    }
}

void NativeBackend::addCube(const vector<int>& cube) {
    for (int lit : cube) {
        vector<int> coeffs(1, 1);
        vector<int> lits(1, lit);
        int rhs = 1;

        WConstraint c(coeffs, lits, rhs);
        addConstraint(c, true);
    }
}

void NativeBackend::addObjective(PBProblem& problem) {
    (void)problem;
    // Objective already loaded in addBaseProblem().
}

void NativeBackend::addObjectiveBound(PBProblem& problem, int bestCost) {
    add_objective_bound_constraint(solver, problem, bestCost);
}

int NativeBackend::assignedVars() const {
    return solver.assignedVars();
}

bool NativeBackend::isUndefLit(int lit) const {
    return solver.isUndefLit(lit);
}

bool NativeBackend::assumeAndPropagate(int lit) {
    return solver.assumeAndPropagate(lit);
}

void NativeBackend::backtrack(int levels) {
    solver.backtrack(levels);
}

CubeSolveResult NativeBackend::solve(bool optimizing, int timeLimitSeconds) {
    solver.solve(timeLimitSeconds);

    CubeSolveResult res;
    res.status = solver.currentStatus();
    res.hasSolution =
        res.status == Solver::SOME_SOLUTION_FOUND ||
        res.status == Solver::OPTIMUM_FOUND;
    res.bestCost = res.hasSolution ? solver.cost_best_solution() : 0;

    (void)optimizing;
    return res;
}

int NativeBackend:: nonSatisfiedConstraints ( ) {
  return solver.reducedFormulaSize();
}

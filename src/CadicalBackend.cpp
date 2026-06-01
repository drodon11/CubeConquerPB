#include "CadicalBackend.h"

#include <mpi.h>
#include <chrono>
#include <stdexcept>
#include <vector>

using namespace std;

// Globals defined in cubePB.cpp that CadicalTerminator and CadicalBackend need
extern int global_stop_flag;
extern int split_requested_flag;
extern chrono::steady_clock::time_point worker_cube_start;
extern int current_split_time_limit;
extern vector<vector<int>> global_cnf_clauses;

// ---------------------------------------------------------------------------
// CadicalTerminator
// ---------------------------------------------------------------------------

bool CadicalTerminator::terminate() {
    MPI_Status status;
    int flag = 0;

    MPI_Iprobe(0, TAG_STOP, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        int stopmsg[2];
        MPI_Recv(stopmsg, 2, MPI_INT, 0, TAG_STOP,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        global_stop_flag = 1;
        return true;
    }

    auto now = chrono::steady_clock::now();
    double elapsed =
        chrono::duration<double>(now - worker_cube_start).count();

    if (elapsed >= current_split_time_limit) {
        split_requested_flag = 1;
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// CadicalBackend
// ---------------------------------------------------------------------------

CadicalBackend::CadicalBackend(int nVars_) : nVars(nVars_) {
    solver.resize(nVars);
    solver.connect_terminator(&terminator);
}

CadicalBackend::~CadicalBackend() {
    solver.disconnect_terminator();
}

void CadicalBackend::addClauseFromConstraint(WConstraint& c) {
    c.sortByIncreasingVariable();
    c.removeDuplicates();
    c.sortByDecreasingCoefficient();

    vector<int> clause;
    clause.reserve(c.getSize());

    for (int i = 0; i < c.getSize(); ++i) {

        clause.push_back(c.getIthLiteral(i));
    }

    solver.clause(clause);
}


void CadicalBackend::initialPropagate() {
    if (root_conflict) return;

    bool ok = solver.propagateX();
    if (!ok) {
        root_conflict = true;
    }
}

vector<WConstraint> CadicalBackend::goodClauses() {
    return {};
}

void CadicalBackend::addBaseProblem(PBProblem& /*problem*/) {
    for (const auto& clause : global_cnf_clauses)
        solver.clause(clause);
    initialPropagate();
}

void CadicalBackend::addLearnedConstraints(const vector<WConstraint>& constraints) {
    (void)constraints;
}

void CadicalBackend::addCube(const vector<int>& cube) {
    for (int lit : cube) {
        solver.clause(lit);
    }

    initialPropagate();
}

void CadicalBackend::addObjective(PBProblem& problem) {
    if (!problem.objCoeffs.empty()) {
        throw runtime_error(
            "CaDiCaL backend does not support PB objectives"
        );
    }
}

void CadicalBackend::addObjectiveBound(PBProblem& problem, int bestCost) {
    (void)problem;
    (void)bestCost;

    throw runtime_error(
        "CaDiCaL backend does not support PB objective bounds"
    );
}

int CadicalBackend::assignedVars() const {
    if (root_conflict) return nVars;
    return solver.assignedVars();
}

bool CadicalBackend::isTrueLit(int lit) const {
    return solver.value(lit) == 1;
}

bool CadicalBackend::isFalseLit(int lit) const {
    return solver.value(lit) == 0;
}

bool CadicalBackend::isUndefLit(int lit) const {
    return solver.value(lit) == -1;
}

bool CadicalBackend::assumeAndPropagate(int lit) {
    if (root_conflict) return false;

    solver.search_assume_decision(lit);
    return solver.propagateX();
}

void CadicalBackend::backtrack(int levels) {
    if (root_conflict) return;
    solver.backtrackX(levels);
}

CubeSolveResult CadicalBackend::solve(bool optimizing, int timeLimitSeconds) {
    (void)timeLimitSeconds;

    CubeSolveResult res;
    res.status = Solver::NO_SOLUTION_FOUND;
    res.hasSolution = false;
    res.bestCost = 0;

    if (optimizing) {
        throw runtime_error(
            "CaDiCaL backend only supports SAT/CNF, not optimization"
        );
    }

    if (root_conflict) {
        res.status = Solver::INFEASIBLE;
        res.hasSolution = false;
        return res;
    }

    int ans = solver.solve();

    if (ans == 10) {
        res.status = Solver::SOME_SOLUTION_FOUND;
        res.hasSolution = true;
        res.bestCost = 0;
    } else if (ans == 20) {
        res.status = Solver::INFEASIBLE;
        res.hasSolution = false;
        res.bestCost = 0;
    } else {
        res.status = Solver::NO_SOLUTION_FOUND;
        res.hasSolution = false;
        res.bestCost = 0;
    }

    return res;
}

int CadicalBackend::nonSatisfiedConstraints ( ) {
  return 0;
}

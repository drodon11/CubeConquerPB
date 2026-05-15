#include "Parser.h"
#include "Solver.h"
#include "WConstraint.h"

extern "C" {
#include "ipasirpb.h"
}

#include "cadical.hpp"
#include "testing.hpp"
#include "internal.hpp"

#include <limits>
#include <mpi.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <climits>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

using namespace std;

std::chrono::steady_clock::time_point worker_cube_start;

// MPI message tags
const int TAG_STOP   = 1;
const int TAG_WORK   = 2;
const int TAG_RESULT = 3;
const int TAG_SPLIT  = 4;

const int TAG_BEST_REQUEST = 7;
const int TAG_BEST_REPLY   = 8;

// New tags for initial distributed cube generation
const int TAG_INIT_RESULT = 5;
const int TAG_INIT_CUBE   = 6;

const int DEPTH = 5;
const int RES_SPLIT = 30;

// Global flag used by workers to stop early when another process found SAT
int global_stop_flag = 0;
int split_requested_flag = 0;

//const int base_split_time_limit = 240; // seconds
const int base_split_time_limit = 60; // seconds
int current_split_time_limit = base_split_time_limit;

// Global cutoff threshold used during cube generation
double theta = 1000.0;

// Global flag set when SAT is detected during cube generation
bool cube_sat_found = false;

// Global variables for objective function handling
vector<char> global_is_objective_var;
vector<long long> global_objective_weight;
long long global_total_objective_weight = 0;
bool global_use_objective_priority = false;
const int OBJECTIVE_PRIORITY_DEPTH = 8;
const double OBJECTIVE_PRIORITY_MAX_RATIO = 0.3;
const int OBJECTIVE_PRIORITY_MAX_ABS = 1000;

int global_best_cost;
bool global_has_best_cost = false;
PBProblem global_problem;

// Choose solver from command line.
enum class SolverBackendKind {
    Native,
    RoundingSAT,
    CaDiCaL
};

SolverBackendKind selected_backend = SolverBackendKind::Native;

struct CubeTask {
    vector<int> lits;
    int split_depth;
};

struct CubeSolveResult {
    Solver::StatusSolver status;
    bool hasSolution;
    int bestCost;
};

vector<int> cube_of_worker;
vector<int> cube_depth_of_worker;
vector<clock_t> time_of_worker;
int total_number_cubes = 0;

int compute_split_time_limit(int split_depth) {
    long long limit = base_split_time_limit;
    for (int i = 0; i < split_depth; ++i) {
        if (limit > INT_MAX / 2) {
            limit = INT_MAX;
            break;
        }
        limit *= 2;
    }
    return (int)limit;
}

string get_var_name(Parser& parser, int varNum, bool sat) {
    if (sat) return "x" + to_string(varNum);
    return parser.var2string(varNum);
}

int read_cnf_file(const string& filename, vector<vector<int>>& clauses) {
    ifstream in(filename);
    if (!in) {
        cerr << "Could not open CNF file: " << filename << endl;
        return -1;
    }

    string line;
    int nVars = 0;
    int nClauses = 0;
    bool header_seen = false;

    while (getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == 'c') continue;

        if (line[0] == 'p') {
            string p, format;
            stringstream ss(line);
            ss >> p >> format >> nVars >> nClauses;

            if (format != "cnf") {
                cerr << "Invalid CNF format. Expected p cnf." << endl;
                return -1;
            }

            header_seen = true;
            break;
        }
    }

    if (!header_seen || nVars <= 0) {
        cerr << "Invalid CNF file: missing p cnf header." << endl;
        return -1;
    }

    int lit;
    vector<int> clause;

    while (in >> lit) {
        if (lit == 0) {
            clauses.push_back(clause);
            clause.clear();
        } else {
            clause.push_back(lit);
        }
    }

    if (nClauses > 0 && (int)clauses.size() != nClauses) {
        cerr << "Warning: CNF header says " << nClauses
             << " clauses, but parsed " << clauses.size() << endl;
    }

    return nVars;
}

PBProblem cnf_to_pb_problem(const vector<vector<int>>& clauses) {
    PBProblem problem;

    // SAT
    problem.minimizing = true;

    for (const auto& clause : clauses) {
        vector<int> coeffs;
        vector<int> lits;

        for (int lit : clause) {
            coeffs.push_back(1);
            lits.push_back(lit);
        }

        WConstraint c(coeffs, lits, 1);
        c.sortByIncreasingVariable();
        c.removeDuplicates();
        c.sortByDecreasingCoefficient();
        problem.constraints.push_back(c);
    }

    return problem;
}

// Converts either:
//   sum coeff_i * x_i >= rhs
// or:
//   sum coeff_i * x_i <= rhs
// into normalized GEQ form with positive coefficients.
bool build_linear_geq_parts(const vector<int>& coeffs,
                            const vector<int>& varNums,
                            int rhs,
                            bool isGeq,
                            vector<int>& coefficients,
                            vector<int>& literals,
                            int& outRhs) {
    coefficients.clear();
    literals.clear();

    if (!isGeq) rhs = -rhs;

    for (int i = 0; i < (int)coeffs.size(); ++i) {
        int coef = coeffs[i];
        if (!isGeq) coef = -coef;

        int finalCoef = abs(coef);
        int lit = varNums[i];

        if (coef < 0) {
            rhs += finalCoef;
            lit = -lit;
        }

        if (coef != 0) {
            coefficients.push_back(finalCoef);
            literals.push_back(lit);
        }
    }

    // Trivially true constraint
    if (rhs < 1) return false;

    outRhs = rhs;
    return true;
}

void add_linear_constraint_to_solver(Solver& solver,
                                     const vector<int>& coeffs,
                                     const vector<int>& varNums,
                                     int rhs,
                                     bool isGeq) {
    vector<int> coefficients, literals;
    int outRhs = 0;

    if (!build_linear_geq_parts(coeffs, varNums, rhs, isGeq,
                                coefficients, literals, outRhs)) {
        return;
    }

    WConstraint c(coefficients, literals, outRhs);
    c.sortByIncreasingVariable();
    c.removeDuplicates();
    c.sortByDecreasingCoefficient();
    solver.addAndPropagatePBConstraint(c, true, 0, 0);
}

// Add incumbent bound to solver:
// if minimizing and bestCost = B, force obj <= B-1
// if maximizing and bestCost = B, force obj >= B+1
void add_objective_bound_constraint(Solver& solver,
                                    PBProblem& problem,
                                    int bestCost) {
    if (problem.objCoeffs.empty()) return;

    if (problem.minimizing) {
        add_linear_constraint_to_solver(
            solver,
            problem.objCoeffs,
            problem.objVars,
            bestCost - 1,
            false   // <=
        );
    } else {
        add_linear_constraint_to_solver(
            solver,
            problem.objCoeffs,
            problem.objVars,
            bestCost + 1,
            true    // >=
        );
    }
}

extern "C" void import_external_constraints(Solver* solver) {
    static int last_imported_best = INT_MAX;

    int dummy = 0;
    int best_from_master = INT_MAX;

    // Ask the master for the current global incumbent
    MPI_Send(&dummy, 1, MPI_INT, 0, TAG_BEST_REQUEST, MPI_COMM_WORLD);
    MPI_Recv(&best_from_master, 1, MPI_INT, 0, TAG_BEST_REPLY, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // No incumbent available
    if (best_from_master == INT_MAX) return;

    if (best_from_master >= last_imported_best) return;

    add_objective_bound_constraint(*solver, global_problem, best_from_master);

    last_imported_best = best_from_master;
}

// Check whether a STOP message has been sent by the master.
// If so, receive it and update the global stop flag.
extern "C" int terminate_cb(int x) {
    MPI_Status status;
    int flag = 0;
    static int best = INT_MAX;
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (x < best) {
        best = x;
        int msg[3];
        msg[0] = 26;
        msg[1] = rank;
        msg[2] = x;
        MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
    }

    // STOP from master
    MPI_Iprobe(0, TAG_STOP, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        int stopmsg[2];
        MPI_Recv(stopmsg, 2, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        global_stop_flag = 1;
        return 1;
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - worker_cube_start).count();

    if (elapsed >= current_split_time_limit) {
        split_requested_flag = 1;
        return 1;
    }

    return 0;
}

// Callback for RoundingSAT.
// It only checks STOP and split timeout.
// It does not interpret x as a cost.
extern "C" int terminate_decision_cb(int x) {
    (void)x;

    MPI_Status status;
    int flag = 0;

    MPI_Iprobe(0, TAG_STOP, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        int stopmsg[2];
        MPI_Recv(stopmsg, 2, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        global_stop_flag = 1;
        return 1;
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - worker_cube_start).count();

    if (elapsed >= current_split_time_limit) {
        split_requested_flag = 1;
        return 1;
    }

    return 0;
}

// Solver backend interface

class ISolverBackend {
public:
    virtual ~ISolverBackend() = default;

    virtual void addBaseProblem(PBProblem& problem) = 0;
    virtual void addCube(const vector<int>& cube) = 0;
    virtual void addObjective(PBProblem& problem) = 0;
    virtual void addObjectiveBound(PBProblem& problem, int bestCost) = 0;

    virtual int assignedVars() const = 0;
    virtual bool isUndefLit(int lit) const = 0;
    virtual bool assumeAndPropagate(int lit) = 0;
    virtual void backtrack(int levels) = 0;

    virtual CubeSolveResult solve(bool optimizing, int timeLimitSeconds) = 0;
};

// Native solver backend

class NativeBackend : public ISolverBackend {
private:
    Parser& parser;
    int nVars;
    bool sat;
    clock_t beginTime;
    Solver solver;

public:
    NativeBackend(Parser& parser_, int nVars_, bool sat_)
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

    void addBaseProblem(PBProblem& problem) override {
        for (int i = 0; i < (int)problem.constraints.size(); ++i) {
            problem.constraints[i].sortByIncreasingVariable();
            problem.constraints[i].removeDuplicates();
            problem.constraints[i].sortByDecreasingCoefficient();
            solver.addAndPropagatePBConstraint(problem.constraints[i], true, 0, 0);
        }

        solver.addObjectiveFunction(problem.minimizing, problem.objCoeffs, problem.objVars);
    }

    void addCube(const vector<int>& cube) override {
        for (int lit : cube) {
            vector<int> coeffs(1, 1);
            vector<int> lits(1, lit);
            int rhs = 1;

            WConstraint c(coeffs, lits, rhs);
            c.sortByIncreasingVariable();
            c.removeDuplicates();
            c.sortByDecreasingCoefficient();

            solver.addAndPropagatePBConstraint(c, true, 0, 0);
        }
    }

    void addObjective(PBProblem& problem) override {
        (void)problem;
        // Objective already loaded in addBaseProblem().
    }

    void addObjectiveBound(PBProblem& problem, int bestCost) override {
        add_objective_bound_constraint(solver, problem, bestCost);
    }

    int assignedVars() const override {
        return solver.assignedVars();
    }

    bool isUndefLit(int lit) const override {
        return solver.isUndefLit(lit);
    }

    bool assumeAndPropagate(int lit) override {
        return solver.assumeAndPropagate(lit);
    }

    void backtrack(int levels) override {
        solver.backtrack(levels);
    }

    CubeSolveResult solve(bool optimizing, int timeLimitSeconds) override {
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
};


// RoundingSAT backend

class RoundingSatBackend : public ISolverBackend {
private:
    void* solver;

    // Keep objective vectors alive while setting objective.
    vector<int64_t> objective_coeffs;
    vector<int64_t> objective_lits;

public:
    RoundingSatBackend() {
        solver = ipasirpb_init();
    }

    ~RoundingSatBackend() override {
        ipasirpb_release(solver);
    }

    void addWConstraint(WConstraint& c) {
        c.sortByIncreasingVariable();
        c.removeDuplicates();
        c.sortByDecreasingCoefficient();

        vector<int64_t> coeffs;
        vector<int64_t> lits;

        coeffs.reserve(c.getSize());
        lits.reserve(c.getSize());

        for (int i = 0; i < c.getSize(); ++i) {
            coeffs.push_back((int64_t)c.getIthCoefficient(i));
            lits.push_back((int64_t)c.getIthLiteral(i));
        }

        ipasirpb_terms64 t;
        t.coeffs = coeffs.data();
        t.lits = lits.data();
        t.len = (int64_t)coeffs.size();
        t.rhs = (uint64_t)c.getConstant();
        t.rel = IPASIRPB_GEQ;

        ipasirpb_add64(solver, t);
    }

    void addBaseProblem(PBProblem& problem) override {
        for (int i = 0; i < (int)problem.constraints.size(); ++i) {
            addWConstraint(problem.constraints[i]);
        }
    }

    void addCube(const vector<int>& cube) override {
        for (int lit : cube) {
            int64_t coeff = 1;
            int64_t l = lit;

            ipasirpb_terms64 t;
            t.coeffs = &coeff;
            t.lits = &l;
            t.len = 1;
            t.rhs = 1;
            t.rel = IPASIRPB_GEQ;

            ipasirpb_add64(solver, t);
        }
    }

    void addObjective(PBProblem& problem) override {
        if (problem.objCoeffs.empty()) return;

        objective_coeffs.clear();
        objective_lits.clear();

        objective_coeffs.reserve(problem.objCoeffs.size());
        objective_lits.reserve(problem.objVars.size());

        for (int i = 0; i < (int)problem.objCoeffs.size(); ++i) {
            objective_coeffs.push_back((int64_t)problem.objCoeffs[i]);
            objective_lits.push_back((int64_t)problem.objVars[i]);
        }

        ipasirpb_terms64 t;
        t.coeffs = objective_coeffs.data();
        t.lits = objective_lits.data();
        t.len = (int64_t)objective_coeffs.size();
        t.rhs = 0;
        t.rel = IPASIRPB_MIN;

        ipasirpb_set_obj64(solver, t);
    }

    void addLinearConstraint(const vector<int>& coeffs,
                             const vector<int>& varNums,
                             int rhs,
                             bool isGeq) {
        vector<int> coefficients, literals;
        int outRhs = 0;

        if (!build_linear_geq_parts(coeffs, varNums, rhs, isGeq,
                                    coefficients, literals, outRhs)) {
            return;
        }

        WConstraint c(coefficients, literals, outRhs);
        addWConstraint(c);
    }

    void addObjectiveBound(PBProblem& problem, int bestCost) override {
        if (problem.objCoeffs.empty()) return;

        if (problem.minimizing) {
            addLinearConstraint(
                problem.objCoeffs,
                problem.objVars,
                bestCost - 1,
                false   // obj <= bestCost - 1
            );
        } else {
            addLinearConstraint(
                problem.objCoeffs,
                problem.objVars,
                bestCost + 1,
                true    // obj >= bestCost + 1
            );
        }
    }

    int assignedVars() const override {
        int n = 0;
        ipasirpb_assignedVars(solver, &n);
        return n;
    }

    bool isTrueLit(int lit) const {
        bool v = false;
        ipasirpb_is_true_lit(solver, lit, &v);
        return v;
    }

    bool isFalseLit(int lit) const {
        bool v = false;
        ipasirpb_is_false_lit(solver, lit, &v);
        return v;
    }

    bool isUndefLit(int lit) const override {
        return !isTrueLit(lit) && !isFalseLit(lit);
    }

    bool assumeAndPropagate(int lit) override {
        bool conflict = false;
        ipasirpb_assume_and_propagate(solver, lit, &conflict);
        return !conflict;
    }

    void backtrack(int levels) override {
        ipasirpb_backjump(solver, levels);
    }

    ipasirpb_return solve_raw(int seconds) {
        ipasirpb_set_periodic_function(solver, terminate_decision_cb);
        return ipasirpb_solve(solver, nullptr, 0, seconds);
    }

    int64_t primalBound() const {
        int64_t value = 0;
        ipasirpb_get_primal_bound64(solver, &value);
        return value;
    }

    int64_t dualBound() const {
        int64_t value = 0;
        ipasirpb_get_dual_bound64(solver, &value);
        return value;
    }

    CubeSolveResult solve(bool optimizing, int timeLimitSeconds) override {
        CubeSolveResult res;
        res.status = Solver::NO_SOLUTION_FOUND;
        res.hasSolution = false;
        res.bestCost = 0;

        ipasirpb_return ans = solve_raw(timeLimitSeconds);

        if (optimizing) {
            if (ans == IPASIRPB_OPT) {
                res.status = Solver::OPTIMUM_FOUND;
                res.hasSolution = true;
                res.bestCost = (int)primalBound();
            } else if (ans == IPASIRPB_SAT) {
                res.status = Solver::SOME_SOLUTION_FOUND;
                res.hasSolution = true;
                res.bestCost = (int)primalBound();
            } else if (ans == IPASIRPB_UNSAT) {
                res.status = Solver::INFEASIBLE;
                res.hasSolution = false;
            } else {
                res.status = Solver::NO_SOLUTION_FOUND;
                res.hasSolution = false;
            }
        } else {
            if (ans == IPASIRPB_SAT) {
                res.status = Solver::SOME_SOLUTION_FOUND;
                res.hasSolution = true;
            } else if (ans == IPASIRPB_UNSAT) {
                res.status = Solver::INFEASIBLE;
                res.hasSolution = false;
            } else {
                res.status = Solver::NO_SOLUTION_FOUND;
                res.hasSolution = false;
            }
        }

        return res;
    }
};


// CaDiCaL backend

class CadicalTerminator : public CaDiCaL::Terminator {
public:
    bool terminate() override {
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

        auto now = std::chrono::steady_clock::now();
        double elapsed =
            std::chrono::duration<double>(now - worker_cube_start).count();

        if (elapsed >= current_split_time_limit) {
            split_requested_flag = 1;
            return true;
        }

        return false;
    }
};

class CadicalBackend : public ISolverBackend {
private:
    mutable CaDiCaL::Solver solver;
    CadicalTerminator terminator;
    int nVars;
    bool root_conflict = false;
    bool conflict_pending = false;

    void clearPendingConflict() {
        CaDiCaL::Testing testing(solver);
        testing.internal()->conflict = nullptr;
        conflict_pending = false;
    }

    void addClauseFromConstraint(WConstraint& c) {
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

    void initialPropagate() {
        if (root_conflict) return;

        bool ok = solver.propagateX();
        if (!ok) {
            root_conflict = true;
        }
    }

public:
    explicit CadicalBackend(int nVars_) : nVars(nVars_) {
        solver.resize(nVars);
        solver.connect_terminator(&terminator);
    }

    ~CadicalBackend() override {
        solver.disconnect_terminator();
    }

    void addBaseProblem(PBProblem& problem) override {
        for (int i = 0; i < (int)problem.constraints.size(); ++i) {
            addClauseFromConstraint(problem.constraints[i]);
        }

        initialPropagate();
    }

    void addCube(const vector<int>& cube) override {
        for (int lit : cube) {
            solver.clause(lit);
        }

        initialPropagate();
    }

    void addObjective(PBProblem& problem) override {
        if (!problem.objCoeffs.empty()) {
            throw std::runtime_error(
                "CaDiCaL backend does not support PB objectives"
            );
        }
    }

    void addObjectiveBound(PBProblem& problem, int bestCost) override {
        (void)problem;
        (void)bestCost;

        throw std::runtime_error(
            "CaDiCaL backend does not support PB objective bounds"
        );
    }

    int assignedVars() const override {
        if (root_conflict) return nVars;
        return solver.assignedVars();
    }

    bool isTrueLit(int lit) const {
        return solver.value(lit) == 1;
    }

    bool isFalseLit(int lit) const {
        return solver.value(lit) == 0;
    }

    bool isUndefLit(int lit) const override {
        return solver.value(lit) == -1;
    }

    bool assumeAndPropagate(int lit) override {
        if (root_conflict) return false;

        solver.search_assume_decision(lit);
        bool ok = solver.propagateX();

        if (!ok) {
            conflict_pending = true;
        }

        return ok;
    }

    void backtrack(int levels) override {
        if (root_conflict) return;
	

        solver.backtrackX(levels);

        if (conflict_pending) clearPendingConflict();
    }

    CubeSolveResult solve(bool optimizing, int timeLimitSeconds) override {
        (void)timeLimitSeconds;

        CubeSolveResult res;
        res.status = Solver::NO_SOLUTION_FOUND;
        res.hasSolution = false;
        res.bestCost = 0;

        if (optimizing) {
            throw std::runtime_error(
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
};

unique_ptr<ISolverBackend> create_solver(Parser& parser, int nVars, bool sat) {
    if (selected_backend == SolverBackendKind::RoundingSAT) {
        return make_unique<RoundingSatBackend>();
    }

    if (selected_backend == SolverBackendKind::CaDiCaL) {
        if (!sat) {
            throw std::runtime_error(
                "--solver=cadical only supports .cnf input"
            );
        }

        return make_unique<CadicalBackend>(nVars);
    }

    return make_unique<NativeBackend>(parser, nVars, sat);
}

// Recursive cube generation using a lookahead evalvar-style heuristic
template <typename SolverT>
void generate_cubes_rec(SolverT& solver,
                        const vector<int>& vars,
                        int nVars,
                        vector<int>& current,
                        vector<vector<int>>& cubes,
                        bool distributed_split,
                        int worker_id,
                        int num_workers,
                        bool optimizing) {
    // Stop immediately if SAT was already found in another branch
    if (cube_sat_found && !optimizing) return;

    // Increase cutoff threshold slightly at every recursive call
    theta *= 1.05;

    // Number of currently assigned variables and number of decision literals
    int assigned = solver.assignedVars();
    int numDec   = current.size();

    // If the search goes too deep, decrease the threshold
    if (numDec > 20) {
        theta *= 0.7;
    }

    // Cutoff heuristic:
    // stop branching and store the current decision path as a cube
    if ((long long)numDec * (long long)assigned > theta * nVars) {
        cubes.push_back(current);
        return;
    }

    // Best branching variable according to evalvar heuristic
    int bestVar = 0;
    long long bestScore = -1;
    long long bestTie   = -1;
    int bestEvalPos = 0;
    int bestEvalNeg = 0;

    // Number of assigned variables before temporary lookaheads
    int baseAssigned = assigned;
    // Counter for failed literals
    int failedLits = 0;
    bool failedLitFound = true;

    while (failedLitFound) {
        baseAssigned = solver.assignedVars();
        // Evaluate every currently undefined variable
        failedLitFound = false;
        for (int v : vars) {
            if (!solver.isUndefLit(v)) continue;

            // Try v = true
            bool okPos = solver.assumeAndPropagate(v);
            int eval_pos;
            if (!okPos) {
                // Conflict during lookahead: assign a large score
                eval_pos = nVars + 10;
            } else {
                // Number of newly assigned variables caused by propagation
                eval_pos = solver.assignedVars() - baseAssigned;
            }
            solver.backtrack(1);

            // Try v = false
            bool okNeg = solver.assumeAndPropagate(-v);
            int eval_neg;
            if (!okNeg) {
                // Conflict during lookahead: assign a large score
                eval_neg = nVars + 10;
            } else {
                // Number of newly assigned variables caused by propagation
                eval_neg = solver.assignedVars() - baseAssigned;
            }
            solver.backtrack(1);

            // If both branches are impossible, the current node is refuted
            if (!okPos && !okNeg) {
                solver.backtrack(failedLits);
                return;
            } else if (!okPos || !okNeg) {
                // If only one branch is possible, assign the variable accordingly and continue
                int lit = okPos ? v : -v;
                solver.assumeAndPropagate(lit);
                bestVar = 0;
                bestScore = -1;
                bestTie = -1;
                bestEvalPos = 0;
                bestEvalNeg = 0;
                failedLitFound = true;
                failedLits++;
                break;
            }

            // Standard evalvar ranking:
            // maximize eval_pos * eval_neg, break ties with eval_pos + eval_neg
            long long score = 1LL * eval_pos * eval_neg;
            if (global_use_objective_priority &&
                numDec < OBJECTIVE_PRIORITY_DEPTH &&
                v >= 0 && v < (int)global_is_objective_var.size() &&
                global_is_objective_var[v] &&
                global_total_objective_weight > 0) {
                double pct = (double)global_objective_weight[v] / (double)global_total_objective_weight;
                long long bonus = (long long)((eval_pos + eval_neg) * 20.0 * pct);
                score += bonus;
            }
            long long tie = 1LL * eval_pos + eval_neg;

            if (score > bestScore || (score == bestScore && tie > bestTie)) {
                bestScore   = score;
                bestTie     = tie;
                bestVar     = v;
                bestEvalPos = eval_pos;
                bestEvalNeg = eval_neg;
            }
        }
    }

    // PB-SAT case:
    // no undefined variables remain and no conflict was found => SAT globally
    if (bestVar == 0) {
        if (!optimizing) cube_sat_found = true;
        else cubes.push_back(current);
        if (failedLits > 0) solver.backtrack(failedLits);
        return;
    }

    // Direction heuristic:
    // explore first the branch with smaller eval (less constrained branch)
    int firstLit, secondLit;
    if (bestEvalPos < bestEvalNeg) {
        firstLit  =  bestVar;
        secondLit = -bestVar;
    } else {
        firstLit  = -bestVar;
        secondLit =  bestVar;
    }

    // Initial distributed split of the search tree among workers.
    // While more than one worker is assigned to the current subtree,
    // each worker follows only one branch according to worker_id.
    if (distributed_split && num_workers > 1) {
        int left_workers  = num_workers / 2;
        int right_workers = num_workers - left_workers;

        if (worker_id < left_workers) {
            // This worker belongs to the left group
            current.push_back(firstLit);
            bool ok = solver.assumeAndPropagate(firstLit);
            if (ok) {
                generate_cubes_rec(solver, vars, nVars, current, cubes,
                                   true, worker_id, left_workers, optimizing);
            } else {
                // If the child node immediately conflicts, decrease the threshold
                theta *= 0.7;
            }
            solver.backtrack(1);
            current.pop_back();
        } else {
            // This worker belongs to the right group
            current.push_back(secondLit);
            bool ok = solver.assumeAndPropagate(secondLit);
            if (ok) {
                generate_cubes_rec(solver, vars, nVars, current, cubes,
                                   true, worker_id - left_workers, right_workers, optimizing);
            } else {
                // If the child node immediately conflicts, decrease the threshold
                theta *= 0.7;
            }
            solver.backtrack(1);
            current.pop_back();
        }

        // If we found conflict nodes we have to backtrack all the way up to the last decision level
        if (failedLits > 0) {
            solver.backtrack(failedLits);
        }
        return;
    }

    // Explore first branch
    current.push_back(firstLit);
    bool ok = solver.assumeAndPropagate(firstLit);
    if (ok) {
        generate_cubes_rec(solver, vars, nVars, current, cubes,
                           distributed_split, worker_id, num_workers, optimizing);
    } else {
        // If the child node immediately conflicts, decrease the threshold
        theta *= 0.7;
    }
    solver.backtrack(1);
    current.pop_back();

    // Stop before exploring the second branch if SAT was already found
    if (cube_sat_found && !optimizing) {
        if (failedLits > 0) solver.backtrack(failedLits);
        return;
    }

    // Explore second branch
    current.push_back(secondLit);
    ok = solver.assumeAndPropagate(secondLit);
    if (ok) {
        generate_cubes_rec(solver, vars, nVars, current, cubes,
                           distributed_split, worker_id, num_workers, optimizing);
    } else {
        // If the child node immediately conflicts, decrease the threshold
        theta *= 0.7;
    }
    solver.backtrack(1);
    current.pop_back();

    // If we found conflict nodes we have to backtrack all the way up to the last decision level
    if (failedLits > 0) {
        solver.backtrack(failedLits);
    }
}

// Wrapper that initializes cube generation from the base solver state
template <typename SolverT>
vector<vector<int>> generate_cubes(SolverT& solver,
                                   const vector<int>& vars,
                                   int nVars,
                                   bool distributed_split,
                                   int worker_id,
                                   int num_workers,
                                   bool optimizing) {
    vector<vector<int>> cubes;
    vector<int> current;

    // Reset global state before generating cubes
    theta = 1000.0;
    cube_sat_found = false;

    generate_cubes_rec(solver, vars, nVars, current, cubes,
                       distributed_split, worker_id, num_workers, optimizing);
    return cubes;
}

// Keep the old wrapper too, so local sequential generation still works
template <typename SolverT>
vector<vector<int>> generate_cubes(SolverT& solver,
                                   const vector<int>& vars,
                                   int nVars) {
    return generate_cubes(solver, vars, nVars, false, 0, 1, false);
}

// Solve one cube with a fresh backend instance.
// This replaces solve_cube() and solve_cube_roundingsat().
CubeSolveResult solve_cube(PBProblem& problem,
                           Parser& parser,
                           const int& nVars,
                           const vector<int>& cube,
                           const int& bestCost,
                           const bool& hasBestCost,
                           bool optimizing,
                           bool sat) {
    auto solver = create_solver(parser, nVars, sat);

    solver->addBaseProblem(problem);
    solver->addCube(cube);

    if (optimizing) {
        solver->addObjective(problem);

        if (hasBestCost) {
            solver->addObjectiveBound(problem, bestCost);
        }
    }

    int tlimit = 0;
    return solver->solve(optimizing, tlimit);
}

vector<vector<int>> split_cube(PBProblem& problem,
                               Parser& parser,
                               int nVars,
                               const vector<int>& cube,
                               bool optimizing,
                               int bestCost,
                               bool hasBestCost,
                               bool sat) {
    vector<int> allVars(nVars);
    for (int i = 0; i < nVars; ++i) allVars[i] = i + 1;

    auto solver = create_solver(parser, nVars, sat);

    solver->addBaseProblem(problem);
    solver->addCube(cube);

    if (optimizing) {
        solver->addObjective(problem);

        if (hasBestCost) {
            solver->addObjectiveBound(problem, bestCost);
        }
    }

    vector<vector<int>> local = generate_cubes(*solver, allVars, nVars,
                                               false, 0, 1, optimizing);

    vector<vector<int>> full;
    full.reserve(local.size());

    for (auto& sub : local) {
        vector<int> c;
        c.reserve(cube.size() + sub.size());
        c.insert(c.end(), cube.begin(), cube.end());
        c.insert(c.end(), sub.begin(), sub.end());
        full.push_back(std::move(c));
    }

    return full;
}

void startTimer(std::atomic<bool>& run, int interval) {
    std::thread t([&run, interval]() {
        while (run) {
            cout << string(50, '=') << endl;
            cout << "TOTAL NUMBER CUBES TO BE PROCESSED: " << total_number_cubes << endl;
            for (uint w = 1; w < cube_of_worker.size(); ++w) {
                cout << "Worker " << w << ":";
                if (cube_of_worker[w] == -1) {
                    cout << " idle" << endl;
                } else {
                    cout << " cube " << setw(4) << cube_of_worker[w]
                         << " [depth " << cube_depth_of_worker[w] << "]"
                         << "\t(" << double(clock() - time_of_worker[w]) / CLOCKS_PER_SEC
                         << " s.)" << endl;
                }
            }
            if (global_has_best_cost) cout << "Global best cost: " << global_best_cost << endl;
            else cout << "Global best cost: Unknown" << endl;
            cout << string(30, '=') << endl;
            std::this_thread::sleep_for(std::chrono::seconds(interval));
        }
    });
    t.detach(); // Let it run independently
}

bool bestSolution(int bestCost, int worker, PBProblem& problem) {
    bool better = problem.minimizing ? (bestCost < global_best_cost) : (bestCost > global_best_cost);
    if (better) {
        global_best_cost = bestCost;
        cout << "*****New global incumbent ***** " << global_best_cost
             << " found by worker " << worker << endl;
    }
    return better;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Basic argument check
    if (argc < 2) {
        if (rank == 0) {
            cerr << "Usage: mpirun -np N ./cubePB formula.opb|formula.lp|formula.cnf [--solver=native|--solver=roundingsat|--solver=cadical]" << endl;
        }
        MPI_Finalize();
        return 1;
    }

    // Need at least one master and one worker
    if (size < 2) {
        if (rank == 0) {
            cerr << "At least 2 processes are required" << endl;
        }
        MPI_Finalize();
        return 1;
    }

    string filename = argv[1];

    // Parse optional solver argument.
    for (int i = 2; i < argc; ++i) {
        string arg = argv[i];

        if (arg == "--solver=roundingsat") {
            selected_backend = SolverBackendKind::RoundingSAT;
        } else if (arg == "--solver=native") {
            selected_backend = SolverBackendKind::Native;
        } else if (arg == "--solver=cadical") {
            selected_backend = SolverBackendKind::CaDiCaL;
        } else {
            if (rank == 0) {
                cerr << "Unknown option: " << arg << endl;
                cerr << "Usage: mpirun -np N ./cubePB formula.opb|formula.lp|formula.cnf [--solver=native|--solver=roundingsat|--solver=cadical]" << endl;
            }
            MPI_Finalize();
            return 1;
        }
    }

    Parser parser;
    PBProblem problem;
    bool optimizing = true;
    bool sat = false;
    int cnf_nVars = 0;

    // Each rank reads the same input file locally
    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".opb") {
        problem = parser.readOPB(filename);
    } else if (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".lp") {
        problem = parser.readLP(filename);
        optimizing = true;
    } else if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".cnf") {
        vector<vector<int>> clauses;
        cnf_nVars = read_cnf_file(filename, clauses);
        if (cnf_nVars <= 0) {
            if (rank == 0) cerr << "Invalid CNF file" << endl;
            MPI_Finalize();
            return 1;
        }
	  
        problem = cnf_to_pb_problem(clauses);
        sat = true;
        optimizing = false;
    } else {
        if (rank == 0) cerr << "Unsupported format. Use .opb, .lp or .cnf" << endl;
        MPI_Finalize();
        return 1;
    }

    global_problem = problem;
    if (!sat) {
        optimizing = !problem.objCoeffs.empty();
    }

    if (selected_backend == SolverBackendKind::CaDiCaL && !sat) {
        if (rank == 0) {
            cerr << "--solver=cadical only supports .cnf files." << endl;
        }
        MPI_Finalize();
        return 1;
    }

    if (selected_backend == SolverBackendKind::CaDiCaL && optimizing) {
        if (rank == 0) {
            cerr << "--solver=cadical does not support optimization/objectives." << endl;
        }
        MPI_Finalize();
        return 1;
    }

    // optimization integration supports minimization only.
    if (optimizing && !problem.minimizing) {
        if (rank == 0) {
            cerr << "optimization integration currently supports minimization only." << endl;
        }
        MPI_Finalize();
        return 1;
    }

    // Number of variables in the problem
    int nVars = sat ? cnf_nVars : parser.numVars();
    bool sat_found_during_generation = false;
    global_best_cost = problem.minimizing ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();
    bool feasible_seen = false;

    if (optimizing) {
        global_is_objective_var.assign(nVars + 1, 0);
        global_objective_weight.assign(nVars + 1, 0);
        global_total_objective_weight = 0;
        int numUniqueObjVars = 0;

        for (int i = 0; i < (int)problem.objVars.size(); ++i) {
            int v = problem.objVars[i];
            long long c = abs(problem.objCoeffs[i]);

            if (v >= 1 && v <= nVars) {
                if (!global_is_objective_var[v]) {
                    global_is_objective_var[v] = 1;
                    numUniqueObjVars++;
                }
                global_objective_weight[v] += c;
                global_total_objective_weight += c;
            }
        }

        double objRatio = (nVars > 0) ? (double)numUniqueObjVars / (double)nVars : 0.0;

        global_use_objective_priority =
            optimizing &&
            numUniqueObjVars <= OBJECTIVE_PRIORITY_MAX_ABS &&
            objRatio <= OBJECTIVE_PRIORITY_MAX_RATIO;
    }

    // -------------------- MASTER --------------------
    if (rank == 0) {
        vector<CubeTask> cubes;

        // Receive the initial cubes generated by the workers
        for (int p = 1; p < size; ++p) {
            int header[2];
            MPI_Recv(header, 2, MPI_INT, p, TAG_INIT_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            int status = header[0];
            int numCubes = header[1];

            if (status == 10) {
                sat_found_during_generation = true;
            }

            for (int i = 0; i < numCubes; ++i) {
                int len;
                MPI_Recv(&len, 1, MPI_INT, p, TAG_INIT_CUBE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                vector<int> cube(len);
                if (len > 0) {
                    MPI_Recv(cube.data(), len, MPI_INT, p, TAG_INIT_CUBE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                cubes.push_back({cube, 0});
            }
        }

        total_number_cubes = cubes.size();

        // If SAT was detected during cube generation, stop everything
        if (sat_found_during_generation && !optimizing) {
            cout << "Global result: SAT found during cube generation" << endl;
            int stopmsg[2] = {1, 0};
            for (int p = 1; p < size; ++p) {
                MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
            }
            MPI_Finalize();
            return 0;
        }

        cout << "Number of generated cubes: " << cubes.size() << endl;

        // No cubes and no SAT => all explored branches were infeasible
        if (cubes.size() == 0) {
            cout << "Global result: UNSAT (all cubes infeasible)" << endl;
            int stopmsg[2] = {1, 0};
            for (int p = 1; p < size; ++p) {
                MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
            }
            MPI_Finalize();
            return 0;
        }

        int next_cube = 0;
        int active_workers = 0;
        bool sat_found = false;
        bool unknown_seen = false;

        cube_of_worker = vector<int>(size, -1);
        cube_depth_of_worker = vector<int>(size, -1);
        time_of_worker = vector<clock_t>(size);
        std::atomic<bool> keepRunning(true);
        startTimer(keepRunning, 10); // Run every 10 seconds

        // Send one initial cube to each worker
        for (int p = 1; p < size; ++p) {
            if (next_cube < (int)cubes.size()) {
                int header[2];
                header[0] = (int)cubes[next_cube].lits.size();
                header[1] = cubes[next_cube].split_depth;
                MPI_Send(header, 2, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);

                cube_of_worker[p] = next_cube;
                cube_depth_of_worker[p] = cubes[next_cube].split_depth;
                time_of_worker[p] = clock();

                if (header[0] > 0) {
                    MPI_Send(cubes[next_cube].lits.data(), header[0], MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                }
                ++active_workers;

                if (optimizing) {
                    int hasBest = global_has_best_cost ? 1 : 0;
                    MPI_Send(&hasBest, 1, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                    if (global_has_best_cost) {
                        MPI_Send(&global_best_cost, 1, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                    }
                }
                cout << "Sent cube " << next_cube << " to worker " << p << endl;
                next_cube++;
            } else {
                cube_of_worker[p] = -1;
                cube_depth_of_worker[p] = -1;
            }
        }

        // Receive results and dynamically send more cubes
        while (!sat_found && active_workers > 0) {
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

            if (status.MPI_TAG == TAG_BEST_REQUEST) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, status.MPI_SOURCE, TAG_BEST_REQUEST,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                int reply = global_has_best_cost ? global_best_cost : INT_MAX;
                MPI_Send(&reply, 1, MPI_INT, status.MPI_SOURCE, TAG_BEST_REPLY, MPI_COMM_WORLD);
                continue;
            }

            if (status.MPI_TAG == TAG_RESULT) {
                int msg[3];
                MPI_Recv(msg, 3, MPI_INT, status.MPI_SOURCE, TAG_RESULT,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                int result = msg[0];
                int worker = msg[1];
                int extra  = msg[2];

                if (result == 25) {
                    feasible_seen = true;
                    bestSolution(extra, worker, problem);
                    global_has_best_cost = true;
                    MPI_Send(&global_best_cost, 1, MPI_INT, worker, TAG_RESULT, MPI_COMM_WORLD);
                    continue;
                } else if (result == 26) {
                    feasible_seen = true;
                    bestSolution(extra, worker, problem);
                    global_has_best_cost = true;
                    continue;
                } else if (result == RES_SPLIT) {
                    int numSubcubes = extra;
                    int child_depth = cube_depth_of_worker[worker] + 1;
                    cube_of_worker[worker] = -1;
                    cube_depth_of_worker[worker] = -1;
                    active_workers--;
                    cout << "Split cube of worker " << worker << " into " << numSubcubes << " additional cubes" << endl;

                    for (int i = 0; i < numSubcubes; ++i) {
                        int len;
                        MPI_Recv(&len, 1, MPI_INT, worker, TAG_SPLIT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        vector<int> subcube(len);
                        if (len > 0) {
                            MPI_Recv(subcube.data(), len, MPI_INT, worker, TAG_SPLIT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        }
                        cubes.push_back({subcube, child_depth});
                    }
                    total_number_cubes = cubes.size();

                    for (int p = 1; p < size; ++p) {
                        if (next_cube < (int)cubes.size()) {
                            if (cube_of_worker[p] != -1) continue; // worker is still busy

                            int header[2];
                            header[0] = (int)cubes[next_cube].lits.size();
                            header[1] = cubes[next_cube].split_depth;
                            MPI_Send(header, 2, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);

                            cube_of_worker[p] = next_cube;
                            cube_depth_of_worker[p] = cubes[next_cube].split_depth;
                            time_of_worker[p] = clock();

                            if (header[0] > 0) {
                                MPI_Send(cubes[next_cube].lits.data(), header[0], MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                            }

                            if (optimizing) {
                                int hasBest = global_has_best_cost ? 1 : 0;
                                MPI_Send(&hasBest, 1, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                                if (global_has_best_cost) {
                                    MPI_Send(&global_best_cost, 1, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                                }
                            }

                            ++active_workers;
                            cout << "Sent cube " << next_cube << " to worker " << p << endl;
                            next_cube++;
                        }
                    }
                } else if (result == 10 && !optimizing) {
                    // One worker found SAT: stop all others
                    sat_found = true;
                    cout << "SAT found by worker " << worker << endl;

                    int stopmsg[2] = {1, 0};
                    for (int p = 1; p < size; ++p) {
                        if (p != worker) {
                            MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
                        }
                    }
                } else {
                    // Track UNKNOWN results if any
                    if (result == 0) unknown_seen = true;
                    else if (result == 10 || result == 15) {
                        feasible_seen = true;
                        bestSolution(extra, worker, problem);
                        global_has_best_cost = true;
                    }

                    // Send another cube to the now free worker
                    if (next_cube < (int)cubes.size()) {
                        int header[2];
                        header[0] = (int)cubes[next_cube].lits.size();
                        header[1] = cubes[next_cube].split_depth;
                        MPI_Send(header, 2, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);

                        cube_of_worker[worker] = next_cube;
                        cube_depth_of_worker[worker] = cubes[next_cube].split_depth;
                        time_of_worker[worker] = clock();

                        if (header[0] > 0) {
                            MPI_Send(cubes[next_cube].lits.data(), header[0], MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
                        }

                        if (optimizing) {
                            int hasBest = global_has_best_cost ? 1 : 0;
                            MPI_Send(&hasBest, 1, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
                            if (global_has_best_cost) {
                                MPI_Send(&global_best_cost, 1, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
                            }
                        }
                        cout << "Sent cube " << next_cube << " to worker " << worker << endl;
                        next_cube++;
                    } else {
                        cube_of_worker[worker] = -1;
                        cube_depth_of_worker[worker] = -1;
                        --active_workers;
                    }
                }
            }
        }

        int stopmsg[2] = {1, 0};
        for (int p = 1; p < size; ++p) {
            MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
        }

        // Final global result
        if (!optimizing) {
            if (!sat_found) {
                if (unknown_seen) {
                    cout << "Global result: UNKNOWN" << endl;
                } else {
                    cout << "Global result: UNSAT (all cubes infeasible)" << endl;
                }
            }
        } else {
            if (!feasible_seen) {
                if (unknown_seen) {
                    cout << "Global result: UNKNOWN" << endl;
                } else {
                    cout << "Global result: UNSAT (all cubes infeasible)" << endl;
                }
            } else {
                if (unknown_seen) {
                    cout << "Global result: BEST KNOWN " << global_best_cost << " (optimality not proved)" << endl;
                } else {
                    cout << "Global result: OPTIMUM = " << global_best_cost << endl;
                }
            }
        }

        keepRunning = false;
        MPI_Finalize();
        return 0;
    }

    // -------------------- WORKERS --------------------

    // Initial distributed generation of cubes
    {
        // All candidate variables for branching
        vector<int> allVars(nVars);
        for (int varNum = 1; varNum <= nVars; ++varNum) {
            allVars[varNum - 1] = varNum;
        }

        int worker_id = rank - 1;
        int num_workers = size - 1;

        auto baseSolver = create_solver(parser, nVars, sat);

        baseSolver->addBaseProblem(problem);

        if (optimizing) {
            baseSolver->addObjective(problem);
        }

	cout << "Generate cubes" << endl;
        vector<vector<int>> cubes = generate_cubes(*baseSolver, allVars, nVars,
                                                   true, worker_id, num_workers, optimizing);

	cout << "Done" << endl;
        int header[2];
        header[0] = cube_sat_found ? 10 : 20;
        header[1] = (int)cubes.size();
        MPI_Send(header, 2, MPI_INT, 0, TAG_INIT_RESULT, MPI_COMM_WORLD);

        for (const auto& cube : cubes) {
            int len = (int)cube.size();
            MPI_Send(&len, 1, MPI_INT, 0, TAG_INIT_CUBE, MPI_COMM_WORLD);
            if (len > 0) {
                MPI_Send(cube.data(), len, MPI_INT, 0, TAG_INIT_CUBE, MPI_COMM_WORLD);
            }
        }
    }

    while (true) {
        int header[2];
        MPI_Status status;

        // Receive either a cube or a stop signal from the master
        MPI_Recv(header, 2, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_STOP) {
            break;
        }

        int len = header[0];
        int split_depth = header[1];

        // Receive the cube literals
        vector<int> cube(len);
        if (len > 0) {
            MPI_Recv(cube.data(), len, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        if (optimizing) {
            int has_best_cost_msg = 0;
            MPI_Recv(&has_best_cost_msg, 1, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (has_best_cost_msg) global_has_best_cost = true;
            if (has_best_cost_msg) {
                MPI_Recv(&global_best_cost, 1, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }

        global_stop_flag = 0;
        split_requested_flag = 0;
        current_split_time_limit = compute_split_time_limit(split_depth);
        worker_cube_start = std::chrono::steady_clock::now();

        // Solve the assigned cube
        CubeSolveResult cubeRes = solve_cube(problem, parser, nVars, cube,
                                             global_best_cost,
                                             global_has_best_cost,
                                             optimizing,
                                             sat);

        Solver::StatusSolver ans = cubeRes.status;

        // If this worker was interrupted because another process found SAT, exit
        if (global_stop_flag) {
            break;
        }

        if (split_requested_flag) {
            int incumbent_for_split = global_best_cost;
            bool has_incumbent_for_split = global_has_best_cost;

            if (optimizing) {
                if (cubeRes.hasSolution) {
                    int header2[3];
                    header2[0] = 25; // SAT AND SPLIT REQUESTED
                    header2[1] = rank;
                    header2[2] = cubeRes.bestCost;
                    MPI_Send(header2, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
                    MPI_Recv(&incumbent_for_split, 1, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    has_incumbent_for_split = true;
                }
            }

            vector<vector<int>> subcubes = split_cube(problem, parser, nVars, cube,
                                                      optimizing,
                                                      incumbent_for_split,
                                                      has_incumbent_for_split,
                                                      sat);

            int header2[3];
            header2[1] = rank;

            if (cube_sat_found && !optimizing) {
                header2[0] = 10; // SAT
                header2[2] = 0;
                MPI_Send(header2, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
                break;
            }

            header2[0] = RES_SPLIT;
            header2[2] = (int)subcubes.size();
            MPI_Send(header2, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);

            for (const auto& sc : subcubes) {
                int slen = (int)sc.size();
                MPI_Send(&slen, 1, MPI_INT, 0, TAG_SPLIT, MPI_COMM_WORLD);
                if (slen > 0) {
                    MPI_Send(sc.data(), slen, MPI_INT, 0, TAG_SPLIT, MPI_COMM_WORLD);
                }
            }
            continue;
        }

        int msg[3];
        msg[1] = rank;
        msg[2] = 0;

        // Send result back to the master
        if (!optimizing) {
            if (ans == Solver::SOME_SOLUTION_FOUND || ans == Solver::OPTIMUM_FOUND) {
                msg[0] = 10;   // SAT for this cube
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
                break;
            } else if (ans == Solver::INFEASIBLE) {
                msg[0] = 20;   // UNSAT for this cube
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            } else {
                msg[0] = 0;    // UNKNOWN for this cube
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            }
        } else {
            if (ans == Solver::OPTIMUM_FOUND) {
                msg[0] = 10;   // OPTIMUM for this cube
                msg[2] = cubeRes.bestCost;
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            } else if (ans == Solver::SOME_SOLUTION_FOUND) {
                msg[0] = 15;   // Feasible solution for this cube
                msg[2] = cubeRes.bestCost;
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            } else if (ans == Solver::INFEASIBLE) {
                msg[0] = 20;   // UNSAT for this cube
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            } else {
                msg[0] = 0;    // UNKNOWN for this cube
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            }
        }
    }

    MPI_Finalize();
    return 0;
}

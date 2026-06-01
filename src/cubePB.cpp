#include "ISolverBackend.h"
#include "NativeBackend.h"
#include "RoundingSatBackend.h"
#include "CadicalBackend.h"

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
#include <unordered_set>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <cctype>

using namespace std;

std::chrono::steady_clock::time_point worker_cube_start;

const int MAX_CUBE_GENERATION_DEPTH = 11;
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
vector<WConstraint> global_learned_from_original;
const int original_worker = 1;
vector<vector<int>> global_cnf_clauses;

// Choose solver from command line.
enum class SolverBackendKind {
    Native,
    RoundingSAT,
    CaDiCaL
};

SolverBackendKind selected_backend = SolverBackendKind::Native;

vector<int> cube_of_worker;
vector<int> cube_depth_of_worker;
vector<clock_t> time_of_worker;
int total_number_cubes = 0;
const int ORIGINAL_WORKER_CUBE_MARKER = -2;

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

static string constraint_key(const WConstraint& c) {
    vector<pair<int,int>> terms;
    terms.reserve(c.getSize());
    for (int i = 0; i < c.getSize(); ++i)
        terms.push_back({c.getIthCoefficient(i), c.getIthLiteral(i)});
    sort(terms.begin(), terms.end());
    string key;
    key.reserve(terms.size() * 8 + 8);
    for (auto& [coef, lit] : terms) {
        key += to_string(coef);
        key += ':';
        key += to_string(lit);
        key += ',';
    }
    key += '=';
    key += to_string(c.getConstant());
    return key;
}

// Constraint set with O(1) amortized deduplication.
// Keys are propagated on copy, avoiding reconstruction from scratch.
struct ConstraintSet {
    vector<WConstraint> constraints;
    unordered_set<string> keys;

    void insert_all(const vector<WConstraint>& src) {
        for (const auto& c : src)
            if (keys.insert(constraint_key(c)).second)
                constraints.push_back(c);
    }
};

struct CubeTask {
    vector<int> lits;
    int split_depth;
    ConstraintSet learned_constraints;
};


void send_constraints(const vector<WConstraint>& constraints, int dest, int tag) {
    int num_constraints = (int)constraints.size();
    MPI_Send(&num_constraints, 1, MPI_INT, dest, tag, MPI_COMM_WORLD);

    for (const WConstraint& c : constraints) {
        int header[2] = {c.getSize(), c.getConstant()};
        MPI_Send(header, 2, MPI_INT, dest, tag, MPI_COMM_WORLD);

        vector<int> terms;
        terms.reserve(2 * c.getSize());
        for (int i = 0; i < c.getSize(); ++i) {
            terms.push_back(c.getIthCoefficient(i));
            terms.push_back(c.getIthLiteral(i));
        }

        if (!terms.empty()) {
            MPI_Send(terms.data(), (int)terms.size(), MPI_INT, dest, tag, MPI_COMM_WORLD);
        }
    }
}

vector<WConstraint> recv_constraints(int source, int tag) {
    int num_constraints = 0;
    MPI_Recv(&num_constraints, 1, MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    vector<WConstraint> constraints;
    constraints.reserve(num_constraints);

    for (int i = 0; i < num_constraints; ++i) {
        int header[2];
        MPI_Recv(header, 2, MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        int size = header[0];
        int rhs = header[1];
        vector<int> coeffs(size);
        vector<int> lits(size);

        if (size > 0) {
            vector<int> terms(2 * size);
            MPI_Recv(terms.data(), (int)terms.size(), MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            for (int j = 0; j < size; ++j) {
                coeffs[j] = terms[2 * j];
                lits[j] = terms[2 * j + 1];
            }
        }

        constraints.emplace_back(coeffs, lits, rhs);
    }

    return constraints;
}

void send_work_to_worker(int worker, const CubeTask& task, bool optimizing) {

    ConstraintSet all_learned = task.learned_constraints;
    all_learned.insert_all(global_learned_from_original);

    int header[3];
    header[0] = (int)task.lits.size();
    header[1] = task.split_depth;
    header[2] = (int)all_learned.constraints.size();
    MPI_Send(header, 3, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);

    if (header[0] > 0) {
        MPI_Send(task.lits.data(), header[0], MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
    }

    send_constraints(all_learned.constraints, worker, TAG_WORK);

    if (optimizing) {
        int hasBest = global_has_best_cost ? 1 : 0;
        MPI_Send(&hasBest, 1, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
        if (global_has_best_cost) {
            MPI_Send(&global_best_cost, 1, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
        }
    }
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
    static int sent_learned_count = 0;

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int dummy = 0;
    int best_from_master = INT_MAX;

    MPI_Send(&dummy, 1, MPI_INT, 0, TAG_BEST_REQUEST, MPI_COMM_WORLD);
    MPI_Status best_recv_status;
    MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &best_recv_status);
    if (best_recv_status.MPI_TAG == TAG_STOP) {
        int stopmsg[2];
        MPI_Recv(stopmsg, 2, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        global_stop_flag = 1;
        return;
    }
    MPI_Recv(&best_from_master, 1, MPI_INT, 0, TAG_BEST_REPLY, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (best_from_master != INT_MAX && best_from_master < last_imported_best) {
        add_objective_bound_constraint(*solver, global_problem, best_from_master);
        last_imported_best = best_from_master;
    }

    // Original worker: send new clauses to master
    if (rank == original_worker) {
        vector<WConstraint> all = solver->collectGoodClauses();
        if ((int)all.size() > sent_learned_count) {
            vector<WConstraint> new_ones(all.begin() + sent_learned_count, all.end());
            send_constraints(new_ones, 0, TAG_LEARNED_UPDATE);
            sent_learned_count = (int)all.size();
        }
    }
}

// Check whether a STOP message has been sent by the master.
// If so, receive it and update the global stop flag.
extern "C" int terminate_cb(int x) {
    if (global_stop_flag) return 1;

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

    // Once a STOP has been received, keep returning 1 on every call. The STOP
    // message is consumed only once, but RoundingSAT's optimize loop checks the
    // periodic function at the top of each iteration: a single return-1 only
    // makes the inner solve() bail out with state SOLVING (treated as
    // INPROCESSED), so without this sticky flag the loop would resume. The
    // original worker has no split timeout (current_split_time_limit = INT_MAX)
    // and no inter-cube receive, so this is its only way to stop.
    if (global_stop_flag) return 1;

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

    // Hard cap on cube generation depth.
    if (numDec >= MAX_CUBE_GENERATION_DEPTH) {
        if (!distributed_split || num_workers == 1 || worker_id == 0) {
            cubes.push_back(current);
        }
        return;
    }

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
                           const vector<WConstraint>& learned_constraints,
                           const int& bestCost,
                           const bool& hasBestCost,
                           bool optimizing,
                           bool sat) {
    auto solver = create_solver(parser, nVars, sat);

    solver->addBaseProblem(problem);
    solver->addLearnedConstraints(learned_constraints);
    solver->addCube(cube);

    if (optimizing) {
        solver->addObjective(problem);

        if (hasBestCost) {
            solver->addObjectiveBound(problem, bestCost);
        }
    }

    int tlimit = 0;
    CubeSolveResult res = solver->solve(optimizing, tlimit);
    if (split_requested_flag) {
        res.learned_constraints = solver->goodClauses();
    }

    return res;
}

vector<vector<int>> split_cube(PBProblem& problem,
                               Parser& parser,
                               int nVars,
                               const vector<int>& cube,
                               const vector<WConstraint>& learned_constraints,
                               bool optimizing,
                               int bestCost,
                               bool hasBestCost,
                               bool sat) {
    vector<int> allVars(nVars);
    for (int i = 0; i < nVars; ++i) allVars[i] = i + 1;

    auto solver = create_solver(parser, nVars, sat);

    solver->addBaseProblem(problem);
    solver->addLearnedConstraints(learned_constraints);
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
                if (cube_of_worker[w] == ORIGINAL_WORKER_CUBE_MARKER) {
                    cout << " original problem" << endl;
                } else if (cube_of_worker[w] == -1) {
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

// ---------------------------------------------------------------------------
// Helper functions extracted from main()
// ---------------------------------------------------------------------------

// Parse optional --solver=X argument. Returns false on error.
bool parse_args(int argc, char** argv, int rank, string& filename) {
    filename = argv[1];

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
            return false;
        }
    }
    return true;
}

// Read the input file and populate problem, sat, optimizing, cnf_nVars. Returns false on error.
bool load_problem(const string& filename, int rank,
                  Parser& parser, PBProblem& problem,
                  bool& sat, bool& optimizing, int& cnf_nVars) {
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
            return false;
        }

        global_cnf_clauses = std::move(clauses);
        if (selected_backend != SolverBackendKind::CaDiCaL)
            problem = cnf_to_pb_problem(global_cnf_clauses);
        sat = true;
        optimizing = false;
    } else {
        if (rank == 0) cerr << "Unsupported format. Use .opb, .lp or .cnf" << endl;
        return false;
    }
    return true;
}

// Check solver-problem compatibility. Returns false on error.
bool validate_config(int rank, bool sat, bool optimizing, const PBProblem& problem) {
    if (selected_backend == SolverBackendKind::CaDiCaL && !sat) {
        if (rank == 0) {
            cerr << "--solver=cadical only supports .cnf files." << endl;
        }
        return false;
    }

    if (selected_backend == SolverBackendKind::CaDiCaL && optimizing) {
        if (rank == 0) {
            cerr << "--solver=cadical does not support optimization/objectives." << endl;
        }
        return false;
    }

    // optimization integration supports minimization only.
    if (optimizing && !problem.minimizing) {
        if (rank == 0) {
            cerr << "optimization integration currently supports minimization only." << endl;
        }
        return false;
    }

    return true;
}

// Initialize global objective-priority variables (only called when optimizing).
void init_objective_priority(bool optimizing, int nVars, const PBProblem& problem) {
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

// Master process: receives initial cubes from workers, dispatches work, collects results.
void run_master(int size, int num_cube_workers, int first_cube_worker,
                bool optimizing, PBProblem& problem) {
    vector<CubeTask> cubes;
    bool sat_found = false;
    bool unknown_seen = false;
    bool original_worker_active = true;
    bool original_proved_unsat = false;
    bool original_proved_optimum = false;
    bool sat_found_during_generation = false;
    bool feasible_seen = false;
    bool stop_sent = false;

    // Receive the initial cubes generated by the workers
    int pending_init_workers = num_cube_workers;
    while (pending_init_workers > 0) {
        MPI_Status mpi_status;
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &mpi_status);

        // The original_worker (rank 1) runs solve() in parallel with cube generation
        // and periodically sends TAG_BEST_REQUEST via import_external_constraints.
        // No solution can exist yet at this point, so we always reply INT_MAX.
        // We must handle it here anyway to unblock the worker waiting on MPI_Recv.
        if (mpi_status.MPI_TAG == TAG_BEST_REQUEST) {
            int dummy;
            MPI_Recv(&dummy, 1, MPI_INT, mpi_status.MPI_SOURCE, TAG_BEST_REQUEST,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            int reply = global_has_best_cost ? global_best_cost : INT_MAX;
            MPI_Send(&reply, 1, MPI_INT, mpi_status.MPI_SOURCE, TAG_BEST_REPLY, MPI_COMM_WORLD);
            continue;
        }

        // Similarly, the original_worker may send learned constraints while solving.
        // Collect them so they can be forwarded to cube workers later.
        if (mpi_status.MPI_TAG == TAG_LEARNED_UPDATE) {
            auto new_clauses = recv_constraints(mpi_status.MPI_SOURCE, TAG_LEARNED_UPDATE);
            global_learned_from_original.insert(global_learned_from_original.end(), new_clauses.begin(), new_clauses.end());
            continue;
        }

        if (mpi_status.MPI_TAG == TAG_RESULT) {
            int msg[3];
            MPI_Recv(msg, 3, MPI_INT, mpi_status.MPI_SOURCE, TAG_RESULT,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	        int result = msg[0];
            int worker = msg[1];
            int extra = msg[2];

            if (worker == original_worker) {
                if (result == 26) {
                    feasible_seen = true;
                    bestSolution(extra, worker, problem);
                    global_has_best_cost = true;
                    continue;
                }

                original_worker_active = false;

                if (!optimizing) {
                    if (result == 10) {
                        sat_found = true;
                        cout << "SAT found by original worker " << worker << endl;
                    } else if (result == 20) {
                        original_proved_unsat = true;
                        cout << "Original worker proved UNSAT" << endl;
                    } else {
                        unknown_seen = true;
                    }
                } else {
                    if (result == 10) {
                        feasible_seen = true;
                        bestSolution(extra, worker, problem);
                        global_has_best_cost = true;
                        original_proved_optimum = true;
                        cout << "Original worker proved OPTIMUM" << endl;
                    } else if (result == 15) {
                        feasible_seen = true;
                        bestSolution(extra, worker, problem);
                        global_has_best_cost = true;
                    } else if (result == 20) {
                        if (feasible_seen) {
                            original_proved_optimum = true;
                            cout << "Original worker proved optimum" << endl;
                        } else {
                            original_proved_unsat = true;
                            cout << "Original worker proved UNSAT" << endl;
                        }
                    } else {
                        unknown_seen = true;
                    }
                }
            }

            continue;
        }

        int p = mpi_status.MPI_SOURCE;
        int header[2];
        MPI_Recv(header, 2, MPI_INT, p, TAG_INIT_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        --pending_init_workers;

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
            cubes.push_back({cube, 0, {}});
        }
    }

    total_number_cubes = cubes.size();

    if (sat_found || original_proved_unsat || original_proved_optimum) {
        int stopmsg[2] = {1, 0};
        for (int p = 1; p < size; ++p) {
            MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
        }

        if (!optimizing) {
            cout << (sat_found ? "Global result: SAT" : "Global result: UNSAT") << endl;
        } else if (original_proved_optimum) {
            cout << "Global result: OPTIMUM = " << global_best_cost << endl;
        } else {
            cout << "Global result: UNSAT" << endl;
        }

        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 0);
        return;
    }

    // If SAT was detected during cube generation, stop everything
    if (sat_found_during_generation && !optimizing) {
        cout << "Global result: SAT found during cube generation" << endl;
        int stopmsg[2] = {1, 0};
        for (int p = 1; p < size; ++p) {
            MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
        }
        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 0);
        return;
    }

    cout << "Number of generated cubes: " << cubes.size() << endl;

    // No cubes and no SAT => all explored branches were infeasible
    if (cubes.size() == 0 && num_cube_workers > 0) {
        cout << "Global result: UNSAT (all cubes infeasible)" << endl;
        int stopmsg[2] = {1, 0};
        for (int p = 1; p < size; ++p) {
            MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
        }
        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 0);
        return;
    }

    int next_cube = 0;
    int active_workers = 0;

    cube_of_worker = vector<int>(size, -1);
    cube_depth_of_worker = vector<int>(size, -1);
    time_of_worker = vector<clock_t>(size);
    cube_of_worker[original_worker] = ORIGINAL_WORKER_CUBE_MARKER;
    time_of_worker[original_worker] = clock();
    std::atomic<bool> keepRunning(true);
    startTimer(keepRunning, 10); // Run every 10 seconds

    // Send one initial cube to each worker
    for (int p = first_cube_worker; p < size; ++p) {
        if (next_cube < (int)cubes.size()) {
            cube_of_worker[p] = next_cube;
            cube_depth_of_worker[p] = cubes[next_cube].split_depth;
            time_of_worker[p] = clock();
            send_work_to_worker(p, cubes[next_cube], optimizing);
            ++active_workers;
            cout << "Sent cube " << next_cube << " to worker " << p << endl;
            next_cube++;
        } else {
            cube_of_worker[p] = -1;
            cube_depth_of_worker[p] = -1;
        }
    }

    // Receive results and dynamically send more cubes
    while (!sat_found && !original_proved_unsat && !original_proved_optimum &&
           (active_workers > 0 || original_worker_active)) {
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

        if (status.MPI_TAG == TAG_LEARNED_UPDATE) {
            auto new_clauses = recv_constraints(status.MPI_SOURCE, TAG_LEARNED_UPDATE);
            global_learned_from_original.insert(global_learned_from_original.end(), new_clauses.begin(), new_clauses.end());
            continue;
        }

        if (status.MPI_TAG == TAG_RESULT) {
            int msg[3];
            MPI_Recv(msg, 3, MPI_INT, status.MPI_SOURCE, TAG_RESULT,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            int result = msg[0];
	        int worker = msg[1];
            int extra  = msg[2];

            if (worker == original_worker) {
                if (result == 26) {
                    feasible_seen = true;
                    bestSolution(extra, worker, problem);
                    global_has_best_cost = true;
                    continue;
                }

                original_worker_active = false;

                if (!optimizing) {
                    if (result == 10) {
                        sat_found = true;
                        cout << "SAT found by original worker " << worker << endl;
                    } else if (result == 20) {
                        original_proved_unsat = true;
                        cout << "Original worker proved UNSAT" << endl;
                    } else {
                        unknown_seen = true;
                    }
                } else {
                    if (result == 10) {
                        feasible_seen = true;
                        bestSolution(extra, worker, problem);
                        global_has_best_cost = true;
                        original_proved_optimum = true;
                        cout << "Original worker proved OPTIMUM" << endl;
                    } else if (result == 15) {
                        feasible_seen = true;
                        bestSolution(extra, worker, problem);
                        global_has_best_cost = true;
                    } else if (result == 20) {
                        if (feasible_seen) {
                            original_proved_optimum = true;
                            cout << "Original worker proved optimum" << endl;
                        } else {
                            original_proved_unsat = true;
                            cout << "Original worker proved UNSAT" << endl;
                        }
                    } else {
                        unknown_seen = true;
                    }
                }
                continue;
            }

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
                int parent_cube = cube_of_worker[worker];
                int child_depth = cube_depth_of_worker[worker] + 1;
                ConstraintSet child_constraints = cubes[parent_cube].learned_constraints;
                vector<WConstraint> new_constraints = recv_constraints(worker, TAG_SPLIT);
                child_constraints.insert_all(new_constraints);
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
                    CubeTask child_task;
                    child_task.lits = subcube;
                    child_task.split_depth = child_depth;
                    child_task.learned_constraints = child_constraints;
                    cubes.push_back(std::move(child_task));
                }
                total_number_cubes = cubes.size();

                for (int p = first_cube_worker; p < size; ++p) {
                    if (next_cube < (int)cubes.size()) {
                        if (cube_of_worker[p] != -1) continue; // worker is still busy

                        cube_of_worker[p] = next_cube;
                        cube_depth_of_worker[p] = cubes[next_cube].split_depth;
                        time_of_worker[p] = clock();
                        send_work_to_worker(p, cubes[next_cube], optimizing);
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
                    cube_of_worker[worker] = next_cube;
                    cube_depth_of_worker[worker] = cubes[next_cube].split_depth;
                    time_of_worker[worker] = clock();
                    send_work_to_worker(worker, cubes[next_cube], optimizing);
                    cout << "Sent cube " << next_cube << " to worker " << worker << endl;
                    next_cube++;
                } else {
                    cube_of_worker[worker] = -1;
                    cube_depth_of_worker[worker] = -1;
                    --active_workers;
                    if (active_workers == 0) {
                        // All cubes processed: stop all remaining workers
                        int stopmsg[2] = {1, 0};
                        for (int p = 1; p < size; ++p) {
                            MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
                        }
                        stop_sent = true;
                        break;
                    }
                }
            }
        }
    }

    if (!stop_sent) {
        int stopmsg[2] = {1, 0};
        for (int p = 1; p < size; ++p) {
            MPI_Send(stopmsg, 2, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
        }
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
    fflush(stdout);
    MPI_Abort(MPI_COMM_WORLD, 0);
}

// Worker rank 1: solves the original (uncubed) problem in parallel with cube generation.
void run_original_worker(int rank, int nVars, PBProblem& problem, Parser& parser,
                         bool optimizing, bool sat) {
    vector<int> original_cube;
    vector<WConstraint> learned_constraints;

    global_stop_flag = 0;
    split_requested_flag = 0;
    current_split_time_limit = INT_MAX;
    worker_cube_start = std::chrono::steady_clock::now();

    cout << "Solving original problem" << endl;
    CubeSolveResult originalRes = solve_cube(problem, parser, nVars, original_cube,
                                             learned_constraints,
                                             global_best_cost,
                                             global_has_best_cost,
                                             optimizing,
                                             sat);

    if (!global_stop_flag) {
        int msg[3];
        msg[1] = rank;
        msg[2] = 0;

        if (!optimizing) {
            if (originalRes.status == Solver::SOME_SOLUTION_FOUND ||
                originalRes.status == Solver::OPTIMUM_FOUND) {
                msg[0] = 10;
            } else if (originalRes.status == Solver::INFEASIBLE) {
                msg[0] = 20;
            } else {
                msg[0] = 0;
            }
        } else {
            if (originalRes.status == Solver::OPTIMUM_FOUND) {
                msg[0] = 10;
                msg[2] = originalRes.bestCost;
            } else if (originalRes.status == Solver::SOME_SOLUTION_FOUND) {
                msg[0] = 15;
                msg[2] = originalRes.bestCost;
            } else if (originalRes.status == Solver::INFEASIBLE) {
                msg[0] = 20;
            } else {
                msg[0] = 0;
            }
        }

        MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
    }

    MPI_Finalize();
}

// Cube workers (rank >= first_cube_worker): generate initial cubes then solve assigned cubes.
void run_cube_worker(int rank, int nVars, PBProblem& problem, Parser& parser,
                     bool optimizing, bool sat, int first_cube_worker, int num_cube_workers) {
    // Initial distributed generation of cubes
    {
        // All candidate variables for branching
        vector<int> allVars(nVars);
        for (int varNum = 1; varNum <= nVars; ++varNum) {
            allVars[varNum - 1] = varNum;
        }

        int worker_id = rank - first_cube_worker;
        int num_workers = num_cube_workers;

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
        int header[3];
        MPI_Status status;

        // Receive either a cube or a stop signal from the master
        MPI_Recv(header, 3, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

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

        ConstraintSet learned_constraints;
        learned_constraints.insert_all(recv_constraints(0, TAG_WORK));

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
                                             learned_constraints.constraints,
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

            ConstraintSet split_constraint_set = learned_constraints;
            split_constraint_set.insert_all(cubeRes.learned_constraints);

            vector<vector<int>> subcubes = split_cube(problem, parser, nVars, cube,
                                                      split_constraint_set.constraints,
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
            send_constraints(cubeRes.learned_constraints, 0, TAG_SPLIT);

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

    string filename;
    if (!parse_args(argc, argv, rank, filename)) {
        MPI_Finalize();
        return 1;
    }

    Parser parser;
    PBProblem problem;
    bool optimizing = true;
    bool sat = false;
    int cnf_nVars = 0;

    if (!load_problem(filename, rank, parser, problem, sat, optimizing, cnf_nVars)) {
        MPI_Finalize();
        return 1;
    }

    global_problem = problem;
    if (!sat) {
        optimizing = !problem.objCoeffs.empty();
    }

    if (!validate_config(rank, sat, optimizing, problem)) {
        MPI_Finalize();
        return 1;
    }

    // Number of variables in the problem
    int nVars = sat ? cnf_nVars : parser.numVars();
    const int first_cube_worker = 2;
    const int num_cube_workers = max(0, size - first_cube_worker);
    global_best_cost = problem.minimizing ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();

    if (optimizing) {
        init_objective_priority(optimizing, nVars, problem);
    }

    // -------------------- MASTER --------------------
    if (rank == 0) {
        run_master(size, num_cube_workers, first_cube_worker, optimizing, problem);
        return 0;
    }

    // -------------------- WORKERS --------------------

    if (rank == original_worker) {
        run_original_worker(rank, nVars, problem, parser, optimizing, sat);
        return 0;
    }

    run_cube_worker(rank, nVars, problem, parser, optimizing, sat, first_cube_worker, num_cube_workers);
    return 0;
}

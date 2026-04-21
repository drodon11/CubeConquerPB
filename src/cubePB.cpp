#include "Parser.h"
#include "Solver.h"
#include "WConstraint.h"
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

// a fer: millora paralelisme, afegir paralelitzacio quan els cubs son massa grans i augmentar el temps 
//cada vegada que es fa split. 
//millora optimització: enviar millor la solució incumbent a les workers, perque facin un cut més fort i potser evitin splits innecessaris.
using namespace std;
std::chrono::steady_clock::time_point worker_cube_start;

// MPI message tags
const int TAG_STOP   = 1;
const int TAG_WORK   = 2;
const int TAG_RESULT = 3;
const int TAG_SPLIT = 4;

// New tags for initial distributed cube generation
const int TAG_INIT_RESULT = 5;
const int TAG_INIT_CUBE   = 6;

const int DEPTH = 5;
const int RES_SPLIT = 30;
// Global flag used by workers to stop early when another process found SAT
int global_stop_flag = 0;
int split_requested_flag = 0;
const int base_split_time_limit = 240; // seconds
int current_split_time_limit = base_split_time_limit;

// Global cutoff threshold used during cube generation
double theta = 1000.0;

// Global flag set when SAT is detected during cube generation
bool cube_sat_found = false;

int global_best_cost;
struct CubeTask {
    vector<int> lits;
    int split_depth;
};

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

// If there is new information, this should add a constraint that forces the objective function to obtain a
// better solution
// TO BE COMPLETED!

extern "C" void import_external_constraints ( Solver* solver ){
  vector<int> coefficients, literals;
  int rhs;

  if (rhs < 1) return; // Trivial constraint
  
  WConstraint c(coefficients, literals, rhs);
  c.sortByIncreasingVariable();
  c.removeDuplicates();
  c.sortByDecreasingCoefficient();
  solver->addAndPropagatePBConstraint(c, true, 0, 0); // treat is as initial since we do not want the constraint that forces a better solution to be deleted
}

// Check whether a STOP message has been sent by the master.
// If so, receive it and update the global stop flag.
extern "C" int terminate_cb(int x) {
    MPI_Status status;
    int flag = 0;
    static int best = INT_MAX;
    if (x < best) {
      cout << "********* Best so far *********" << x << endl;
      best = x;
    }

    // STOP from master
    MPI_Iprobe(0, TAG_STOP, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        int dummy;
        MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
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

// Recursive cube generation using a lookahead evalvar-style heuristic
void generate_cubes_rec(Solver& solver,
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
    while(failedLitFound){
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
            //cout << "Conf for var " << v << endl;
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
            //cout << "Conf (neg) for var " << v << endl;
            } else {
                // Number of newly assigned variables caused by propagation
                eval_neg = solver.assignedVars() - baseAssigned;
            }
            solver.backtrack(1);

            // If both branches are impossible, the current node is refuted
            if (!okPos && !okNeg) {
                solver.backtrack(failedLits);
                return;
            }
            else if (!okPos || !okNeg) {
                // If only one branch is possible, assign the variable accordingly and continue
                int lit = okPos ? v : -v;
                //current.push_back(lit);
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
            long long tie   = 1LL * eval_pos + eval_neg;

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
        if(failedLits>0)solver.backtrack(failedLits);
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
        if(failedLits > 0){
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
        if(failedLits>0)solver.backtrack(failedLits);
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
    //If we found conflict nodes we have to backtrack all the way up to the last decision level
    if(failedLits > 0){
        solver.backtrack(failedLits);
    }
}

void add_linear_constraint_to_solver(Solver& solver,
                                     const vector<int>& coeffs,
                                     const vector<int>& varNums,
                                     int rhs,
                                     bool isGeq) {
    vector<int> coefficients, literals;

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
    if (rhs < 1) return;

    WConstraint c(coefficients, literals, rhs);
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

// Wrapper that initializes cube generation from the base solver state
vector<vector<int>> generate_cubes(Solver& solver,
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
vector<vector<int>> generate_cubes(Solver& solver,
                                   const vector<int>& vars,
                                   int nVars) {
    return generate_cubes(solver, vars, nVars, false, 0, 1, false);
}


// Add the original PB instance to the solver
void add_base_problem(Solver& solver, PBProblem& problem) {
    for (int i = 0; i < (int)problem.constraints.size(); ++i) {
        problem.constraints[i].sortByIncreasingVariable();
        problem.constraints[i].removeDuplicates();
        problem.constraints[i].sortByDecreasingCoefficient();
        solver.addAndPropagatePBConstraint(problem.constraints[i], true, 0, 0);
    }

    solver.addObjectiveFunction(problem.minimizing, problem.objCoeffs, problem.objVars);
}

// Add a cube to the solver as unit PB constraints
void add_cube_constraints(Solver& solver, const vector<int>& cube) {
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

struct CubeSolveResult {
    Solver::StatusSolver status;
    bool hasSolution;
    int bestCost;
};

// Solve a single cube with a fresh solver instance
CubeSolveResult solve_cube(PBProblem& problem,
                           Parser& parser,
                           const int& nVars,
                           const vector<int>& cube,
                           const int& bestCost,
                           const bool& hasBestCost) {
    clock_t beginTime = clock();
    Solver solver(nVars, beginTime);

    solver.setBT0(true);
    solver.set_periodic_function(terminate_cb);
    solver.set_import_external_constraints_procedure(import_external_constraints);
    
    // Store variable names for output/debugging
    for (int varNum = 1; varNum <= nVars; ++varNum) {
        solver.addVarName(varNum, parser.var2string(varNum));
    }

    // Load the base problem and then the cube assumptions
    add_base_problem(solver, problem);
    add_cube_constraints(solver, cube);
    if (hasBestCost) {
        add_objective_bound_constraint(solver, problem, bestCost);
    }
    int tlimit = 0;
    solver.solve(tlimit);

    CubeSolveResult res;
    res.status = solver.currentStatus();
    res.hasSolution = (res.status == Solver::SOME_SOLUTION_FOUND || res.status == Solver::OPTIMUM_FOUND);
    res.bestCost = res.hasSolution ? solver.cost_best_solution() : 0;
    return res;
}


vector<vector<int>> split_cube(PBProblem& problem,
                               Parser& parser,
                               int nVars,
                               const vector<int>& cube,
                               bool optimizing,
                               int bestCost,
                               bool hasBestCost) {
    clock_t beginTime = clock();
    Solver solver(nVars, beginTime);
    solver.setBT0(true);

    for (int varNum = 1; varNum <= nVars; ++varNum) {
        solver.addVarName(varNum, parser.var2string(varNum));
    }

    add_base_problem(solver, problem);
    add_cube_constraints(solver, cube);
    if (optimizing && hasBestCost) {
        add_objective_bound_constraint(solver, problem, bestCost);
    }

    vector<int> allVars(nVars);
    for (int i = 0; i < nVars; ++i) allVars[i] = i + 1;

    // Local split: do not redistribute by worker_id here
    vector<vector<int>> local = generate_cubes(solver, allVars, nVars, false, 0, 1, optimizing);
    vector<vector<int>> full;

    for (auto &sub : local) {
        vector<int> c = cube;
        c.insert(c.end(), sub.begin(), sub.end());
        full.push_back(c);
    }
    return full;
}

vector<int> cube_of_worker;
vector<int> cube_depth_of_worker;
vector<clock_t> time_of_worker;
int total_number_cubes;

void startTimer(std::atomic<bool>& run, int interval) {
    std::thread t([&run, interval]() {
        while (run) {
	  cout << string(50,'=') << endl;
	  cout << "TOTAL NUMBER CUBES TO BE PROCESSED: " << total_number_cubes << endl;
	  for (uint w = 1; w < cube_of_worker.size(); ++w){
	    cout << "Worker " << w << ":";
	    if (cube_of_worker[w] == -1) cout << " idle" << endl;
	   else {
    cout << " cube " << setw(4) << cube_of_worker[w]
         << " [depth " << cube_depth_of_worker[w] << "]"
         << "\t(" << double(clock() - time_of_worker[w]) / CLOCKS_PER_SEC
         << " s.)" << endl;
	}
	  }
      cout<< "Global best cost: " << global_best_cost << endl;
	  cout << string(30,'=') << endl;
            std::this_thread::sleep_for(std::chrono::seconds(interval));
        }
    });
    t.detach(); // Let it run independently
}

bool bestSolution(int bestCost, int worker, PBProblem& problem){

    bool better = problem.minimizing ? (bestCost < global_best_cost): (bestCost > global_best_cost);
    if (better) {
        global_best_cost = bestCost;
        cout << "New global incumbent " << global_best_cost
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
            cerr << "Usage: mpirun -np N ./cubePB formula.opb" << endl;
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

    Parser parser;
    PBProblem problem;
    bool optimizing = true;
    // Each rank reads the same input file locally
    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".opb") {
        problem = parser.readOPB(filename);
    } else if (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".lp") {
        problem = parser.readLP(filename);
        optimizing = true;
    } else {
        if (rank == 0) cerr << "Unsupported format. Use .opb or .lp" << endl;
        MPI_Finalize();
        return 1;
    }

    optimizing = !problem.objCoeffs.empty();

    // Number of variables in the problem
    int nVars = parser.numVars();

    // -------------------- MASTER --------------------
    if (rank == 0) {

        vector<CubeTask> cubes;
        bool sat_found_during_generation = false;
        global_best_cost = problem.minimizing ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();
        bool feasible_seen = false;
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
            int stop = 1;
            for (int p = 1; p < size; ++p) {
                MPI_Send(&stop, 1, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
            }
            MPI_Finalize();
            return 0;
        }

        cout << "Number of generated cubes: " << cubes.size() << endl;

        // No cubes and no SAT => all explored branches were infeasible
        if (cubes.size() == 0) {
            cout << "Global result: UNSAT (all cubes infeasible)" << endl;
            int stop = 1;
            for (int p = 1; p < size; ++p) {
                MPI_Send(&stop, 1, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
            }
            MPI_Finalize();
            return 0;
        }

        int next_cube = 0;
        int active_workers = 0;
        bool sat_found = false;
        bool unknown_seen = false;
        clock_t last_printed_stats = clock();
        cube_of_worker = vector<int>(size,-1);
        cube_depth_of_worker = vector<int>(size,-1);
        time_of_worker = vector<clock_t>(size);
        std::atomic<bool> keepRunning(true);
        startTimer(keepRunning, 10); // Run every 10 seconds
        bool global_has_best_cost = false;
        // Send one initial cube to each worker
        for (int p = 1; p < size; ++p) {
            if (next_cube < (int)cubes.size()) {
                int header[2];
                header[0] = (int)cubes[next_cube].lits.size();
                header[1] = cubes[next_cube].split_depth;
                MPI_Send(header, 2, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                if (header[0] > 0) {
                    cube_of_worker[p] = next_cube;
                    cube_depth_of_worker[p] = cubes[next_cube].split_depth;
                    time_of_worker[p] = clock();
                    MPI_Send(cubes[next_cube].lits.data(), header[0], MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
		            ++active_workers;
                }
                if(optimizing){
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
	        int msg[3];
            MPI_Status status;
            MPI_Recv(msg, 3, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);

            int result = msg[0];
            int worker = msg[1];
            int extra  = msg[2];

            if(result == 25){
                feasible_seen = true;
                bestSolution(extra, worker, problem);
                global_has_best_cost = true;
                MPI_Send(&global_best_cost, 1, MPI_INT, worker, TAG_RESULT, MPI_COMM_WORLD);
                continue;
            }
            else if (result == RES_SPLIT) {
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
                        if(cube_of_worker[p] != -1) continue; // worker is still busy
                        int header[2];
                        header[0] = (int)cubes[next_cube].lits.size();
                        header[1] = cubes[next_cube].split_depth;
                        MPI_Send(header, 2, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                        if (header[0] > 0) {
                            cube_of_worker[p] = next_cube;
                            cube_depth_of_worker[p] = cubes[next_cube].split_depth;
                            time_of_worker[p] = clock();
                            MPI_Send(cubes[next_cube].lits.data(), header[0], MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                            if(optimizing){
                                int hasBest = global_has_best_cost ? 1 : 0;
                                MPI_Send(&hasBest, 1, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                                if (global_has_best_cost) {
                                    MPI_Send(&global_best_cost, 1, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                                }
                            
                            }
                            ++active_workers;
                        }
                        cout << "Sent cube " << next_cube << " to worker " << p << endl;
                        next_cube++;
                    }
                }

            }
            else if (result == 10 && !optimizing) {
                // One worker found SAT: stop all others
                sat_found = true;
                cout << "SAT found by worker " << worker << endl;

                int stop = 1;
                for (int p = 1; p < size; ++p) {
                    if (p != worker) {
                        MPI_Send(&stop, 1, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
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
                    if (header[0] > 0) {
                        MPI_Send(cubes[next_cube].lits.data(), header[0], MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
                        cube_of_worker[worker] = next_cube;
                        cube_depth_of_worker[worker] = cubes[next_cube].split_depth;
                        time_of_worker[worker] = clock();
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
        for (int p = 1; p < size; ++p) {
            int stop = 1;
            MPI_Send(&stop, 1, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
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
        clock_t beginTime = clock();
        Solver baseSolver(nVars, beginTime);
        baseSolver.setBT0(true);

        // All candidate variables for branching
        vector<int> allVars(nVars);
        for (int varNum = 1; varNum <= nVars; ++varNum) {
            baseSolver.addVarName(varNum, parser.var2string(varNum));
            allVars[varNum - 1] = varNum;
        }

        add_base_problem(baseSolver, problem);

        int worker_id = rank - 1;
        int num_workers = size - 1;

        vector<vector<int>> cubes = generate_cubes(baseSolver, allVars, nVars, true, worker_id, num_workers, optimizing);

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
        int global_has_best_cost = false;
        if(optimizing){
            MPI_Recv(&global_has_best_cost, 1, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if(global_has_best_cost)MPI_Recv(&global_best_cost, 1, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        global_stop_flag = 0;
        split_requested_flag = 0;
        current_split_time_limit = compute_split_time_limit(split_depth);
        worker_cube_start = std::chrono::steady_clock::now();
        // Solve the assigned cube
        CubeSolveResult cubeRes = solve_cube(problem, parser, nVars, cube, global_best_cost, global_has_best_cost);
        Solver::StatusSolver ans = cubeRes.status;

        // If this worker was interrupted because another process found SAT, exit
        if (global_stop_flag) {
            break;
        }
        if (split_requested_flag) {
            int incumbent_for_split = global_best_cost;
            bool has_incumbent_for_split = global_has_best_cost;
            if(optimizing){
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

            vector<vector<int>> subcubes = split_cube(problem, parser, nVars, cube, optimizing, incumbent_for_split, has_incumbent_for_split);

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
        msg[2] = 0;   // solver.cost_best_solution()

        // Send result back to the master
        if (not optimizing){
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
        }
        else {
            if (ans == Solver::OPTIMUM_FOUND) {
                msg[0] = 10;   // OPTIMUM for this cube
                msg[2] = cubeRes.bestCost;
                MPI_Send(msg, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            } else if (ans == Solver::SOME_SOLUTION_FOUND) {
                
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

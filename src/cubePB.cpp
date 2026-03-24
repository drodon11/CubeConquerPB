#include "Parser.h"
#include "Solver.h"
#include "WConstraint.h"

#include <mpi.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <chrono>
#include <thread>
#include <atomic>

using namespace std;

// MPI message tags
const int TAG_STOP   = 1;
const int TAG_WORK   = 2;
const int TAG_RESULT = 3;

const int DEPTH = 5;

// Global flag used by workers to stop early when another process found SAT
int global_stop_flag = 0;

// Global cutoff threshold used during cube generation
double theta = 1000.0;

// Global flag set when SAT is detected during cube generation
bool cube_sat_found = false;

// Check whether a STOP message has been sent by the master.
// If so, receive it and update the global stop flag.
extern "C" int terminate_cb() {
    MPI_Status status;
    int flag = 0;
    MPI_Iprobe(0, TAG_STOP, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        int dummy;
        MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        global_stop_flag = 1;
    }
    return global_stop_flag;
}

// Recursive cube generation using a lookahead evalvar-style heuristic
void generate_cubes_rec(Solver& solver,
                        const vector<int>& vars,
                        int nVars,
                        vector<int>& current,
                        vector<vector<int>>& cubes) {
    // Stop immediately if SAT was already found in another branch
    if (cube_sat_found) return;

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

    cout << "Start looking for variable" << endl;
    // Evaluate every currently undefined variable
    for (int v : vars) {
        if (!solver.isUndefLit(v)) continue;

        // Try v = true
        bool okPos = solver.assumeAndPropagate(v);
        int eval_pos;
        if (!okPos) {
            // Conflict during lookahead: assign a large score
            eval_pos = nVars + 10;
	    cout << "Conf for var " << v << endl;
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
	    cout << "Conf (neg) for var " << v << endl;
        } else {
            // Number of newly assigned variables caused by propagation
            eval_neg = solver.assignedVars() - baseAssigned;
        }
        solver.backtrack(1);

        // If both branches are impossible, the current node is refuted
        if (!okPos && !okNeg) {
            return;
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

    // PB-SAT case:
    // no undefined variables remain and no conflict was found => SAT globally
    if (bestVar == 0) {
        cube_sat_found = true;
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

    cout << "Choose lit " << firstLit << endl;
    // Explore first branch
    current.push_back(firstLit);
    bool ok = solver.assumeAndPropagate(firstLit);
    if (ok) {
        generate_cubes_rec(solver, vars, nVars, current, cubes);
    } else {
        // If the child node immediately conflicts, decrease the threshold
        theta *= 0.7;
    }
    solver.backtrack(1);
    current.pop_back();

    // Stop before exploring the second branch if SAT was already found
    if (cube_sat_found) return;

    // Explore second branch
    current.push_back(secondLit);
    ok = solver.assumeAndPropagate(secondLit);
    if (ok) {
        generate_cubes_rec(solver, vars, nVars, current, cubes);
    } else {
        // If the child node immediately conflicts, decrease the threshold
        theta *= 0.7;
    }
    solver.backtrack(1);
    current.pop_back();
}

// Wrapper that initializes cube generation from the base solver state
vector<vector<int>> generate_cubes(Solver& solver,
                                   const vector<int>& vars,
                                   int nVars) {
    vector<vector<int>> cubes;
    vector<int> current;

    // Reset global state before generating cubes
    theta = 1000.0;
    cube_sat_found = false;

    generate_cubes_rec(solver, vars, nVars, current, cubes);
    return cubes;
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

// Solve a single cube with a fresh solver instance
Solver::StatusSolver solve_cube(PBProblem& problem,
                                Parser& parser,
                                int nVars,
                                const vector<int>& cube) {
    clock_t beginTime = clock();
    Solver solver(nVars, beginTime);

    solver.setBT0(true);
    solver.set_periodic_function(terminate_cb);

    // Store variable names for output/debugging
    for (int varNum = 1; varNum <= nVars; ++varNum) {
        solver.addVarName(varNum, parser.var2string(varNum));
    }

    // Load the base problem and then the cube assumptions
    add_base_problem(solver, problem);
    add_cube_constraints(solver, cube);

    int tlimit = 0;
    solver.solve(tlimit);
    return solver.currentStatus();
}

vector<int> cube_of_worker;
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
	    else cout << " cube " << format("{:4}", cube_of_worker[w]) << "\t(" << double(clock() - time_of_worker[w])/CLOCKS_PER_SEC << " s.)" << endl;
	  }
	  cout << string(30,'=') << endl;	  
            std::this_thread::sleep_for(std::chrono::seconds(interval));
        }
    });
    t.detach(); // Let it run independently
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

    // Each rank reads the same input file locally
    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".opb") {
        problem = parser.readOPB(filename);
    } else if (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".lp") {
        problem = parser.readLP(filename);
    } else {
        if (rank == 0) cerr << "Unsupported format. Use .opb or .lp" << endl;
        MPI_Finalize();
        return 1;
    }

    // Number of variables in the problem
    int nVars = parser.numVars();

    // -------------------- MASTER --------------------
    if (rank == 0) {

        // Build the base solver used only for cube generation
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

        // Generate cubes using lookahead on the base solver
        vector<vector<int>> cubes = generate_cubes(baseSolver, allVars, nVars);
	total_number_cubes = cubes.size();;
	
        // If SAT was detected during cube generation, stop everything
        if (cube_sat_found) {
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
        int finished_workers = size - 1;
        bool sat_found = false;
        bool unknown_seen = false;
	clock_t last_printed_stats = clock();
	cube_of_worker = vector<int>(size,-1);
	time_of_worker = vector<clock_t>(size);
	std::atomic<bool> keepRunning(true);
	startTimer(keepRunning, 10); // Run every 10 seconds
	
        // Send one initial cube to each worker
        for (int p = 1; p < size; ++p) {
            if (next_cube < (int)cubes.size()) {
                int len = (int)cubes[next_cube].size();
                MPI_Send(&len, 1, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
                if (len > 0) {
		    cube_of_worker[p] = next_cube;
		    time_of_worker[p] = clock();
                    MPI_Send(cubes[next_cube].data(), len, MPI_INT, p, TAG_WORK, MPI_COMM_WORLD);
		    --finished_workers;
                }
                cout << "Sent cube " << next_cube << " to worker " << p << endl;
                next_cube++;
            } else {
                int stop = 1;
                MPI_Send(&stop, 1, MPI_INT, p, TAG_STOP, MPI_COMM_WORLD);
            }
        }

        // Receive results and dynamically send more cubes
        while (!sat_found && finished_workers < size - 1) {

	    int msg[2];
            MPI_Status status;
            MPI_Recv(msg, 2, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);
	    
            int result = msg[0];
            int worker = msg[1];

            if (result == 10) {
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

                // Send another cube to the now free worker
                if (next_cube < (int)cubes.size()) {
                    int len = (int)cubes[next_cube].size();
                    MPI_Send(&len, 1, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
                    if (len > 0) {
                        MPI_Send(cubes[next_cube].data(), len, MPI_INT, worker, TAG_WORK, MPI_COMM_WORLD);
			cube_of_worker[worker] = next_cube;
			time_of_worker[worker] = clock();
                    }
                    cout << "Sent cube " << next_cube << " to worker " << worker << endl;
                    next_cube++;
                } else {
                    // No more cubes left: stop this worker
                    int stop = 1;
		    cube_of_worker[worker] = -1;
                    MPI_Send(&stop, 1, MPI_INT, worker, TAG_STOP, MPI_COMM_WORLD);
                    finished_workers++;
		    cout << "Worker " << worker << " finished" << endl;
                }
            }
        }

        // Final global result
        if (!sat_found) {
            if (unknown_seen) {
                cout << "Global result: UNKNOWN" << endl;
            } else {
                cout << "Global result: UNSAT (all cubes infeasible)" << endl;
            }
        }

	keepRunning = false;	
        MPI_Finalize();
        return 0;
    }

    // -------------------- WORKERS --------------------
    while (true) {
        int len;
        MPI_Status status;

        // Receive either a cube or a stop signal from the master
        MPI_Recv(&len, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_STOP) {
            break;
        }

        // Receive the cube literals
        vector<int> cube(len);
        if (len > 0) {
            MPI_Recv(cube.data(), len, MPI_INT, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        global_stop_flag = 0;

        // Solve the assigned cube
        Solver::StatusSolver ans = solve_cube(problem, parser, nVars, cube);

        // If this worker was interrupted because another process found SAT, exit
        if (global_stop_flag) {
            break;
        }

        int msg[2];
        msg[1] = rank;

        // Send result back to the master
        if (ans == Solver::SOME_SOLUTION_FOUND || ans == Solver::OPTIMUM_FOUND) {
            msg[0] = 10;   // SAT for this cube
            MPI_Send(msg, 2, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            break;
        } else if (ans == Solver::INFEASIBLE) {
            msg[0] = 20;   // UNSAT for this cube
            MPI_Send(msg, 2, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
        } else {
            msg[0] = 0;    // UNKNOWN for this cube
            MPI_Send(msg, 2, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
        }
    }

    MPI_Finalize();
    return 0;
}

#pragma once

#include "Parser.h"
#include "WConstraint.h"
#include "Solver.h"
#include <vector>

// MPI message tags
inline constexpr int TAG_STOP           = 1;
inline constexpr int TAG_WORK           = 2;
inline constexpr int TAG_RESULT         = 3;
inline constexpr int TAG_SPLIT          = 4;
inline constexpr int TAG_INIT_RESULT    = 5;
inline constexpr int TAG_INIT_CUBE      = 6;
inline constexpr int TAG_BEST_REQUEST   = 7;
inline constexpr int TAG_BEST_REPLY     = 8;
inline constexpr int TAG_LEARNED_UPDATE = 9;

struct CubeSolveResult {
    Solver::StatusSolver status;
    bool hasSolution;
    int bestCost;
    std::vector<WConstraint> learned_constraints;
};

// Solver backend interface
class ISolverBackend {
public:
    virtual ~ISolverBackend() = default;

    virtual void addBaseProblem(PBProblem& problem) = 0;
    virtual void addLearnedConstraints(const std::vector<WConstraint>& constraints) = 0;
    virtual void addCube(const std::vector<int>& cube) = 0;
    virtual void addObjective(PBProblem& problem) = 0;
    virtual void addObjectiveBound(PBProblem& problem, int bestCost) = 0;

    virtual int assignedVars() const = 0;
    virtual bool isUndefLit(int lit) const = 0;
    virtual bool assumeAndPropagate(int lit) = 0;
    virtual void backtrack(int levels) = 0;

    virtual std::vector<WConstraint> goodClauses() = 0;

    virtual CubeSolveResult solve(bool optimizing, int timeLimitSeconds) = 0;
};

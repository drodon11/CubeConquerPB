#pragma once

#include "ISolverBackend.h"

// Native solver backend
class NativeBackend : public ISolverBackend {
private:
    Solver solver;

    void addConstraint(WConstraint c, bool isInitial);

public:
    NativeBackend(Parser& parser_, int nVars_, bool sat_);

    void addBaseProblem(PBProblem& problem) override;
    void addLearnedConstraints(const std::vector<WConstraint>& constraints) override;
    void addCube(const std::vector<int>& cube) override;
    void addObjective(PBProblem& problem) override;
    void addObjectiveBound(PBProblem& problem, int bestCost) override;
    void addObjectiveLowerBound(PBProblem& problem, int lb) override;

    int assignedVars() const override;
    bool isUndefLit(int lit) const override;
    bool assumeAndPropagate(int lit) override;
    void backtrack(int levels) override;

    int  nonSatisfiedConstraints ( ) override;
    std::vector<WConstraint> goodClauses() override;
    std::vector<int> getSolution(int nVars) override;
    CubeSolveResult solve(bool optimizing, int timeLimitSeconds) override;
};

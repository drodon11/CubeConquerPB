#pragma once

#include "ISolverBackend.h"
extern "C" {
#include "ipasirpb.h"
}
#include <cstdint>

// RoundingSAT backend
class RoundingSatBackend : public ISolverBackend {
private:
    void* solver;

    void addWConstraint(WConstraint& c);
    void addLinearConstraint(const std::vector<int>& coeffs,
                             const std::vector<int>& varNums,
                             int rhs,
                             bool isGeq);
    ipasirpb_return solve_raw(int seconds);
    int64_t primalBound() const;
    int64_t dualBound() const;
    bool isTrueLit(int lit) const;
    bool isFalseLit(int lit) const;

public:
    explicit RoundingSatBackend(int nVars_);
    ~RoundingSatBackend() override;

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

    int nonSatisfiedConstraints ( ) override;
    std::vector<WConstraint> goodClauses() override;
    std::vector<int> getSolution(int nVars) override;
    CubeSolveResult solve(bool optimizing, int timeLimitSeconds) override;
};

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

    // Keep objective vectors alive while setting objective.
    std::vector<int64_t> objective_coeffs;
    std::vector<int64_t> objective_lits;

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
    RoundingSatBackend();
    ~RoundingSatBackend() override;

    void addBaseProblem(PBProblem& problem) override;
    void addLearnedConstraints(const std::vector<WConstraint>& constraints) override;
    void addCube(const std::vector<int>& cube) override;
    void addObjective(PBProblem& problem) override;
    void addObjectiveBound(PBProblem& problem, int bestCost) override;

    int assignedVars() const override;
    bool isUndefLit(int lit) const override;
    bool assumeAndPropagate(int lit) override;
    void backtrack(int levels) override;

    int nonSatisfiedConstraints ( ) override;
    std::vector<WConstraint> goodClauses() override;
    CubeSolveResult solve(bool optimizing, int timeLimitSeconds) override;
};

#pragma once

#include "ISolverBackend.h"
#include "cadical.hpp"
#include "testing.hpp"
#include "internal.hpp"

// CaDiCaL backend

class CadicalTerminator : public CaDiCaL::Terminator {
public:
    bool terminate() override;
};

class CadicalBackend : public ISolverBackend {
private:
    mutable CaDiCaL::Solver solver;
    CadicalTerminator terminator;
    int nVars;
    bool root_conflict = false;

    void addClauseFromConstraint(WConstraint& c);
    void initialPropagate();
    bool isTrueLit(int lit) const;
    bool isFalseLit(int lit) const;

public:
    explicit CadicalBackend(int nVars_);
    ~CadicalBackend() override;

    void addBaseProblem(PBProblem& problem) override;
    void addLearnedConstraints(const std::vector<WConstraint>& constraints) override;
    void addCube(const std::vector<int>& cube) override;
    void addObjective(PBProblem& problem) override;
    void addObjectiveBound(PBProblem& problem, int bestCost) override;

    int assignedVars() const override;
    bool isUndefLit(int lit) const override;
    bool assumeAndPropagate(int lit) override;
    void backtrack(int levels) override;

    std::vector<WConstraint> goodClauses() override;
    CubeSolveResult solve(bool optimizing, int timeLimitSeconds) override;
};

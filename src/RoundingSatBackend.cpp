#include "RoundingSatBackend.h"

using namespace std;

// Functions defined in cubePB.cpp that RoundingSatBackend needs
bool build_linear_geq_parts(const vector<int>& coeffs,
                             const vector<int>& varNums,
                             int rhs,
                             bool isGeq,
                             vector<int>& coefficients,
                             vector<int>& literals,
                             int& outRhs);

extern "C" int terminate_decision_cb(int LB, int UB);
extern "C" int report_external_UB ( );

RoundingSatBackend::RoundingSatBackend() {
    solver = ipasirpb_init();
}

RoundingSatBackend::~RoundingSatBackend() {
    ipasirpb_release(solver);
}




vector<WConstraint> RoundingSatBackend::goodClauses() {
    const ipasirpb_terms64* clauses = nullptr;
    int64_t count = 0;
    ipasirpb_good_clauses(solver, &clauses, &count);

    vector<WConstraint> result;
    result.reserve(count);
    for (int64_t i = 0; i < count; ++i) {
        const ipasirpb_terms64& t = clauses[i];
        vector<int> coeffs(t.len), lits(t.len);
        for (int64_t j = 0; j < t.len; ++j) {
            coeffs[j] = (int)t.coeffs[j];
            lits[j]   = (int)t.lits[j];
        }
        result.emplace_back(coeffs, lits, (int)t.rhs);
    }
    return result;
}

void RoundingSatBackend::addWConstraint(WConstraint& c) {
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

void RoundingSatBackend::addBaseProblem(PBProblem& problem) {
    for (int i = 0; i < (int)problem.constraints.size(); ++i) {
        addWConstraint(problem.constraints[i]);
    }
}

void RoundingSatBackend::addLearnedConstraints(const vector<WConstraint>& constraints) {
    for (WConstraint c : constraints) {
        addWConstraint(c);
    }
}

void RoundingSatBackend::addCube(const vector<int>& cube) {
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

void RoundingSatBackend::addObjective(PBProblem& problem) {
    if (problem.objCoeffs.empty()) return;

    std::vector<int64_t> objective_coeffs;
    std::vector<int64_t> objective_lits;

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

void RoundingSatBackend::addLinearConstraint(const vector<int>& coeffs,
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

void RoundingSatBackend::addObjectiveBound(PBProblem& problem, int bestCost) {
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

void RoundingSatBackend::addObjectiveLowerBound(PBProblem& problem, int lb) {
    // Only for minimization
    if (problem.objCoeffs.empty() || !problem.minimizing) return;

    addLinearConstraint(
        problem.objCoeffs,
        problem.objVars,
        lb,
        true    // obj >= lb
    );
}

int RoundingSatBackend::assignedVars() const {
    int n = 0;
    ipasirpb_assignedVars(solver, &n);
    return n;
}

bool RoundingSatBackend::isTrueLit(int lit) const {
    bool v = false;
    ipasirpb_is_true_lit(solver, lit, &v);
    return v;
}

bool RoundingSatBackend::isFalseLit(int lit) const {
    bool v = false;
    ipasirpb_is_false_lit(solver, lit, &v);
    return v;
}

bool RoundingSatBackend::isUndefLit(int lit) const {
    return !isTrueLit(lit) && !isFalseLit(lit);
}

bool RoundingSatBackend::assumeAndPropagate(int lit) {
    bool conflict = false;
    ipasirpb_assume_and_propagate(solver, lit, &conflict);
    return !conflict;
}

void RoundingSatBackend::backtrack(int levels) {
    ipasirpb_backjump(solver, levels);
}

ipasirpb_return RoundingSatBackend::solve_raw(int seconds) {
    ipasirpb_set_periodic_function(solver, terminate_decision_cb);

    return ipasirpb_solve(solver, nullptr, 0, seconds);
}

int64_t RoundingSatBackend::primalBound() const {
    int64_t value = 0;
    ipasirpb_get_primal_bound64(solver, &value);
    return value;
}

int64_t RoundingSatBackend::dualBound() const {
    int64_t value = 0;
    ipasirpb_get_dual_bound64(solver, &value);
    return value;
}

CubeSolveResult RoundingSatBackend::solve(bool optimizing, int timeLimitSeconds) {
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

int RoundingSatBackend::nonSatisfiedConstraints ( ) {
  int n;
  ipasirpb_number_non_satisfied_constraints(solver, &n);
  return n;
}

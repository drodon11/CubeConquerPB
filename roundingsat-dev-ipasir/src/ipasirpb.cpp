/***********************************************************************
Copyright (c) 2014-2020, Jan Elffers
Copyright (c) 2019-2021, Jo Devriendt
Copyright (c) 2020-2021, Stephan Gocht
Copyright (c) 2014-2024, Jakob Nordström
Copyright (c) 2022-2024, Andy Oertel
Copyright (c) 2024, Marc Vinyals

Parts of the code were copied or adapted from MiniSat.

MiniSat -- Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
           Copyright (c) 2007-2010  Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***********************************************************************/

#include "ipasirpb.h"

#include "optimize.hpp"
#include "run.hpp"
#include "ConstrExp.hpp"
#include "ConstrExpPool.hpp"
#include "Solver.hpp"
#include "Env.hpp"
#include <vector>
#include <span>

namespace rs {
struct ipasir_solver {
  std::unique_ptr<Env> env;
  rs::Solver* solver;
  Ce64 input;
  Ce64 objective;
  // We add assumptions just before solving
  IntSet assumptions;
  // Track if we got unsat with no assumptions
  bool unsat;
  int64_t obj_primal;
  int64_t obj_dual;
  std::vector<CeSuper> cores;
  std::vector<int64_t> core_coeffs;
  std::vector<int64_t> core_vars;
  size_t next_core = 0;

  ipasir_solver() {
    env = std::make_unique<Env>();
    env->cePools = std::make_unique<ConstrExpPools>(*env);
    env->solver = std::make_unique<Solver>(*env);
    solver = env->solver.get();
    input = env->cePools->take64();
    objective = nullptr;
    unsat = false;
  }
  ~ipasir_solver() {}

  void addInput() {
    if (unsat) return;
    auto [id, idpost] = solver->addConstraint(input, Origin::FORMULA);
    unsat = (idpost == ID_Unsat);
  }
  void add(int64_t coeff, int lit) {
    solver->setNbVars(std::abs(lit), true);
    input->addLhs(coeff, lit);
  }
  void finalize(enum ipasirpb_relation rel, uint64_t rhs) {
    if (rel == IPASIRPB_MIN) {
      objective = env->cePools->take64();
      input->copyTo(objective);
    }
    else {
      input->addRhs(rhs);
      if (rel == IPASIRPB_EQ or rel == IPASIRPB_GEQ) {
        addInput();
      }
      if (rel == IPASIRPB_EQ or rel == IPASIRPB_LEQ) {
        input->invert();
        addInput();
      }
    }
    input->reset();
  }
  void add64(ipasirpb_terms64 constr) {
    for (int64_t i=0; i<constr.len; ++i) {
      add(constr.coeffs[i], constr.lits[i]);
    }
    finalize(constr.rel, constr.rhs);
  }
  void assume(int lit) {
    assumptions.add(lit);
  }
  ipasirpb_return decide(int lit) {
    solver->decide(lit);
    return IPASIRPB_OK;
  }
  ipasirpb_return propagate(bool* conflict ){
    CeSuper tmp = solver->runPropagation(true);
    if (tmp) *conflict = true;
    else *conflict = false;
    return IPASIRPB_OK;
  }
  ipasirpb_return assignedVars(int *n){
    *n = solver->assignedVars();
    return IPASIRPB_OK;
  }
  ipasirpb_return is_true_lit(int lit, bool *v) {
    *v = solver->isTrueX(lit);
    return IPASIRPB_OK;
  }
  ipasirpb_return is_false_lit(int lit, bool *v) {
    *v = solver->isFalseX(lit);    
    return IPASIRPB_OK;
  }
  ipasirpb_return backjump(int levels){
    int current = solver->decisionLevel();
    int goal = current-levels;
    solver->backjumpTo(goal);
    return IPASIRPB_OK;
  }
  ipasirpb_return assume_and_propagate(int lit, bool* conflict) {
    solver->decide(lit);
    CeSuper tmp = solver->runPropagation(true);
    if (tmp) *conflict = true;
    else *conflict = false;
    return IPASIRPB_OK;    
  }
  ipasirpb_return decideX(int seconds) {
    cores.clear();
    next_core = 0;
    solver->setAssumptions(assumptions);
    solver->setTimeLimit(seconds);
    SolveAnswer answer = solver->solve();
    SolveState state = answer.state;

    // switch(state) {
    // case SolveState::SOLVING: {std::cout << "SOLVING" << std::endl;break;}
    // case SolveState::SAT: {std::cout << "SAT" << std::endl;break;}
    // case SolveState::UNSAT: {std::cout << "UNSAT" << std::endl;break;}
    // case SolveState::INCONSISTENT: {std::cout << "INCONSISTENT" << std::endl;break;}
    // case SolveState::INPROCESSED: {std::cout << "INPROCESSED" << std::endl;break;}
    // case SolveState::RESTARTED: {std::cout << "RESTARTED" << std::endl;break;}
    // case SolveState::INTERRUPTED: {std::cout << "INTERRUPTED" << std::endl;break;}
    // }    

    switch(state) {
    case SolveState::SAT: return IPASIRPB_SAT;
    case SolveState::UNSAT: return IPASIRPB_UNSAT;
    case SolveState::INCONSISTENT:
      cores = answer.cores;
      return IPASIRPB_INCONSISTENT;
    }
    return IPASIRPB_UNKNOWN;
  }
  ipasirpb_return set_periodic_function(int (*f) (int x)) {
    solver->set_periodic_function(f);
    return IPASIRPB_UNKNOWN;
  }
  ipasirpb_return optimize(int seconds) {
    // We ignore user assumptions for now
    objective->removeUnitsAndZeroes(solver->getLevel(), solver->getPos(), false);
    Optimization<long long, int128> optim(*env, objective);
    solver->setTimeLimit(seconds);
    OptState state = optim.optimize();
    obj_primal = static_cast<int64_t>(optim.getUpperBound());
    obj_dual = static_cast<int64_t>(optim.getLowerBound());
    switch(state) {
    case OptState::SAT: return IPASIRPB_SAT;
    case OptState::UNSAT: return IPASIRPB_UNSAT;
    case OptState::OPT: return IPASIRPB_OPT;
    }
    return IPASIRPB_UNKNOWN;
  }
  ipasirpb_return solve(std::span<int64_t> assumptions,int seconds) {
    this->assumptions.clear();
    for (int64_t lit : assumptions)
      assume(lit);
    if (unsat) return IPASIRPB_UNSAT;
    if (objective) {
      return optimize(seconds);
    }
    else return decideX(seconds);
  }
  bool val(int lit) {
    return solver->lastSol[lit] > 0;
  }
  ipasirpb_return get_core(ipasirpb_terms64* core) {
    if (next_core>=cores.size()) return IPASIRPB_NULL;
    const auto& this_core = cores[next_core++];
    if (!this_core->largestCoefFitsIn(63)) return IPASIRPB_OVERFLOW;
    if (!this_core->degreeFitsIn(63)) return IPASIRPB_OVERFLOW;
    Ce64 core64 = env->cePools->take64();
    this_core->copyTo(core64);
    core_coeffs.resize(core64->vars.size());
    core_vars.resize(core64->vars.size());
    for (size_t i = 0; i < core64->vars.size(); ++i) {
      Var v = core64->vars[i];
      core_vars[i] = v;
      core_coeffs[i] = core64->coefs[v];
    }
    core->coeffs = core_coeffs.data();
    core->lits = core_vars.data();
    core->len = core64->vars.size();
    core->rel = IPASIRPB_GEQ;
    core->rhs = core64->rhs;
    return IPASIRPB_OK;
  }
};
}

const char * ipasirpb_signature () {
  return "roundingsat2";
}

void * ipasirpb_init () {
  return new rs::ipasir_solver();
}

void ipasirpb_release (void * solver) {
  delete static_cast<rs::ipasir_solver*>(solver);
}

ipasirpb_return ipasirpb_add64 (void * solver, ipasirpb_terms64 constr) {
  static_cast<rs::ipasir_solver*>(solver)->add64(constr);
  return IPASIRPB_OK;
}
ipasirpb_return ipasirpb_litval (void * solver, int64_t lit, bool* value) {
  *value = static_cast<rs::ipasir_solver*>(solver)->val(lit);
  return IPASIRPB_OK;
}
ipasirpb_return ipasirpb_get_core64 (void * solver, ipasirpb_terms64* core) {
  return static_cast<rs::ipasir_solver*>(solver)->get_core(core);
}
ipasirpb_return ipasirpb_set_obj64 (void * solver, ipasirpb_terms64 obj) {
  static_cast<rs::ipasir_solver*>(solver)->add64(obj);
  return IPASIRPB_OK;
}
ipasirpb_return ipasirpb_get_primal_bound64 (void * solver, int64_t* value) {
  *value = static_cast<rs::ipasir_solver*>(solver)->obj_primal;
  return IPASIRPB_OK;
}
ipasirpb_return ipasirpb_get_dual_bound64 (void * solver, int64_t* value) {
  *value = static_cast<rs::ipasir_solver*>(solver)->obj_dual;
  return IPASIRPB_OK;
}

ipasirpb_return ipasirpb_solve (void * solver, int64_t* assumptions, int64_t len, int seconds) {
  return static_cast<rs::ipasir_solver*>(solver)->solve({assumptions, len},seconds);
}

ipasirpb_return ipasirpb_decide(void * solver, int lit) {
  return static_cast<rs::ipasir_solver*>(solver)->decide(lit);
}

ipasirpb_return ipasirpb_propagate(void * solver, bool* conflict) {
  return static_cast<rs::ipasir_solver*>(solver)->propagate(conflict);
}

ipasirpb_return ipasirpb_assignedVars(void * solver, int* n) {
  return static_cast<rs::ipasir_solver*>(solver)->assignedVars(n);
}

ipasirpb_return ipasirpb_is_true_lit(void * solver, int lit, bool* v) {
  return static_cast<rs::ipasir_solver*>(solver)->is_true_lit(lit,v);
}

ipasirpb_return ipasirpb_is_false_lit(void * solver, int lit, bool* v) {
  return static_cast<rs::ipasir_solver*>(solver)->is_false_lit(lit,v);
}

ipasirpb_return ipasirpb_backjump(void * solver, int levels) {
  return static_cast<rs::ipasir_solver*>(solver)->backjump(levels);
}

ipasirpb_return ipasirpb_assume_and_propagate(void * solver, int lit, bool* conflict) {
  return static_cast<rs::ipasir_solver*>(solver)->assume_and_propagate(lit,conflict);
}


ipasirpb_return ipasirpb_set_periodic_function(void * solver, int (*f) (int x) ) {
  return static_cast<rs::ipasir_solver*>(solver)->set_periodic_function(f);
}

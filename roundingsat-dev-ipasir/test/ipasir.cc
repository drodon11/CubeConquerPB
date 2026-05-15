// g++ -g -std=c++20 -o ipasir -L../build_debug/ -Wl,-rpath=../build_debug/ -lrsipasir ipasir.cc

#include "../src/ipasirpb.h"
#include <iostream>
#include <vector>

void test_inconsistent_eqs() {
  void* solver = ipasirpb_init();
  std::vector<int64_t> coeffs;
  std::vector<int64_t> lits;
  {
    coeffs = {1,1,1,1,1};
    lits = {1,2,3,4,5};
    ipasirpb_add64(solver, {coeffs.data(), lits.data(), 5, IPASIRPB_EQ, 2});
  }
  std::cout << ipasirpb_solve(solver, NULL, 0) << std::endl;
  for (int i=1; i<=5; ++i) {
    bool ret;
    ipasirpb_litval(solver, i, &ret);
    std::cout << ret << std::endl;
  }
  {
    coeffs = {1,1,1,1,1};
    lits = {1,2,3,4,5};
    ipasirpb_add64(solver, {coeffs.data(), lits.data(), 5, IPASIRPB_EQ, 3});
  }
  std::cout << ipasirpb_solve(solver, NULL, 0) << std::endl;
  ipasirpb_release(solver);
}

void test_unit() {
  void* solver = ipasirpb_init();
  std::vector<int64_t> coeffs;
  std::vector<int64_t> lits;
  {
    coeffs = {1};
    lits = {1};
    ipasirpb_add64(solver, {coeffs.data(), lits.data(), 1, IPASIRPB_EQ, 1});
  }
  bool ret;
  std::cout << ipasirpb_solve(solver, NULL, 0) << std::endl;
  ipasirpb_litval(solver, 1, &ret);
  std::cout << ret << std::endl;
  {
    coeffs = {1};
    lits = {1};
    ipasirpb_add64(solver, {coeffs.data(), lits.data(), 1, IPASIRPB_EQ, 0});
  }
  std::cout << ipasirpb_solve(solver, NULL, 0) << std::endl;
  ipasirpb_litval(solver, 1, &ret);
  std::cout << ret << std::endl;
  ipasirpb_release(solver);
}

void test_opt() {
  void* solver = ipasirpb_init();
  std::vector<int64_t> coeffs;
  std::vector<int64_t> lits;
  {
    coeffs = {1,1,1,1};
    lits = {1,2,3,4};
    ipasirpb_add64(solver, {coeffs.data(), lits.data(), 4, IPASIRPB_GEQ, 3});
  }
  {
    coeffs = {1,1,1};
    lits = {1,2,3};
    ipasirpb_set_obj64(solver, {coeffs.data(), lits.data(), 3, IPASIRPB_MIN, 0});
  }
  std::cout << ipasirpb_solve(solver, NULL, 0) << std::endl;
  int64_t opt;
  ipasirpb_get_primal_bound64(solver, &opt);
  std::cout << opt << std::endl;
  ipasirpb_release(solver);
}

void test_assumptions() {
  void* solver = ipasirpb_init();
  std::vector<int64_t> coeffs;
  std::vector<int64_t> lits;
  {
    coeffs = {1,1,1,1};
    lits = {1,2,3,4};
    ipasirpb_add64(solver, {coeffs.data(), lits.data(), 4, IPASIRPB_GEQ, 3});
  }
  std::vector<int64_t> assumptions;
  assumptions = {-1, -2, -3};
  std::cout << ipasirpb_solve(solver, assumptions.data(), 3) << std::endl;
  ipasirpb_terms64 core;
  ipasirpb_return err = ipasirpb_get_core64(solver, &core);
  if (err) {
    std::cout << "???" << err << std::endl;
    return;
  }
  for (int i = 0; i < core.len; ++i) {
    std::cout << core.coeffs[i] << " x" << core.lits[i] << "  ";
  }
  std::cout << ">=  " << core.rhs << std::endl;
}

int main() {
  std::cout << ipasirpb_signature() << std::endl;
  //test_inconsistent_eqs();
  //test_unit();
  //test_opt();
  test_assumptions();
}

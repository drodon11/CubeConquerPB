#pragma once

#include <vector>
#include "Functions.h"

using namespace std;

template <class Constraint>
int maxCoefOfConstraint(const Constraint& cons) {
  int max = 0;
  for (int i = 0; i < cons.getSize(); ++i) { 
    int coef = cons.getIthCoefficient(i);
    if ( max < coef ) max=coef;
  }
  return max;
}

template <class Constraint>
void simplify(Constraint& cons) {
  assert (cons.getSize() > 0);
  int c = cons.getConstant();
  
  int gcdV = 0; // compute gcd of coeffs < c, normalize coeffs > c
  for (int i = 0; i < cons.getSize(); ++i) {
    int coef = cons.getIthCoefficient(i);
    if (coef < c) 
      gcdV = GCD(gcdV, coef);
    else if (coef > c)
      cons.setIthCoefficient(i, c);
  }
  if (gcdV == 0) gcdV = c; // in this case all coefs are larger than or equal to the constant c

  cons.setConstant(divisionRoundedUp( c, gcdV ));
  for (int i = 0; i < cons.getSize(); ++i) 
    cons.setIthCoefficient(i, divisionRoundedUp(cons.getIthCoefficient(i), gcdV));
}



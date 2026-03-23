#include "WConstraint.h"
#include "Functions.h"


ostream& operator<<(ostream& os, const WConstraint& wc) {
  for (int i = 0; i < wc.getSize(); ++i) {
    int coeff = wc.getIthCoefficient(i);
    int lit = wc.getIthLiteral(i);

    if (lit < 0) os << "+ " << coeff << "*-x" << abs(lit) << " ";
    else         os << "+ " << coeff << "*x"  << abs(lit) << " ";
  }
  os << "  >= " << wc.getConstant();
  return os;
}

void WConstraint::sortByModel (const Model& m) {
  sort(lhs.begin(), lhs.end(),
       [&](const pair<int,int>& m1, const pair<int,int>& m2) {
	 int h1 = m.getHeightOfVar(abs(m1.second));
	 int h2 = m.getHeightOfVar(abs(m2.second));
	 return h1 < h2;
       });  
}

void WConstraint::sortByFalseCoefficient(const Model& m) {
    sort(lhs.begin(), lhs.end(),
            [&](const pair<int,int>& m1, const pair<int,int>& m2) {
            if (m.isFalseLit(m1.second) != m.isFalseLit(m2.second)) return m.isFalseLit(m1.second);
            return m1.first > m2.first;
            });  
}

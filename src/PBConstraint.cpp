#include "Functions.h"
#include "PBConstraint.h"
using namespace std;

ostream& operator<<(ostream& os, const PBConstraint& pc) {
  for (int i = 0; i < pc.getSize(); ++i) {
    int coeff = pc.getIthCoefficient(i);
    int lit = pc.getIthLiteral(i);

    if (lit < 0) os << "+ " << coeff << "*-x" << abs(lit) << " ";
    else         os << "+ " << coeff << "*x"  << abs(lit) << " ";
  }
  os << "  >= " << pc.getConstant();

  os << "  [act = " << pc.getActivity() << ", isInitial " << pc.isInitial() << "]";
  return os;
}

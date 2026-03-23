#pragma once

#include <cassert>
#include <iostream>
#include <cmath>
#include <stdint.h>

#include "Clause.h" 

using namespace std;

class Reason{
  uint64_t reason; // Albert-Ignasi: try with two 32-bit integers, one for type and one for content

public:
  typedef enum {
    NO_REASON,  //                         (all is zero    )
    PB_CONSTRAINT, // add constraint num   (last two bits 1)
    CLAUSE,     // add clause num       (last two bits 2)
    BIN_CLAUSE,  // add other lit in clause (last two bits 3)
                // lit 2 --> stored as 4; lit -2 --> stored as 5, lit 3 --> as 6, lit -3 --> as 7, ...
    CARDINALITY // add cardinality num    (last two bits 4)
  } ReasonType;

  inline Reason();                        // no reason (decisions or unit clauses)
  inline Reason(int r, ReasonType type);  // theory-props (any number n will do)
  inline friend Reason noReason();

  inline bool isUnitOrDecision() const {return reason   == 0;}
  inline bool isConstraint()     const {return reason%8 == 1;}
  inline bool isClause()         const {return reason%8 == 2;}
  inline bool isBinClause()      const {return reason%8 == 3;}
  inline bool isCardinality()    const {return reason%8 == 4;}

  inline int  getCtrId() const {return int(reason >> 3);}
  inline int  getClauseNum()     const {return int(reason >> 3);}
  inline int  getOtherBinLit()   const {return int((reason>>3)%2 == 0)?(reason>>4):(-(reason>>4));}
  inline int  getCardinalityNum() const {return int(reason >> 3);}

  inline friend ostream& operator<<(ostream& os, const Reason& c);
  inline bool operator==(const Reason& other) const {return reason == other.reason; }
};

inline Reason noReason() { return Reason(0,Reason::NO_REASON); }

inline Reason::Reason () : reason(0) {}

inline Reason::Reason (int r, ReasonType type) {
  switch (type) {
    case NO_REASON:
      reason = 0;
      break;
    case PB_CONSTRAINT:
      assert(r >= 0);
      reason = 8*r + 1;
      assert(isConstraint());
      assert(getCtrId() == r);
      break;
    case CLAUSE:
      reason = 8*r + 2;
      assert(isClause());
      assert(getClauseNum() == r);  
      break;
    case BIN_CLAUSE:
      reason = 8*(2*abs(r)+(r>0?0:1)) + 3;
      assert(isBinClause());
      assert(getOtherBinLit() == r);
      break;
    case CARDINALITY:
      assert(r >= 0);
      reason = 8*r + 4;
      assert(isCardinality());
      assert(getCardinalityNum() == r);
      break;
  }
}

inline ostream& operator<<(ostream& os, const Reason& c) {
  if (c.isUnitOrDecision())  os << "noReason";
  else if (c.isConstraint()) os << "constraintNum " << c.getCtrId();
  else if (c.isCardinality()) os << "cardinalityNum " << c.getCardinalityNum();
  else if (c.isClause())     os << "clauseNum " << c.getClauseNum();
  else if (c.isBinClause())  os << "binClause " << c.getOtherBinLit();
  return os;
}


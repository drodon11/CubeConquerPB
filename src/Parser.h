#pragma once

#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <map>
#include <limits.h>

#include "WConstraint.h"

using namespace std;

class PBProblem {
public:
  vector<WConstraint> constraints; // in normalized form, i.e. positive coeff >= rhs (but might have repeated/opposite lits)
  bool minimizing; // Original problem was minimizing or not
  vector<int> objCoeffs; // The objective function as expressed in the input file
  vector<int> objVars; // they are vars, not lits

  void addConstraint (const vector<int>& coefficients, const vector<int>& literals, int rhs, bool isGeq);
  void writeObjectiveFunction (); // debugging mostly
};

class Parser {

  // Mapping between strings of original variables and internal variables
  map<string, int> variables;
  vector<string> varNames; // name for varNum 0, which is a non-used varNum
  int nextVarID;

  // Temporary data structures for parsing
  vector<int> varNums, coeffs;
  vector<string> words; 
  string tmp;  
  int wordsIndex;
  string varString;
  int coef, sign;  

  vector<string> split (const string& s) const;
  void getMonomialOPB ( ); // updates coef, varString, wordsIndex
  void getMonomialLP ( ); // updates coef, varString, wordsIndex

  // Mapping strings original variables <--> internal variables
  int getVarNum(const string& varStr);

  
public:
  PBProblem readOPB ( const string& filename);
  PBProblem readLP  ( const string& filename);
  int numVars ( );

  // The following two assume that "s" is an external variable name and "v" is an internal variable id
  int string2Var (const string& s); 
  string var2string (int v);
  
  Parser();
};


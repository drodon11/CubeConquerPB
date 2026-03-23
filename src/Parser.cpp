#include "Parser.h"

void PBProblem::addConstraint (const vector<int>& coeffs, const vector<int>& varNums, int rhs, bool isGeq) {
  static vector<int> coefficients, literals;
  coefficients.clear(); literals.clear();
  
  if (not isGeq) rhs = -rhs;
  for (int i=0; i < (int)coeffs.size(); ++i) {
    int coef = coeffs[i];    
    if (not isGeq) coef = -coef;
    int finalCoef = abs(coef);
    int lit  = varNums[i];
    if (coef < 0) { rhs += finalCoef; lit = -lit; }
    if (coef != 0) {
      coefficients.push_back(finalCoef);
      literals.push_back(lit);
    }
  }

  // We are not doing shaving/saturation here. Should we?
  if (rhs >= 1)
    constraints.push_back(WConstraint(coefficients,literals,rhs));
}

void PBProblem::writeObjectiveFunction (){ // debugging mostly
  cout << (minimizing ? "Minimize" : "Maximize");
  for (uint i = 0; i < objCoeffs.size(); ++i)
    cout << " " << objCoeffs[i] << "* v" << objVars[i];
  cout << ";" << endl;
}

Parser::Parser ( ):
  varNames(1),
  nextVarID(1) { }

int Parser::numVars ( ) {
  return varNames.size() - 1; // minus 1 because varNum 0 is not used
}

// The following two assume that "s" is an external variable name and "v" is an internal variable id
int Parser::string2Var (const string& s) {
  assert(variables.count(s) > 0);
  return variables[s];
}

string Parser::var2string (int v) {
  assert(v < int(varNames.size()));
  return varNames[v];
}

vector<string> Parser::split (const string& s) const {
  vector<string> vec;
  istringstream in(s);
  string str;
  while (in >> str)
    vec.push_back(str);
  return vec;
}

void Parser::getMonomialOPB ( ) {
  string s = words[wordsIndex];
  assert(s.size() >= 2);
  
  long long int c = stoll(s.substr(1, s.length()-1), nullptr, 10);
  
  if (abs(c) >= INT_MAX) {cout << "input coef " << c << " is >= INT_MAX " << INT_MAX << ", int64? " << (abs(c) >= LLONG_MAX) << endl << endl; assert(false); exit(0);}
  
  coef = stoi(s.substr(1, s.length()-1));
  
  if      (s.substr(0,1) == "-") coef = -coef;
  else assert(s.substr(0,1) == "+");
  varString = words[wordsIndex+1];
  wordsIndex += 2;
}

void Parser::getMonomialLP ( ){ // gets coef and varString from words and advances wordsIndex
  if      (words[wordsIndex] == "-") { sign = -1; wordsIndex++; }
  else if (words[wordsIndex] == "+") { sign =  1; wordsIndex++; }
  else     sign = 1;  // words[wordsIndex] is coeff of varname (no + or - sign; e.g., the first monomial)               
  if (wordsIndex+1 >= (int)words.size()   or words[wordsIndex+1] == "="
      or words[wordsIndex+1] == "<=" or words[wordsIndex+1] == ">=" 
      or words[wordsIndex+1] == "<"  or words[wordsIndex+1] == ">" 
      or words[wordsIndex+1] == "+"  or words[wordsIndex+1] == "-") {
    varString = words[wordsIndex];  // there is no coefficient and words[wordsIndex] is a variable name
    coef = sign;
    wordsIndex += 1;
  } else {  // words[wordsIndex] is a coefficient and words[wordsIndex+1] is the variable name
    varString = words[wordsIndex+1];
    coef = stoi(words[wordsIndex]) * sign;
    wordsIndex += 2;
  }
}

int Parser::getVarNum(const string& varStr) {
  int& n = variables[varStr];
  if (n == 0) {
    n = varNames.size();
    varNames.push_back(varStr);
  }
  return n;
}

PBProblem Parser::readOPB (const string& filename ){
  cout << endl << "reading input file " << filename << endl;
  fstream input(filename.c_str(), fstream::in);
  if (not input) {cout << "Input file " << filename << " does not exist" << endl; exit(1);}


  PBProblem pbm;
  int varNum=0;
  while (!input.eof()) { // treat one line of input
    getline(input,tmp);
    if (tmp[0] == '*') continue;
    if (tmp[0] == '\\') continue;
    if (tmp == "") continue; 
    words = split(tmp);  
    wordsIndex = 0;
    if (words[0] == "min:") {  
      pbm.minimizing = true;
      assert(tmp.substr(tmp.size()-1) == ";");
      wordsIndex++;
      while (wordsIndex < (int)words.size()) { // keep adding monomials to the objective polynomial
        getMonomialOPB();
        pbm.objCoeffs.push_back(coef);
        pbm.objVars.push_back(getVarNum(varString));
        if(words[wordsIndex] == ";") break;
      }
    }
    else if (words[0] == "max:") { // To be completed????
      cout << "objective is to maximize..." << endl;
      exit(0);
    }
    else {
      int rhs;
      assert(words[0].substr(words[0].size()-1) != ":");
      
      while (wordsIndex < (int)words.size()) {
        
        if (words[wordsIndex] == "<" or words[wordsIndex] == ">") 
          { cout << "Error: strict inequality constraint " << tmp << endl; assert(false); exit(0); }

        if (words[wordsIndex] == ">=") {
          rhs = stoi(words[wordsIndex+1]);
          pbm.addConstraint(coeffs, varNums, rhs, true);
          coeffs.clear(); varNums.clear();
          wordsIndex += 2;
          assert(words[wordsIndex] == ";");
          ++wordsIndex; assert(wordsIndex == (int)words.size());
          continue;
        }
        if (words[wordsIndex] == "<=") {
          rhs = stoi(words[wordsIndex+1]);
          pbm.addConstraint(coeffs, varNums, rhs, false);
          coeffs.clear(); varNums.clear();
          wordsIndex += 2;
          assert(words[wordsIndex] == ";");
          ++wordsIndex; assert(wordsIndex == (int)words.size());
          continue;
        }
        if(words[wordsIndex] == "=") {
          rhs = stoi(words[wordsIndex+1]);
          pbm.addConstraint(coeffs, varNums, rhs, true);
          pbm.addConstraint(coeffs, varNums, rhs, false);
          coeffs.clear(); varNums.clear();
          wordsIndex += 2;
          assert(words[wordsIndex] == ";");
          ++wordsIndex; assert(wordsIndex == (int)words.size());
          continue;
        }
        getMonomialOPB();
        varNum = getVarNum(varString);
        if (coef == 0) { cout << "Error: zero or non-integer coefficient: " << tmp << endl; assert(false); exit(0);}
        coeffs.push_back(coef);
        varNums.push_back(varNum);
      }
    }
  }
  input.close();
  return pbm;
}


PBProblem Parser::readLP (const string& filename ){
  cout << endl << "reading input file " << filename << endl;
  fstream input(filename.c_str(), fstream::in);
  if (not input) {cout << "Input file " << filename << " does not exist" << endl; exit(1);}

  PBProblem pbm;
  int varNum=0;
  enum Modes { OBJECTIVE, CONSTRAINTS, BOUNDS, GENERALS, BINARIES, END  };
  Modes mode=END;
  while (!input.eof()) { // treat one line of input
    getline(input,tmp);
    words = split(tmp);  
    wordsIndex = 0;
    if (tmp[0] == '*') continue;
    if (tmp[0] == '\\') continue;
    if (tmp == "") continue;
    if (tmp == "Minimize")   { mode = OBJECTIVE;   pbm.minimizing=true;  continue; }
    if (tmp == "Maximize")   { mode = OBJECTIVE;   pbm.minimizing=false; continue; }
    if (tmp == "Subject To") { mode = CONSTRAINTS; continue; }
    if (tmp == "Bounds")     { mode = BOUNDS;      continue; }
    if (tmp == "Binaries")   { mode = BINARIES;    continue; }
    if (tmp == "Generals")   { mode = GENERALS;    continue; }
    if (tmp == "End")        { mode = END;         continue; }
    if ( mode==BOUNDS or mode==BINARIES or mode==GENERALS or mode==END ) break; // exit while !input.eof()) loop
    if ( mode==OBJECTIVE ) {
      if (words.size()!=0) {
        if (words[0].substr(words[0].size()-1) == ":") wordsIndex++; // ignore label
        while (wordsIndex < (int)words.size()) { // keep adding monomials to the objective polynomial
          getMonomialLP();
	  pbm.objCoeffs.push_back(coef); 
          pbm.objVars.push_back(getVarNum(varString));
        }
      }
    }

    if (mode==CONSTRAINTS) {
      int rhs;
      
      while (wordsIndex < (int)words.size()) {
        if (words[wordsIndex].substr(words[wordsIndex].size()-1) == ":") ++wordsIndex;
        if (words[wordsIndex] == "<" or words[wordsIndex] == ">") 
          { cout << "Error: strict inequality constraint " << tmp << endl; assert(false); }
	
        if (words[wordsIndex] == ">=") {
          rhs = stoi(words[wordsIndex+1]);
          pbm.addConstraint(coeffs, varNums, rhs, true);
          coeffs.clear(); varNums.clear();
          wordsIndex += 2;
          continue;
        }
        if (words[wordsIndex] == "<=") {
          rhs = stoi(words[wordsIndex+1]);
          pbm.addConstraint(coeffs, varNums, rhs, false);
          coeffs.clear(); varNums.clear();
          wordsIndex += 2;
          continue;
        }
        if (words[wordsIndex] == "=") {
          rhs = stoi(words[wordsIndex+1]);
          pbm.addConstraint(coeffs, varNums, rhs, true);
          pbm.addConstraint(coeffs, varNums, rhs, false);
          coeffs.clear(); varNums.clear();
          wordsIndex += 2;
          continue;
        }
        getMonomialLP();
        varNum = getVarNum(varString); 
        if (coef == 0) { cout << "Error: zero or non-integer coefficient: " << tmp << endl; assert(false); }
        coeffs.push_back(coef);
        varNums.push_back(varNum);
      }
    }
  }
  input.close();
  return pbm;
}

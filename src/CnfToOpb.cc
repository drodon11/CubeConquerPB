#include <iostream>
#include <vector>
#include <sstream>

using namespace std;


int read_formula(vector<vector<int>>& clauses) {
    string line;

    // Saltar comentaris
    while (getline(cin, line) && !line.empty() && line[0] == 'c') {}

    stringstream ss(line);
    string tmp;
    int nVars, nClauses;
    ss >> tmp >> tmp >> nVars >> nClauses;

    for (int i = 0; i < nClauses; ++i) {
        vector<int> clause;
        int lit;
        while (cin >> lit && lit != 0) {
            clause.push_back(lit);
        }
        clauses.push_back(move(clause));
    }

    return nVars;
}

void writePB(vector<vector<int>> &clauses){
    int clause_count = clauses.size();
    for(int i=0; i<clause_count; i++){
        int count = 0; 
        int size = clauses[i].size();
        for(int j=0; j<size; j++){
            if(clauses[i][j] > 0){
                cout<<"+1 x"<<clauses[i][j]<<" ";
            }
            else {
                count++;
                cout<<"-1 x"<<abs(clauses[i][j])<<" ";
            }
        }
        cout<<">= "<<1-count<<" ;"<<endl;

    }
}

int main(){
    vector<vector<int>> clauses;
    int nVars = read_formula(clauses);

    writePB(clauses);

}
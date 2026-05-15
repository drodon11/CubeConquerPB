#!/usr/bin/env python
import sys
import math

lines = list(map(str.strip,sys.stdin))

eqs = 0
geqs = 0
maxweight = 1
maxvar = 0

for i,line in enumerate(lines):
    if line == "" or line[0]=="*": continue
    tokens = line.split()
    if ">=" in tokens: geqs += 1
    elif "=" in tokens: eqs += 1
    weight = 0
    for j,token in enumerate(tokens):
        try:
            token = int(token)
            weight += abs(token)
        except ValueError:
            try:
                var = int(token[1:]) + 1
                maxvar = max(maxvar, var)
                tokens[j] = f"x{var}"
            except ValueError:
                pass
    maxweight = max(maxweight, weight)
    lines[i]=" ".join(tokens)

intsize = maxweight.bit_length()

print(f"* #variable= {maxvar} #constraint= {eqs+geqs} #equal= {eqs} intsize= {intsize}")
for line in lines:
    print(line)

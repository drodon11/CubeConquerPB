#!/usr/bin/env python
import sys
import math

lines = list(map(str.strip,sys.stdin))

eqs = 0
geqs = 0
maxweight = 1
maxvar = 0

for line in lines:
    if line == "" or line[0]=="*": continue
    tokens = line.split()
    if ">=" in tokens: geqs += 1
    elif "=" in tokens: eqs += 1
    weight = 0
    for token in tokens:
        try:
            token = int(token)
            weight += abs(token)
        except ValueError:
            try:
                var = int(token[1:])
                maxvar = max(maxvar, var)
            except ValueError:
                try:
                    var = int(token[2:])
                    maxvar = max(maxvar, var)
                except ValueError:
                    pass
    maxweight = max(maxweight, weight)

intsize = maxweight.bit_length()

print(f"* #variable= {maxvar} #constraint= {eqs+geqs} #equal= {eqs} intsize= {intsize}")
for line in lines:
    print(line)

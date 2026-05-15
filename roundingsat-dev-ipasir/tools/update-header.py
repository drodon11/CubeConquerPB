#!/usr/bin/env python
import sys
import math

lines = list(map(str.strip,sys.stdin))

eqs = 0
geqs = 0
maxweight = 1

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
            pass
    maxweight = max(maxweight, weight)

intsize = maxweight.bit_length()

print(lines[0] + " #equal= " + str(eqs) + " intsize= " + str(intsize))
for line in lines[1:]:
    print(line)

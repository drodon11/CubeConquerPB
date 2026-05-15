#!/bin/sh
INPUT=$1
OUTPUT=${INPUT/.mps/.opb}
SCIP=${SCIP:=scip}
$SCIP -q -c "read $INPUT" -c "write problem $OUTPUT" -c quit
python fix-scip-opb.py < $OUTPUT | sponge $OUTPUT

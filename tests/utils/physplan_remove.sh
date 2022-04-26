#!/bin/bash
#
NTYPE=$1

# remove physical plan recrods
# (pairs of lines where the first one has '>[pNode]' in it
# where the operator is NTYPE
awk -v NTYPE="$NTYPE" '
$1 ~ />\[pNode\]/ { if($2 != NTYPE) { print $0 ; getline; print $0; }}
'

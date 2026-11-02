#!/bin/bash
# each line on its own, after one that is fine
while IFS= read -r line; do
    printf '[RS274NGC]\nSUBROUTINE_PATH = .\nREMAP= M6 modalgroup=6 ngc=handler\nREMAP= %s\n' "$line" > test.ini
    echo "== $line"
    rs274 -i test.ini -n 0 -g test.ngc 2>&1 | grep -i "remap"
done <<'LINES'
M1000 modalgroup=10 ngc=handler
M123 modalgroup=10 ngc=handler
G1 modalgroup=1 ngc=handler
M6x modalgroup=6 ngc=handler
G88.12 modalgroup=1 ngc=handler
M400 modalgroup=10 ngc=handler prologue=foo
X1 ngc=handler
T modalgroup=6 ngc=handler
M06 modalgroup=6 ngc=handler
LINES
rm -f test.ini
exit 0

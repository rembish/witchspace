#!/bin/sh
# Unpack ELITE.EXE, import it into a headless Ghidra project and dump decompiled C
# (elite_decomp.c) and a plain listing (elite_disasm.txt).
# Needs GHIDRA (default ~/tools/ghidra_*).
set -e
cd "$(dirname "$0")"
GHIDRA=${GHIDRA:-$(ls -d ~/tools/ghidra_*_PUBLIC | tail -1)}
python3 ../tools/unexepack.py ../../original/ELITE.EXE ../../original/elite_unpacked.exe
mkdir -p proj
"$GHIDRA/support/analyzeHeadless" "$PWD/proj" elite -import "$PWD/../../original/elite_unpacked.exe" -overwrite \
    -scriptPath "$PWD" -preScript SetDS.java -postScript ApplyNames.java "$PWD/names.txt" \
    -postScript DumpAll.java "$PWD/elite_decomp.c" -postScript DumpListing.java "$PWD/elite_disasm.txt" > headless.log 2>&1
grep -c '=====' elite_decomp.c

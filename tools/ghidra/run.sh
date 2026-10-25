#!/bin/sh
# Run XrefStrings.java (or $GHIDRA_SCRIPT, e.g. FindDisp.java) on an iw4sp.exe already imported and analyzed in a Ghidra project.
# Args go straight to the script; '%' is escaped for cmd.exe. Prints the script's lines.
#
# GHIDRA_PROJECT_NAME / GHIDRA_PROGRAM: the project and program inside it (default mw2sp64 / iw4sp.exe; the 32-bit image: mw2sp32 / image32.bin)
# Environment (Windows paths):
#   GHIDRA_DIR      Ghidra install folder, e.g. C:\tools\ghidra_12.1.4_PUBLIC
#   JAVA_HOME       JDK 21
#   GHIDRA_PROJECT  folder holding the project "mw2sp64" with iw4sp.exe imported
# One-time import:  %GHIDRA_DIR%\support\analyzeHeadless.bat %GHIDRA_PROJECT% mw2sp64 -import iw4sp.exe
#
# Script arguments: "<string>" find a string and its references; @<rva> decompile a function;
#                   ref:<rva> references to an address; sym:<name> call sites of an import.
: "${GHIDRA_DIR:?set GHIDRA_DIR}" "${JAVA_HOME:?set JAVA_HOME}" "${GHIDRA_PROJECT:?set GHIDRA_PROJECT}"
HERE=$(cd "$(dirname "$0")" && pwd -W)
LOG="${TMP:-/tmp}/mw2coop-ghidra.log"
ARGS=""
for a in "$@"; do
	e=$(printf '%s' "$a" | sed 's/%/%%/g')
	ARGS="$ARGS \"$e\""
done
eval cmd //c "\"$GHIDRA_DIR\\support\\analyzeHeadless.bat\"" \
	"\"$GHIDRA_PROJECT\"" ${GHIDRA_PROJECT_NAME:-mw2sp64} -process ${GHIDRA_PROGRAM:-iw4sp.exe} -noanalysis -readOnly \
	-scriptPath "\"$HERE\"" -postScript ${GHIDRA_SCRIPT:-XrefStrings.java} $ARGS > "$LOG" 2>&1
SCRIPT=${GHIDRA_SCRIPT:-XrefStrings.java}
sed -n "/$SCRIPT>/,\$p" "$LOG" | sed -E "s/^INFO  $SCRIPT> //; s/ \(GhidraScript\)  \$//" |
	grep -v -E '^(INFO|WARN)  '

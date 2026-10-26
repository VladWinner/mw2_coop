#!/bin/sh
# The Ghidra helpers (run.sh) with the paths filled in, for the 64-bit game by default (MW2COOP_GHIDRA_BITS=32: the 32-bit image).
# Usage: sh tools/ghidra/g.sh [-s <Script.java>] <script arguments>...
#   no -s       XrefStrings.java:  "<string>" (a string and its references), @<rva> (decompile a function), -decomp "<string>", ref:<rva>, sym:<name>, dis:<rva>:<count>
#   -s CallChain.java <rva> [depth]      callers level by level
#   -s GlobalsOf.java <rva> [functions] [gap]   the writable globals a function and its callees touch, in address clusters
#   -s FuncAt.java <rva>...             the function that contains an address
# G_AGENT=<name> (set it when several runs go at the same time): the project is copied once to local/scratch/ghidra-<name> and that copy is used, so the runs
# do not wait for each other's project lock. Output is printed; redirect it to a file for the big ones.
SCRIPT=XrefStrings.java
if [ "$1" = "-s" ]; then
	SCRIPT=$2
	shift 2
fi
HERE=$(cd "$(dirname "$0")" && pwd)
LOCAL=$(cd "$HERE/../../.." && pwd)
LOCALW=$(cd "$LOCAL" && pwd -W | tr '/' '\\')
BITS=${MW2COOP_GHIDRA_BITS:-64}
if [ "$BITS" = 32 ]; then
	PROJ=ghidra-proj32
	export GHIDRA_PROJECT_NAME=mw2sp32 GHIDRA_PROGRAM=image32.bin
else
	PROJ=ghidra-proj
fi
if [ -n "${G_AGENT:-}" ]; then
	COPY="$LOCAL/scratch/ghidra-$G_AGENT-$BITS"
	if [ ! -d "$COPY" ]; then
		mkdir -p "$LOCAL/scratch"
		cp -r "$LOCAL/$PROJ" "$COPY"
		rm -f "$COPY"/*.lock "$COPY"/*.lock~ 2>/dev/null
	fi
	PROJW=$(cd "$COPY" && pwd -W | tr '/' '\\')
else
	PROJW="$LOCALW\\$PROJ"
fi
export GHIDRA_DIR="$LOCALW\\tools\\ghidra\\ghidra_12.1.4_PUBLIC"
export JAVA_HOME="$LOCALW\\tools\\jdk21\\jdk-21.0.12.1+1"
export GHIDRA_PROJECT="$PROJW"
export GHIDRA_SCRIPT=$SCRIPT
exec sh "$HERE/run.sh" "$@"

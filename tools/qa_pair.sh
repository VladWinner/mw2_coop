#!/bin/sh
# Run one QA scenario with two game copies on this PC: host (mw2-64) + client (mw2-64-b).
# Usage: tools/qa_pair.sh <scenario> [start point] [seconds in level] [map]
# Needs two test copies of the game (see README), Steam running and logged in. run_test.ps1 kills
# any iw4sp.exe outside the test root within 200 ms, so your real install is never run.
#   MW2COOP_TEST_ROOT  test root, POSIX path (default /d/coop-test)
SCEN=${1:?scenario}; START=${2:-}; LEVEL_S=${3:-60}; MAP=${4:-trainer}
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
A=$ROOT/mw2-64; B=$ROOT/mw2-64-b
HERE=$(cd "$(dirname "$0")" && pwd -W)
COMMON="!watch coop mapname sv_running specialops mw2coop_qa
set mw2coop_qa $SCEN
set mw2coop_qa_phase \"\"
set start \"$START\""
LONG=$(( (LEVEL_S + 30) * 1000 ))
printf '# host\n%s\n!sleep 35000\nset coop_mapName %s\nlisten\n!sleep 2000\ncoopLaunch\n!sleep %d\n' "$COMMON" "$MAP" $LONG > $A/mw2coop/commands.txt
printf '# client (QA: shifted XUID, party join without Steam lobby)\n%s\n!sleep 20000\n!xuid_add 1\n!sleep 19000\nmw2coop_party_join\n!sleep 1000\nconnect 127.0.0.1 8305\n!sleep %d\n' "$COMMON" $LONG > $B/mw2coop/commands.txt
rm -f $A/mw2coop/mw2coop.log $B/mw2coop/mw2coop.log
TOTAL=$(( 50 + LEVEL_S ))
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/run_test.ps1" -Tag "qa-$SCEN" -Timeout $TOTAL -TestRoot "$(cygpath -w "$ROOT")" -Dirs "$(cygpath -w "$A"),$(cygpath -w "$B")" | grep -E "killing|exited|untouched|TOUCHED|WRONG"
for side in A B; do
	eval d=\$$side
	echo "=== $side ($( [ $side = A ] && echo host || echo client ))"
	grep -a -E "script:|qa:|scripts: .*(patches|mw2coop)|FAIL|watch: (mapname|coop)" $d/mw2coop/mw2coop.log | sed 's/^\([0-9:.]*\) \[[ 0-9]*\] /\1 /'
done

#!/bin/sh
# Run one QA scenario with two game copies on this PC: host (mw2-64) + client (mw2-64-b).
# Usage: tools/qa_pair.sh <scenario> [start point] [seconds in level] [map]
#   start point "@continue": the host runs mw2coop_continue (mission and start point from its
#   progress.txt) and the client gets no start point, as in a real session.
# Needs two test copies of the game (see README), Steam running and logged in. run_test.ps1 kills
# any iw4sp.exe outside the test root within 200 ms, so your real install is never run.
#   MW2COOP_TEST_ROOT  test root, POSIX path (default /d/coop-test)
#   MW2COOP_QA_WATCH   extra dvars or memory (@rva:size, *rva+off:size) to log on change
#   MW2COOP_QA_CMDS    extra commands.txt lines for both copies, separated by ";" (e.g. "!trace 342c88")
#   MW2COOP_QA_LATE    extra host lines run right after coopLaunch, ";"-separated (start with a !sleep)
#   MW2COOP_GOD        host's mw2coop_god (default: unset = 1, god mode on; "restart" forces 0).
#                      Set on the host only: the client gets it from the host's launch packet.
#   MW2COOP_HOST_CMDS  extra host lines run in the lobby right before coopLaunch, ";"-separated
#                      (e.g. what the difficulty popup runs, to pick a difficulty on the host only)
SCEN=${1:?scenario}; START=${2:-}; LEVEL_S=${3:-60}; MAP=${4:-trainer}
GOD=${MW2COOP_GOD:-1}
[ "$SCEN" = "restart" ] && GOD=0
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
BITS=${MW2COOP_BITS:-64}
A=$ROOT/mw2-$BITS; B=$ROOT/mw2-$BITS-b
HERE=$(cd "$(dirname "$0")" && pwd -W)
# a copy left from an earlier run (a crashed one keeps its window) holds the ports, the log and winmm.dll
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/close_test_copies.ps1" -Root "$(cygpath -w "$ROOT")" | tr -d '\r'
HERE=$(cd "$(dirname "$0")" && pwd -W)
HOST_START="$START"; LAUNCH="set coop_mapName $MAP"
if [ "$START" = "@continue" ]; then HOST_START=""; START=""; LAUNCH="mw2coop_continue"; fi
COMMON="!watch coop mapname sv_running specialops mw2coop_qa start mw2coop_god ${MW2COOP_QA_WATCH:-}
set mw2coop_qa $SCEN
set mw2coop_qa_phase \"\"
$(printf '%s' "${MW2COOP_QA_CMDS:-}" | tr ';' '
')"
LONG=$(( (LEVEL_S + 30) * 1000 ))
LATE="$(printf '%s' "${MW2COOP_QA_LATE:-}" | tr ';' '
')"
HOSTCMDS="$(printf '%s' "${MW2COOP_HOST_CMDS:-}" | tr ';' '
')"
if [ "$BITS" = 32 ]; then
	# the 32-bit game: loads faster, needs the sign-in command before listening, and its join takes the PC's own address
	# (a loopback address makes the host ignore the client's heartbeat, see party.c)
	PRE='set arcademode 0
set solo_play 0'
	OWN_IP=$(powershell.exe -NoProfile -Command "(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { \$_.IPAddress -notlike '169.*' -and \$_.IPAddress -ne '127.0.0.1' } | Select-Object -First 1).IPAddress" | tr -d '\r')
	printf '# host\n%s\n%s\nset start "%s"\nset mw2coop_god "%s"\n!sleep 8000\n%s\nlisten\n!sleep 2000\n%s\ncoopLaunch\n%s\n!sleep %d\n' "$COMMON" "$PRE" "$HOST_START" "$GOD" "$LAUNCH" "$HOSTCMDS" "$LATE" $LONG > $A/mw2coop/commands.txt
	printf '# client (QA: shifted XUID, join by address without a Steam lobby)\n%s\n%s\nset start "%s"\n!sleep 14000\n!xuid_add 1\n!sleep 6000\nmw2coop_join %s\n!sleep %d\n' "$COMMON" "$PRE" "$START" "$OWN_IP" $LONG > $B/mw2coop/commands.txt
else
printf '# host\n%s\nset start "%s"\nset mw2coop_god "%s"\n!sleep 35000\n%s\nlisten\n!sleep 2000\n%s\ncoopLaunch\n%s\n!sleep %d\n' "$COMMON" "$HOST_START" "$GOD" "$LAUNCH" "$HOSTCMDS" "$LATE" $LONG > $A/mw2coop/commands.txt
printf '# client (QA: shifted XUID, join by address without a Steam lobby)\n%s\nset start "%s"\n!sleep 20000\n!xuid_add 1\n!sleep 19000\nmw2coop_join 127.0.0.1\n!sleep %d\n' "$COMMON" "$START" $LONG > $B/mw2coop/commands.txt
fi
rm -f $A/mw2coop/mw2coop.log $B/mw2coop/mw2coop.log
TOTAL=$(( 50 + LEVEL_S ))
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/run_test.ps1" -Tag "qa-$SCEN" -Timeout $TOTAL -TestRoot "$(cygpath -w "$ROOT")" -Dirs "$(cygpath -w "$A"),$(cygpath -w "$B")" -DoneLogs "$(cygpath -w "$A/mw2coop/mw2coop.log"),$(cygpath -w "$B/mw2coop/mw2coop.log")" | grep -E "killing|exited|untouched|TOUCHED|WRONG|closing"
for side in A B; do
	eval d=\$$side
	echo "=== $side ($( [ $side = A ] && echo host || echo client ))"
	grep -a -E "script:|qa:|progress:|scripts: .*(patches|mw2coop)|FAIL|watch: (mapname|coop|start)" $d/mw2coop/mw2coop.log | sed 's/^\([0-9:.]*\) \[[ 0-9]*\] /\1 /'
done

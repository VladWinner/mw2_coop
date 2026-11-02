#!/bin/sh
# QA: the split screen started from the menu twice in one process (the second start used to crash): lobby, Start, play, quit to the main menu (disconnect), lobby, Start again.
# Usage: tools/qa_restart.sh [seconds in each level, default 25]   (64-bit test copy mw2-64; MW2COOP_PAD player 2's pad, default 0)
LEVEL_S=${1:-25}
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
A=$ROOT/mw2-64
HERE=$(cd "$(dirname "$0")" && pwd -W)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/close_test_copies.ps1" -Root "$(cygpath -w "$ROOT")" | tr -d '\r'
printf '[network]\nport=28960\n\n[native]\n' > "$A/mw2coop/settings.ini"
lobby() {
	echo "mw2coop_split_menu"
	echo "!sleep 2000"
	echo "mw2coop_host_go"
	echo "!sleep 4000"
	echo "mw2coop_split_pad ${MW2COOP_PAD:-0} ${MW2COOP_PAD0:--1}"
	echo "!sleep 1500"
	echo "mw2coop_start"
	echo "!sleep 10000"
	echo "mw2coop_briefskip" # the briefing film waits for a skip
}
{
	echo "set mw2coop_god 1"
	echo "set r_fullscreen 0"
	echo 'set r_customMode "1920x1080"'
	echo "!sleep 6000"
	lobby
	echo "!sleep $((LEVEL_S * 1000))"
	echo "!shot restart-a"
	echo "disconnect"
	echo "!sleep 9000"
	lobby
	echo "!sleep $((LEVEL_S * 1000))"
	echo "!shot restart-b"
} > "$A/mw2coop/commands.txt"
rm -f "$A/mw2coop/mw2coop.log" "$A/mw2coop/shots/restart-"*.bmp
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/run_test.ps1" -Tag restart -Timeout $((LEVEL_S * 2 + 90)) -TestRoot "$(cygpath -w "$ROOT")" -Dirs "$(cygpath -w "$A")" | tail -2
grep -a -E "split screen session|session over|CRASH: (access|exc)|split view on" "$A/mw2coop/mw2coop.log" | sed 's/^\([0-9:.]*\) \[[ 0-9]*\] /\1 /' | cut -c1-150

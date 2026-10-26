#!/bin/sh
# Split screen with ONE game copy: the host (mw2-<bits>) starts the second process itself (mw2coop_split, instance.c) and
# launches a mission with it. Usage: tools/qa_split.sh <scenario> <seconds in level> [map]
#   MW2COOP_BITS=32      the 32-bit build
#   MW2COOP_SPLIT_PAD    0..3: the XInput pad of the second player (default -1: none)
#   MW2COOP_MENU=1       go through the menu commands (Split screen button, host popup, Start) instead of mw2coop_split
#   MW2COOP_FAKEPAD      "lx ly rx ry buttons lt rt": a made-up pad for the second player (with MW2COOP_SPLIT_PAD=0)
# The second process lives in <copy>\mw2coop\p2 (links to the copy's files).
SCEN=${1:-hold}; LEVEL_S=${2:-60}; MAP=${3:-trainer}
PAD=${MW2COOP_SPLIT_PAD:--1}
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
BITS=${MW2COOP_BITS:-64}
A=$ROOT/mw2-$BITS
HERE=$(cd "$(dirname "$0")" && pwd -W)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/close_test_copies.ps1" -Root "$(cygpath -w "$ROOT")" | tr -d '\r'
PRE=""
[ "$BITS" = 32 ] && PRE='set arcademode 0
set solo_play 0'
WAIT=35000
[ "$BITS" = 32 ] && WAIT=8000
{
	echo "# split screen host"
	echo "!watch coop mapname sv_running mw2coop_qa start"
	echo "set mw2coop_qa $SCEN"
	echo 'set mw2coop_qa_phase ""'
	[ -n "$PRE" ] && echo "$PRE"
	echo 'set start ""'
	echo 'set mw2coop_god "1"'
	echo "!sleep $WAIT"
	echo "set coop_mapName $MAP"
	if [ -n "${MW2COOP_MENU:-}" ]; then
		# the menu path: the Split screen button, the lobby (the second player presses a button on his pad = Ready), Start; the second process
		# starts with Start, joins by itself and the launch follows
		echo "mw2coop_split_menu"
		echo "!sleep 12000"
		echo "mw2coop_host_go"
		echo "!sleep 4000"
		echo "set coop_mapName $MAP"
		echo 'set start ""'
		echo "!sleep 2000"
		echo "mw2coop_split_pad ${PAD#-}"
		echo "mw2coop_split_ready"
		echo "mw2coop_ready"
		echo "!sleep 2000"
		echo "mw2coop_start"
	else
		echo "listen"
		echo "!sleep 2000"
		echo "mw2coop_split $PAD"
		echo "!sleep 45000"
		echo "mw2coop_splitlaunch"
	fi
	echo "!sleep $(( (LEVEL_S + 60) * 1000 ))"
} > $A/mw2coop/commands.txt
# a made-up pad goes into the host's settings.ini, which the second process copies
INI=$A/mw2coop/settings.ini
python "$HERE/qa_split_ini.py" "$(cygpath -w "$INI")" "${MW2COOP_FAKEPAD:-}"
if [ -n "${MW2COOP_FAKEPAD:-}" ]; then
	mkdir -p $A/mw2coop/p2/mw2coop
	echo "$MW2COOP_FAKEPAD" > $A/mw2coop/p2/mw2coop/fakepad.txt
fi
rm -f $A/mw2coop/mw2coop.log $A/mw2coop/p2/mw2coop/mw2coop.log
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/run_test.ps1" -Tag "qa-split" -Timeout $(( 140 + LEVEL_S )) -TestRoot "$(cygpath -w "$ROOT")" -Dirs "$(cygpath -w "$A")" | grep -E "killing|exited|untouched|TOUCHED|WRONG|closing"
for f in $A/mw2coop/mw2coop.log $A/mw2coop/p2/mw2coop/mw2coop.log; do
	echo "=== $f"
	grep -a -E "instance:|splitscreen:|qa:|script:|FAIL|watch: (mapname|coop)" $f | sed 's/^\([0-9:.]*\) \[[ 0-9]*\] /\1 /' | head -40
done

#!/bin/sh
# QA: the pause menu in the native split screen started from the menu (player 1 on the keyboard, player 2 on a pad): ESC in the level, a frame of what is shown, ESC again.
# Usage: tools/qa_pause.sh [seconds in the level before ESC, default 35]   (64-bit test copy mw2-64; frames mw2coop\shots\pause-*.bmp)
LEVEL_S=${1:-35}
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
A=$ROOT/${MW2COOP_COPY:-mw2-64}
HERE=$(cd "$(dirname "$0")" && pwd -W)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/close_test_copies.ps1" -Root "$(cygpath -w "$ROOT")" | tr -d '\r'
printf '[network]\nport=28960\n\n[native]\n' > "$A/mw2coop/settings.ini"
# MW2COOP_PAUSE_BY=pad2: player 2's pad presses Start (qa pad dvar) instead of the keyboard's Escape
pause_press() {
	if [ "${MW2COOP_PAUSE_BY:-}" = pad2 ]; then
		echo 'set mw2coop_qa_pad2 "0 0 0 0 16 0 0"'
		echo "!sleep 300"
		echo 'set mw2coop_qa_pad2 "0 0 0 0 0 0 0"'
	else
		echo "!key 1b"
	fi
}
{
	echo "set mw2coop_god 1"
	[ -n "${MW2COOP_QA:-}" ] && echo "set mw2coop_qa $MW2COOP_QA" # a QA scenario of _mw2coop_qa.gsc (e.g. airwalk)
	echo "set r_fullscreen 0"
	echo 'set r_customMode "1920x1080"'
	echo "!sleep 6000"
	echo "mw2coop_split_menu"
	echo "!sleep 2000"
	echo "mw2coop_host_go"
	echo "!sleep 4000"
	echo "mw2coop_split_pad ${MW2COOP_PAD:-0} ${MW2COOP_PAD0:--1}"
	echo "!sleep 1500"
	echo "mw2coop_start"
	echo "!sleep 10000"
	echo "mw2coop_briefskip"
	echo "!sleep $((LEVEL_S * 1000))"
	echo "!pshot pause-0"
	pause_press
	echo "!sleep 3000"
	echo "!pshot pause-a"
	echo "!sleep 1500"
	echo "!pshot pause-a2"
	pause_press
	echo "!sleep 3000"
	echo "!pshot pause-b"
	echo "!sleep 2000"
} > "$A/mw2coop/commands.txt"
if [ -n "$MW2COOP_FAKEPAD" ]; then echo "0 0 0 0 0 0 0" > "$A/mw2coop/fakepad.txt"; else rm -f "$A/mw2coop/fakepad.txt"; fi
rm -f "$A/mw2coop/mw2coop.log" "$A/mw2coop/shots/pause-"*.bmp
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/run_test.ps1" -Tag pause -Timeout $((LEVEL_S + 100)) -TestRoot "$(cygpath -w "$ROOT")" -Dirs "$(cygpath -w "$A")" | tail -2
grep -a -E "split screen session|split view|CRASH: (access|exc)|camera watch|CAMERA" "$A/mw2coop/mw2coop.log" | sed 's/^\([0-9:.]*\) \[[ 0-9]*\] /\1 /' | cut -c1-170

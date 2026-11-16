#!/bin/sh
# Campaign HUD tour (native split screen, mode 11, D3D9): every given campaign level is played for MW2COOP_TOUR_S seconds (default 80) and four frames are kept
# (shot11 at present MW2COOP_TOUR_AT, MW2COOP_TOUR_STEP apart): intro titles, hints, subtitles, the HUD of each half. Frames -> local/scratch/camp-tour/<level>-<present>.bmp, one line per level in summary.txt.
# Usage: tools/qa_campaign_tour.sh <level> [level ...]     serialised with qa_lock.sh
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
A=$ROOT/${MW2COOP_COPY:-mw2-64}
OUT="$HERE/../../scratch/camp-tour"
mkdir -p "$OUT"
S=${MW2COOP_TOUR_S:-80}
AT=${MW2COOP_TOUR_AT:-1500}
STEP=${MW2COOP_TOUR_STEP:-1500}
for L in "$@"; do
	rm -f "$A"/mw2coop/shots/m11-*.bmp
	MW2COOP_MODE=11 MW2COOP_MAP=$L MW2COOP_MIDAT=15 MW2COOP_RES=${MW2COOP_RES:-1280x720} MW2COOP_TIMEOUT=$((S + 30)) MW2COOP_PRE='set mw2coop_native_split 1' MW2COOP_MID='mw2coop_briefskip' \
		sh "$HERE/qa_lock.sh" sh "$HERE/qa_native.sh" $S "presentkeep=1;presentlog=1;shot11=$AT;shot11step=$STEP;hudelemlog=1" > "$OUT/$L.run.txt" 2>&1
	LOG="$A/mw2coop/mw2coop.log"
	cp "$LOG" "$OUT/$L.log" 2>/dev/null
	for f in "$A"/mw2coop/shots/m11-*.bmp; do [ -f "$f" ] && cp "$f" "$OUT/$L-$(basename "$f" | sed 's/m11-//')"; done
	echo "$L | script errors $(grep -c 'SCRIPT ERROR' "$LOG") | crash lines $(grep -c -i 'crash report\|access violation\|exception code' "$LOG") | frames $(ls "$OUT"/$L-*.bmp 2>/dev/null | wc -l) | split $(grep -c 'split view on' "$LOG")" | tee -a "$OUT/summary.txt"
done

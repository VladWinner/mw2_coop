#!/bin/sh
# Spec Ops sweep (native split screen, mode 11): every so_* level is started once with "set specialops 1" and played for MW2COOP_SWEEP_S seconds (default 40); per level the log is kept
# and one line is written to local/scratch/specops-sweep.txt: how it ended (ran to the timeout / died early), SCRIPT ERROR count, the first error site, the last present count.
# Usage: tools/qa_specops_sweep.sh [level ...]     (no level: all of them)   needs MW2COOP_DISPLAY etc. like qa_native.sh; serialised with qa_lock.sh
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
A=$ROOT/${MW2COOP_COPY:-mw2-64}
OUT="$HERE/../../scratch/specops-sweep"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.txt"
LEVELS="$*"
[ -z "$LEVELS" ] && LEVELS="so_killspree_trainer so_killspree_favela so_killspree_invasion so_defense_invasion so_chopper_invasion so_rooftop_contingency so_forest_contingency so_snowrace1_cliffhanger so_snowrace2_cliffhanger so_sabotage_cliffhanger so_escape_airport so_download_arcadia so_intel_boneyard so_juggernauts_favela so_defuse_favela_escape so_showers_gulag so_takeover_estate so_takeover_oilrig so_assault_oilrig so_ac130_co_hunted so_ghillies so_hidden_so_ghillies so_bridge so_crossing_so_bridge so_demo_so_bridge"
S=${MW2COOP_SWEEP_S:-40}
for L in $LEVELS; do
	START=$(date +%s)
	MW2COOP_MAP=$L MW2COOP_MIDAT=15 MW2COOP_RES=${MW2COOP_RES:-1280x720} MW2COOP_TIMEOUT=$((S + 30)) MW2COOP_PRE='set specialops 1' MW2COOP_MID='mw2coop_briefskip' \
		sh "$HERE/qa_lock.sh" sh "$HERE/qa_native.sh" $S "presentkeep=1;presentlog=1" > "$OUT/$L.run.txt" 2>&1
	END=$(date +%s)
	LOG="$A/mw2coop/mw2coop.log"
	cp "$LOG" "$OUT/$L.log" 2>/dev/null
	ERRS=$(grep -c "SCRIPT ERROR" "$LOG")
	FIRST=$(grep -A1 "SCRIPT ERROR" "$LOG" | grep "at:" | head -1 | sed 's/.*at: //')
	CRASH=$(grep -c -i "exception\|crash report\|access violation" "$LOG")
	ENDED=$(grep -c "timeout, killing" "$OUT/$L.run.txt")
	SPLIT=$(grep -c "split view on" "$LOG")
	echo "$L | $((END - START)) s | timeout-kill $ENDED | split $SPLIT | script errors $ERRS | first: ${FIRST:-none} | crash lines $CRASH" | tee -a "$SUMMARY"
done

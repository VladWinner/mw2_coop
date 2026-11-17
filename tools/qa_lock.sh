#!/bin/sh
# Serialises game runs between agents: tools/qa_lock.sh <command...> takes local/qa.lock (waiting while another run holds it), runs the command, releases.
# Every agent that launches the test copy must go through this: one game copy at a time, or two runs fight over the port, the log and the DLL.
# - A lock is taken over only when the lock itself is older than 20 minutes (a holder that died). The age is the lock folder's, not this caller's wait
#   (counting the wait let a caller that had queued behind several runs take a lock a few seconds old, 2026-11-17).
# - The lock goes only when the command has ended normally: a holder that is killed (a tool's time-out, a stopped task) leaves it, because the game it
#   started may still be running; the trap on INT/TERM used to release it and let the next caller start a second game beside it (2026-11-17).
# - After taking the lock, a game copy still running from the test root (an orphan of a killed holder) is waited for, up to 5 minutes.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOCK="$ROOT/qa.lock"
while ! mkdir "$LOCK" 2>/dev/null; do
	born=$(stat -c %Y "$LOCK" 2>/dev/null || echo 0)
	now=$(date +%s)
	if [ "$born" -gt 0 ] && [ $((now - born)) -gt 1200 ]; then
		echo "qa_lock: $LOCK is stale ($(((now - born) / 60)) min old), taking it" >&2
		rm -f "$LOCK/holder"
		rmdir "$LOCK" 2>/dev/null
		continue
	fi
	sleep 3
done
echo $$ > "$LOCK/holder" 2>/dev/null
i=0
while tasklist //FI "IMAGENAME eq iw4sp.exe" 2>/dev/null | grep -qi "iw4sp.exe" || tasklist //FI "IMAGENAME eq iw4x-sp.exe" 2>/dev/null | grep -qi "iw4x-sp.exe"; do
	[ $i -eq 0 ] && echo "qa_lock: a game copy is still running (a previous holder's?): waiting for it" >&2
	i=$((i + 1))
	[ $i -gt 100 ] && { echo "qa_lock: it is still running after 5 minutes, going on" >&2; break; }
	sleep 3
done
"$@"
rc=$?
# released only by its holder: the folder may meanwhile belong to another caller (taken over as stale)
if [ "$(cat "$LOCK/holder" 2>/dev/null)" = "$$" ]; then
	rm -f "$LOCK/holder"
	rmdir "$LOCK" 2>/dev/null
fi
exit $rc

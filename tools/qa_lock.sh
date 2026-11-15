#!/bin/sh
# Serialises game runs between agents: tools/qa_lock.sh <command...> takes local/qa.lock (waiting up to 20 min), runs the command, releases.
# Every agent that launches the test copy must go through this: one game copy at a time, or two runs fight over the port, the log and the DLL.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOCK="$ROOT/qa.lock"
i=0
while ! mkdir "$LOCK" 2>/dev/null; do
	i=$((i + 1))
	if [ $i -gt 400 ]; then echo "qa_lock: $LOCK is stale (20 min), taking it" >&2; rmdir "$LOCK" 2>/dev/null; i=0; fi
	sleep 3
done
trap 'rmdir "$LOCK" 2>/dev/null' EXIT INT TERM
"$@"

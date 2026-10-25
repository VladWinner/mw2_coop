#!/bin/sh
# Build and copy mw2coop into TEST copies of the game (never into your real install).
# Usage: ./deploy.sh [test dir ...]
#   MW2COOP_TEST_ROOT  folder holding the test copies (default /d/coop-test); targets must be inside it
#   MW2COOP_GSC_DIR    extracted game scripts, to check the patches first (optional)
# Default targets: $MW2COOP_TEST_ROOT/mw2-64 (host) and $MW2COOP_TEST_ROOT/mw2-64-b (client).
#   MW2COOP_BITS=32   build the 32-bit DLL (sh build.sh x86) and deploy it to mw2-32 and mw2-32-b instead (docs/32bit-plan.md)
set -e
cd "$(dirname "$0")"
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
BITS=${MW2COOP_BITS:-64}
if [ "$MW2COOP_IW4X" = 1 ]; then
	# the iw4x-sp client: the 32-bit DLL with the 1.0.159 profile, into mw2-iw4x and mw2-iw4x-b
	DLL=build/x86-iw4x/winmm.dll; sh build.sh x86 iw4x
	[ $# -gt 0 ] || set -- "$ROOT/mw2-iw4x" "$ROOT/mw2-iw4x-b"
elif [ "$BITS" = 32 ]; then DLL=build/x86/winmm.dll; sh build.sh x86; else DLL=build/winmm.dll; sh build.sh; fi
if [ -n "$MW2COOP_GSC_DIR" ]; then
	# a patch that does not apply stops the deploy: the game would load a script that fails to compile
	if ! out=$(python -I tools/check_patches.py content "$MW2COOP_GSC_DIR"); then
		printf '%s\n' "$out" | grep -v '^  ok'
		echo "patches do not apply: nothing deployed (fix them, or unset MW2COOP_GSC_DIR to skip the check)"
		exit 1
	fi
	printf '%s\n' "$out" | tail -1
fi
[ $# -gt 0 ] || set -- "$ROOT/mw2-$BITS" "$ROOT/mw2-$BITS-b"
# a running copy holds its winmm.dll: close the ones left from an earlier test run first
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/close_test_copies.ps1 -Root "$(cygpath -w "$ROOT")" | tr -d '\r'
for t in "$@"; do
	case "$t" in "$ROOT"/*) ;; *) echo "refusing to deploy outside $ROOT: $t"; exit 1 ;; esac
	cp "$DLL" "$t/winmm.dll"
	rm -rf "$t/mw2coop/scripts" "$t/mw2coop/patches"
	mkdir -p "$t/mw2coop"
	rm -rf "$t/mw2coop/fonts"
	cp -r content/scripts content/patches content/fonts "$t/mw2coop/"
	[ -f "$t/mw2coop/settings.ini" ] || cp content/release/settings.ini "$t/mw2coop/"
	cp content/release/mw2coop-firewall.ps1 "$t/mw2coop/"
	rm -f "$t/mw2coop/mw2coop.log"
	echo "deployed to $t"
done

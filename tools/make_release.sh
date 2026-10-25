#!/bin/sh
# Build the release archive: an optimized, stripped winmm.dll, the mod's scripts and patches,
# the fonts (SIL OFL, with their licenses), settings.ini, the firewall helper script and INSTALL.txt, ready to copy into the game folder.
# Usage: tools/make_release.sh <version> [x86]   e.g. tools/make_release.sh v0.2.0-alpha
#   x86: the experimental archive for the 32-bit game (build/x86/winmm.dll, INSTALL-x86.txt)
# Output: dist/mw2coop-<version>[-x86].zip and its .sha256
set -e
cd "$(dirname "$0")/.."
VERSION=${1:?version, e.g. v0.2.0-alpha}
if [ "$2" = x86 ]; then
	sh build.sh x86 release
	DLL=build/x86/winmm.dll; NAME=$VERSION-x86; INSTALL=content/release/INSTALL-x86.txt
else
	sh build.sh release
	DLL=build/winmm.dll; NAME=$VERSION; INSTALL=content/release/INSTALL.txt
fi
# the release DLL must not carry local paths (debug info, pdb names)
if grep -a -q -i -e 'users' -e '\.pdb' $DLL; then
	echo "release winmm.dll contains local paths or a pdb name"; exit 1
fi
OUT=dist/mw2coop-$NAME
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/mw2coop"
cp $DLL "$OUT/winmm.dll"
cp -r content/scripts content/patches content/fonts "$OUT/mw2coop/"
cp content/release/settings.ini content/release/mw2coop-firewall.ps1 "$OUT/mw2coop/"
sed "s/{VERSION}/$VERSION/" $INSTALL | sed 's/$/\r/' > "$OUT/INSTALL.txt"
sed 's/$/\r/' LICENSE > "$OUT/LICENSE.txt"
( cd dist && python -I -c "
import os, sys, zipfile
root = sys.argv[1]
with zipfile.ZipFile(root + '.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk(root):
        for f in sorted(files):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, root))
" "mw2coop-$NAME" )
( cd dist && sha256sum "mw2coop-$NAME.zip" > "mw2coop-$NAME.zip.sha256" && cat "mw2coop-$NAME.zip.sha256" )
echo "winmm.dll sha256: $(sha256sum $DLL | cut -d' ' -f1)"

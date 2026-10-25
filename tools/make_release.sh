#!/bin/sh
# Build the release archive: an optimized, stripped winmm.dll, the mod's scripts and patches,
# the fonts (SIL OFL, with their licenses), settings.ini, the firewall helper script and INSTALL.txt, ready to copy into the game folder.
# Usage: tools/make_release.sh <version>      e.g. tools/make_release.sh v0.2.0-alpha
# Output: dist/mw2coop-<version>.zip and dist/mw2coop-<version>.zip.sha256
set -e
cd "$(dirname "$0")/.."
VERSION=${1:?version, e.g. v0.2.0-alpha}
sh build.sh release
# the release DLL must not carry local paths (debug info, pdb names)
if grep -a -q -i -e 'users' -e '\.pdb' build/winmm.dll; then
	echo "release winmm.dll contains local paths or a pdb name"; exit 1
fi
OUT=dist/mw2coop-$VERSION
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/mw2coop"
cp build/winmm.dll "$OUT/"
cp -r content/scripts content/patches content/fonts "$OUT/mw2coop/"
cp content/release/settings.ini content/release/mw2coop-firewall.ps1 "$OUT/mw2coop/"
sed "s/{VERSION}/$VERSION/" content/release/INSTALL.txt | sed 's/$/\r/' > "$OUT/INSTALL.txt"
sed 's/$/\r/' LICENSE > "$OUT/LICENSE.txt"
( cd dist && python -I -c "
import os, sys, zipfile
root = sys.argv[1]
with zipfile.ZipFile(root + '.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk(root):
        for f in sorted(files):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, root))
" "mw2coop-$VERSION" )
( cd dist && sha256sum "mw2coop-$VERSION.zip" > "mw2coop-$VERSION.zip.sha256" && cat "mw2coop-$VERSION.zip.sha256" )
echo "winmm.dll sha256: $(sha256sum build/winmm.dll | cut -d' ' -f1)"

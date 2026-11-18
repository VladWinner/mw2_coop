#!/bin/sh
# Build the separate DLSS archive of a release (owner, 2026-11-18: the bridge goes to players as its own archive in the releases):
# dist/mw2coop-dlss-<version>.zip with mw2coop\dlss\mw2coop_dlss.dll, INSTALL-DLSS.txt and NOTICE-DLSS.txt, and its .sha256.
# Usage: tools/make_dlss_archive.sh <version>        e.g. tools/make_dlss_archive.sh v1.0.2
#   DLSS_SDK  NVIDIA's DLSS SDK (a checkout of https://github.com/NVIDIA/DLSS; default ../third-party/dlss): needed to build the bridge (src/dlss/README.md).
# The bridge links NVIDIA's static NGX library, so the archive follows the NVIDIA RTX SDKs License (src/dlss/README.md): it is a part of the mod, never NVIDIA's SDK on its own; NVIDIA's
# runtime nvngx_dlss.dll is NOT packed (INSTALL-DLSS.txt says where players get it); NOTICE-DLSS.txt carries terms at least as protective as NVIDIA's for its portions.
# The GitHub workflows cannot build it (no SDK there): it is made on the developer's PC and published with the release, on the owner's word.
set -e
cd "$(dirname "$0")/.."
VERSION=${1:?version, e.g. v1.0.2}
sh src/dlss/build_dlss.sh release
DLL=build/mw2coop_dlss.dll
# no local paths or pdb names in the published DLL
if grep -a -q -i -e 'users' -e '\.pdb' $DLL; then
	echo "mw2coop_dlss.dll contains local paths or a pdb name"; exit 1
fi
OUT=dist/mw2coop-dlss-$VERSION
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/mw2coop/dlss"
cp $DLL "$OUT/mw2coop/dlss/"
sed "s/{VERSION}/$VERSION/" content/release/dlss/INSTALL-DLSS.txt | sed 's/$/\r/' > "$OUT/INSTALL-DLSS.txt"
sed 's/$/\r/' content/release/dlss/NOTICE-DLSS.txt > "$OUT/NOTICE-DLSS.txt"
# never NVIDIA's own files
if ls -R "$OUT" | grep -i -q 'nvngx\|nvsdk'; then
	echo "an NVIDIA SDK file is in the archive"; exit 1
fi
( cd dist && python -I -c "
import os, sys, zipfile
root = sys.argv[1]
with zipfile.ZipFile(root + '.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk(root):
        for f in sorted(files):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, root))
" "mw2coop-dlss-$VERSION" )
( cd dist && sha256sum "mw2coop-dlss-$VERSION.zip" > "mw2coop-dlss-$VERSION.zip.sha256" && cat "mw2coop-dlss-$VERSION.zip.sha256" )
echo "mw2coop_dlss.dll sha256: $(sha256sum $DLL | cut -d' ' -f1)"

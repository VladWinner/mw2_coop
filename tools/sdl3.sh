#!/bin/sh
# Prints the path of SDL3.dll (the mod's gamepads other than Xbox pads: src/pads.c) for an architecture, after checking it against the pinned hashes.
#   sh tools/sdl3.sh x64|x86        -> build/sdl3/<version>/<arch>/SDL3.dll (and LICENSE.txt next to it)
# The DLL comes from the official release archive of SDL (zlib license), downloaded once into build/ (or copied from $SDL3_ZIP_DIR, a folder holding the two zips) and checked by
# SHA-256: the archive's hash as the release page publishes it, and the DLL's own. Nothing that does not match is used. Release: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20
set -e
cd "$(dirname "$0")/.."
ARCH=$1
VER=3.4.20
case "$ARCH" in
x64) ZIP_SHA=368bed8b24e44ad9ea372990814f2343fcfb9e468abb7f8f4a3bc146c7304752; DLL_SHA=66844f10251cc1c10619ed5d8e4e3224f564900ee6d69ba87d63de782addae40 ;;
x86) ZIP_SHA=f6fbf5afa42195a01b1da8adf25567377d0e212f4b2b6770923903eb3feea254; DLL_SHA=6d7e653aa9c762ceda4fffc054a504dcce273e016eb9f1fe079b6409a058a9f7 ;;
*) echo "usage: sh tools/sdl3.sh x64|x86" >&2; exit 2 ;;
esac
OUT=build/sdl3/$VER/$ARCH
ZIP=SDL3-$VER-win32-$ARCH.zip
if [ ! -f "$OUT/SDL3.dll" ] || [ "$(sha256sum "$OUT/SDL3.dll" | cut -d' ' -f1)" != "$DLL_SHA" ]; then
	rm -rf "$OUT"
	mkdir -p "$OUT"
	if [ -n "${SDL3_ZIP_DIR:-}" ] && [ -f "$SDL3_ZIP_DIR/$ZIP" ]; then
		cp "$SDL3_ZIP_DIR/$ZIP" "$OUT/$ZIP"
	else
		curl -fsSL -o "$OUT/$ZIP" "https://github.com/libsdl-org/SDL/releases/download/release-$VER/$ZIP"
	fi
	echo "$ZIP_SHA  $OUT/$ZIP" | sha256sum -c - >&2
	python3 -I -c "import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); [open(sys.argv[2] + '/' + n, 'wb').write(z.read(n)) for n in ('SDL3.dll', 'LICENSE.txt')]" "$OUT/$ZIP" "$OUT" 2>/dev/null ||
		python -I -c "import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); [open(sys.argv[2] + '/' + n, 'wb').write(z.read(n)) for n in ('SDL3.dll', 'LICENSE.txt')]" "$OUT/$ZIP" "$OUT"
	rm -f "$OUT/$ZIP"
	echo "$DLL_SHA  $OUT/SDL3.dll" | sha256sum -c - >&2
fi
echo "$OUT/SDL3.dll"

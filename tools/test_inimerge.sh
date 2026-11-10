#!/bin/sh
# Builds and runs tools/test_inimerge.c (the settings.ini merge, src/inimerge.c) against the real template, content/release/settings.ini.
set -e
cd "$(dirname "$0")/.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
T=build/inimerge-test
mkdir -p "$T"
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" -O2 -Wall -Wextra -Isrc -o "$T/test_inimerge.exe" tools/test_inimerge.c src/inimerge.c
"$T/test_inimerge.exe" content/release/settings.ini
# the same on disk: src/inisync.c makes and updates a settings.ini in a scratch folder
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" -O2 -Wall -Wextra -Isrc -o "$T/test_inisync.exe" tools/test_inisync.c src/inisync.c src/inimerge.c
rm -rf "$T/disk" && "$T/test_inisync.exe" "$T/disk"

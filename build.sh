#!/bin/sh
# Build build/winmm.dll (x64) with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
# Usage: ./build.sh [release]
#   LLVM_MINGW  llvm-mingw folder (default: ../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64)
#   release     optimized and stripped: no debug info and no local paths inside the DLL
set -e
cd "$(dirname "$0")"
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
CC="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang"
SRC="src/main.c src/log.c src/proxy.c src/hooks.c src/game.c src/control.c src/crash.c src/scripts.c src/net.c
	src/party.c src/progress.c src/scrdebug.c src/winmm_stubs.S src/winmm.def"
mkdir -p build
if [ "$1" = release ]; then
	"$CC" -O2 -Wall -Wextra -shared -static -static-libgcc -s -o build/winmm.dll $SRC -lws2_32
else
	"$CC" -O2 -g -Wall -Wextra -shared -static -static-libgcc -o build/winmm.dll $SRC -lws2_32 \
		-Wl,--pdb=build/winmm.pdb
fi
echo "built build/winmm.dll${1:+ ($1)}"

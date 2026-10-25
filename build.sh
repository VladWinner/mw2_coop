#!/bin/sh
# Build build/winmm.dll (x64) with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
# Usage: ./build.sh [release]
#   LLVM_MINGW  llvm-mingw folder (default: ../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64)
#   release     optimized and stripped: no debug info and no local paths inside the DLL
set -e
cd "$(dirname "$0")"
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
CC="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang"
SRC="src/main.c src/log.c src/proxy.c src/hooks.c src/hook.c src/game.c src/control.c src/crash.c src/scripts.c src/localize.c src/memdump.c src/menus.c src/mirror.c src/transplant.c src/access.c src/settings.c src/upnp.c src/steamnet.c src/firewall.c src/zones.c src/net.c src/fonts.c
	src/party.c src/progress.c src/scrdebug.c src/shot.c src/fov.c src/winmm_stubs.S src/winmm.def"
if [ "$1" = x86 ]; then
	# the 32-bit build of the game (see docs/32bit-plan.md): only the first slice is ported so far
	CC32="$LLVM_MINGW/bin/i686-w64-mingw32-clang"
	mkdir -p build/x86
	"$CC32" -O2 -Wall -Wextra -shared -static -static-libgcc -Isrc/x86 -Isrc -o build/x86/winmm.dll 		src/x86/main32.c src/x86/stubs32.c src/log.c src/proxy.c src/hooks.c src/hook.c src/game.c src/scripts.c src/zones.c 		src/control.c src/party.c src/settings.c src/upnp.c src/crash.c src/fov.c src/mirror.c src/localize.c src/progress.c src/fonts.c src/x86/winmm_stubs.S src/x86/winmm.def -lbcrypt -lws2_32 -liphlpapi -lgdi32
	echo "built build/x86/winmm.dll"
	exit 0
fi
mkdir -p build
if [ "$1" = release ]; then
	"$CC" -O2 -Wall -Wextra -shared -static -static-libgcc -Isrc -s -o build/winmm.dll $SRC -lws2_32 -liphlpapi -lgdi32
else
	"$CC" -O2 -g -Wall -Wextra -shared -static -static-libgcc -Isrc -o build/winmm.dll $SRC -lws2_32 -liphlpapi -lgdi32 \
		-Wl,--pdb=build/winmm.pdb
fi
echo "built build/winmm.dll${1:+ ($1)}"

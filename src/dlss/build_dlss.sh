#!/bin/sh
# Build build/mw2coop_dlss.dll, the DLSS bridge (src/dlss/README.md), with llvm-mingw. Needs NVIDIA's DLSS SDK on this machine (it is NOT part of this repository; license: NVIDIA RTX SDKs
# License, see the SDK's LICENSE.txt). Usage: sh src/dlss/build_dlss.sh [release]
#   DLSS_SDK    the SDK folder (the NVIDIA/DLSS repository checkout; default ../third-party/dlss, next to the mod's folder)
#   LLVM_MINGW  llvm-mingw folder (default: ../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64)
set -e
cd "$(dirname "$0")/../.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
DLSS_SDK=${DLSS_SDK:-../third-party/dlss}
if [ ! -f "$DLSS_SDK/include/nvsdk_ngx.h" ] || [ ! -f "$DLSS_SDK/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib" ]; then
	echo "no DLSS SDK in $DLSS_SDK (include/nvsdk_ngx.h, lib/Windows_x86_64/x64/nvsdk_ngx_s.lib): set DLSS_SDK; the bridge is built only where the SDK is"
	exit 1
fi
CC="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang"
CXX="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++"
STRIP=; [ "$1" = release ] && STRIP=-s
mkdir -p build/dlss
"$CC" -O2 -Wall -Wextra -c src/dlss/msvc_shims.c -o build/dlss/msvc_shims.o
"$CXX" -O2 -Wall -Wextra -fno-exceptions -fno-rtti -std=c++17 -I"$DLSS_SDK/include" -c src/dlss/dlss_bridge.cpp -o build/dlss/dlss_bridge.o
# the SDK's static library is built with MSVC: its default-library directives for the MSVC runtime are ignored, the few runtime symbols it needs come from msvc_shims.c
"$CXX" -shared -static $STRIP -o build/mw2coop_dlss.dll build/dlss/dlss_bridge.o build/dlss/msvc_shims.o "$DLSS_SDK/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib" \
	-ladvapi32 -lole32 -Wl,/nodefaultlib:libcpmt -Wl,/nodefaultlib:LIBCMT -Wl,/nodefaultlib:OLDNAMES
echo "built build/mw2coop_dlss.dll"

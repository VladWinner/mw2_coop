#!/bin/sh
# Build build/dlssprobe.exe with the same llvm-mingw as build.sh. DLSS_SDK: the NVIDIA DLSS SDK folder (default: ../third-party/dlss).
set -e
cd "$(dirname "$0")/../.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
DLSS_SDK=${DLSS_SDK:-../third-party/dlss}
mkdir -p build
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" -O1 -Wno-unknown-pragmas -I"$DLSS_SDK/include" -o build/dlssprobe.exe tools/dlssprobe/dlssprobe.cpp tools/dlssprobe/msvc_shim.cpp "$DLSS_SDK/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib" -Lbuild/ngxstubs -luser32 -ladvapi32 -lole32 -lversion -lshlwapi -lwintrust

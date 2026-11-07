#!/bin/sh
# Build build/ffxprobe.exe with the same llvm-mingw as build.sh. FFX_SDK: the FidelityFX SDK's Kits/FidelityFX folder (default: ../third-party/fidelityfx-sdk/Kits/FidelityFX).
set -e
cd "$(dirname "$0")/../.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
FFX_SDK=${FFX_SDK:-../third-party/fidelityfx-sdk/Kits/FidelityFX}
mkdir -p build
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" -O1 -Wall -Wno-unknown-pragmas -I"$FFX_SDK/api/include" -I"$FFX_SDK/upscalers/include" -o build/ffxprobe.exe tools/ffxprobe/ffxprobe.c -lkernel32 -luser32

#!/bin/sh
# Build and run the packet guard's unit test with the same llvm-mingw as build.sh (LLVM_MINGW overrides its folder).
set -e
cd "$(dirname "$0")/.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
mkdir -p build
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" -O1 -Wall -Wextra -o build/test_netguard.exe tools/test_netguard.c src/netguard.c
build/test_netguard.exe

#!/bin/sh
# Builds tools/test_addr.exe (the resolver of src/addr.c on a game exe file) with llvm-mingw.
set -e
cd "$(dirname "$0")/.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" -O2 -Wall -Wextra -DADDR_TEST -Isrc -o build/test_addr.exe tools/test_addr.c src/addr.c
echo "built build/test_addr.exe"

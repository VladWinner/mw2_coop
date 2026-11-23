#!/bin/sh
# Tests of the updater's core (src/updcore.c) outside the game, on both builds' compilers (x86_64 and i686):
#   sh tools/updcore_test.sh [archive.zip ...]
# 1. the self test (versions, paths, CRC-32, SHA-256, JSON, inflate of known streams);
# 2. archives made here by Python's zipfile (random and repeating data, deflate levels 0-9, stored, empty files, one large file) and the archives given
#    (the release's), unpacked by the core and compared file by file with Python's own unpacking;
# 3. damaged copies of each archive: nothing may crash, and no damaged file may come out as a good one.
set -e
cd "$(dirname "$0")/.."
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
OUT=build/updcore_test
mkdir -p $OUT
for arch in x86_64 i686; do
	"$LLVM_MINGW/bin/$arch-w64-mingw32-clang" -O2 -g -Wall -Wextra -o $OUT/updcore_test_$arch.exe tools/updcore_test.c src/updcore.c -lbcrypt
done
python -I - "$OUT" <<'PY'
import os, random, sys, zipfile
out = sys.argv[1]
rnd = random.Random(7)
files = {
    "mw2coop/random.bin": bytes(rnd.getrandbits(8) for _ in range(300000)),
    "mw2coop/text.txt": ("".join(rnd.choice("abc ") for _ in range(50000)) * 3).encode(),
    "mw2coop/zeros.bin": bytes(2000000),
    "mw2coop/empty.txt": b"",
    "mw2coop/sub/dir/one.txt": b"x",
    "winmm.dll": bytes(range(256)) * 700,
}
for level in range(10):
    with zipfile.ZipFile(os.path.join(out, "deflate%d.zip" % level), "w", zipfile.ZIP_DEFLATED, compresslevel=level) as z:
        for name, data in files.items():
            z.writestr(name, data)
with zipfile.ZipFile(os.path.join(out, "stored.zip"), "w", zipfile.ZIP_STORED) as z:
    for name, data in files.items():
        z.writestr(name, data)
    z.writestr("mw2coop/dir/", b"")  # a directory entry (left out by the core)
PY
fail=0
for arch in x86_64 i686; do
	echo "== $arch"
	$OUT/updcore_test_$arch.exe selftest || fail=1
done
for z in $OUT/deflate*.zip $OUT/stored.zip "$@"; do
	for arch in x86_64 i686; do
		$OUT/updcore_test_$arch.exe zip "$z" > $OUT/mine.txt || { echo "FAIL unpack $z ($arch)"; fail=1; }
		python -I - "$z" <<'PY' | tr -d '\r' > $OUT/python.txt
import hashlib, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for i in z.infolist():
    if i.filename.endswith("/"):
        continue
    d = z.read(i)
    print(hashlib.sha256(d).hexdigest(), len(d), i.filename)
PY
		sed 's/ (refused path)$//' $OUT/mine.txt | tr -d '\r' > $OUT/mine2.txt
		if cmp -s $OUT/mine2.txt $OUT/python.txt; then echo "ok   $z ($arch): $(wc -l < $OUT/python.txt) files"; else echo "FAIL $z ($arch) differs from Python"; diff $OUT/mine2.txt $OUT/python.txt | head; fail=1; fi
	done
	$OUT/updcore_test_x86_64.exe fuzz "$z" 1 300 || { echo "FAIL fuzz $z"; fail=1; }
done
[ $fail = 0 ] && echo "updcore: all passed" || { echo "updcore: FAILED"; exit 1; }

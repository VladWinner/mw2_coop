"""Finds the rip-relative LEA / MOV instructions of a 64-bit PE image that point at an rva (from the file on disk).
Usage: python -I tools/leascan.py <exe> <target rva hex> [<target rva hex> ...]"""
import struct, sys

data = open(sys.argv[1], 'rb').read()
targets = [int(a, 16) for a in sys.argv[2:]]
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
sec = pe + 24 + opt
for i in range(nsec):
    name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', data, sec + 40 * i)
    if not name.startswith(b'.text'):
        continue
    buf = data[raw:raw + rsize]
    for j in range(len(buf) - 7):
        # REX.W 8d /r with mod=00 rm=101 (lea r64,[rip+disp32]); also 8b (mov) and 0f 10 etc. are not needed here
        if buf[j] in (0x48, 0x4c) and buf[j + 1] == 0x8d and (buf[j + 2] & 0xc7) == 0x05:
            disp = struct.unpack_from('<i', buf, j + 3)[0]
            tgt = va + j + 7 + disp
            for t in targets:
                if tgt == t:
                    print('lea at rva 0x%x -> 0x%x' % (va + j, t))

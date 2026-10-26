# Prints the bytes of a 64-bit PE image at an rva (from the file on disk): python -I tools/rvabytes.py <exe> <rva hex> [count]
import struct, sys
data = open(sys.argv[1], 'rb').read()
rva = int(sys.argv[2], 16)
count = int(sys.argv[3]) if len(sys.argv) > 3 else 16
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
sec = pe + 24 + opt
for i in range(nsec):
    name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', data, sec + 40 * i)
    if va <= rva < va + max(vsize, rsize):
        off = raw + rva - va
        print(' '.join('%02x' % b for b in data[off:off + count]))
        break
else:
    print('rva not in a section')

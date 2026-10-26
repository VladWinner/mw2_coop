"""Contact sheet of BMP screenshots (no libraries): python tools/sheet.py out.png cols scale in1.bmp in2.bmp ...
Each picture is shrunk by the integer `scale` (box filter) and put in a grid; 24 and 32 bit BMPs."""
import struct, sys, zlib


def read_bmp(path):
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h, planes, bpp = struct.unpack_from('<iiHH', d, 18)
    flip = h > 0
    h = abs(h)
    bp = bpp // 8
    stride = (w * bp + 3) & ~3
    rows = []
    for y in range(h):
        sy = h - 1 - y if flip else y
        rows.append(d[off + sy * stride: off + sy * stride + w * bp])
    return w, h, bp, rows


def shrink(img, s):
    w, h, bp, rows = img
    ow, oh = w // s, h // s
    out = []
    for oy in range(oh):
        acc = [[0, 0, 0] for _ in range(ow)]
        for dy in range(s):
            r = rows[oy * s + dy]
            for ox in range(ow):
                for dx in range(s):
                    i = (ox * s + dx) * bp
                    a = acc[ox]
                    a[0] += r[i + 2]; a[1] += r[i + 1]; a[2] += r[i]
        n = s * s
        out.append(b''.join(bytes((a[0] // n, a[1] // n, a[2] // n)) for a in acc))
    return ow, oh, out


def write_png(path, w, h, rows):
    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(t, c):
        x = struct.pack('>I', len(c)) + t + c
        return x + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


out, cols, scale = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
tiles = [shrink(read_bmp(p), scale) for p in sys.argv[4:]]
tw, th = tiles[0][0], tiles[0][1]
nrows = (len(tiles) + cols - 1) // cols
rows = [bytearray(tw * cols * 3) for _ in range(th * nrows)]
for k, (w, h, r) in enumerate(tiles):
    cx, cy = (k % cols) * tw, (k // cols) * th
    for y in range(h):
        rows[cy + y][cx * 3:(cx + w) * 3] = r[y]
        if y == 0 or y == h - 1:
            rows[cy + y][cx * 3:(cx + w) * 3] = b'\xff\x00\x00' * w
write_png(out, tw * cols, th * nrows, [bytes(r) for r in rows])

"""Crops a BMP screenshot to a PNG, optionally enlarged: python tools/crop.py in.bmp out.png x y w h [zoom]"""
import sys, struct, zlib
sys.argv, args = sys.argv[:1], sys.argv[1:]
exec(open(__file__.replace('crop.py', 'sheet.py')).read().split("out, cols, scale")[0])
src, out, x, y, w, h = args[0], args[1], *map(int, args[2:6])
zoom = int(args[6]) if len(args) > 6 else 1
W, H, bp, rows = read_bmp(src)
res = []
for yy in range(y, y + h):
    r = rows[yy]
    line = b''.join(bytes((r[i * bp + 2], r[i * bp + 1], r[i * bp])) * zoom for i in range(x, x + w))
    res.extend([line] * zoom)
write_png(out, w * zoom, h * zoom, res)

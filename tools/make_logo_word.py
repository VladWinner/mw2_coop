"""Makes content/release/logo-enhanced.tga: the word ENHANCED in the style of the game's logo (white-green letters with a green glow and the old TV's scan lines),
1024 x 256 with a transparent background, to be put under MODERN WARFARE by src/logo.c at run time. The picture is the mod's own: the face is the bundled H*-Mod
Bank Gothic style font (content/fonts/bank.ttf, SIL OFL); nothing of the game's logo is in it, only numbers measured on it (below).

What the game's logo looks like (measured 2026-11-24 on the dumped 1024 x 256 logo texture, rows from the top): MODERN's letters span columns 24..465 and rows
111..153; the letters and their glow carry baked scan lines, period 5.15 rows, the brightest rows at 111.6 + k * 5.15, nearly white there (253, 253, 253) and
green-grey between them (194, 216, 177); the glow is striped the same way. The word here is laid on the same columns as MODERN and on the same scan lines
(owner 2026-11-24: ENHANCED stood off MODERN's edge and had none of the old TV look).

  python -I tools/make_logo_word.py            write the file
  python -I tools/make_logo_word.py --preview <logo png, rows from the top>   also write a preview of the word over the game's logo (for looking only)
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "..", "content", "fonts", "bank.ttf")
OUT = os.path.join(HERE, "..", "content", "release", "logo-enhanced.tga")
W, H = 1024, 256
TEXT = "ENHANCED"
LEFT, RIGHT = 24, 465     # MODERN's ink, first and last column
TOP = 170                 # the word's cap top (a little under MODERN, whose letters end at row 153)
CAP_HEIGHT = 34           # the game's wordmark is 42 tall
SCAN_PERIOD = 5.15        # the game's scan lines: period and a bright row (texture rows)
SCAN_PEAK = 111.6
BRIGHT = np.array([253, 253, 253], dtype=np.float32)  # the face on a bright line and between two
DARK = np.array([194, 216, 177], dtype=np.float32)
GREEN = np.array([70, 215, 90], dtype=np.float32)     # the glow


def word_mask():
    size = 8
    while True:
        font = ImageFont.truetype(FONT, size)
        box = font.getbbox("E")
        if box[3] - box[1] >= CAP_HEIGHT:
            break
        size += 1
    cap_top = font.getbbox("E")[1]

    def draw(gap, shift):
        mask = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(mask)
        x = 0.0
        for c in TEXT:
            b = font.getbbox(c)
            d.text((round(shift + x - b[0]), TOP - cap_top), c, font=font, fill=255)
            x += b[2] - b[0] + gap
        return mask

    def ink(mask):
        cols = np.where(np.array(mask).max(0) >= 128)[0]
        return int(cols[0]), int(cols[-1])

    # the gaps even, then the word moved and the gaps widened so that its ink spans MODERN's columns exactly
    gap, shift = 10.0, float(LEFT)
    for _ in range(4):
        left, right = ink(draw(gap, shift))
        shift += LEFT - left
        gap += ((RIGHT - right) - (LEFT - left)) / (len(TEXT) - 1)
    mask = draw(gap, shift)
    print("ENHANCED ink: columns %d..%d (MODERN %d..%d), gap %.2f" % (ink(mask) + (LEFT, RIGHT, gap)))
    return mask


def build():
    mask = word_mask()
    m = np.array(mask, dtype=np.float32) / 255.0
    glow_wide = np.array(mask.filter(ImageFilter.GaussianBlur(7)), dtype=np.float32) / 255.0
    glow_near = np.array(mask.filter(ImageFilter.GaussianBlur(2.5)), dtype=np.float32) / 255.0
    ys = np.arange(H, dtype=np.float32)[:, None]
    # the scan lines: 1 on a bright row, 0 halfway between two
    s = (0.5 + 0.5 * np.cos(2 * np.pi * (ys - SCAN_PEAK) / SCAN_PERIOD))[:, :, None]
    face = DARK + (BRIGHT - DARK) * s
    # the glow: a wide soft green halo and a closer one, under the face, striped like the game's
    ga = np.clip(glow_wide * 0.85 + glow_near * 0.55, 0, 1) * (0.55 + 0.45 * s[:, :, 0])
    face_rgb = np.broadcast_to(face, (H, W, 3))
    # colour of the pixel: the face where the letter is, the green where only the glow is (premultiplied mix, then divided by alpha)
    num = face_rgb * m[:, :, None] + GREEN * (ga * (1 - m))[:, :, None]
    den = np.maximum(m + ga * (1 - m), 1e-4)[:, :, None]
    out_rgb = num / den
    out_a = np.clip(m + ga * (1 - m), 0, 1)
    rgba = np.dstack([out_rgb, out_a * 255]).clip(0, 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def write_tga(img, path):
    a = np.array(img)
    bgra = a[:, :, [2, 1, 0, 3]]
    head = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, W, H, 32, 8 | 0x20)  # uncompressed, 32 bit, top-left origin
    with open(path, "wb") as f:
        f.write(head)
        f.write(bgra.tobytes())


def main():
    img = build()
    write_tga(img, OUT)
    print("wrote", os.path.normpath(OUT))
    if "--preview" in sys.argv:
        logo = Image.open(sys.argv[sys.argv.index("--preview") + 1]).convert("RGBA")
        logo.alpha_composite(img)
        bg = Image.new("RGBA", logo.size, (74, 74, 70, 255))
        bg.alpha_composite(logo)
        bg.convert("RGB").save(os.path.join(HERE, "..", "..", "scratch", "logo", "preview.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

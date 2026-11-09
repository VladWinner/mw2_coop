"""Makes content/release/logo-enhanced.tga: the word ENHANCED in the style of the game's logo (white-green letters with a green glow and the faint scan lines),
1024 x 256 with a transparent background, to be put under MODERN WARFARE by src/logo.c at run time. The picture is the mod's own: the face is the bundled H*-Mod
Bank Gothic style font (content/fonts/bank.ttf, SIL OFL); nothing of the game's logo is in it.

  python -I tools/make_logo_word.py            write the file
  python -I tools/make_logo_word.py --preview <logo dump png>   also write a preview of the word over the dumped game logo (for looking only)
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
LEFT, TOP = 24, 170       # where the word starts (the left edge of MODERN, a little under the game's line)
CAP_HEIGHT = 34           # the game's wordmark is 42 tall
TARGET_WIDTH = 452        # as wide as MODERN


def word_mask():
    size = 8
    while True:
        font = ImageFont.truetype(FONT, size)
        box = font.getbbox("E")
        if box[3] - box[1] >= CAP_HEIGHT:
            break
        size += 1
    glyphs = [font.getbbox(c) for c in TEXT]
    ink = sum(b[2] - b[0] for b in glyphs)
    gap = max(2, (TARGET_WIDTH - ink) // (len(TEXT) - 1))
    mask = Image.new("L", (W, H), 0)
    draw = ImageDraw.Draw(mask)
    x = LEFT
    cap_top = font.getbbox("E")[1]
    for c, b in zip(TEXT, glyphs):
        draw.text((x - b[0], TOP - cap_top), c, font=font, fill=255)
        x += b[2] - b[0] + gap
    return mask


def build():
    mask = word_mask()
    m = np.array(mask, dtype=np.float32) / 255.0
    glow_wide = np.array(mask.filter(ImageFilter.GaussianBlur(7)), dtype=np.float32) / 255.0
    glow_near = np.array(mask.filter(ImageFilter.GaussianBlur(2.5)), dtype=np.float32) / 255.0
    ys = np.arange(H, dtype=np.float32)[:, None]
    # face: white at the top, a little green at the bottom, like the game's letters; scan lines: every third row a little darker
    top = np.array([236, 250, 236], dtype=np.float32)
    bottom = np.array([190, 222, 190], dtype=np.float32)
    t = np.clip((ys - TOP) / CAP_HEIGHT, 0, 1)[:, :, None]
    face = top * (1 - t) + bottom * t
    scan = np.where((np.arange(H)[:, None] % 3) == 0, 0.82, 1.0).astype(np.float32)
    green = np.array([70, 215, 90], dtype=np.float32)
    # the glow: a wide soft green halo and a closer one, under the face
    ga = np.clip(glow_wide * 0.85 + glow_near * 0.55, 0, 1)
    out_a = np.clip(ga + m - ga * m, 0, 1)
    out_rgb = np.zeros((H, W, 3), dtype=np.float32)
    face_rgb = np.broadcast_to(face, (H, W, 3))
    # colour of the pixel: the face where the letter is, the green where only the glow is (premultiplied mix, then divided by alpha)
    num = face_rgb * (m * scan)[:, :, None] + green * (ga * (1 - m))[:, :, None]
    den = np.maximum((m * scan + ga * (1 - m)), 1e-4)[:, :, None]
    out_rgb = num / den
    out_a = np.clip(m * (0.9 + 0.1 * scan) + ga * (1 - m), 0, 1)
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

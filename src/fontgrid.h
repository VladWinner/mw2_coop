// The game's glyph quads live on a grid (x0, y0 are s8, pixelWidth and pixelHeight u8, dx u8), while fonts.c rasterizes every glyph at FONT_RENDER_SCALE (k) texels per logical
// pixel. The grid's unit is 1/m of a logical pixel: the font's pixelHeight is stored as size * m and the engine normalizes text by it, so the size on the screen does not
// change with m (the method of ttf2iwi: m = 1 is the stock whole-pixel table, m = 2 or 4 puts positions and advances on a half or quarter of a pixel). One unit is q = k / m
// texels. m must divide k.
//
// Rounding the four edges of the ink to the grid one by one (the quad then no longer matches the bitmap that is stretched onto it) makes equal letters differ by up to
// a unit in height and position, and stretches some glyphs by more than 10 % at small sizes: the text looks like it jumps up and down. The fix is to grow the quad
// outwards to the grid, and to put the same transparent margin around the bitmap, so that the quad and the bitmap are exactly q texels per unit and nothing is resampled
// or moved.
//
// Pure integer maths, no Windows headers: tools/test_fontgrid.c checks it on any host.
#pragma once

// Floor and ceiling of a / d for d > 0 and any sign of a (C division truncates towards zero).
static inline int fontgrid_floor_div(int a, int d)
{
	int q = a / d;
	return (a % d != 0 && a < 0) ? q - 1 : q;
}

static inline int fontgrid_ceil_div(int a, int d)
{
	return -fontgrid_floor_div(-a, d);
}

// value / d rounded to the nearest integer, halves away from zero (an advance in texels to units).
static inline int fontgrid_round_div(double value, int d)
{
	double v = value / d;
	return v < 0.0 ? -(int)(-v + 0.5) : (int)(v + 0.5);
}

typedef struct {
	int left;     // units from the pen to the quad's left edge (the game's x0)
	int top;      // units from the baseline up to the quad's top edge
	int width;    // quad size in units (the game's pixelWidth, pixelHeight)
	int height;
	int pad_left; // transparent texels to add left of / above the rasterized ink so that it fills the quad exactly (0 .. scale - 1)
	int pad_top;
} FontGridBox;

// The ink as the rasterizer reports it, in texels: ox from the pen to the ink's left edge, oy from the baseline up to the ink's top edge (GDI's gmptGlyphOrigin), w and h its
// size; scale = q texels per unit. The result is the smallest whole-unit quad that holds all of it. The bitmap to put on the quad is (width * scale) x (height * scale)
// texels with the ink at (pad_left, pad_top).
static inline FontGridBox fontgrid_box(int ox, int oy, int w, int h, int scale)
{
	FontGridBox b;
	int right = fontgrid_ceil_div(ox + w, scale);
	int bottom = fontgrid_floor_div(oy - h, scale);
	b.left = fontgrid_floor_div(ox, scale);
	b.top = fontgrid_ceil_div(oy, scale);
	b.width = right - b.left;
	b.height = b.top - bottom;
	b.pad_left = ox - b.left * scale;
	b.pad_top = b.top * scale - oy;
	return b;
}

// Everything the game's 24-byte glyph record needs from one glyph, in units.
typedef struct {
	FontGridBox box;
	int y0; // from the pen (the bottom of the font's cell) up: -(baseline + top)
	int dx; // the advance
} FontGridMetrics;

// `base` is the baseline's height above the pen in units, `adv` the advance in texels. A glyph without ink (w or h 0) has no box.
static inline FontGridMetrics fontgrid_metrics(int ox, int oy, int w, int h, double adv, int base, int scale)
{
	FontGridMetrics g = {{0, 0, 0, 0, 0, 0}, 0, fontgrid_round_div(adv, scale)};
	if (w > 0 && h > 0) {
		g.box = fontgrid_box(ox, oy, w, h, scale);
		g.y0 = -(base + g.box.top);
	}
	return g;
}

// 1 when the record's fields hold the values: x0, y0 s8, pixelWidth, pixelHeight, dx u8. A glyph without ink needs only its advance to fit.
static inline int fontgrid_in_range(const FontGridMetrics *g, int has_ink)
{
	if (g->dx > 255)
		return 0;
	if (!has_ink)
		return 1;
	return g->box.left >= -128 && g->box.left <= 127 && g->y0 >= -128 && g->y0 <= 127 && g->box.width >= 1 && g->box.width <= 255 && g->box.height >= 1 &&
	       g->box.height <= 255;
}

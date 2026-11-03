// Host test of src/fontgrid.h (no Windows needed):  gcc -O2 -Wall -Wextra -Isrc -o /tmp/test_fontgrid tools/test_fontgrid.c && /tmp/test_fontgrid
// For every ink box in a wide range (negative bearings, boxes below the baseline, sizes from 1 texel up) the quad must hold the ink, be no bigger than needed,
// be on the grid, and the bitmap padded by (pad_left, pad_top) must land exactly on it, so a quad texel is never stretched or moved.
#include <stdio.h>
#include <stdlib.h>

#include "fontgrid.h"

int main(void)
{
	long checked = 0, bad = 0;
	for (int scale = 1; scale <= 8; scale++)
		for (int ox = -37; ox <= 37; ox++)
			for (int oy = -37; oy <= 53; oy++)
				for (int w = 1; w <= 26; w++)
					for (int h = 1; h <= 26; h++) {
						FontGridBox b = fontgrid_box(ox, oy, w, h, scale);
						checked++;
						int ok = 1;
						// the padded bitmap is exactly the quad at `scale` texels per pixel
						int W = b.width * scale, H = b.height * scale;
						// the ink sits inside it where it should: pen-relative position of the ink's left / top edge is unchanged
						ok &= b.left * scale + b.pad_left == ox;
						ok &= b.top * scale - b.pad_top == oy;
						// the ink fits
						ok &= b.pad_left >= 0 && b.pad_left + w <= W;
						ok &= b.pad_top >= 0 && b.pad_top + h <= H;
						// the quad is the smallest one: less than a whole pixel of margin on every side
						ok &= b.pad_left < scale && b.pad_top < scale;
						ok &= W - (b.pad_left + w) < scale && H - (b.pad_top + h) < scale;
						ok &= b.width >= 1 && b.height >= 1;
						if (!ok && bad++ < 10)
							printf("FAIL scale %d ox %d oy %d w %d h %d -> left %d top %d %dx%d pad %d,%d\n", scale, ox, oy, w, h, b.left, b.top, b.width, b.height,
							       b.pad_left, b.pad_top);
					}
	// the helpers on their own
	static const int a[] = {-9, -8, -7, -4, -3, -1, 0, 1, 3, 4, 5, 8, 9};
	for (unsigned i = 0; i < sizeof(a) / sizeof(a[0]); i++) {
		int fl = fontgrid_floor_div(a[i], 4), ce = fontgrid_ceil_div(a[i], 4);
		int exp_fl = (int)(a[i] >= 0 ? a[i] / 4 : -((-a[i] + 3) / 4));
		int exp_ce = (int)(a[i] >= 0 ? (a[i] + 3) / 4 : -((-a[i]) / 4));
		checked++;
		if (fl != exp_fl || ce != exp_ce) {
			bad++;
			printf("FAIL div %d: floor %d (want %d) ceil %d (want %d)\n", a[i], fl, exp_fl, ce, exp_ce);
		}
	}
	// advances: nearest unit, halves away from zero, never more than half a unit off
	{
		struct { double v; int d, want; } r[] = {{6, 4, 2}, {-6, 4, -2}, {5, 4, 1}, {2, 4, 1}, {-2, 4, -1}, {1, 4, 0}, {0, 4, 0}, {7.5, 2, 4}, {9, 1, 9}};
		for (unsigned i = 0; i < sizeof(r) / sizeof(r[0]); i++) {
			checked++;
			if (fontgrid_round_div(r[i].v, r[i].d) != r[i].want && bad++ < 10)
				printf("FAIL round_div(%g, %d) = %d, want %d\n", r[i].v, r[i].d, fontgrid_round_div(r[i].v, r[i].d), r[i].want);
		}
		for (int d = 1; d <= 8; d++)
			for (int t = 0; t <= 4000; t++) {
				double adv = t * 0.37;
				double err = fontgrid_round_div(adv, d) * (double)d - adv;
				checked++;
				if ((err > d / 2.0 + 1e-9 || err < -d / 2.0 - 1e-9) && bad++ < 10)
					printf("FAIL advance %g at %d texels per unit is %g texels off\n", adv, d, err);
			}
	}
	// the glyph record: values and the edges of the s8 / u8 fields
	{
		FontGridMetrics g = fontgrid_metrics(-5, 30, 22, 30, 41.5, 9, 4); // ink from x -5 to 17, y 30 down to 0, at 4 texels per unit
		checked++;
		if (!(g.box.left == -2 && g.box.top == 8 && g.box.width == 7 && g.box.height == 8 && g.y0 == -(9 + 8) && g.dx == 10) && bad++ < 10)
			printf("FAIL metrics: left %d top %d %dx%d y0 %d dx %d\n", g.box.left, g.box.top, g.box.width, g.box.height, g.y0, g.dx);
		g = fontgrid_metrics(0, 0, 0, 0, 12, 9, 4); // no ink: only the advance
		checked++;
		if (!(g.box.width == 0 && g.box.height == 0 && g.y0 == 0 && g.dx == 3 && fontgrid_in_range(&g, 0)) && bad++ < 10)
			printf("FAIL metrics without ink\n");
		struct { int ox, oy, w, h; double adv; int base, want; const char *what; } e[] = {
			{-128, 8, 4, 4, 4, 0, 1, "x0 = -128"},   {-129, 8, 4, 4, 4, 0, 0, "x0 = -129"},  {127 , 8, 1, 4, 4, 0, 1, "x0 = 127"},  {128, 8, 1, 4, 4, 0, 0, "x0 = 128"},
			{0, 128, 4, 4, 4, 0, 1, "y0 = -128"},    {0, 129, 4, 4, 4, 0, 0, "y0 = -129"},   {0, 8, 255, 4, 4, 0, 1, "width 255"},  {0, 8, 256, 4, 4, 0, 0, "width 256"},
			{0, 255, 4, 255, 4, 0, 0, "y0 too low"}, {0, 8, 4, 255, 4, 0, 1, "height 255"},  {0, 8, 4, 256, 4, 0, 0, "height 256"},  {0, 8, 4, 4, 255, 0, 1, "dx 255"},
			{0, 8, 4, 4, 256, 0, 0, "dx 256"},       {0, 8, 4, 4, 4, 120, 1, "baseline 120 + top 8: y0 = -128"}, {0, 8, 4, 4, 4, 121, 0, "baseline 121 + top 8: y0 = -129"},
		};
		for (unsigned i = 0; i < sizeof(e) / sizeof(e[0]); i++) {
			FontGridMetrics m = fontgrid_metrics(e[i].ox, e[i].oy, e[i].w, e[i].h, e[i].adv, e[i].base, 1);
			checked++;
			if (fontgrid_in_range(&m, 1) != e[i].want && bad++ < 10)
				printf("FAIL range: %s -> %d, want %d\n", e[i].what, fontgrid_in_range(&m, 1), e[i].want);
		}
	}
	printf("%ld cases, %ld failures\n", checked, bad);
	return bad ? 1 : 0;
}

// A font closer to the English original for the Russian game.
//
// The original game draws its menus in Bank Gothic and ITC Conduit and its briefing screens in T26 Carbon. Those fonts have no
// Cyrillic, so the Russian build draws everything in one generic condensed sans. The mod replaces the glyphs of the game's
// font assets in memory with glyphs rendered (Windows GDI) from free fonts with Cyrillic that look like the originals: Tektur for
// the headings and the briefing, Fira Sans Condensed for the running text (both SIL OFL, shipped in <game dir>\mw2coop\fonts).
//
// How the game's fonts work (mapped with "!font <name>", memdump.c):
//   Font_s (asset type 24): name, pixelHeight (int), glyphCount (int), Material *material, Material *glowMaterial, Glyph *glyphs.
//   Glyph (24 bytes): letter (u16, a Windows-1251 code in this build), x0 y0 (s8: from the pen to the glyph's top left; the pen is
//   at the bottom of the font's cell, the baseline about pixelHeight / 8 above it), dx (u8 advance), pixelWidth pixelHeight (u8), pad,
//   s0 t0 s1 t1 (floats: the glyph's rectangle in the atlas).
//   All fonts of the build share one atlas: material "fonts/gamefonts_pc" (the glow material uses the same texture), a DXT5 texture
//   (512 x 844, white colour, the glyph shapes in alpha) reached as Material -> textureTable (MaterialTextureDef, 16 bytes, GfxImage *
//   at +8) -> GfxImage, whose first field is the IDirect3DTexture9 * and which holds width/height as two u16 at +0x18.
// A new atlas does not fit the old texture (more glyph area), so a bigger texture is made on the game's own device (taken from the old
// texture), filled, and put into the GfxImage; the glyph tables get their new metrics and rectangles. The glyphs of the Latin
// letters and of their Cyrillic look-alikes are the same bitmaps, as in the game's own tables.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fonts.h"
#include "localize.h"
#include "log.h"

#ifdef _WIN64
#define FONT_HEIGHT 8
#define FONT_COUNT 0xc
#define FONT_MATERIAL 0x10
#define FONT_GLYPHS 0x20
#define MATERIAL_TABLE 0x60
#define IMAGE_DIMS 0x18
#define IMAGE_DATALEN 0x10
#else
#define FONT_HEIGHT 4
#define FONT_COUNT 8
#define FONT_MATERIAL 0x0c
#define FONT_GLYPHS 0x14
#define MATERIAL_TABLE 0x30
#define IMAGE_DIMS 0x18
#define IMAGE_DATALEN 0x10
#endif
#define DEF_IMAGE 8
#define GLYPH_SIZE 24

enum { FACE_TEKTUR, FACE_FIRA };

// The font assets that live on the shared atlas, and the face of each. The heading fonts (Bank Gothic in the English game) and the
// briefing text get Tektur; the running text (Conduit) gets Fira Sans Condensed.
static const struct {
	const char *name;
	int face;
	int bold;
} plan[] = {
	{"fonts/bigfont", FACE_TEKTUR, 0},      {"fonts/extrabigfont", FACE_TEKTUR, 0}, {"fonts/objectivefont", FACE_TEKTUR, 0},
	{"fonts/hudbigfont", FACE_TEKTUR, 0},   {"fonts/hudsmallfont", FACE_TEKTUR, 0}, {"fonts/normalfont", FACE_FIRA, 0},
	{"fonts/smallfont", FACE_FIRA, 0},      {"fonts/boldfont", FACE_FIRA, 1},       {"fonts/consolefont", FACE_FIRA, 0},
};
#define PLAN_COUNT (int)(sizeof(plan) / sizeof(plan[0]))

typedef struct {
	int w, h;       // black box
	int ox, oy;     // left bearing, top above the baseline
	int adv;        // advance
	uint8_t *alpha; // w * h, 0..255
	uint32_t hash;
	int x, y;       // where the bitmap sits in the atlas (set by the packer)
	int shared;     // index of an identical bitmap, or -1
} Bitmap;

static Bitmap *bitmaps;
static int bitmap_count, bitmap_cap;

static int readable(const void *p, size_t size)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		return 0;
	return (const uint8_t *)p + size <= (const uint8_t *)mbi.BaseAddress + mbi.RegionSize;
}

static uint8_t *material_image(const uint8_t *material)
{
	if (!material || !readable(material, MATERIAL_TABLE + sizeof(void *)))
		return NULL;
	const uint8_t *table = *(const uint8_t *const *)(material + MATERIAL_TABLE);
	if (!table || !readable(table, 16))
		return NULL;
	uint8_t *image = *(uint8_t *const *)(table + DEF_IMAGE);
	return image && readable(image, 0x30) ? image : NULL;
}

// ---- GDI: the faces and their glyphs ------------------------------------------------------------------------------------------

static char font_dir[MAX_PATH];
static HDC dc;

static int load_faces(void)
{
	static const char *const files[] = {"Tektur.ttf", "FiraSansCondensed-Regular.ttf"};
	DWORD n = GetModuleFileNameA(NULL, font_dir, MAX_PATH);
	while (n > 0 && font_dir[n - 1] != '\\')
		n--;
	font_dir[n] = 0;
	lstrcatA(font_dir, "mw2coop\\fonts\\");
	int ok = 1;
	for (int i = 0; i < 2; i++) {
		char path[MAX_PATH];
		snprintf(path, sizeof(path), "%s%s", font_dir, files[i]);
		int added = AddFontResourceExA(path, FR_PRIVATE, NULL);
		log_printf("fonts: %s: %s", path, added ? "loaded" : "NOT FOUND");
		ok &= added > 0;
	}
	dc = CreateCompatibleDC(NULL);
	return ok && dc;
}

static HFONT make_font(int face, int bold, int px)
{
	return CreateFontA(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
			   ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face == FACE_TEKTUR ? "Tektur" : "Fira Sans Condensed");
}

// cap height / em of a face, from "H" at a big size
static double cap_ratio(int face)
{
	HFONT f = make_font(face, 0, 200);
	HGDIOBJ old = SelectObject(dc, f);
	GLYPHMETRICS gm;
	MAT2 id = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
	double ratio = 0.7;
	if (GetGlyphOutlineW(dc, L'H', GGO_METRICS, &gm, 0, NULL, &id) != GDI_ERROR && gm.gmBlackBoxY)
		ratio = gm.gmBlackBoxY / 200.0;
	SelectObject(dc, old);
	DeleteObject(f);
	return ratio;
}

static uint32_t hash_bytes(const uint8_t *p, size_t n, uint32_t h)
{
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

// Renders one character with the given GDI font into a new Bitmap; returns its index (an empty bitmap for a space; adv -1 for a
// character the face does not have).
static int render(HFONT font, wchar_t ch)
{
	if (bitmap_count == bitmap_cap) {
		bitmap_cap = bitmap_cap ? bitmap_cap * 2 : 4096;
		bitmaps = realloc(bitmaps, (size_t)bitmap_cap * sizeof(Bitmap));
	}
	Bitmap *b = &bitmaps[bitmap_count];
	memset(b, 0, sizeof(*b));
	b->shared = -1;
	HGDIOBJ old = SelectObject(dc, font);
	GLYPHMETRICS gm;
	MAT2 id = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
	WORD index = 0xffff;
	GetGlyphIndicesW(dc, &ch, 1, &index, GGI_MARK_NONEXISTING_GLYPHS);
	DWORD size = index == 0xffff ? (DWORD)GDI_ERROR : GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &gm, 0, NULL, &id);
	if (size != (DWORD)GDI_ERROR) {
		b->adv = gm.gmCellIncX;
		if (size > 0 && gm.gmBlackBoxX > 0 && gm.gmBlackBoxY > 0) {
			uint8_t *raw = malloc(size);
			if (GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &gm, size, raw, &id) != GDI_ERROR) {
				b->w = (int)gm.gmBlackBoxX;
				b->h = (int)gm.gmBlackBoxY;
				b->ox = gm.gmptGlyphOrigin.x;
				b->oy = gm.gmptGlyphOrigin.y;
				b->alpha = malloc((size_t)b->w * b->h);
				int stride = (b->w + 3) & ~3;
				for (int y = 0; y < b->h; y++)
					for (int x = 0; x < b->w; x++) {
						int v = raw[y * stride + x] * 255 / 64;
						b->alpha[y * b->w + x] = (uint8_t)(v > 255 ? 255 : v);
					}
			}
			free(raw);
		}
	} else {
		b->adv = -1;
	}
	SelectObject(dc, old);
	b->hash = hash_bytes(b->alpha ? b->alpha : (const uint8_t *)"", b->alpha ? (size_t)b->w * b->h : 0, 2166136261u ^ (uint32_t)(b->w * 977 + b->h));
	return bitmap_count++;
}

// ---- packing and DXT5 ---------------------------------------------------------------------------------------------------------

#define ATLAS_W 1024

static int cmp_height(const void *a, const void *b)
{
	const Bitmap *x = &bitmaps[*(const int *)a], *y = &bitmaps[*(const int *)b];
	return y->h != x->h ? y->h - x->h : y->w - x->w;
}

// Cells are multiples of 4 texels with a texel of empty border, so a 4 x 4 DXT block never holds two glyphs. Returns the atlas height
// (a multiple of 4), or 0 when it does not fit in 4096.
static int pack(const int *order, int count)
{
	int x = 0, y = 0, row_h = 0;
	for (int i = 0; i < count; i++) {
		Bitmap *b = &bitmaps[order[i]];
		int cw = (b->w + 2 + 3) & ~3, ch = (b->h + 2 + 3) & ~3;
		if (x + cw > ATLAS_W) {
			x = 0;
			y += row_h;
			row_h = 0;
		}
		b->x = x + 1;
		b->y = y + 1;
		x += cw;
		if (ch > row_h)
			row_h = ch;
	}
	y += row_h;
	return y <= 4096 ? (y + 3) & ~3 : 0;
}

// DXT5: 8 bytes of alpha (two end points, 16 x 3-bit indices) and 8 bytes of colour (white, as the game's own atlas)
static void encode_block(const uint8_t *alpha, int stride, uint8_t out[16])
{
	int mx = 0, mn = 255;
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++) {
			int a = alpha[y * stride + x];
			if (a > mx)
				mx = a;
			if (a < mn)
				mn = a;
		}
	int pal[8];
	pal[0] = mx;
	pal[1] = mn;
	if (mx > mn) {
		for (int i = 1; i < 7; i++)
			pal[1 + i] = ((7 - i) * mx + i * mn) / 7;
	} else {
		for (int i = 2; i < 8; i++)
			pal[i] = mx;
	}
	uint64_t bits = 0;
	for (int p = 0; p < 16; p++) {
		int a = alpha[(p / 4) * stride + (p % 4)], best = 0, bd = 1 << 30;
		for (int i = 0; i < 8; i++) {
			int d = abs(a - pal[i]);
			if (d < bd) {
				bd = d;
				best = i;
			}
		}
		bits |= (uint64_t)best << (3 * p);
	}
	out[0] = (uint8_t)mx;
	out[1] = (uint8_t)mn;
	for (int i = 0; i < 6; i++)
		out[2 + i] = (uint8_t)(bits >> (8 * i));
	out[8] = 0xff; // colour end points 0xffff and 0x0000, all indices 0: white, as the game's atlas
	out[9] = 0xff;
	out[10] = 0;
	out[11] = 0;
	memset(out + 12, 0, 4);
}

// ---- the replacement ----------------------------------------------------------------------------------------------------------

static int patched;

static wchar_t unicode_of(unsigned letter)
{
	if (letter < 0x80 || letter > 0xff)
		return (wchar_t)letter;
	char c = (char)letter;
	wchar_t w = 0;
	if (MultiByteToWideChar(1251, 0, &c, 1, &w, 1) != 1)
		return 0;
	return w;
}

typedef struct {
	uint8_t *font;
	int count;
	int *bitmap_of; // per glyph entry: the bitmap index
	int pixel_height;
} FontJob;

static void patch_all(void *(*find)(int, const char *), const uint8_t *atlas_material)
{
	patched = 1; // once, whatever happens
	if (!load_faces()) {
		log_printf("fonts: the font files are missing, the game keeps its own fonts");
		return;
	}
	double ratio[2] = {cap_ratio(FACE_TEKTUR), cap_ratio(FACE_FIRA)};
	FontJob jobs[16];
	int njobs = 0;
	for (int p = 0; p < PLAN_COUNT && njobs < 16; p++) {
		uint8_t *f = find(24, plan[p].name);
		if (!f || !readable(f, 0x30) || *(const uint8_t *const *)(f + FONT_MATERIAL) != atlas_material)
			continue;
		FontJob *j = &jobs[njobs++];
		j->font = f;
		j->count = *(const int *)(f + FONT_COUNT);
		j->pixel_height = *(const int *)(f + FONT_HEIGHT);
		j->bitmap_of = malloc((size_t)j->count * sizeof(int));
		const uint8_t *glyphs = *(const uint8_t *const *)(f + FONT_GLYPHS);
		int other = plan[p].face == FACE_TEKTUR ? FACE_FIRA : FACE_TEKTUR;
		HFONT font = make_font(plan[p].face, plan[p].bold, (int)(j->pixel_height * 0.65 / ratio[plan[p].face] + 0.5));
		HFONT fallback = make_font(other, 0, (int)(j->pixel_height * 0.65 / ratio[other] + 0.5));
		for (int i = 0; i < j->count; i++) {
			unsigned letter = *(const uint16_t *)(glyphs + (size_t)i * GLYPH_SIZE);
			wchar_t ch = unicode_of(letter);
			int bi = render(font, ch);
			if (bitmaps[bi].adv < 0) // the face has no such character: the other face
				bi = render(fallback, ch);
			if (bitmaps[bi].adv < 0)
				bitmaps[bi].adv = j->pixel_height / 3;
			j->bitmap_of[i] = bi;
		}
		DeleteObject(font);
		DeleteObject(fallback);
	}
	if (!njobs) {
		log_printf("fonts: none of the planned fonts lives on the shared atlas");
		return;
	}
	// identical bitmaps (the Latin letters and their Cyrillic look-alikes) are stored once
	int *unique = malloc((size_t)bitmap_count * sizeof(int));
	int nuniq = 0;
	for (int i = 0; i < bitmap_count; i++) {
		Bitmap *b = &bitmaps[i];
		if (b->w == 0 || b->h == 0)
			continue;
		int found = -1;
		for (int k = 0; k < nuniq && found < 0; k++) {
			Bitmap *o = &bitmaps[unique[k]];
			if (o->hash == b->hash && o->w == b->w && o->h == b->h && !memcmp(o->alpha, b->alpha, (size_t)b->w * b->h))
				found = unique[k];
		}
		if (found >= 0)
			b->shared = found;
		else
			unique[nuniq++] = i;
	}
	qsort(unique, (size_t)nuniq, sizeof(int), cmp_height);
	int atlas_h = pack(unique, nuniq);
	log_printf("fonts: %d fonts, %d glyph renders, %d distinct bitmaps, atlas %d x %d", njobs, bitmap_count, nuniq, ATLAS_W, atlas_h);
	if (!atlas_h) {
		log_printf("fonts: the glyphs do not fit in an atlas, the game keeps its own fonts");
		return;
	}
	// the atlas as 8-bit alpha, then DXT5
	uint8_t *alpha = calloc((size_t)ATLAS_W * atlas_h, 1);
	for (int k = 0; k < nuniq; k++) {
		Bitmap *b = &bitmaps[unique[k]];
		for (int y = 0; y < b->h; y++)
			memcpy(alpha + (size_t)(b->y + y) * ATLAS_W + b->x, b->alpha + (size_t)y * b->w, (size_t)b->w);
	}
	// the texture on the game's own device
	uint8_t *image = material_image(atlas_material);
	IDirect3DTexture9 *old_tex = image ? *(IDirect3DTexture9 **)image : NULL;
	IDirect3DDevice9 *dev = NULL;
	IDirect3DTexture9 *tex = NULL;
	if (!old_tex || FAILED(IDirect3DTexture9_GetDevice(old_tex, &dev)) || !dev ||
	    FAILED(IDirect3DDevice9_CreateTexture(dev, ATLAS_W, atlas_h, 1, 0, D3DFMT_DXT5, D3DPOOL_MANAGED, &tex, NULL))) {
		log_printf("fonts: cannot make the new atlas texture, the game keeps its own fonts");
		free(alpha);
		return;
	}
	D3DLOCKED_RECT r;
	if (FAILED(IDirect3DTexture9_LockRect(tex, 0, &r, NULL, 0))) {
		log_printf("fonts: cannot lock the new atlas texture");
		free(alpha);
		return;
	}
	for (int by = 0; by < atlas_h / 4; by++)
		for (int bx = 0; bx < ATLAS_W / 4; bx++)
			encode_block(alpha + (size_t)(by * 4) * ATLAS_W + bx * 4, ATLAS_W, (uint8_t *)r.pBits + (size_t)by * r.Pitch + (size_t)bx * 16);
	IDirect3DTexture9_UnlockRect(tex, 0);
	free(alpha);
	// the glyph tables, then the texture
	for (int n = 0; n < njobs; n++) {
		FontJob *j = &jobs[n];
		uint8_t *glyphs = *(uint8_t **)(j->font + FONT_GLYPHS);
		int baseline = (int)(j->pixel_height * 0.13 + 0.5);
		for (int i = 0; i < j->count; i++) {
			Bitmap *b = &bitmaps[j->bitmap_of[i]];
			Bitmap *src = b->shared >= 0 ? &bitmaps[b->shared] : b;
			uint8_t *g = glyphs + (size_t)i * GLYPH_SIZE;
			int adv = b->adv > 255 ? 255 : b->adv < 0 ? 0 : b->adv;
			g[2] = (uint8_t)(int8_t)b->ox;
			g[3] = (uint8_t)(int8_t)(-(baseline + b->oy));
			g[4] = (uint8_t)adv;
			if (src->w && src->h) {
				g[5] = (uint8_t)b->w;
				g[6] = (uint8_t)b->h;
				*(float *)(g + 8) = (float)src->x / ATLAS_W;
				*(float *)(g + 12) = (float)src->y / atlas_h;
				*(float *)(g + 16) = (float)(src->x + src->w) / ATLAS_W;
				*(float *)(g + 20) = (float)(src->y + src->h) / atlas_h;
			} else {
				g[2] = 0;
				g[3] = (uint8_t)(int8_t)(-j->pixel_height);
				g[5] = 0;
				g[6] = 0;
				*(float *)(g + 8) = *(float *)(g + 16) = 0.99f;
				*(float *)(g + 12) = *(float *)(g + 20) = 0.99f;
			}
		}
		free(j->bitmap_of);
	}
	*(IDirect3DTexture9 **)image = tex;
	*(uint16_t *)(image + IMAGE_DIMS) = ATLAS_W;
	*(uint16_t *)(image + IMAGE_DIMS + 2) = (uint16_t)atlas_h;
	*(uint32_t *)(image + IMAGE_DATALEN) = ATLAS_W * atlas_h;
	*(uint32_t *)(image + IMAGE_DATALEN + 4) = ATLAS_W * atlas_h;
	log_printf("fonts: %d fonts now use the mod's glyphs (Tektur and Fira Sans Condensed)", njobs);
	for (int i = 0; i < bitmap_count; i++)
		free(bitmaps[i].alpha);
	free(bitmaps);
	free(unique);
	bitmaps = NULL;
	bitmap_count = bitmap_cap = 0;
}

void fonts_seen(void *font, const char *name, void *(*find)(int, const char *))
{
	(void)name;
#ifndef _WIN64
	(void)find;
	return; // the 32-bit layouts (FONT_* and MATERIAL_TABLE above are guesses) are not mapped yet
#endif
	if (patched || !font)
		return;
	const uint8_t *f = (const uint8_t *)font;
	if (!readable(f, 0x30))
		return;
	const uint8_t *mat = *(const uint8_t *const *)(f + FONT_MATERIAL);
	if (!mat || !readable(mat, 8))
		return;
	const char *mname = *(const char *const *)mat;
	if (!mname || !readable(mname, 20) || strcmp(mname, "fonts/gamefonts_pc") != 0)
		return;
	uint8_t *image = material_image(mat);
	if (!image || !*(void **)image)
		return; // the renderer has not made the texture yet
	if (!localize_is_russian())
		return;
	patch_all(find, mat);
}

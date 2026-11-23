// The main menu's logo can use either the untouched game's artwork or an Enhanced composite. Both
// are kept in mod-owned managed textures and material copies; the original game's material/texture
// is never rewritten. mw2coop\logo-enhanced.tga is the transparent 1024 x 256 overlay.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3dcap.h"
#include "log.h"
#include "modtex.h"
#include "dx/natboot.h"
#include "logo.h"

#ifdef _WIN64
#define MATERIAL_TABLE 0x60
#else
#define MATERIAL_TABLE 0x54
#endif
#define DEF_IMAGE 8

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

static void game_dir_file(const char *rel, char *out, size_t size)
{
	DWORD n = GetModuleFileNameA(NULL, out, (DWORD)size);
	while (n > 0 && out[n - 1] != 0x5c)
		n--;
	out[n] = 0;
	strncat(out, rel, size - strlen(out) - 1);
}

// 32-bit BGRA pixels of level 0 of a texture, w x h; handles A8R8G8B8 / X8R8G8B8 and DXT1, DXT3, DXT5. Returns a malloc'd buffer or NULL.
static uint32_t *read_texture(IDirect3DTexture9 *tex, int *w, int *h)
{
	D3DSURFACE_DESC d;
	if (FAILED(modtex_get_level_desc(tex, 0, &d)))
		return NULL;
	log_printf("logo: texture %ux%u format %u pool %u usage 0x%x", d.Width, d.Height, (unsigned)d.Format, (unsigned)d.Pool, (unsigned)d.Usage);
	D3DLOCKED_RECT lr;
	if (FAILED(modtex_lock(tex, 0, &lr, D3DLOCK_READONLY))) {
		log_printf("logo: the texture cannot be locked (pool %u)", (unsigned)d.Pool);
		return NULL;
	}
	uint32_t *px = calloc((size_t)d.Width * d.Height, 4);
	const uint8_t *src = lr.pBits;
	if (px && (d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8)) {
		for (UINT y = 0; y < d.Height; y++) {
			uint32_t *row = px + (size_t)y * d.Width;
			memcpy(row, src + (size_t)y * lr.Pitch, d.Width * 4);
			if (d.Format == D3DFMT_X8R8G8B8)
				for (UINT x = 0; x < d.Width; x++)
					row[x] |= 0xff000000u;
		}
	} else if (px && (d.Format == D3DFMT_DXT1 || d.Format == D3DFMT_DXT3 || d.Format == D3DFMT_DXT5)) {
		int block = d.Format == D3DFMT_DXT1 ? 8 : 16;
		for (UINT by = 0; by < d.Height / 4; by++)
			for (UINT bx = 0; bx < d.Width / 4; bx++) {
				const uint8_t *b = src + (size_t)by * lr.Pitch + (size_t)bx * block;
				const uint8_t *c = b + (block == 16 ? 8 : 0);
				uint16_t c0 = c[0] | (c[1] << 8), c1 = c[2] | (c[3] << 8);
				uint8_t col[4][4];
				int rgb[2][3] = {{(c0 >> 11) * 255 / 31, ((c0 >> 5) & 63) * 255 / 63, (c0 & 31) * 255 / 31}, {(c1 >> 11) * 255 / 31, ((c1 >> 5) & 63) * 255 / 63, (c1 & 31) * 255 / 31}};
				for (int k = 0; k < 3; k++) {
					col[0][k] = (uint8_t)rgb[0][k];
					col[1][k] = (uint8_t)rgb[1][k];
					if (c0 > c1 || block == 16) {
						col[2][k] = (uint8_t)((2 * rgb[0][k] + rgb[1][k]) / 3);
						col[3][k] = (uint8_t)((rgb[0][k] + 2 * rgb[1][k]) / 3);
					} else {
						col[2][k] = (uint8_t)((rgb[0][k] + rgb[1][k]) / 2);
						col[3][k] = 0;
					}
				}
				uint8_t alpha[16];
				for (int i = 0; i < 16; i++)
					alpha[i] = 255;
				if (d.Format == D3DFMT_DXT3) {
					for (int i = 0; i < 16; i++)
						alpha[i] = (uint8_t)(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
				} else if (d.Format == D3DFMT_DXT5) {
					uint8_t a0 = b[0], a1 = b[1], tab[8];
					tab[0] = a0, tab[1] = a1;
					if (a0 > a1)
						for (int k = 1; k < 7; k++)
							tab[k + 1] = (uint8_t)(((7 - k) * a0 + k * a1) / 7);
					else {
						for (int k = 1; k < 5; k++)
							tab[k + 1] = (uint8_t)(((5 - k) * a0 + k * a1) / 5);
						tab[6] = 0, tab[7] = 255;
					}
					uint64_t bits = 0;
					for (int i = 0; i < 6; i++)
						bits |= (uint64_t)b[2 + i] << (8 * i);
					for (int i = 0; i < 16; i++)
						alpha[i] = tab[(bits >> (3 * i)) & 7];
				}
				uint32_t idx = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
				for (int i = 0; i < 16; i++) {
					const uint8_t *k = col[(idx >> (2 * i)) & 3];
					px[(size_t)(by * 4 + i / 4) * d.Width + bx * 4 + (i & 3)] = (uint32_t)alpha[i] << 24 | (uint32_t)k[0] << 16 | (uint32_t)k[1] << 8 | k[2];
				}
			}
	} else {
		log_printf("logo: texture format %u is not handled", (unsigned)d.Format);
		free(px);
		px = NULL;
	}
	modtex_unlock(tex, 0);
	*w = (int)d.Width;
	*h = (int)d.Height;
	return px;
}

static void write_tga(const char *path, const uint32_t *px, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	uint8_t head[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8), 32, 8 | 0x20}; // top-left origin
	fwrite(head, 1, sizeof(head), f);
	fwrite(px, 4, (size_t)w * h, f);
	fclose(f);
}

// An uncompressed 32-bit TGA (top-left or bottom-left origin) as BGRA pixels, or NULL.
static uint32_t *read_tga(const char *path, int *w, int *h)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	uint8_t head[18];
	uint32_t *px = NULL;
	if (fread(head, 1, 18, f) == 18 && head[2] == 2 && head[16] == 32) {
		int tw = head[12] | (head[13] << 8), th = head[14] | (head[15] << 8);
		fseek(f, head[0], SEEK_CUR);
		px = malloc((size_t)tw * th * 4);
		if (px && fread(px, 4, (size_t)tw * th, f) == (size_t)tw * th) {
			if (!(head[17] & 0x20)) // bottom-left origin: flip
				for (int y = 0; y < th / 2; y++)
					for (int x = 0; x < tw; x++) {
						uint32_t t = px[(size_t)y * tw + x];
						px[(size_t)y * tw + x] = px[(size_t)(th - 1 - y) * tw + x];
						px[(size_t)(th - 1 - y) * tw + x] = t;
					}
			*w = tw;
			*h = th;
		} else {
			free(px);
			px = NULL;
		}
	}
	fclose(f);
	return px;
}

// <over> (straight alpha, same size) laid over <dst> (straight alpha)
static void composite(uint32_t *dst, const uint32_t *over, int n)
{
	for (int i = 0; i < n; i++) {
		uint32_t o = over[i], d = dst[i];
		unsigned oa = o >> 24;
		if (!oa)
			continue;
		unsigned da = d >> 24, ra = oa + da * (255 - oa) / 255;
		if (!ra)
			continue;
		unsigned c[3];
		for (int k = 0; k < 3; k++) {
			unsigned oc = (o >> (8 * k)) & 255, dc = (d >> (8 * k)) & 255;
			c[k] = (oc * oa + dc * da * (255 - oa) / 255) / ra;
			if (c[k] > 255)
				c[k] = 255;
		}
		dst[i] = (uint32_t)ra << 24 | c[2] << 16 | c[1] << 8 | c[0];
	}
}

// the result written into every mip level of the (managed, A8R8G8B8) texture; the lower levels by averaging 2 x 2 pixels of the one above
static int write_texture(IDirect3DTexture9 *tex, const uint32_t *px, int w, int h)
{
	DWORD levels = modtex_levels(tex);
	uint32_t *cur = malloc((size_t)w * h * 4);
	if (!cur)
		return 0;
	memcpy(cur, px, (size_t)w * h * 4);
	int cw = w, ch = h, ok = 1;
	for (DWORD lv = 0; lv < levels; lv++) {
		D3DLOCKED_RECT lr;
		if (FAILED(modtex_lock(tex, lv, &lr, 0))) {
			ok = 0;
			break;
		}
		for (int y = 0; y < ch; y++)
			memcpy((uint8_t *)lr.pBits + (size_t)y * lr.Pitch, cur + (size_t)y * cw, (size_t)cw * 4);
		modtex_unlock(tex, lv);
		if (lv + 1 < levels && cw > 1 && ch > 1) {
			int nw = cw / 2, nh = ch / 2;
			uint32_t *next = malloc((size_t)nw * nh * 4);
			if (!next) {
				ok = 0;
				break;
			}
			for (int y = 0; y < nh; y++)
				for (int x = 0; x < nw; x++) {
					const uint32_t *p[4] = {cur + (size_t)(2 * y) * cw + 2 * x, cur + (size_t)(2 * y) * cw + 2 * x + 1, cur + (size_t)(2 * y + 1) * cw + 2 * x, cur + (size_t)(2 * y + 1) * cw + 2 * x + 1};
					unsigned sum[4] = {0, 0, 0, 0}, wsum = 0;
					for (int i = 0; i < 4; i++) {
						unsigned a = *p[i] >> 24;
						wsum += a;
						for (int k = 0; k < 3; k++)
							sum[k] += ((*p[i] >> (8 * k)) & 255) * a;
						sum[3] += a;
					}
					unsigned a = sum[3] / 4;
					next[(size_t)y * nw + x] = wsum ? (uint32_t)a << 24 | (sum[2] / wsum) << 16 | (sum[1] / wsum) << 8 | (sum[0] / wsum) : 0;
				}
			free(cur);
			cur = next;
			cw = nw;
			ch = nh;
		}
	}
	free(cur);
	return ok;
}

// Keep the original and Enhanced pictures in two mod-owned managed textures, then make two
// shallow copies of the game's Material/table/image that point at those textures. The original
// logo texture may live in D3DPOOL_DEFAULT and cannot reliably be locked for repeated writes;
// swapping the menu item's material is safe for both D3D9 and the native renderer.
static void *logo_source_material;
static uint8_t *logo_source_table;
static uint8_t *logo_source_image;
static IDirect3DTexture9 *logo_source_texture;
static IDirect3DTexture9 *logo_variant_textures[2];
static uint8_t logo_material_copies[2][0x100];
static uint8_t logo_table_copies[2][0x40];
static uint8_t logo_image_copies[2][0x80];
static void *logo_material_variants[2];

static int logo_is_variant(void *material)
{
	return material && (material == logo_material_variants[0] || material == logo_material_variants[1]);
}

static int make_logo_variant(int index, void *material, uint8_t *table, uint8_t *image, IDirect3DTexture9 *texture)
{
	static const char *const names[2] = {"mw2coop_logo_original", "mw2coop_logo_enhanced"};
	memcpy(logo_material_copies[index], material, sizeof(logo_material_copies[index]));
	memcpy(logo_table_copies[index], table, sizeof(logo_table_copies[index]));
	memcpy(logo_image_copies[index], image, sizeof(logo_image_copies[index]));
	*(IDirect3DTexture9 **)logo_image_copies[index] = texture;
	for (int i = 0; i < 4; i++) {
		uint8_t **slot = (uint8_t **)(logo_table_copies[index] + i * 16 + DEF_IMAGE);
		if (*slot == image)
			*slot = logo_image_copies[index];
	}
	*(uint8_t **)(logo_material_copies[index] + MATERIAL_TABLE) = logo_table_copies[index];
	*(const char **)logo_material_copies[index] = names[index];
	logo_material_variants[index] = logo_material_copies[index];
	return 1;
}

static int prepare_logo_variants(void *material)
{
	if (!(natboot_active() || d3dcap_device()))
		return 0;
	if (logo_is_variant(material))
		material = logo_source_material;
	if (!material || !readable(material, MATERIAL_TABLE + sizeof(void *)))
		return 0;
	uint8_t *table = *(uint8_t **)((uint8_t *)material + MATERIAL_TABLE);
	if (!table || !readable(table, 0x40))
		return 0;
	uint8_t *image = *(uint8_t **)(table + DEF_IMAGE);
	if (!image || !readable(image, 0x80))
		return 0;
	IDirect3DTexture9 *source = *(IDirect3DTexture9 **)image;
	if (!source || !readable(source, sizeof(void *)))
		return 0;
	if (material == logo_source_material && table == logo_source_table && image == logo_source_image &&
	    source == logo_source_texture && logo_variant_textures[0] && logo_variant_textures[1])
		return 1;

	int w = 0, h = 0;
	uint32_t *original = read_texture(source, &w, &h);
	if (!original)
		return 0;
	uint32_t *enhanced = malloc((size_t)w * h * sizeof(*enhanced));
	if (!enhanced) {
		free(original);
		return 0;
	}
	memcpy(enhanced, original, (size_t)w * h * sizeof(*enhanced));
	int ow = 0, oh = 0;
	char path[MAX_PATH];
	game_dir_file("mw2coop\\logo-enhanced.tga", path, sizeof(path));
	uint32_t *over = read_tga(path, &ow, &oh);
	int have_overlay = over && ow == w && oh == h;
	if (have_overlay)
		composite(enhanced, over, w * h);
	else
		log_printf("logo: Enhanced overlay unavailable (picture %s, %dx%d against %dx%d); keeping an original fallback",
			   over ? "read" : "missing", ow, oh, w, h);
	free(over);

	// These are new managed textures, not writes into the game's original (possibly DEFAULT-pool) image.
	IDirect3DTexture9 *original_tex = modtex_create(source, (UINT)w, (UINT)h, 1, D3DFMT_A8R8G8B8);
	IDirect3DTexture9 *enhanced_tex = modtex_create(source, (UINT)w, (UINT)h, 1, D3DFMT_A8R8G8B8);
	if (!original_tex || !enhanced_tex || !write_texture(original_tex, original, w, h) ||
	    !write_texture(enhanced_tex, enhanced, w, h)) {
		modtex_release(original_tex);
		modtex_release(enhanced_tex);
		free(original);
		free(enhanced);
		log_printf("logo: could not create the two managed logo textures; keeping the game's material");
		return 0;
	}
	free(original);
	free(enhanced);

	IDirect3DTexture9 *old_original = logo_variant_textures[0];
	IDirect3DTexture9 *old_enhanced = logo_variant_textures[1];
	make_logo_variant(0, material, table, image, original_tex);
	make_logo_variant(1, material, table, image, enhanced_tex);
	logo_source_material = material;
	logo_source_table = table;
	logo_source_image = image;
	logo_source_texture = source;
	logo_variant_textures[0] = original_tex;
	logo_variant_textures[1] = enhanced_tex;
	modtex_release(old_original);
	modtex_release(old_enhanced);
	log_printf("logo: original and Enhanced material variants ready (%dx%d; overlay %s)", w, h,
		   have_overlay ? "applied" : "not available");
	return 1;
}

void *logo_material_variant(void *material, int enhanced)
{
	if (logo_is_variant(material))
		material = logo_source_material;
	if (!prepare_logo_variants(material))
		return material;
	return logo_material_variants[!!enhanced];
}

void logo_install(void *material, int dump_only)
{
	if (!material || !(natboot_active() || d3dcap_device()))
		return;
	if (logo_is_variant(material))
		material = logo_source_material;
	if (dump_only) {
		uint8_t *image = material_image((const uint8_t *)material);
		IDirect3DTexture9 *tex = image ? *(IDirect3DTexture9 **)image : NULL;
		int w = 0, h = 0;
		uint32_t *px = tex ? read_texture(tex, &w, &h) : NULL;
		if (px) {
			char path[MAX_PATH];
			game_dir_file("mw2coop\\logo-dump.tga", path, sizeof(path));
			write_tga(path, px, w, h);
			log_printf("logo: the game's original logo (%dx%d) written to %s", w, h, path);
			free(px);
		}
		return;
	}
	(void)prepare_logo_variants(material);
}

// A copy of <template_material> (a Material *, here the logo's) with the About page's four icons (mw2coop\about-icons.tga, 512 x 128, straight alpha) as its picture. The game's own
// material and image are not touched: the copy has its own texture table and image (as padicon.c does for the gamepad picture; copied a little over their ends, as menu copies are).
// The page is made again after every vid_restart, and the copy with it when the template changed (modtex_snapshot_t: a black box, then errors, before 2026-11-20).
void *icons_material(void *template_material)
{
	static uint8_t *copy;
	static uint8_t mat[0x100], tab[0x40], img[0x80];
	static modtex_snapshot_t made_from;
	uint8_t *src = template_material;
	uint8_t *table = src && readable(src, 0x100) ? *(uint8_t **)(src + MATERIAL_TABLE) : NULL;
	uint8_t *image = table && readable(table, 0x40) ? *(uint8_t **)(table + DEF_IMAGE) : NULL;
	if (!image || !readable(image, 0x80) || !(natboot_active() || d3dcap_device()))
		return copy;
	if (copy && !modtex_snapshot_differs(&made_from, src, table, image))
		return copy;
	char path[MAX_PATH];
	game_dir_file("mw2coop\\about-icons.tga", path, sizeof(path));
	int w = 0, h = 0;
	uint32_t *px = read_tga(path, &w, &h);
	if (!px) {
		log_printf("logo: %s is missing: no icons on the About page", path);
		return copy;
	}
	IDirect3DTexture9 *tex = modtex_create(*(void **)image, (UINT)w, (UINT)h, 1, D3DFMT_A8R8G8B8);
	if (!tex) {
		free(px);
		return copy;
	}
	D3DLOCKED_RECT lr;
	if (FAILED(modtex_lock(tex, 0, &lr, 0))) {
		modtex_release(tex);
		free(px);
		return copy;
	}
	for (int y = 0; y < h; y++)
		memcpy((uint8_t *)lr.pBits + (size_t)y * lr.Pitch, px + (size_t)y * w, (size_t)w * 4);
	modtex_unlock(tex, 0);
	free(px);
	IDirect3DTexture9 *old = copy ? *(IDirect3DTexture9 **)img : NULL;
	modtex_snapshot_take(&made_from, src, table, image);
	memcpy(mat, src, 0x100);
	memcpy(tab, table, 0x40);
	memcpy(img, image, 0x80);
	*(IDirect3DTexture9 **)img = tex;
	for (int i = 0; i < 4; i++)
		if (*(uint8_t **)(tab + i * 16 + DEF_IMAGE) == image)
			*(uint8_t **)(tab + i * 16 + DEF_IMAGE) = img;
	*(uint8_t **)(mat + MATERIAL_TABLE) = tab;
	*(const char **)mat = "mw2coop_about_icons";
	log_printf("logo: the About page's icons made%s (%dx%d)", copy ? " again (the template changed: a vid_restart)" : "", w, h);
	copy = mat;
	modtex_release(old);
	return copy;
}

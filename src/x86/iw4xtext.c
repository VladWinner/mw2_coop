// The iw4x-sp client's own texts at the mod's fonts' size (player report 2026-11-20: "the writing at the top is cut off").
//
// The client draws its mark "AlterWare IW4x-SP" itself (its code, not the game's): R_AddCmdDrawText (1.0.159 VA 0x50e7a0, through a pointer of the client) with the
// cgame's small font (the font registered as "fonts/smallfont", 1.0.159 VA 0x192a0dc), at x/y scale 1 and its baseline 30 pixels from the top; for the right-aligned
// placement it measures the text with R_TextWidth (VA 0x508960) at scale 1 too. The mod draws its fonts on a finer grid (fonts.c: the glyph tables and the pixel height
// in m units per pixel, m = 4 for smallfont) and the game scales every text of its own by the font's pixel height, so its texts keep their size; a text drawn at a fixed
// scale of 1 comes out m times larger, and the mark went over the top of the screen. Here a draw or a width asked by the client's own code (a return address in its
// ".main" section) is scaled by 1 / m: the client's texts are the size the client meant. The game's texts and the mod's own are not touched.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "fonts.h"
#include "hook.h"
#include "iw4xtext.h"
#include "log.h"

#define DRAW_TEXT_VA 0x50e7a0u // void R_AddCmdDrawText(const char *text, int max chars, Font *font, float x, float y, float x scale, float y scale, float rotation, const float *color, int style)
static const uint8_t draw_head[7] = {0x8b, 0x44, 0x24, 0x04, 0x80, 0x38, 0x00}; // mov eax, [esp+4]; cmp byte [eax], 0

static uintptr_t client_lo, client_hi; // the client's own code (its ".main" section), 0 when not found

// From the client's file: in memory the headers at the image base are the game's (the client maps iw4sp.exe over its own image, headers included), so its own
// section table is read from iw4x-sp.exe on disk (the first iw4xtext run, 2026-11-20, found no .main section in memory).
static void find_client_code(void)
{
	char path[MAX_PATH];
	uint8_t head[4096];
	DWORD got = 0;
	if (!GetModuleFileNameA(NULL, path, sizeof(path)))
		return;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return;
	const BOOL ok = ReadFile(f, head, sizeof(head), &got, NULL);
	CloseHandle(f);
	if (!ok || got < sizeof(IMAGE_DOS_HEADER))
		return;
	const LONG nt_at = ((IMAGE_DOS_HEADER *)head)->e_lfanew;
	if (nt_at <= 0 || (DWORD)nt_at + sizeof(IMAGE_NT_HEADERS32) > got)
		return;
	IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(head + nt_at);
	IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	for (int i = 0; i < nt->FileHeader.NumberOfSections && (uint8_t *)(s + 1) <= head + got; i++, s++)
		if (!memcmp(s->Name, ".main", 6)) {
			client_lo = (uintptr_t)base + s->VirtualAddress;
			client_hi = client_lo + (s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData);
		}
}

int iw4xtext_scale_of(const void *return_address, const void *font)
{
	const uintptr_t r = (uintptr_t)return_address;
	return client_lo && r >= client_lo && r < client_hi ? fonts_units_per_pixel(font) : 1;
}

static void (*draw_original)(const char *, int, void *, float, float, float, float, float, const float *, int);
static void draw_hook(const char *text, int max_chars, void *font, float x, float y, float xs, float ys, float rotation, const float *color, int style)
{
	const int m = iw4xtext_scale_of(__builtin_return_address(0), font);
	if (m > 1) {
		static int logged;
		if (!logged++)
			log_printf("iw4xtext: the client's own text \"%.40s\" drawn at 1/%d of its scale (the mod's font grid)", text ? text : "", m);
		xs /= (float)m, ys /= (float)m;
	}
	draw_original(text, max_chars, font, x, y, xs, ys, rotation, color, style);
}

void iw4xtext_install(void)
{
	find_client_code();
	if (!client_lo) {
		log_printf("iw4xtext: the client has no .main section: its own texts are left as they are");
		return;
	}
	uint8_t *target = (uint8_t *)(uintptr_t)DRAW_TEXT_VA;
	if (!hook_function(target, draw_head, sizeof(draw_head), (void *)draw_hook, (void **)&draw_original)) {
		log_printf("iw4xtext: R_AddCmdDrawText is not as expected: the client's own texts are left as they are");
		return;
	}
	log_printf("iw4xtext: the client's own texts (code %08lx..%08lx) are drawn at the mod's font grid", (unsigned long)client_lo, (unsigned long)client_hi);
}

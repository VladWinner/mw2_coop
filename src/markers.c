// The distance on the co-op markers (owner 2026-11-24: "put the distance on the markers that are there, rather than build extra things"): the partner's
// marker (the game's co-op icon, maps\_coop friendlyIcon: coop_player_location, hint_health while he is down) and the marker wheel's markers
// (maps\_mw2coop_ping) show "13 m" over the icon.
//
// How the game draws a waypoint (static reading, 2026-11-24): the HUD element draw (64-bit rva 0xa3b80, type 0xd) fills a waypoint record and calls the
// waypoint draw (64-bit rva 0xa2790; 32-bit VA 0x448cf0, cdecl; iw4x-sp's 1.0.159 0x48fd40): (local client, record*). The record holds the world position
// (+0, a vec3 pointer), the icon's material (+1 pointer: Material, its name first), the colour (+2) and a text (+3 pointers; 32-bit +0xc): the draw puts the
// icon where it belongs (on the screen, or pinned to its edge with the arrow), and when the text is set, it draws it (CL_DrawTextPhysical: 64-bit rva
// 0xd2ce0 called at 0xa2db7, 32-bit VA 0x4b8a70 called at 0x44927e) centred on the icon in the icon's colour, the objective font (objectiveFontSize) with a
// shadow, its baseline objectiveTextOffsetY virtual units from the icon's centre. The HUD element draw always passes no text. Here the text is the distance
// from the view (the cgame's view origin, the one the draw measures from too) to the waypoint, for the markers' materials only, set for the call and taken
// back after it: where the game shows no icon (an off-screen-only icon in view), it shows no text either.
//
// Where the text goes: the game's offset (-5.33) is made for an objective's label written across the icon, and on a near marker (a full size icon, 36
// units) it lies over the icon's top half (the wheel's "need ammo" icon has its own word there). So the draw's text call is taken over: it gets the icon's
// centre back from the game's y (the offset times the view's virtual-to-real scale, ScreenPlacement of the local client), and puts the baseline over the
// icon (half the icon's height, scaled by distance as the draw scales it: waypointIconHeight, waypointDistScale*), or under it when the text would leave
// the view at its top (an icon pinned to the top edge; its arrow then points up). [display] marker_distance=0 in settings.ini turns it off.
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "localize.h"
#include "log.h"
#include "markers.h"
#include "net.h"
#include "netwatch.h"
#include "settings.h"

#ifdef _WIN64
#define REF_WP_DRAW 0xa2790	   // void (int local client, void **record)
#define REF_CG_VIEWORG 0x42d310 // the cgame's view origin (then the axis)
#define REF_WP_TEXTCALL 0xa2db7 // the waypoint draw's call of the text draw
#define REF_TEXTDRAW 0xd2ce0	   // CL_DrawTextPhysical (text, max chars, font, x, y, x scale, y scale, colour, style)
#define REF_SCRPLACE 0xdaf10	   // ScreenPlacement *(int local client)
#define WP_TEXT 3		   // the record's text pointer (+0x18)
#define FONT_HEIGHT 8		   // Font_s.pixelHeight (after the name pointer)
static const uint8_t wp_head[14] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x55, 0x56, 0x57, 0x48, 0x8d, 0x68, 0x88}; // mov rax,rsp; mov [rax+8],rbx; push rbp,rsi,rdi; lea rbp,[rax-0x78]
#else
#define REF_WP_DRAW 0x48cf0	   // VA 0x448cf0, cdecl void (int local client, void **record)
#define REF_CG_VIEWORG 0x37fae8 // VA 0x77fae8
#define REF_WP_TEXTCALL 0x4927e // VA 0x44927e
#define REF_TEXTDRAW 0xb8a70	   // VA 0x4b8a70, cdecl
#define REF_SCRPLACE 0x48a30	   // VA 0x448a30, cdecl
#define WP_TEXT 3		   // (+0xc)
#define FONT_HEIGHT 4
static const uint8_t wp_head[11] = {0x83, 0xec, 0x7c, 0xd9, 0xe8, 0x53, 0x55, 0xd9, 0x5c, 0x24, 0x18}; // sub esp,0x7c; fld1; push ebx; push ebp; fstp [esp+0x18]
#endif

typedef void (*text_draw_t)(const char *text, int max_chars, void *font, float x, float y, float x_scale, float y_scale, const float *color, int style);
typedef const float *(*scrplace_t)(int local_client);

static uint8_t *base;
static void (*wp_original)(int, void **);
static text_draw_t text_original;
static int text_hooked;
static volatile LONG drawn_logged, placed_logged;

// set by wp_hook for the one text call of a marker's draw
static int marker_lc = -1;
static float marker_half; // half the icon's height in virtual units, scaled by distance as the draw scales it

static dvar_t *dv_offset, *dv_height, *dv_dmin, *dv_dmax, *dv_dsmall;
static int dvars_looked;

static float dvar_float(const dvar_t *d, float fallback)
{
	return d && d->type == DVAR_TYPE_FLOAT ? d->current.value : fallback;
}

// the markers' icons: the partner's (maps\_coop) and the wheel's (maps\_mw2coop_ping shader_of)
static const char *const names[] = {"coop_player_location", "coop_player_location_fire", "hint_health", "waypoint_ammo", "hud_grenadeicon",
				    "hud_burningbarrelicon", "hud_burningcaricon"};
#define MARKERS ((int)(sizeof(names) / sizeof(names[0])))

// 1 + the index in names[], or 0
static int marker_of(const char *m)
{
	for (int i = 0; i < MARKERS; i++)
		if (!strcmp(m, names[i]))
			return i + 1;
	return 0;
}

// QA (mw2coop_qa set): the markers' last draws with their text's place on the screen (markers_poll answers the scenarios with them; the draws run on the
// main thread, as the poll does)
static int qa_on = -1, text_place; // the draw under way reached its text: 1 put over the icon, 2 under it, 3 where the game puts it
static float qa_x, qa_cy, qa_y;	   // text_hook: the text's left edge, the icon's centre, the text's baseline (pixels)
static struct qa_draw {
	int idx, m, place;
	float x, cy, y;
	DWORD t;
} qa_ring[32];
static unsigned qa_next;
static volatile DWORD trace_until; // MW2COOP_MARKERS_TRACE=1: every draw and text call logged for 0.3 s after each QA question

static void qa_note(int idx, int m, int place)
{
	if (qa_on < 0) {
		char q[64];
		qa_on = game_dvar_raw("mw2coop_qa", q, sizeof(q)) && q[0];
	}
	if (!qa_on)
		return;
	qa_ring[qa_next++ & 31] = (struct qa_draw){idx, m, place, qa_x, qa_cy, qa_y, GetTickCount()};
}

// QA, control.c's poll (every 250 ms, main thread): a scenario asks with mw2coop_qa_markers_expect "<tag> <icon> <least metres> <most metres>" (least -1:
// the icon must not be drawn), and 0.5 s later the log says what the draws of the last 0.4 s showed: "qa: markers <tag>: PASS|FAIL ...". The scenario does not
// read it back: both PCs of a network session run the same script, and the game adds what GetDvar returns to its lockstep state hash, so a value that differs
// between the PCs (each draws its own view) set netwatch off reloading a checkpoint (QA mk2 2026-11-24). Player 1's view answers: the host of a network
// session, and the split screen (both views there); the network partner's PC stays silent.
void markers_poll(void)
{
	static char want[128];
	static DWORD asked;
	static int pending;
	char v[128];
	if (game_dvar_raw("mw2coop_qa_markers_expect", v, sizeof(v)) && v[0] && strcmp(v, want)) {
		lstrcpynA(want, v, sizeof(want));
		asked = GetTickCount();
		pending = 1;
		if (getenv("MW2COOP_MARKERS_TRACE"))
			trace_until = asked + 300;
		return;
	}
	if (!pending || GetTickCount() - asked < 500)
		return;
	pending = 0;
	if (net_partner_known() && !netwatch_this_pc_hosts())
		return; // (the partner's view: not what the scenario asks about)
	char tag[48], icon[48];
	int lo, hi;
	if (sscanf(want, "%47s %47s %d %d", tag, icon, &lo, &hi) != 4) {
		log_printf("qa: markers: FAIL the request '%s' is not '<tag> <icon> <least> <most>'", want);
		return;
	}
	// the draws of the last 0.4 s, one line per icon and place (a draw repeated each frame at the same spot counts once, with how many frames)
	const DWORD now = GetTickCount();
	char list[600] = "";
	size_t n = 0;
	int m = -1, places = 0, wrong = 0;
	DWORD newest = 0;
	int done[32] = {0};
	for (int i = 0; i < 32; i++) {
		const struct qa_draw *a = &qa_ring[i];
		if (done[i] || !a->t || now - a->t >= 400)
			continue;
		int frames = 0;
		for (int j = i; j < 32; j++) {
			const struct qa_draw *b = &qa_ring[j];
			if (!done[j] && b->t && now - b->t < 400 && b->idx == a->idx && b->place == a->place && fabsf(b->cy - a->cy) < 24.0f)
				done[j] = 1, frames++;
		}
		if (n < sizeof(list) - 120)
			n += (size_t)snprintf(list + n, sizeof(list) - n, "%s%s %d m %s (icon centre y %.0f, text baseline y %.0f, x %.0f; %d frames)", n ? ", " : "", names[a->idx], a->m,
					      a->place == 1 ? "over" : a->place == 2 ? "under" : "where the game puts it", a->cy, a->y, a->x, frames);
		if (!strcmp(icon, names[a->idx])) {
			places |= 1 << a->place;
			if (a->place != 1 && a->place != 2)
				wrong = 1;
			if (a->t - newest < 0x80000000u || !newest)
				newest = a->t, m = a->m;
		}
	}
	const int ok = lo < 0 ? m < 0 : m >= lo && m <= hi && !wrong;
	if (lo < 0)
		log_printf("qa: markers %s: %s no %s drawn (drawn: %s)", tag, ok ? "PASS" : "FAIL", icon, n ? list : "none");
	else
		log_printf("qa: markers %s: %s %s says %d m (%d..%d), %s the icon (drawn: %s)", tag, ok ? "PASS" : "FAIL", icon, m, lo, hi,
			   places == 2 ? "over" : places == 4 ? "under" : places == 6 ? "over and under" : "not on", n ? list : "none");
}

static void text_hook(const char *text, int max_chars, void *font, float x, float y, float x_scale, float y_scale, const float *color, int style)
{
	if (marker_lc >= 0)
		text_place = 3;
	if (marker_lc >= 0 && font && !IsBadReadPtr(font, FONT_HEIGHT + sizeof(int))) {
		const float *pl = ((scrplace_t)(void *)(base + XR(REF_SCRPLACE)))(marker_lc);
		if (pl && !IsBadReadPtr(pl, 27 * sizeof(float))) {
			// ScreenPlacement: scaleVirtualToReal [0..1], realViewportPosition [6..7], realViewportSize [8..9]
			const float sy = pl[1], top = pl[7];
			const float cy = y - dvar_float(dv_offset, -5.33f) * sy; // the icon's centre
			const float th = (float)*(const int *)((const uint8_t *)font + FONT_HEIGHT) * y_scale;
			const float half = marker_half * sy, gap = 2.0f * sy;
			const int over = cy - half - gap - th >= top;
			y = over ? cy - half - gap : cy + half + gap + th;
			text_place = over ? 1 : 2;
			qa_x = x, qa_cy = cy, qa_y = y;
			if (trace_until && GetTickCount() < trace_until)
				log_printf("markers trace: text x %.0f y %.0f -> %.0f (centre %.0f, half %.1f, gap %.1f, text %.1f, top %.0f, scale %.3f)", x,
					   cy + dvar_float(dv_offset, -5.33f) * sy, y, cy, half, gap, th, top, sy);
			if (InterlockedCompareExchange(&placed_logged, 1, 0) == 0)
				log_printf("markers: the distance text goes %s the icon (centre %.0f, half height %.1f, text %.1f px, view top %.0f)", over ? "over" : "under", cy,
					   half, th, top);
		}
	}
	text_original(text, max_chars, font, x, y, x_scale, y_scale, color, style);
}

static void wp_hook(int lc, void **w)
{
	if (w && w[0] && w[1] && !w[WP_TEXT] && !IsBadReadPtr(w[1], sizeof(void *))) {
		const char *name = *(const char *const *)w[1];
		const int idx = name && !IsBadStringPtrA(name, 64) ? marker_of(name) : 0;
		if (idx) {
			const float *p = (const float *)w[0], *o = (const float *)(base + XR(REF_CG_VIEWORG));
			const float dx = p[0] - o[0], dy = p[1] - o[1], dz = p[2] - o[2];
			const float d = sqrtf(dx * dx + dy * dy + dz * dz);
			const int m = (int)(d * 0.0254f + 0.5f); // (inches to metres, as the scripts count)
			const char *fmt = localize_text("MW2COOP_MARKER_DIST");
			char text[48];
			const char *at = fmt ? strstr(fmt, "&&1") : NULL;
			if (at)
				snprintf(text, sizeof(text), "%.*s%d%s", (int)(at - fmt), fmt, m, at + 3);
			else
				snprintf(text, sizeof(text), "%d m", m);
			if (!dvars_looked) {
				dvars_looked = 1;
				dv_offset = game_find_dvar("objectiveTextOffsetY");
				dv_height = game_find_dvar("waypointIconHeight");
				dv_dmin = game_find_dvar("waypointDistScaleRangeMin");
				dv_dmax = game_find_dvar("waypointDistScaleRangeMax");
				dv_dsmall = game_find_dvar("waypointDistScaleSmallest");
			}
			// the draw's distance scale: full size up to RangeMin, Smallest from RangeMax, linear between
			const float dmin = dvar_float(dv_dmin, 1000.0f), dmax = dvar_float(dv_dmax, 3000.0f), small = dvar_float(dv_dsmall, 0.8f);
			float s = 1.0f;
			if (d > dmin) {
				if (d < dmax) {
					const float t = (d - dmin) / (dmax - dmin > 0.0f ? dmax - dmin : 1.0f);
					s = t * small + (1.0f - t);
				} else
					s = small;
			}
			marker_half = dvar_float(dv_height, 36.0f) * s * 0.5f;
			if (trace_until && GetTickCount() < trace_until)
				log_printf("markers trace: draw %s lc %d at %.0f %.0f %.0f, %.0f units", name, lc, p[0], p[1], p[2], d);
			marker_lc = text_hooked ? lc : -1;
			text_place = text_hooked ? 0 : 3; // (no text call taken over: where the game puts it)
			w[WP_TEXT] = text;
			wp_original(lc, w);
			w[WP_TEXT] = NULL;
			marker_lc = -1;
			if (text_place) // (the draw reached its text: the icon is shown)
				qa_note(idx - 1, m, text_place);
			if (InterlockedCompareExchange(&drawn_logged, 1, 0) == 0)
				log_printf("markers: the distance is drawn on a marker (%s, \"%s\")", name, text);
			return;
		}
	}
	wp_original(lc, w);
}

void markers_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	if (!settings_marker_distance()) {
		log_printf("markers: no distance on the markers ([display] marker_distance=0)");
		return;
	}
	if (!addr_resolved(REF_WP_DRAW) || !addr_resolved(REF_CG_VIEWORG) ||
	    !hook_function(base + XR(REF_WP_DRAW), wp_head, sizeof(wp_head), (void *)wp_hook, (void **)&wp_original)) {
		log_printf("markers: the waypoint draw (rva 0x%x) is not the expected code: no distance on the markers", (unsigned)REF_WP_DRAW);
		return;
	}
	// the text's place: the draw's call of the text draw, checked to be that call
	if (addr_resolved(REF_WP_TEXTCALL) && addr_resolved(REF_TEXTDRAW) && addr_resolved(REF_SCRPLACE)) {
		uint8_t *site = base + XR(REF_WP_TEXTCALL);
		if (site[0] == 0xe8 && site + 5 + *(const int32_t *)(site + 1) == base + XR(REF_TEXTDRAW))
			text_hooked = hook_call(site, (void *)text_hook, (void **)&text_original);
	}
	log_printf("markers: the distance is drawn on the co-op markers (%s)", text_hooked ? "over the icon" : "where the game puts an objective's text: the text call is not the expected code");
}

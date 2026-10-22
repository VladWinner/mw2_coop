// No Russian: both players look like Makarov's men (owner, 2026-10-22). A player's body must be
// a Spec Ops co-op body: the player animations need its skeleton, and an AI body (the crew's
// body_airport_com_*) on a player breaks the pose. So the crew's mesh is put on the co-op body's
// skeleton, in memory: the target model (coop_body_us_army, worn only by the players there) keeps
// its bones, hit locations, collision and bounds, and takes the donor's (body_airport_com_c)
// surfaces and materials, every vertex's bones renumbered by bone name (a donor bone the target
// does not have takes its nearest ancestor that it has). The surfaces are copies; the donor and
// the old surfaces are left as they were (the render thread may still draw them this frame).
//
// A level script asks for it with the dvar mw2coop_transplant "<map> <target> <donor>" (other
// levels clear it); the control thread queues "mw2coop_transplant" and it runs on the main thread
// between frames, only while that map runs (asset lookups from another thread, or while the next
// level's zones load, hang the game). Drawing only: the skeleton is the same, so
// both lockstep machines simulate the same; each does it on its own. The level's zones are loaded
// fresh for the next level, which undoes it; a restart of the same level finds it already done.
//
// 64-bit layouts (mapped with "!bones", "!surfs", "!asset 4" in memdump.c):
//   XModel: name +0, numBones +8, numRootBones +9, numsurfs +0xa, boneNames +0x28 (script string
//     indices, interned: equal names are equal numbers), parentList +0x30 (byte per non-root
//     bone: its index minus its parent's), materialHandles +0x58, lodInfo[4] +0x60 (stride 0x38)
//   lodInfo: dist +0, numsurfs +4, surfIndex +6, modelSurfs +8, partBits[6] +0x10, surfs +0x28
//   XModelSurfs: name +0, surfs +8, numsurfs +0x10, partBits[6] +0x14 (0x30 bytes)
//   XSurface (0x58): deformed +1, vertCount +2, vertInfo.vertCount[4] +0x18, vertsBlend +0x20,
//     vertListCount +0x30, vertList +0x38, partBits[6] +0x40
//   vertsBlend: vertCount[0] verts of 1 bone (bone), [1] of 2 (bone bone weight), [2] of 3
//     (bone bone weight bone weight), [3] of 4 (7 values); a bone is stored as its index * 64
//   XRigidVertList (0x10): boneOffset (index * 64) +0, vertCount +2
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "transplant.h"

#define MODEL_LODS 0x60
#define LOD_SIZE 0x38
#define SURF_SIZE 0x58
#define MODELSURFS_SIZE 0x30
#define BONE_STRIDE 64

typedef void *(*find_t)(int, const char *);
static uint8_t *base;
static char failed[192]; // the request that failed (logged once)
static volatile LONG queued;
static volatile DWORD queued_at;

static uint8_t *find_model(const char *name)
{
	return ((find_t)(base + RVA_DB_FIND_HEADER))(4, name);
}

static int parent_of(const uint8_t *m, int bone)
{
	int roots = m[9];
	if (bone < roots)
		return -1;
	return bone - (*(uint8_t **)(m + 0x30))[bone - roots];
}

// partBits: one bit per bone, bone 0 the top bit of word 0 (checked on the donor's own surfaces
// before use; with_ancestors as the donor's data says).
static void set_bit(int32_t *bits, int bone)
{
	bits[bone >> 5] |= (int32_t)(0x80000000u >> (bone & 31));
}

static void add_bone(const uint8_t *m, int32_t *bits, int bone, int with_ancestors)
{
	for (; bone >= 0; bone = with_ancestors ? parent_of(m, bone) : -1)
		set_bit(bits, bone);
}

// Calls f( bone ) for every bone value of a surface (blend bones and rigid lists).
typedef void (*bone_fn)(void *ctx, uint16_t *value);

static int blend_len(const uint8_t *s)
{
	const int16_t *vc = (const int16_t *)(s + 0x18);
	return vc[0] + 3 * vc[1] + 5 * vc[2] + 7 * vc[3];
}

static void each_bone(uint8_t *s, uint16_t *blend, uint8_t *lists, bone_fn f, void *ctx)
{
	const int16_t *vc = (const int16_t *)(s + 0x18);
	if (s[1] && blend) {
		uint16_t *p = blend;
		for (int g = 0; g < 4; g++) {
			int width = 2 * g + 1;
			for (int v = 0; v < vc[g]; v++, p += width)
				for (int k = 0; k < width; k += (k == 0 ? 1 : 2))
					f(ctx, p + k);
		}
	}
	uint32_t n = *(uint32_t *)(s + 0x30);
	for (uint32_t k = 0; lists && k < n; k++)
		f(ctx, (uint16_t *)(lists + 0x10 * k));
}

struct bits_ctx {
	const uint8_t *model;
	int32_t bits[6];
	int with_ancestors;
};

static void bits_cb(void *c, uint16_t *value)
{
	struct bits_ctx *ctx = c;
	add_bone(ctx->model, ctx->bits, *value / BONE_STRIDE, ctx->with_ancestors);
}

struct remap_ctx {
	const int *map;
	int bones;
	int bad;
};

static void remap_cb(void *c, uint16_t *value)
{
	struct remap_ctx *ctx = c;
	int bone = *value / BONE_STRIDE;
	if (*value % BONE_STRIDE || bone >= ctx->bones) {
		ctx->bad++;
		return;
	}
	*value = (uint16_t)(ctx->map[bone] * BONE_STRIDE);
}

// How the donor's surfaces' partBits were made: 1 with ancestors, 0 without, -1 neither.
static int bits_rule(const uint8_t *donor)
{
	int match[2] = {0, 0}, total = 0;
	for (int l = 0; l < 4; l++) {
		const uint8_t *lod = donor + MODEL_LODS + LOD_SIZE * l;
		int n = *(const uint16_t *)(lod + 4);
		uint8_t *surfs = *(uint8_t **)(lod + 0x28);
		for (int i = 0; surfs && i < n; i++, total++) {
			uint8_t *s = surfs + SURF_SIZE * i;
			for (int a = 0; a < 2; a++) {
				struct bits_ctx ctx = {donor, {0}, a};
				each_bone(s, *(uint16_t **)(s + 0x20), *(uint8_t **)(s + 0x38), bits_cb, &ctx);
				if (memcmp(ctx.bits, s + 0x40, sizeof(ctx.bits)) == 0)
					match[a]++;
			}
		}
	}
	log_printf("transplant: donor partBits: %d of %d surfaces match bones only, %d with ancestors", match[0], total, match[1]);
	if (match[1] == total)
		return 1;
	if (match[0] == total)
		return 0;
	return -1;
}

static int transplant(const char *target_name, const char *donor_name)
{
	uint8_t *target = find_model(target_name), *donor = find_model(donor_name);
	if (!target || !donor || !*(void **)(target + 0x28) || !*(void **)(donor + 0x28)) {
		static DWORD logged;
		if (GetTickCount() - logged > 10000) {
			log_printf("transplant: %s or %s not loaded (yet)", target_name, donor_name);
			logged = GetTickCount();
		}
		return 0; // tried again a second later
	}
	if (*(void **)(target + 0x58) == *(void **)(donor + 0x58))
		return 1; // done already (a restart of the level)
	int rule = bits_rule(donor);
	if (rule < 0) {
		log_printf("transplant: the donor's partBits follow neither rule; not done");
		return -1;
	}

	// donor bone -> target bone, by name, else the nearest ancestor the target has
	int tbones = target[8], dbones = donor[8];
	uint16_t *tnames = *(uint16_t **)(target + 0x28), *dnames = *(uint16_t **)(donor + 0x28);
	int map[256];
	char missing[512] = "";
	for (int i = 0; i < dbones; i++) {
		map[i] = -1;
		for (int j = 0; j < tbones && map[i] < 0; j++)
			if (tnames[j] == dnames[i])
				map[i] = j;
		if (map[i] < 0) {
			int p = parent_of(donor, i);
			map[i] = p >= 0 ? map[p] : 0;
			snprintf(missing + strlen(missing), sizeof(missing) - strlen(missing), "%d->%d ", i, map[i]);
		}
	}
	log_printf("transplant: %s (%d bones) takes %s's surfaces (%d bones); donor bones without a match: %s", target_name,
		   tbones, donor_name, dbones, missing[0] ? missing : "none");

	// new LODs: copies of the donor's with renumbered bones
	uint8_t lods[4 * LOD_SIZE];
	memcpy(lods, donor + MODEL_LODS, sizeof(lods));
	int surfaces = 0, bad = 0;
	for (int l = 0; l < 4; l++) {
		uint8_t *lod = lods + LOD_SIZE * l;
		int n = *(uint16_t *)(lod + 4);
		uint8_t *from = *(uint8_t **)(lod + 0x28);
		if (!n || !from)
			continue;
		uint8_t *surfs = calloc((size_t)n, SURF_SIZE);
		int32_t lod_bits[6] = {0};
		for (int i = 0; i < n; i++) {
			uint8_t *s = surfs + SURF_SIZE * i;
			memcpy(s, from + SURF_SIZE * i, SURF_SIZE);
			uint16_t *blend = NULL;
			uint8_t *lists = NULL;
			if (s[1] && *(uint16_t **)(s + 0x20)) {
				size_t len = (size_t)blend_len(s) * 2;
				blend = malloc(len);
				memcpy(blend, *(uint16_t **)(s + 0x20), len);
				*(uint16_t **)(s + 0x20) = blend;
			}
			uint32_t nl = *(uint32_t *)(s + 0x30);
			if (nl && *(uint8_t **)(s + 0x38)) {
				lists = malloc(0x10u * nl);
				memcpy(lists, *(uint8_t **)(s + 0x38), 0x10u * nl);
				*(uint8_t **)(s + 0x38) = lists;
			}
			struct remap_ctx rc = {map, dbones, 0};
			each_bone(s, blend, lists, remap_cb, &rc);
			bad += rc.bad;
			struct bits_ctx bc = {target, {0}, rule};
			each_bone(s, blend, lists, bits_cb, &bc);
			memcpy(s + 0x40, bc.bits, sizeof(bc.bits));
			for (int k = 0; k < 6; k++)
				lod_bits[k] |= bc.bits[k];
			surfaces++;
		}
		*(uint8_t **)(lod + 0x28) = surfs;
		memcpy(lod + 0x10, lod_bits, sizeof(lod_bits));
		uint8_t *ms = *(uint8_t **)(lod + 8);
		if (ms) {
			uint8_t *copy = malloc(MODELSURFS_SIZE);
			memcpy(copy, ms, MODELSURFS_SIZE);
			*(uint8_t **)(copy + 8) = surfs;
			memcpy(copy + 0x14, lod_bits, sizeof(lod_bits));
			*(uint8_t **)(lod + 8) = copy;
		}
	}
	if (bad) {
		log_printf("transplant: %d bone values were not bone offsets; not done", bad);
		return -1;
	}
	// swap in: LODs, surface count, materials (indexed by the LODs' surfIndex)
	memcpy(target + MODEL_LODS, lods, sizeof(lods));
	target[0xa] = donor[0xa];
	*(void **)(target + 0x58) = *(void **)(donor + 0x58);
	log_printf("transplant: done, %d surfaces in %d LODs, partBits %s ancestors", surfaces, 4,
		   rule ? "with" : "without");
	return 1;
}

// Whether the map in the request runs now (mapname changes as soon as another level loads).
static int request(char *req, size_t size, char *target, char *donor)
{
	char map[64], now[64] = "", running[8] = "";
	game_dvar_raw("sv_running", running, sizeof(running));
	game_dvar_raw("mapname", now, sizeof(now));
	if (running[0] != '1' || !game_dvar_raw("mw2coop_transplant", req, (int)size) || !req[0])
		return 0;
	return sscanf(req, "%63s %63s %63s", map, target, donor) == 3 && _stricmp(map, now) == 0;
}

static void transplant_f(void)
{
	InterlockedExchange(&queued, 0);
	char req[192], target[64], donor[64];
	if (!request(req, sizeof(req), target, donor) || strcmp(req, failed) == 0)
		return;
	if (transplant(target, donor) < 0)
		lstrcpynA(failed, req, sizeof(failed)); // once: the next level or request tries again
}

static cmd_function_t commands[] = {
	{NULL, "mw2coop_transplant", NULL, NULL, transplant_f},
};

// Control thread, every 250 ms: while a level runs and asks for a transplant, have the main
// thread check it once a second (it does nothing when the target already wears the donor).
void transplant_poll(void)
{
	char req[192], target[64], donor[64];
	if (!request(req, sizeof(req), target, donor))
		return;
	if (strcmp(req, failed) != 0)
		failed[0] = 0;
	if (queued && GetTickCount() - queued_at < 3000)
		return;
	if (GetTickCount() - queued_at < 1000)
		return;
	queued_at = GetTickCount();
	InterlockedExchange(&queued, 1);
	game_cbuf_addtext("mw2coop_transplant\n");
}

void transplant_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	cmd_function_t **head = (cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		commands[i].next = *head;
		*head = &commands[i];
	}
}

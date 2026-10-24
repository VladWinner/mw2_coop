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

static int put_surfaces(uint8_t *target, uint8_t *donor, const char *target_name, const char *donor_name);

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
	return put_surfaces(target, donor, target_name, donor_name);
}

// The donor's surfaces and materials on the target's skeleton (see the top of the file).
static int put_surfaces(uint8_t *target, uint8_t *donor, const char *target_name, const char *donor_name)
{
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

// A model that is a copy of another, under its own name: "mw2coop_rider" (the dvar of that name
// holding "<skeleton> <mesh>") is the skeleton model (bones, hit locations, collision, bounds: an
// AI body the campaign's animations are made for) wearing the mesh model's surfaces (a player's
// co-op body). Cliffhanger's snowmobile rider: the level's AI driving animations twist the co-op
// body's multiplayer skeleton, and Soap's own model cannot take the mesh (Soap rides beside the
// players). DB_FindXAssetHeader answers the name with the copy (localize.c's hook calls
// transplant_clone) and DB_IsXAssetDefault calls it no default (hook_is_default), so
// PrecacheModel and SetModel take it like any model. Made once per pair while the level's zones
// are loaded, made again when the next level loads new models under the same names (a level's
// zones free and refill the pools, the entry included).
#define PICK_NAME "mw2coop_pick_up" // see upright_pick
static uint8_t *pick_model, *pick_source;
#define CLONE_NAME "mw2coop_rider" // model and dvar: the dvar holds "<skeleton> <mesh>"
#define RVA_POOLS 0x3bfe00 // void *[43]: { free list head; entries[] } (see zones.c)

static struct {
	char name[160];
	uint8_t *skeleton, *mesh; // the models it was made from (a new level loads new ones)
	uint8_t *model;
} clones[4];

static size_t gcd(size_t a, size_t b)
{
	while (b) {
		size_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

// The copy lives in the XModel pool, like every model: made outside it (calloc), the game hung
// a frame after the first script_model wore it (QA snowview, 2026-10-24), while the same mesh on
// Soap's own model did not. An entry is taken off the pool's free list as the game's pool alloc
// (rva 0xeee20) does. The entry size is not stored: it is the greatest common divisor of where
// entries sit from the start of the entries (two models in use and the free list's first ones).
static uint8_t *pool_entry(uint8_t *model_a, uint8_t *model_b, size_t *size)
{
	void **pool = ((void ***)(base + RVA_POOLS))[4];
	if (!pool || !*pool)
		return NULL;
	uint8_t *entries = (uint8_t *)pool + 8;
	size_t g = 0;
	uint8_t *samples[34] = {model_a, model_b};
	int n = 2;
	for (uint8_t *e = *pool; e && n < 34; e = *(uint8_t **)e)
		samples[n++] = e;
	for (int i = 0; i < n; i++) {
		if (samples[i] < entries)
			return NULL; // not one pool after all
		g = gcd(g, (size_t)(samples[i] - entries));
	}
	if (g < MODEL_LODS + 4 * LOD_SIZE || g > 0x1000)
		return NULL;
	uint8_t *entry = *pool;
	*pool = *(void **)entry;
	*size = g;
	return entry;
}

static float pick_grip_back(void)
{
	return -9.0f;
}

static void *upright_pick(void *(*find)(int, const char *));

void *transplant_clone(const char *name, void *(*find)(int, const char *))
{
	if (strcmp(name, PICK_NAME) == 0)
		return upright_pick(find);
	if (strcmp(name, CLONE_NAME) != 0)
		return NULL;
	char req[160], skel_name[64], mesh_name[64];
	if (!game_dvar_raw(CLONE_NAME, req, sizeof(req)) || sscanf(req, "%63s %63s", skel_name, mesh_name) != 2)
		return NULL;
	name = req; // the clone is known by the pair
	uint8_t *skeleton = find(4, skel_name), *mesh = find(4, mesh_name);
	if (!skeleton || !mesh || !*(void **)(skeleton + 0x28) || !*(void **)(mesh + 0x28))
		return NULL;
	int free_slot = -1;
	for (int i = 0; i < 4; i++) {
		if (strcmp(clones[i].name, name) == 0) {
			if (clones[i].skeleton == skeleton && clones[i].mesh == mesh)
				return clones[i].model;
			free_slot = i; // the same name in a new level: made again
			break;
		}
		if (!clones[i].name[0] && free_slot < 0)
			free_slot = i;
	}
	if (free_slot < 0)
		return NULL;
	size_t size = 0;
	uint8_t *model = pool_entry(skeleton, mesh, &size);
	if (!model) {
		log_printf("transplant: %s: no XModel pool entry", name);
		return NULL;
	}
	memcpy(model, skeleton, size);
	char *own = (char *)CLONE_NAME;
	*(char **)model = own;
	if (put_surfaces(model, mesh, skel_name, mesh_name) != 1) {
		memcpy(model, skeleton, size); // a working model in the entry; the name is still ours
		*(char **)model = own;
		return NULL;
	}
	lstrcpynA(clones[free_slot].name, name, sizeof(clones[free_slot].name));
	clones[free_slot].skeleton = skeleton;
	clones[free_slot].mesh = mesh;
	clones[free_slot].model = model;
	log_printf("transplant: %s made in the XModel pool (entry of %u bytes, from %s)", name, (unsigned)size, skel_name);
	return model;
}

// "mw2coop_pick_up": Cliffhanger's ice pick (weapon_ice_picker) standing upright in the hand that
// holds it. A co-op body holds it in the pistol grip, where the pick stuck out ahead of the fist
// like a pistol (owner, 2026-10-24: "turn them upright, right into his hands"); Attach takes no
// angles, and models linked to the hands followed the server's pose of the body and floated
// beside them. The copy's geometry hangs off its second bone (the pick's head tag) instead of the
// root, and that bone is turned 90 degrees up about the grip: the pick turns with it.

static void *upright_pick(void *(*find)(int, const char *))
{
	uint8_t *src = find(4, "weapon_ice_picker");
	if (!src || src[8] != 2 || src[9] != 1)
		return NULL;
	if (pick_model && pick_source == src)
		return pick_model;
	size_t size = 0;
	uint8_t *model = pool_entry(src, src, &size);
	if (!model) {
		log_printf("transplant: %s: no XModel pool entry", PICK_NAME);
		return NULL;
	}
	memcpy(model, src, size);
	*(const char **)model = PICK_NAME;
	// bone 1 (the only one not a root, the pick's head tag): local rotation as short[4] x y z w
	// (* 32767) at +0x38, offset float[3] at +0x40; its base pose (DObjAnimMat at +0x50) stays. A
	// vertex is drawn as bone * inverse(base pose) * vertex, so the bone is set to R * its base
	// pose, R being 90 degrees about Y (ahead becomes up) about the grip: the rotation R * q, the
	// offset R * t. Overwriting q (it is not the identity: -0.31 0.63 0.63 0.31) lost the pick.
	const int16_t *q0 = *(const int16_t **)(src + 0x38);
	const float *t0 = *(const float **)(src + 0x40);
	float bx = q0[0] / 32767.0f, by = q0[1] / 32767.0f, bz = q0[2] / 32767.0f, bw = q0[3] / 32767.0f;
	const float ra = -0.70710678f, rc = 0.70710678f; // R = (0, ra, 0, rc)
	float q[4];
	q[0] = rc * bx + ra * bz;
	q[1] = rc * by + ra * bw;
	q[2] = rc * bz - ra * bx;
	q[3] = rc * bw - ra * by;
	int16_t *quats = malloc(8);
	for (int k = 0; k < 4; k++)
		quats[k] = (int16_t)(q[k] * 32767.0f + (q[k] < 0 ? -0.5f : 0.5f));
	*(int16_t **)(model + 0x38) = quats;
	float *trans = malloc(3 * sizeof(float));
	// R: x' = -z, z' = x; then back toward the wrist: the model's origin is ahead of the fist
	// (QA frame: upright, but 8 to 10 units ahead of it)
	trans[0] = -t0[2] + pick_grip_back();
	trans[1] = t0[1];
	trans[2] = t0[0];
	*(float **)(model + 0x40) = trans;
	// every LOD's surfaces: copies drawn by bone 1
	int map[2] = {1, 1};
	for (int l = 0; l < 4; l++) {
		uint8_t *lod = model + MODEL_LODS + LOD_SIZE * l;
		int n = *(uint16_t *)(lod + 4);
		uint8_t *from = *(uint8_t **)(lod + 0x28);
		if (!n || !from)
			continue;
		uint8_t *surfs = calloc((size_t)n, SURF_SIZE);
		int32_t bits[6] = {(int32_t)0xc0000000u, 0, 0, 0, 0, 0}; // bones 0 and 1
		for (int i = 0; i < n; i++) {
			uint8_t *sf = surfs + SURF_SIZE * i;
			memcpy(sf, from + SURF_SIZE * i, SURF_SIZE);
			uint16_t *blend = NULL;
			uint8_t *lists = NULL;
			if (sf[1] && *(uint16_t **)(sf + 0x20)) {
				size_t len = (size_t)blend_len(sf) * 2;
				blend = malloc(len);
				memcpy(blend, *(uint16_t **)(sf + 0x20), len);
				*(uint16_t **)(sf + 0x20) = blend;
			}
			uint32_t nlists = *(uint32_t *)(sf + 0x30);
			if (nlists && *(uint8_t **)(sf + 0x38)) {
				lists = malloc(0x10u * nlists);
				memcpy(lists, *(uint8_t **)(sf + 0x38), 0x10u * nlists);
				*(uint8_t **)(sf + 0x38) = lists;
			}
			struct remap_ctx rc = {map, 2, 0};
			each_bone(sf, blend, lists, remap_cb, &rc);
			memcpy(sf + 0x40, bits, sizeof(bits));
		}
		*(uint8_t **)(lod + 0x28) = surfs;
		memcpy(lod + 0x10, bits, sizeof(bits));
		uint8_t *ms = *(uint8_t **)(lod + 8);
		if (ms) {
			uint8_t *copy = malloc(MODELSURFS_SIZE);
			memcpy(copy, ms, MODELSURFS_SIZE);
			*(uint8_t **)(copy + 8) = surfs;
			memcpy(copy + 0x14, bits, sizeof(bits));
			*(uint8_t **)(lod + 8) = copy;
		}
	}
	pick_model = model;
	pick_source = src;
	log_printf("transplant: %s made in the XModel pool (entry of %u bytes)", PICK_NAME, (unsigned)size);
	return model;
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

// DB_IsXAssetDefault (rva 0xefff0) on a name the database does not have returns "default" without
// taking itself off the database's reader count (its lookup loop returns before the decrement),
// so the next lookup that waits for no readers (a playsound) waited for ever: both machines hung
// a frame after a script_model wore the rider copy (QA snowview, 2026-10-24; the count at rva
// 0xa8a830). For the copy's name it answers "not default" itself, before the count is touched.
#define RVA_DB_IS_DEFAULT 0x0efff0
typedef char (*is_default_t)(unsigned type, const char *name);
static is_default_t is_default_original;

static char is_default(unsigned type, const char *name)
{
	if (type == 4 && name && strcmp(name, PICK_NAME) == 0 && pick_model)
		return 0;
	if (type == 4 && name && strcmp(name, CLONE_NAME) == 0)
		for (int i = 0; i < 4; i++)
			if (clones[i].model)
				return 0;
	return is_default_original(type, name);
}

// mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi (the same as DB_FindXAssetHeader's)
static const uint8_t is_default_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};

static void hook_is_default(void)
{
	uint8_t *target = base + RVA_DB_IS_DEFAULT;
	if (memcmp(target, is_default_bytes, sizeof(is_default_bytes)) != 0) {
		log_printf("transplant: unexpected bytes at DB_IsXAssetDefault, not hooking");
		return;
	}
	// trampoline: the 15 original bytes, then jmp [rip+0] to target+15
	uint8_t *tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!tramp)
		return;
	memcpy(tramp, target, sizeof(is_default_bytes));
	uint8_t back[14] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t to = (uint64_t)(uintptr_t)(target + sizeof(is_default_bytes));
	memcpy(back + 6, &to, 8);
	memcpy(tramp + sizeof(is_default_bytes), back, sizeof(back));
	FlushInstructionCache(GetCurrentProcess(), tramp, 64);
	is_default_original = (is_default_t)tramp;

	uint8_t jmp[14] = {0xff, 0x25, 0, 0, 0, 0};
	to = (uint64_t)(uintptr_t)is_default;
	memcpy(jmp + 6, &to, 8);
	DWORD old;
	VirtualProtect(target, sizeof(is_default_bytes), PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, jmp, sizeof(jmp));
	target[14] = 0xcc; // the 15th copied byte is never run: the trampoline resumes at +15
	VirtualProtect(target, sizeof(is_default_bytes), old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, 15);
}

void transplant_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	hook_is_default();
	cmd_function_t **head = (cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		commands[i].next = *head;
		*head = &commands[i];
	}
}

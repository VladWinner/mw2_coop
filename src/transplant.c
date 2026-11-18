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
#include "hook.h"
#include "log.h"
#include "transplant.h"
#include "scripts.h"

// Layouts (the 64-bit ones mapped with "!asset 4"; the 32-bit ones are the same fields with 4-byte pointers, checked on
// coop_body_us_army: lodInfo[0].dist 300.0 at +0x40, [1].dist 500.0 at +0x6c).
#ifdef _WIN64
#define M_BONES 8	  // XModel: u8 numBones
#define M_ROOTS 9	  // u8 numRootBones
#define M_SURFS 0xa	  // u8 numsurfs
#define M_BONENAMES 0x28  // script string indices
#define M_PARENTS 0x30	  // u8 per non-root bone: its index minus its parent's
#define M_QUATS 0x38	  // short[4] per non-root bone
#define M_TRANS 0x40	  // float[3] per non-root bone
#define M_MATERIALS 0x58  // Material **
#define MODEL_LODS 0x60
#define LOD_SIZE 0x38
#define L_MSURFS 8	  // lodInfo: XModelSurfs *
#define L_PARTBITS 0x10
#define L_SURFS 0x28	  // XSurface *
#define SURF_SIZE 0x58
#define S_VCOUNT 0x18	  // XSurface: vertInfo.vertCount[4]
#define S_BLEND 0x20	  // vertsBlend
#define S_NLISTS 0x30	  // vertListCount
#define S_LISTS 0x38	  // XRigidVertList *
#define S_PARTBITS 0x40
#define VL_SIZE 0x10	  // XRigidVertList
#define MODELSURFS_SIZE 0x30
#define MS_SURFS 8	  // XModelSurfs: name, surfs, numsurfs, partBits[6]
#define MS_PARTBITS 0x14
#define BONE_STRIDE 64
#else
#define M_BONES 4
#define M_ROOTS 5
#define M_SURFS 6
#define M_BONENAMES 0x24
#define M_PARENTS 0x28
#define M_QUATS 0x2c
#define M_TRANS 0x30
#define M_MATERIALS 0x3c
#define MODEL_LODS 0x40
#define LOD_SIZE 0x2c
#define L_MSURFS 8
#define L_PARTBITS 0xc
#define L_SURFS 0x24
#define SURF_SIZE 0x40
#define S_VCOUNT 0x10
#define S_BLEND 0x18
#define S_NLISTS 0x20
#define S_LISTS 0x24
#define S_PARTBITS 0x28
#define VL_SIZE 0xc
#define MODELSURFS_SIZE 0x24
#define MS_SURFS 4
#define MS_PARTBITS 0xc
#define BONE_STRIDE 64
#endif

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
	int roots = m[M_ROOTS];
	if (bone < roots)
		return -1;
	return bone - (*(uint8_t **)(m + M_PARENTS))[bone - roots];
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
	const int16_t *vc = (const int16_t *)(s + S_VCOUNT);
	return vc[0] + 3 * vc[1] + 5 * vc[2] + 7 * vc[3];
}

static void each_bone(uint8_t *s, uint16_t *blend, uint8_t *lists, bone_fn f, void *ctx)
{
	const int16_t *vc = (const int16_t *)(s + S_VCOUNT);
	if (s[1] && blend) {
		uint16_t *p = blend;
		for (int g = 0; g < 4; g++) {
			int width = 2 * g + 1;
			for (int v = 0; v < vc[g]; v++, p += width)
				for (int k = 0; k < width; k += (k == 0 ? 1 : 2))
					f(ctx, p + k);
		}
	}
	uint32_t n = *(uint32_t *)(s + S_NLISTS);
	for (uint32_t k = 0; lists && k < n; k++)
		f(ctx, (uint16_t *)(lists + VL_SIZE * k));
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
		uint8_t *surfs = *(uint8_t **)(lod + L_SURFS);
		for (int i = 0; surfs && i < n; i++, total++) {
			uint8_t *s = surfs + SURF_SIZE * i;
			for (int a = 0; a < 2; a++) {
				struct bits_ctx ctx = {donor, {0}, a};
				each_bone(s, *(uint16_t **)(s + S_BLEND), *(uint8_t **)(s + S_LISTS), bits_cb, &ctx);
				if (memcmp(ctx.bits, s + S_PARTBITS, sizeof(ctx.bits)) == 0)
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

// Buffers made for a model the game keeps (surfaces, bone lists, copies of the model-surface headers) belong to
// the level that made them. The game frees and refills its model pools with every level load, so a model made for
// an earlier level is gone, but the previous level's may still be drawn while the next one loads: the buffers of
// a level are freed when the level after next loads (transplant_new_level, from the zone loader's hook), never
// by how many models were made: a level makes dozens (No Russian's crew) and every one of them is live.
#define GENERATIONS 3
static struct {
	void **item;
	int count, cap;
} generations[GENERATIONS];
static int generation_now;

void transplant_new_level(void)
{
	generation_now = (generation_now + 1) % GENERATIONS;
	for (int i = 0; i < generations[generation_now].count; i++)
		free(generations[generation_now].item[i]);
	generations[generation_now].count = 0;
}

static void *bag_add(void *buffer)
{
	if (!buffer)
		return buffer;
	if (generations[generation_now].count == generations[generation_now].cap) {
		int cap = generations[generation_now].cap ? generations[generation_now].cap * 2 : 64;
		void **more = realloc(generations[generation_now].item, (size_t)cap * sizeof(void *));
		if (!more)
			return buffer; // never freed
		generations[generation_now].item = more;
		generations[generation_now].cap = cap;
	}
	generations[generation_now].item[generations[generation_now].count++] = buffer;
	return buffer;
}

static int transplant(const char *target_name, const char *donor_name)
{
	uint8_t *target = find_model(target_name), *donor = find_model(donor_name);
	if (!target || !donor || !*(void **)(target + M_BONENAMES) || !*(void **)(donor + M_BONENAMES)) {
		static DWORD logged;
		if (GetTickCount() - logged > 10000) {
			log_printf("transplant: %s or %s not loaded (yet)", target_name, donor_name);
			logged = GetTickCount();
		}
		return 0; // tried again a second later
	}
	if (*(void **)(target + M_MATERIALS) == *(void **)(donor + M_MATERIALS))
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
	int tbones = target[M_BONES], dbones = donor[M_BONES];
	uint16_t *tnames = *(uint16_t **)(target + M_BONENAMES), *dnames = *(uint16_t **)(donor + M_BONENAMES);
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
		uint8_t *from = *(uint8_t **)(lod + L_SURFS);
		if (!n || !from)
			continue;
		uint8_t *surfs = bag_add(calloc((size_t)n, SURF_SIZE));
		int32_t lod_bits[6] = {0};
		for (int i = 0; i < n; i++) {
			uint8_t *s = surfs + SURF_SIZE * i;
			memcpy(s, from + SURF_SIZE * i, SURF_SIZE);
			uint16_t *blend = NULL;
			uint8_t *lists = NULL;
			if (s[1] && *(uint16_t **)(s + S_BLEND)) {
				size_t len = (size_t)blend_len(s) * 2;
				blend = bag_add(malloc(len));
				memcpy(blend, *(uint16_t **)(s + S_BLEND), len);
				*(uint16_t **)(s + S_BLEND) = blend;
			}
			uint32_t nl = *(uint32_t *)(s + S_NLISTS);
			if (nl && *(uint8_t **)(s + S_LISTS)) {
				lists = bag_add(malloc(VL_SIZE * nl));
				memcpy(lists, *(uint8_t **)(s + S_LISTS), VL_SIZE * nl);
				*(uint8_t **)(s + S_LISTS) = lists;
			}
			struct remap_ctx rc = {map, dbones, 0};
			each_bone(s, blend, lists, remap_cb, &rc);
			bad += rc.bad;
			struct bits_ctx bc = {target, {0}, rule};
			each_bone(s, blend, lists, bits_cb, &bc);
			memcpy(s + S_PARTBITS, bc.bits, sizeof(bc.bits));
			for (int k = 0; k < 6; k++)
				lod_bits[k] |= bc.bits[k];
			surfaces++;
		}
		*(uint8_t **)(lod + L_SURFS) = surfs;
		memcpy(lod + L_PARTBITS, lod_bits, sizeof(lod_bits));
		uint8_t *ms = *(uint8_t **)(lod + L_MSURFS);
		if (ms) {
			uint8_t *copy = bag_add(malloc(MODELSURFS_SIZE));
			memcpy(copy, ms, MODELSURFS_SIZE);
			*(uint8_t **)(copy + MS_SURFS) = surfs;
			memcpy(copy + MS_PARTBITS, lod_bits, sizeof(lod_bits));
			*(uint8_t **)(lod + L_MSURFS) = copy;
		}
	}
	if (bad) {
		log_printf("transplant: %d bone values were not bone offsets; not done", bad);
		return -1;
	}
	// swap in: LODs, surface count, materials (indexed by the LODs' surfIndex)
	memcpy(target + MODEL_LODS, lods, sizeof(lods));
	target[M_SURFS] = donor[M_SURFS];
	*(void **)(target + M_MATERIALS) = *(void **)(donor + M_MATERIALS);
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
#define PICK_NAME "mw2coop_pick_up" // see turned_copy
#define CLONE_NAME "mw2coop_rider" // model and dvar: the dvar holds "<skeleton> <mesh>"
#ifdef _WIN64
#define RVA_POOLS XR(0x3bfe00) // void *[43]: { free list head; entries[] } (see zones.c)
#else
#define RVA_POOLS XRA(0x3307f8) // void *[43] at 0x7307f8, read by the per-type allocator (0x5801b0 for models): { free list head; entries[] }
#endif

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
	uint8_t *entries = (uint8_t *)pool + sizeof(void *);
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

static void *turned_copy(size_t ti, void *(*find)(int, const char *));
static int turned_index(const char *name);

void *transplant_clone(const char *name, void *(*find)(int, const char *))
{
	const int ti = turned_index(name);
	if (ti >= 0)
		return turned_copy((size_t)ti, find);
	if (strcmp(name, CLONE_NAME) != 0)
		return NULL;
	char req[160], skel_name[64], mesh_name[64];
	if (!game_dvar_raw(CLONE_NAME, req, sizeof(req)) || sscanf(req, "%63s %63s", skel_name, mesh_name) != 2)
		return NULL;
	name = req; // the clone is known by the pair
	uint8_t *skeleton = find(4, skel_name), *mesh = find(4, mesh_name);
	if (!skeleton || !mesh || !*(void **)(skeleton + M_BONENAMES) || !*(void **)(mesh + M_BONENAMES))
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

// Turned copies of a held model, for a hand whose tag holds it the wrong way (Attach takes no angles, and models linked to the hands followed the server's pose of
// the body and floated beside them). The copy's geometry hangs off its second bone instead of the root, and that bone is turned about the grip: the model turns with it.
// - "mw2coop_pick_up": Cliffhanger's ice pick (weapon_ice_picker) standing upright in the hand that holds it. A co-op body holds it in the pistol grip, where the pick
//   stuck out ahead of the fist like a pistol (owner, 2026-10-24: "turn them upright, right into his hands"): 90 degrees about Y (ahead becomes up), then 9 units back.
// - "mw2coop_knife_held": Endgame's knife (weapon_commando_knife, 4 bones) in a body's left fist, for its j_wrist_le (owner, 2026-11-18: "in the left hand as in the
//   first-person view", "it should lie in the hand like a person's, not stick out", forward grip "B"). knife_hand() below turns the wrist as the first-person rig's in
//   the fight and leaves the body's own half-closed fingers; the handle lies across the palm in their curl: the fingers point along the wrist's +X and close toward +Y,
//   the thumb is on -Z (the body's ring, little finger and thumb joints, QA knifehand with mw2coop_qa_knifegrip; the server's index and middle finger joints were not
//   usable). So the blade goes out of the thumb's side (-Z), turned about its own axis to the side that looked right (QA knifehand, three grips side by side):
//   quaternion 0.7071 0 -0.7071 0, the handle's middle (3 units behind the model's origin) at 3.5 0.5 0.5. "mw2coop_knife_reverse" is the reverse grip (the blade out
//   of the little finger's side), kept for QA.
static struct {
	const char *name, *source;
	float r[4];     // the turn, a quaternion x y z w
	float back[3];  // then this offset (model units)
	uint8_t *model, *from;
} turned[] = {
    {PICK_NAME, "weapon_ice_picker", {0, -0.70710678f, 0, 0.70710678f}, {-9.0f, 0, 0}, NULL, NULL},
    {"mw2coop_knife_held", "weapon_commando_knife", {0.70711f, 0, -0.70711f, 0}, {3.5f, 0.5f, -2.5f}, NULL, NULL},     // forward grip: the blade out of the thumb's side
    {"mw2coop_knife_reverse", "weapon_commando_knife", {0.70711f, 0, 0.70711f, 0}, {3.5f, 0.5f, 3.5f}, NULL, NULL},    // (reverse grip, the blade out of the little finger's side)
};
#define TURNED_N (sizeof(turned) / sizeof(turned[0]))
static int turned_index(const char *name)
{
	for (size_t i = 0; name && i < TURNED_N; i++)
		if (strcmp(name, turned[i].name) == 0)
			return (int)i;
	return -1;
}

static void *turned_copy(size_t ti, void *(*find)(int, const char *))
{
	const char *copy_name = turned[ti].name;
	uint8_t *src = find(4, turned[ti].source);
	if (!src || src[M_BONES] < 2 || src[M_BONES] > 32 || src[M_ROOTS] != 1) {
		static uint32_t told;
		if (src && !(told & (1u << ti))) {
			told |= 1u << ti;
			log_printf("transplant: %s: %s has %u bones (%u roots), a turned copy needs 2 to 32 with 1 root", copy_name, turned[ti].source, src[M_BONES], src[M_ROOTS]);
		}
		return NULL;
	}
	if (turned[ti].model && turned[ti].from == src)
		return turned[ti].model;
	size_t size = 0;
	uint8_t *model = pool_entry(src, src, &size);
	if (!model) {
		log_printf("transplant: %s: no XModel pool entry", copy_name);
		return NULL;
	}
	memcpy(model, src, size);
	*(const char **)model = copy_name;
	// The non-root bones (the pick: its head tag; the knife: three tags): local rotation as short[4] x y z w (* 32767) per bone at +0x38, offset float[3] at +0x40,
	// the parent as "index minus the parent's" per bone at +0x30; the base poses (DObjAnimMat at +0x50) stay. A vertex is drawn as bone * inverse(base pose) * vertex,
	// so each child of the root is set to R * its base pose, R the copy's turn about the grip (the rotation R * q, the offset R * t), its own children follow, and the
	// root's vertices are drawn by its first child. Overwriting q (the pick's is not the identity: -0.31 0.63 0.63 0.31) lost the pick.
	const int bones = src[M_BONES], nb = bones - 1;
	const int16_t *q0all = *(const int16_t **)(src + M_QUATS);
	const float *t0all = *(const float **)(src + M_TRANS);
	const uint8_t *parents = *(const uint8_t **)(src + M_PARENTS);
	const float rx = turned[ti].r[0], ry = turned[ti].r[1], rz = turned[ti].r[2], rw = turned[ti].r[3];
	int16_t *quats = bag_add(malloc((size_t)nb * 8));
	float *trans = bag_add(malloc((size_t)nb * 3 * sizeof(float)));
	memcpy(quats, q0all, (size_t)nb * 8);
	memcpy(trans, t0all, (size_t)nb * 3 * sizeof(float));
	int first_child = 0;
	for (int b = 1; b < bones; b++) {
		if (!parents || b - parents[b - 1] != 0) // (not a child of the root: it follows its parent)
			continue;
		if (!first_child)
			first_child = b;
		const int16_t *q0 = q0all + 4 * (b - 1);
		const float *t0 = t0all + 3 * (b - 1);
		const float bx = q0[0] / 32767.0f, by = q0[1] / 32767.0f, bz = q0[2] / 32767.0f, bw = q0[3] / 32767.0f;
		float q[4];
		q[0] = rw * bx + rx * bw + ry * bz - rz * by;
		q[1] = rw * by - rx * bz + ry * bw + rz * bx;
		q[2] = rw * bz + rx * by - ry * bx + rz * bw;
		q[3] = rw * bw - rx * bx - ry * by - rz * bz;
		for (int k = 0; k < 4; k++)
			quats[4 * (b - 1) + k] = (int16_t)(q[k] * 32767.0f + (q[k] < 0 ? -0.5f : 0.5f));
		// R t = t + 2 r x (r x t + w t), then the copy's offset (the pick: back toward the wrist, its origin is ahead of the fist: QA frame, upright but 8 to 10 units ahead)
		const float cx = ry * t0[2] - rz * t0[1] + rw * t0[0], cy = rz * t0[0] - rx * t0[2] + rw * t0[1], cz = rx * t0[1] - ry * t0[0] + rw * t0[2];
		float *t = trans + 3 * (b - 1);
		t[0] = t0[0] + 2.0f * (ry * cz - rz * cy) + turned[ti].back[0];
		t[1] = t0[1] + 2.0f * (rz * cx - rx * cz) + turned[ti].back[1];
		t[2] = t0[2] + 2.0f * (rx * cy - ry * cx) + turned[ti].back[2];
	}
	if (!first_child) {
		log_printf("transplant: %s: %s has no child of its root", copy_name, turned[ti].source);
		return NULL; // (the pool entry stays taken: a level's zones refill the pools)
	}
	*(int16_t **)(model + M_QUATS) = quats;
	*(float **)(model + M_TRANS) = trans;
	// every LOD's surfaces: copies whose root vertices are drawn by the root's first child
	int map[32];
	for (int b = 0; b < 32; b++)
		map[b] = b;
	map[0] = first_child;
	int32_t all_bits[6] = {0};
	for (int b = 0; b < bones; b++)
		all_bits[b / 32] |= (int32_t)(0x80000000u >> (b % 32));
	for (int l = 0; l < 4; l++) {
		uint8_t *lod = model + MODEL_LODS + LOD_SIZE * l;
		int n = *(uint16_t *)(lod + 4);
		uint8_t *from = *(uint8_t **)(lod + L_SURFS);
		if (!n || !from)
			continue;
		uint8_t *surfs = bag_add(calloc((size_t)n, SURF_SIZE));
		int32_t bits[6];
		memcpy(bits, all_bits, sizeof(bits)); // every bone of the model (the pick: 0 and 1)
		for (int i = 0; i < n; i++) {
			uint8_t *sf = surfs + SURF_SIZE * i;
			memcpy(sf, from + SURF_SIZE * i, SURF_SIZE);
			uint16_t *blend = NULL;
			uint8_t *lists = NULL;
			if (sf[1] && *(uint16_t **)(sf + S_BLEND)) {
				size_t len = (size_t)blend_len(sf) * 2;
				blend = bag_add(malloc(len));
				memcpy(blend, *(uint16_t **)(sf + S_BLEND), len);
				*(uint16_t **)(sf + S_BLEND) = blend;
			}
			uint32_t nlists = *(uint32_t *)(sf + S_NLISTS);
			if (nlists && *(uint8_t **)(sf + S_LISTS)) {
				lists = bag_add(malloc(VL_SIZE * nlists));
				memcpy(lists, *(uint8_t **)(sf + S_LISTS), VL_SIZE * nlists);
				*(uint8_t **)(sf + S_LISTS) = lists;
			}
			struct remap_ctx rc = {map, bones, 0};
			each_bone(sf, blend, lists, remap_cb, &rc);
			memcpy(sf + S_PARTBITS, bits, sizeof(bits));
		}
		*(uint8_t **)(lod + L_SURFS) = surfs;
		memcpy(lod + L_PARTBITS, bits, sizeof(bits));
		uint8_t *ms = *(uint8_t **)(lod + L_MSURFS);
		if (ms) {
			uint8_t *copy = bag_add(malloc(MODELSURFS_SIZE));
			memcpy(copy, ms, MODELSURFS_SIZE);
			*(uint8_t **)(copy + MS_SURFS) = surfs;
			memcpy(copy + MS_PARTBITS, bits, sizeof(bits));
			*(uint8_t **)(lod + L_MSURFS) = copy;
		}
	}
	turned[ti].model = model;
	turned[ti].from = src;
	log_printf("transplant: %s made in the XModel pool (entry of %u bytes, %s turned by %.3f %.3f %.3f %.3f)", copy_name, (unsigned)size, turned[ti].source, rx, ry, rz, rw);
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
	{NULL, "mw2coop_transplant", NULL, NULL, transplant_f CMD_NODE_FLAG},
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
typedef char (*is_default_t)(unsigned type, const char *name);
static is_default_t is_default_original;

static char is_default(unsigned type, const char *name)
{
#ifndef _WIN64
	if (game_is_iw4x() && type == 0x24 && name && scripts_is_override(name)) // a script of ours (scripts.c): the rawfile exists
		return 0;
#endif
	if (type == 4 && name) {
		const int ti = turned_index(name);
		if (ti >= 0 && turned[ti].model)
			return 0;
	}
	if (type == 4 && name && strcmp(name, CLONE_NAME) == 0)
		for (int i = 0; i < 4; i++)
			if (clones[i].model)
				return 0;
	return is_default_original(type, name);
}

// mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi (the same as DB_FindXAssetHeader's)
#ifdef _WIN64
static const uint8_t is_default_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
#else
// 32-bit DB_IsXAssetDefault (0x4c32b0): push ebx; mov ebx,[esp+0xc]; push esi; push edi; mov edi,[esp+0x10]
static const uint8_t is_default_bytes[] = {0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10};
#endif


// The leak itself (all names, not only the two above): on a name the database does not have, DB_IsXAssetDefault returns 1
// from the end of its lookup loop without the "lock dec" of the reader count that the found path does, so the count grows
// by one for every such lookup and a writer of the database (a zone load, the next playsound) waits for no readers
// for ever: the intermittent hang of both copies a moment after a level start (32-bit contingency, 2026-10-28).
// The not-found exit jumps to a small cave that does the decrement first.
#ifdef _WIN64
#define LEAK_EXIT_RVA XR(0xf00c3)
#define LEAK_COUNT_RVA XR(0xa8a830)
#define LEAK_RESUME_RVA XR(0xf00c8) // the epilogue the found path also ends in
static const uint8_t leak_exit_bytes[] = {0xb8, 0x01, 0x00, 0x00, 0x00}; // mov eax,1
#else
#define LEAK_EXIT_RVA XRA(0xc332d) // (the iw4x-sp client's exe: DB_IsXAssetDefault at 0x1ab70, the same not-found exit at +0x7d: 0x1abed)
#define LEAK_COUNT_RVA XRA(0x8fc8b4) // (0x8ffe34 there)
static const uint8_t leak_exit_bytes[] = {0x5f, 0x5e, 0xb8, 0x01, 0x00, 0x00, 0x00, 0x5b, 0xc3}; // pop edi; pop esi; mov eax,1; pop ebx; ret
#endif

static void fix_reader_leak(void)
{
	uint8_t *exit_at = base + LEAK_EXIT_RVA;
	if (memcmp(exit_at, leak_exit_bytes, sizeof(leak_exit_bytes)) != 0) {
		log_printf("transplant: unexpected bytes at the not-found exit of DB_IsXAssetDefault, the reader count leak is not fixed");
		return;
	}
	uint8_t *cave = NULL;
#ifdef _WIN64
	for (uintptr_t at = (uintptr_t)base - 0x10000000; at < (uintptr_t)base + 0x10000000 && !cave; at += 0x10000) // raw-ok: a window around the exe for a code cave, no game address
		cave = VirtualAlloc((void *)at, 64, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
	cave = VirtualAlloc(NULL, 64, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#endif
	if (!cave) {
		log_printf("transplant: no memory for the reader count fix");
		return;
	}
	uint8_t *c = cave;
	*c++ = 0xf0; // lock dec dword ptr [count]
	*c++ = 0xff;
	*c++ = 0x0d;
#ifdef _WIN64
	*(int32_t *)c = (int32_t)((base + LEAK_COUNT_RVA) - (c + 4));
	c += 4;
	memcpy(c, leak_exit_bytes, sizeof(leak_exit_bytes)); // mov eax,1
	c += sizeof(leak_exit_bytes);
	*c++ = 0xe9; // jmp the epilogue
	*(int32_t *)c = (int32_t)((base + LEAK_RESUME_RVA) - (c + 4));
	c += 4;
#else
	*(uint32_t *)c = (uint32_t)(uintptr_t)(base + LEAK_COUNT_RVA);
	c += 4;
	memcpy(c, leak_exit_bytes, sizeof(leak_exit_bytes)); // the original exit, including its ret
	c += sizeof(leak_exit_bytes);
#endif
	DWORD old;
	VirtualProtect(exit_at, 5, PAGE_EXECUTE_READWRITE, &old);
	exit_at[0] = 0xe9;
	*(int32_t *)(exit_at + 1) = (int32_t)(cave - (exit_at + 5));
	VirtualProtect(exit_at, 5, old, &old);
	FlushInstructionCache(GetCurrentProcess(), exit_at, 5);
	log_printf("transplant: DB_IsXAssetDefault gives the reader count back on a name it does not have (cave %p)", (void *)cave);
}

static void hook_is_default(void)
{
	uint8_t *target = base + RVA_DB_IS_DEFAULT;
	if (memcmp(target, is_default_bytes, sizeof(is_default_bytes)) != 0) {
		log_printf("transplant: unexpected bytes at DB_IsXAssetDefault, not hooking");
		return;
	}
	if (!hook_function(target, is_default_bytes, sizeof(is_default_bytes), is_default, (void **)&is_default_original))
		return;
}

// ---- The hand that holds Endgame's knife (owner, 2026-11-18: "not only the knife, the hand too", "as in the first-person view")
// The body's animations have no pose for a held knife: in the rifle stance the left hand is the open support hand. A body that wears the knife copy (mw2coop_knife_held,
// on j_wrist_le) gets the left wrist of the first-person rig in the fight (player_afchase_ending_turnbuckle as the knife is attached, QA knifeframe): the forearm twist
// and the wrist turned as the rig's relative to their parents, the elbow, the arm and the fingers as the body's own animation has them. The rig's finger turns are in
// the table too (QA modes 2 / 4 / 8 of knife_hand_mode), but on the body's finger bones they crumpled the hand (owner: "the hand looks crumpled"): off by default. DObjCalcSkel (64-bit rva 0x299250)
// first has the animations write each bone's local turn into the DObj's matrices (rva 0x2a2df0), then composes them parent first, the attached models included: the
// turns are replaced in between, so the fingers and the knife on the wrist follow. The body's bones have the rig's axes turned 180 degrees about the bone (found on the
// knife's hold: the axis to the child agrees), so a rig turn L is the body's C^-1 L C, C = 180 degrees about X. The rig has two joints per finger; the body's third
// takes the second's turn. Only the bones this call composes are touched.
static const struct {
	const char *name;
	float q[4]; // the rig bone's turn in the rig's space (x y z w), from its tag axes
} rig_hand[] = {
    {"j_elbow_le", {0.50781f, -0.06794f, 0.76147f, 0.39708f}},
    {"j_wristtwist_le", {0.41824f, -0.21867f, 0.73257f, 0.49052f}},
    {"j_wrist_le", {0.35198f, -0.28630f, 0.78066f, 0.42978f}},
    {"j_thumb_le_1", {0.60771f, -0.24017f, 0.61489f, 0.44149f}},
    {"j_thumb_le_2", {0.68889f, 0.08734f, 0.24878f, 0.67521f}},
    {"j_index_le_1", {-0.44270f, 0.44343f, -0.24984f, 0.73821f}},
    {"j_index_le_2", {-0.14947f, 0.62814f, 0.12945f, 0.75256f}},
    {"j_mid_le_1", {-0.30701f, 0.61373f, -0.14364f, 0.71306f}},
    {"j_mid_le_2", {0.13031f, 0.67795f, 0.35491f, 0.63044f}},
    {"j_ring_le_1", {-0.21882f, 0.64762f, -0.19857f, 0.70234f}},
    {"j_ring_le_2", {0.25219f, 0.63325f, 0.30672f, 0.66431f}},
    {"j_pinky_le_1", {-0.21858f, 0.66934f, -0.24351f, 0.66702f}},
    {"j_pinky_le_2", {0.27638f, 0.66064f, 0.28456f, 0.63734f}},
};
#define RIG_HAND_N (sizeof(rig_hand) / sizeof(rig_hand[0]))
static const struct {
	const char *name;
	int like; // the rig_hand entry whose turn it takes
} rig_hand_like[] = {{"j_thumb_le_3", 4}, {"j_index_le_3", 6}, {"j_mid_le_3", 8}, {"j_ring_le_3", 10}, {"j_pinky_le_3", 12}};
#define RIG_LIKE_N (sizeof(rig_hand_like) / sizeof(rig_hand_like[0]))
#ifdef _WIN64
#define RVA_XANIM_CALC XR(0x2a2df0)      // (obj, partBits[6], workspace): the local turns of the bones asked for, into the DObj's matrices
#define RVA_DOBJ_BONE_INDEX XR(0x296020) // (obj, name, byte *index): 1 and the DObj's bone index of a name (the index is a cache: 0xfe for none yet)
#define RVA_SL_GET_STRING XR(0x18f440)   // (name): the script string of a name
#define OBJ_NUM_MODELS 0xd
#define OBJ_DONE_BITS 0x60 // the bones composed already (6 words)
#define OBJ_MATS 0x80      // the bones' matrices: quaternion x y z w, translation, weight (0x20 each)
#define OBJ_MODELS 0xa8    // the models' XModel pointers
static const uint8_t xanim_calc_bytes[] = {0x40, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50, 0x44, 0x8b, 0x0a, 0x49, 0x8b, 0xf0};
#else
// 32 bits (the same engine; matched by tools/match_funcs.py and read in Ghidra: DObjCalcSkel VA 0x466d60 calls 0x4d07b0 through 0x63bde0 / 0x63be20, cdecl with the same
// three arguments; the DObj's fields at other offsets). The functions are only called or (0x4d07b0) hooked: none lies in the ranges the exe hashes.
#define RVA_XANIM_CALC XR(0xd07b0)       // VA 0x4d07b0
#define RVA_DOBJ_BONE_INDEX XR(0x1f630)  // VA 0x41f630 (cdecl)
#define RVA_SL_GET_STRING XR(0x75dc0)    // VA 0x475dc0 (cdecl)
#define OBJ_NUM_MODELS 0x9
#define OBJ_DONE_BITS 0x5c
#define OBJ_MATS 0x78
#define OBJ_MODELS 0x98
static const uint8_t xanim_calc_bytes[] = {0x83, 0xec, 0x18, 0x53, 0x55, 0x56, 0x57, 0x8b, 0x4c, 0x24, 0x30}; // sub esp,18h; push ebx; push ebp; push esi; push edi; mov ecx,[esp+30h]
#endif
static void (*xanim_calc_original)(uint8_t *obj, const uint32_t *bits, void *work);
static uint16_t hand_ids[RIG_HAND_N + RIG_LIKE_N];
static int hand_ids_made;
static void quat_mul(const float *a, const float *b, float *o) // o = a b (x y z w)
{
	o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static int knife_hand_mode(void) // QA (the dvar mw2coop_qa_knifehand, read once a second): the parts of the hand posed, default 1 (the wrist: the rig's fingers on
{                                // the body's finger bones looked crumpled, owner 2026-11-18; the body's own half-closed fingers hold the handle)
	static int mode = 1;
	static DWORD at;
	if (GetTickCount() - at >= 1000) {
		at = GetTickCount();
		char v[16] = "";
		mode = game_dvar_raw("mw2coop_qa_knifehand", v, sizeof(v)) && v[0] ? atoi(v) : 1;
	}
	return mode;
}
static void knife_hand(uint8_t *obj, const uint32_t *bits, const uint32_t *done)
{
	const int nmodels = obj[OBJ_NUM_MODELS];
	uint8_t **models = *(uint8_t ***)(obj + OBJ_MODELS);
	float *mats = *(float **)(obj + OBJ_MATS);
	if (!models || !mats || nmodels < 2 || !models[0])
		return;
	int wears = 0; // (a turned copy of the knife: mw2coop_knife_held, or a QA one)
	for (size_t k = 0; k < TURNED_N && !wears; k++)
		if (turned[k].model && strcmp(turned[k].source, "weapon_commando_knife") == 0)
			for (int i = 1; i < nmodels; i++)
				wears |= models[i] == turned[k].model;
	if (!wears)
		return;
	if (!hand_ids_made) {
		uint16_t (*sl_get)(const char *) = (uint16_t (*)(const char *))(void *)(base + RVA_SL_GET_STRING);
		for (size_t i = 0; i < RIG_HAND_N; i++)
			hand_ids[i] = sl_get(rig_hand[i].name);
		for (size_t i = 0; i < RIG_LIKE_N; i++)
			hand_ids[RIG_HAND_N + i] = sl_get(rig_hand_like[i].name);
		hand_ids_made = 1;
	}
	char (*bone_index)(uint8_t *, unsigned, uint8_t *) = (char (*)(uint8_t *, unsigned, uint8_t *))(void *)(base + RVA_DOBJ_BONE_INDEX);
	int idx[RIG_HAND_N + RIG_LIKE_N];
	for (size_t i = 0; i < RIG_HAND_N + RIG_LIKE_N; i++) {
		uint8_t c = 0xfe;
		idx[i] = bone_index(obj, hand_ids[i], &c) ? c : -1;
	}
	const uint8_t *body = models[0];
	const int nb = body[M_BONES], nr = body[M_ROOTS];
	const uint8_t *parents = *(const uint8_t *const *)(body + M_PARENTS);
	if (!parents)
		return;
	float local[RIG_HAND_N][4];
	int have[RIG_HAND_N] = {0};
	static int logged;
	int set = 0;
	const int mode = knife_hand_mode();
	for (size_t i = 0; i < RIG_HAND_N; i++) {
		const int b = idx[i];
		if (!(mode & (i < 3 ? 1 : 2)))
			continue;
		if (b < nr || b >= nb || !(bits[b >> 5] & ~done[b >> 5] & (0x80000000u >> (b & 31))))
			continue;
		const int p = b - parents[b - nr];
		size_t j = 0;
		while (j < RIG_HAND_N && idx[j] != p)
			j++;
		if (j == RIG_HAND_N || j == i)
			continue; // (the parent is not one of the rig's bones measured: the elbow keeps the body's own turn)
		const float pc[4] = {-rig_hand[j].q[0], -rig_hand[j].q[1], -rig_hand[j].q[2], rig_hand[j].q[3]};
		float l[4];
		quat_mul(pc, rig_hand[i].q, l); // the rig's local turn
		if (i < 3 || !(mode & 8))
			l[1] = -l[1], l[2] = -l[2]; // in the body's axes (C^-1 L C, C = 180 degrees about X)
		memcpy(local[i], l, sizeof(l));
		have[i] = 1;
		memcpy(mats + (size_t)b * 8, l, sizeof(l));
		set++;
	}
	for (size_t k = 0; k < RIG_LIKE_N && (mode & 4); k++) {
		const int b = idx[RIG_HAND_N + k], from = rig_hand_like[k].like;
		if (b < nr || b >= nb || !have[from] || !(bits[b >> 5] & ~done[b >> 5] & (0x80000000u >> (b & 31))))
			continue;
		memcpy(mats + (size_t)b * 8, local[from], sizeof(local[from]));
		set++;
	}
	if (logged < 3 && set) {
		logged++;
		log_printf("transplant: the knife hand: %d bones of the left hand turned as the first-person rig's (wrist bone %d)", set, idx[2]);
	}
}
static void xanim_calc_hook(uint8_t *obj, const uint32_t *bits, void *work)
{
	uint32_t done[6];
	memcpy(done, obj + OBJ_DONE_BITS, sizeof(done));
	xanim_calc_original(obj, bits, work);
	knife_hand(obj, bits, done);
}
static void hook_knife_hand(void)
{
	uint8_t *target = base + RVA_XANIM_CALC;
	if (memcmp(target, xanim_calc_bytes, sizeof(xanim_calc_bytes)) != 0) {
		log_printf("transplant: unexpected bytes at the animation calc, the knife hand keeps the body's pose");
		return;
	}
#ifdef _WIN64
	hook_function(target, xanim_calc_bytes, sizeof(xanim_calc_bytes), xanim_calc_hook, (void **)&xanim_calc_original);
#else
	hook_function(target, xanim_calc_bytes, 7, xanim_calc_hook, (void **)&xanim_calc_original); // (the first 7 bytes: whole instructions)
#endif
}

void transplant_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	hook_is_default();
	fix_reader_leak();
	hook_knife_hand();
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
}

// QA ([native] ctxdiff=<first frame>): finds per-client state of the engine that the two client contexts share because it is not in a swapped range. The writable data of the game's
// image (the .data section, 72 MB) is copied before and after each context's client frame, for 40 frames in a row from the given one; the words a frame changed are recorded per
// context. A word that BOTH contexts' frames change and that holds a different value at the end of each (so the two contexts overwrite each other with different numbers there) and
// that is outside the swapped ranges is a candidate: per-client state, the way the console's arrays [4] are. They are grouped in 256-byte blocks and written to the log; the shared
// state that only looks the same (clocks, counters, caches) mostly drops out by the "different value" test. Other threads' writes are noise; the report lists how often a block was seen.
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ctxdiff.h"
#include "log.h"

#ifdef _WIN64
#define DATA_RVA 0x3bc000u
#define DATA_SIZE 0x450726cu
#define FRAMES 40

typedef struct {
	uint32_t index; // (qword offset)
	uint64_t value;
} change_t;

static uint8_t *snap;
static change_t *changes[2][FRAMES];
static size_t nchanges[2][FRAMES];
static int frames_done[2];
static int active;
static int armed;
static int (*swapped)(uint32_t rva);
static uint8_t *base_ptr;

void ctxdiff_start(uint8_t *base, int (*in_swapped_range)(uint32_t rva))
{
	base_ptr = base;
	swapped = in_swapped_range;
	armed = 1;
	snap = VirtualAlloc(NULL, DATA_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!snap)
		armed = 0;
}

void ctxdiff_before(int ctx)
{
	if (!armed || frames_done[ctx] >= FRAMES || (frames_done[0] > frames_done[1] + 1 && ctx == 0))
		return;
	memcpy(snap, base_ptr + DATA_RVA, DATA_SIZE);
	active = 1;
}

static int by_index(const void *a, const void *b)
{
	uint32_t x = ((const change_t *)a)->index, y = ((const change_t *)b)->index;
	return x < y ? -1 : x > y;
}

void ctxdiff_after(int ctx)
{
	if (!armed || !active)
		return;
	active = 0;
	int f = frames_done[ctx];
	const uint64_t *now = (const uint64_t *)(base_ptr + DATA_RVA), *was = (const uint64_t *)snap;
	size_t n = DATA_SIZE / 8, count = 0, cap = 1 << 16;
	change_t *list = malloc(cap * sizeof(change_t));
	for (size_t i = 0; i < n && list; i++)
		if (now[i] != was[i]) {
			if (count == cap) {
				cap *= 2;
				list = realloc(list, cap * sizeof(change_t));
				if (!list)
					break;
			}
			list[count].index = (uint32_t)i;
			list[count].value = now[i];
			count++;
		}
	changes[ctx][f] = list;
	nchanges[ctx][f] = list ? count : 0;
	frames_done[ctx]++;
	if (frames_done[0] >= FRAMES && frames_done[1] >= FRAMES)
		armed = 0, ctxdiff_report();
}

void ctxdiff_report(void)
{
	// the last frame's changes of each context, sorted; the words changed in every recorded frame of both
	typedef struct {
		uint32_t block;
		unsigned words;
	} block_t;
	static block_t blocks[20000];
	size_t nblocks = 0;
	for (int c = 0; c < 2; c++)
		qsort(changes[c][FRAMES - 1], nchanges[c][FRAMES - 1], sizeof(change_t), by_index);
	const change_t *a = changes[0][FRAMES - 1], *b = changes[1][FRAMES - 1];
	size_t na = nchanges[0][FRAMES - 1], nb = nchanges[1][FRAMES - 1], i = 0, j = 0, both = 0, different = 0;
	while (i < na && j < nb) {
		if (a[i].index < b[j].index)
			i++;
		else if (a[i].index > b[j].index)
			j++;
		else {
			both++;
			uint32_t rva = DATA_RVA + a[i].index * 8;
			if (a[i].value != b[j].value && !(swapped && swapped(rva))) {
				different++;
				uint32_t block = rva & ~0xffu;
				size_t k = 0;
				while (k < nblocks && blocks[k].block != block)
					k++;
				if (k == nblocks && nblocks < 20000)
					blocks[nblocks++] = (block_t){block, 0};
				if (k < nblocks)
					blocks[k].words++;
			}
			i++, j++;
		}
	}
	log_printf("ctxdiff: last frame: ctx 0 changed %zu words, ctx 1 %zu, both %zu, with different values outside the swapped ranges %zu in %zu blocks", na, nb, both, different, nblocks);
	for (size_t k = 0; k < nblocks; k++) // the busiest first (simple selection of 80)
		for (size_t m = k + 1; m < nblocks; m++)
			if (blocks[m].words > blocks[k].words) {
				block_t t = blocks[k];
				blocks[k] = blocks[m];
				blocks[m] = t;
			}
	// the blocks grouped in pages of 4 KB, in address order (a run of busy pages is one array)
	for (size_t k = 0; k < nblocks; k++)
		for (size_t m = k + 1; m < nblocks; m++)
			if (blocks[m].block < blocks[k].block) {
				block_t t = blocks[k];
				blocks[k] = blocks[m];
				blocks[m] = t;
			}
	size_t k = 0;
	int lines = 0;
	while (k < nblocks && lines < 400) {
		uint32_t page = blocks[k].block & ~0xfffu;
		unsigned words = 0, blocks_in = 0;
		uint32_t first = blocks[k].block, last = first;
		while (k < nblocks && (blocks[k].block & ~0xfffu) == page) {
			words += blocks[k].words, blocks_in++, last = blocks[k].block;
			k++;
		}
		log_printf("ctxdiff: page rva 0x%06x: %u words in %u blocks (0x%06x .. 0x%06x)", page, words, blocks_in, first, last);
		lines++;
	}
}
#else
void ctxdiff_start(uint8_t *base, int (*in_swapped_range)(uint32_t rva))
{
	(void)base;
	(void)in_swapped_range;
}
void ctxdiff_before(int ctx)
{
	(void)ctx;
}
void ctxdiff_after(int ctx)
{
	(void)ctx;
}
void ctxdiff_report(void)
{
}
#endif

// Runs the game's address resolver (src/addr.c) on a game exe file and prints what it resolved, for comparing with tools/addrsig.py verify
// and with a table of known answers:
//   test_addr <signature file> <iw4sp.exe> [truth file of "0xref -> 0xrva" lines]
// Build: see tools/test_addr.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/addr.h"

static char *slurp(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc((size_t)*size + 1);
	if (!buf || fread(buf, 1, (size_t)*size, f) != (size_t)*size)
		return NULL;
	buf[*size] = 0;
	fclose(f);
	return buf;
}

static uint32_t u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: test_addr <signature file> <iw4sp.exe> [truth file]\n");
		return 2;
	}
	long ssize, esize;
	char *sigs = slurp(argv[1], &ssize);
	uint8_t *exe = (uint8_t *)slurp(argv[2], &esize);
	if (!sigs || !exe) {
		fprintf(stderr, "cannot read the inputs\n");
		return 2;
	}
	uint32_t pe = u32(exe + 0x3c);
	int nsec = exe[pe + 6] | (exe[pe + 7] << 8);
	int optsize = exe[pe + 20] | (exe[pe + 21] << 8);
	int is64 = (exe[pe + 24] | (exe[pe + 25] << 8)) == 0x20b;
	uint64_t image_base = is64 ? (uint64_t)u32(exe + pe + 24 + 24) | ((uint64_t)u32(exe + pe + 24 + 28) << 32) : u32(exe + pe + 24 + 28);
	const uint8_t *sec = exe + pe + 24 + optsize;
	AddrSection secs[16];
	int n = 0;
	for (int i = 0; i < nsec && n < 16; i++) {
		const uint8_t *s = sec + 40 * i;
		uint32_t vs = u32(s + 8), va = u32(s + 12), rs = u32(s + 16), raw = u32(s + 20), ch = u32(s + 36);
		if (!(ch & 0x20000000))
			continue;
		secs[n].data = exe + raw;
		secs[n].rva = va;
		secs[n].size = vs < rs ? vs : rs;
		n++;
	}
	AddrResult res;
	int got = addr_resolve(sigs, secs, n, image_base, &res);
	printf("resolved %d, not resolved %d\n", got, res.nmissing);
	for (int i = 0; i < res.nmissing; i++)
		printf("missing 0x%x\n", (unsigned)res.missing[i]);
	if (argc > 3) {
		FILE *t = fopen(argv[3], "r");
		unsigned ref, want;
		char line[128];
		int right = 0, wrong = 0, absent = 0;
		while (t && fgets(line, sizeof(line), t)) {
			if (sscanf(line, " 0x%x -> 0x%x", &ref, &want) != 2)
				continue;
			int found = 0;
			for (int i = 0; i < res.n; i++)
				if (res.pairs[i].ref == ref) {
					found = 1;
					if (res.pairs[i].resolved == want)
						right++;
					else {
						wrong++;
						printf("wrong: 0x%x -> 0x%x, truth 0x%x\n", ref, res.pairs[i].resolved, want);
					}
				}
			if (!found)
				absent++;
		}
		printf("against the truth: %d right, %d wrong, %d not resolved\n", right, wrong, absent);
		return wrong != 0;
	}
	for (int i = 0; i < res.n; i++)
		printf("0x%x -> 0x%x\n", res.pairs[i].ref, res.pairs[i].resolved);
	return 0;
}

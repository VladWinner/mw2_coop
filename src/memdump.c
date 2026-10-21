// QA / reverse engineering: annotated memory dumps in the log, to map structures of this 64-bit
// build (menus have no text parser here, so the in-game co-op buttons are built from the zone's
// own menu assets).
//   !asset <type> <name>  -> DB_FindXAssetHeader(type, name), then dump 0x180 bytes of it
//   !menu <name> [item]  -> a menu's items with their string fields, or one item dumped
//   !menus                -> the names of the menus the UI has loaded
//   !rva <rva> <size>     -> the same at an address relative to the exe
//   !mem <address> <size> -> dump <size> bytes at an absolute address (hex)
// Each qword is printed with what it points to: a string, or "ptr" to readable memory.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "memdump.h"

static int readable(uintptr_t p, size_t n)
{
	if (p < 0x10000 || p >= 0x7fffffffffffull)
		return 0;
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery((void *)p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
		return 0;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		return 0;
	return p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static void describe(uint64_t q, char *out, size_t size)
{
	out[0] = 0;
	if (!readable((uintptr_t)q, 1))
		return;
	const char *s = (const char *)(uintptr_t)q;
	int n = 0;
	while (n < 60 && readable((uintptr_t)q + n, 1) && s[n] && (unsigned char)s[n] >= 0x20 && (unsigned char)s[n] < 0x7f)
		n++;
	if (n >= 2 && readable((uintptr_t)q + n, 1) && (s[n] == 0 || n == 60))
		snprintf(out, size, "\"%.*s\"%s", n, s, n == 60 ? "..." : "");
	else
		snprintf(out, size, "ptr");
}

void memdump(uintptr_t addr, size_t size)
{
	if (!readable(addr, size)) {
		log_printf("mem: %p+0x%zx not readable", (void *)addr, size);
		return;
	}
	for (size_t off = 0; off + 8 <= size; off += 8) {
		uint64_t q = *(uint64_t *)(addr + off);
		int32_t lo = (int32_t)q, hi = (int32_t)(q >> 32);
		float flo, fhi;
		memcpy(&flo, &lo, 4);
		memcpy(&fhi, &hi, 4);
		char what[96];
		describe(q, what, sizeof(what));
		log_printf("mem: %p +%03zx %016llx  %d %d  %g %g  %s", (void *)(addr + off), off, (unsigned long long)q, lo,
			   hi, flo, fhi, what);
	}
}

static void print_statement(const char *what, uint8_t *st);

// MenuEventHandlerSet {int count; MenuEventHandler **handlers}; MenuEventHandler {data; char type}:
// 0 script string, 1 if {MenuEventHandlerSet *, Statement *}, 2 else (set), 3..6 setLocalVar
static void print_handlers(const char *what, uint8_t *set, int depth)
{
	if (!set || !readable((uintptr_t)set, 0x10) || depth > 6)
		return;
	int count = *(int *)set;
	uint8_t **h = *(uint8_t ***)(set + 8);
	for (int i = 0; i < count && i < 64 && readable((uintptr_t)(h + i), 8); i++) {
		uint8_t *e = h[i];
		if (!readable((uintptr_t)e, 9))
			continue;
		uint8_t *data = *(uint8_t **)e;
		int type = e[8];
		if (type == 0 && readable((uintptr_t)data, 1))
			log_printf("mem: %s %*s%s", what, depth * 2, "", (const char *)data);
		else if (type == 1 && readable((uintptr_t)data, 16)) {
			log_printf("mem: %s %*sif (statement %p) {", what, depth * 2, "", *(void **)(data + 8));
			print_statement("  if", *(uint8_t **)(data + 8));
			print_handlers(what, *(uint8_t **)data, depth + 1);
			log_printf("mem: %s %*s}", what, depth * 2, "");
		} else if (type == 2) {
			log_printf("mem: %s %*selse {", what, depth * 2, "");
			print_handlers(what, data, depth + 1);
			log_printf("mem: %s %*s}", what, depth * 2, "");
		} else if (type >= 3 && type <= 6 && readable((uintptr_t)data, 16)) {
			// SetLocalVarData {const char *name; Statement *expression}: 3 bool, 4 int, 5 float, 6 string
			char d[96];
			describe(*(uint64_t *)data, d, sizeof(d));
			log_printf("mem: %s %*ssetLocalVar(%d) %s =", what, depth * 2, "", type, d);
			print_statement("    value", *(uint8_t **)(data + 8));
		} else {
			char d[96];
			describe((uint64_t)(uintptr_t)data, d, sizeof(d));
			log_printf("mem: %s %*s(type %d %s)", what, depth * 2, "", type, d);
		}
	}
}

// Statement_s {int numEntries; expressionEntry *entries; ...}; expressionEntry (0x18 bytes):
// int type (0 operator, 1 operand) at +0; operator number at +8, or operand {int dataType
// (0 int, 1 float, 2 string) at +8, value at +0x10}. Operators 27 and up are functions named by
// the exe's table at rva 0x3c4b38 ("int", "string", "float", "sin", ... "dvarbool" = 36).
#define RVA_EXP_FUNCTION_NAMES 0x3c4b38
#define EXP_FIRST_FUNCTION 27
#define EXP_FUNCTION_COUNT 169
static void print_statement(const char *what, uint8_t *st)
{
	static const char *const ops[EXP_FIRST_FUNCTION] = {"noop", ")", "*", "/", "%", "+", "-", "!", "<", "<=", ">",
		">=", "==", "!=", "&&", "||", "(", ",", "&", "|", "~", "<<", ">>", "dvarint(static)", "dvarbool(static)",
		"dvarfloat(static)", "dvarstring(static)"};
	if (!st || !readable((uintptr_t)st, 0x10))
		return;
	int n = *(int *)st;
	uint8_t *e = *(uint8_t **)(st + 8);
	if (n <= 0 || n > 400 || !readable((uintptr_t)e, (size_t)n * 0x18))
		return;
	const char **names = (const char **)((uint8_t *)GetModuleHandleA(NULL) + RVA_EXP_FUNCTION_NAMES);
	char out[2048];
	int len = 0;
	for (int i = 0; i < n && len < (int)sizeof(out) - 80; i++, e += 0x18) {
		int type = *(int *)e, a = *(int *)(e + 8);
		if (type != 0 && type != 1) {
			len += snprintf(out + len, sizeof(out) - len, "<type %d %d> ", type, a);
		} else if (type == 0) {
			const char *name = a < 0 ? "?" : a < EXP_FIRST_FUNCTION ? ops[a]
				: a - EXP_FIRST_FUNCTION < EXP_FUNCTION_COUNT ? names[a - EXP_FIRST_FUNCTION] : "?";
			if (!readable((uintptr_t)name, 1))
				name = "?";
			len += snprintf(out + len, sizeof(out) - len, "%s#%d ", name, a);
		} else if (a == 0) {
			len += snprintf(out + len, sizeof(out) - len, "%d ", *(int *)(e + 0x10));
		} else if (a == 1) {
			len += snprintf(out + len, sizeof(out) - len, "%g ", *(float *)(e + 0x10));
		} else {
			char d[96];
			describe(*(uint64_t *)(e + 0x10), d, sizeof(d));
			len += snprintf(out + len, sizeof(out) - len, "%s ", d);
		}
	}
	log_printf("mem: %s when %s", what, out);
}

int memdump_command(char *line)
{
	if (strncmp(line, "!asset ", 7) == 0) {
		char *end;
		int type = (int)strtol(line + 7, &end, 0);
		while (*end == ' ')
			end++;
		typedef void *(*find_t)(int, const char *);
		typedef int (*is_default_t)(int, const char *);
		uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
		void *h = ((find_t)(base + RVA_DB_FIND_HEADER))(type, end);
		int def = ((is_default_t)(base + RVA_DB_IS_DEFAULT))(type, end);
		log_printf("mem: asset %d '%s' at %p%s", type, end, h, def ? " (default asset: not in a loaded zone)" : "");
		if (h && !def)
			memdump((uintptr_t)h, 0x180);
		return 1;
	}
	if (strncmp(line, "!menu ", 6) == 0) {
		// "!menu <name> [item]": the menu's items with every string field, or one item in full
		// (menuDef_t: itemCount +0xbc, itemDef_t **items +0x168)
		char name[64];
		int item = -1;
		if (sscanf(line + 6, "%63s %d", name, &item) < 1)
			return 1;
		typedef void *(*find_t)(int, const char *);
		uint8_t *menu = ((find_t)((uint8_t *)GetModuleHandleA(NULL) + RVA_DB_FIND_HEADER))(26, name);
		if (!menu || !readable((uintptr_t)menu, 0x170))
			return 1;
		int count = *(int *)(menu + 0xbc);
		uint8_t **items = *(uint8_t ***)(menu + 0x168);
		log_printf("mem: menu %s at %p, %d items at %p", name, (void *)menu, count, (void *)items);
		if (item < 0) {
			print_handlers("onOpen", *(uint8_t **)(menu + 0xe0), 0);
			print_handlers("onClose", *(uint8_t **)(menu + 0xf0), 0);
			print_handlers("onESC", *(uint8_t **)(menu + 0xf8), 0);
		}
		for (int i = 0; i < count && readable((uintptr_t)(items + i), 8); i++) {
			if (item >= 0 && i != item)
				continue;
			uint8_t *it = items[i];
			if (item >= 0) {
				memdump((uintptr_t)it, 0x200);
				static const struct { int off; const char *name; } sets[] = {
					{0x108, "mouseEnterText"}, {0x110, "mouseExitText"}, {0x118, "mouseEnter"}, {0x120, "mouseExit"},
					{0x128, "action"}, {0x130, "accept"}, {0x138, "onFocus"}, {0x140, "leaveFocus"}};
				for (size_t k = 0; k < sizeof(sets) / sizeof(sets[0]); k++)
					print_handlers(sets[k].name, *(uint8_t **)(it + sets[k].off), 0);
				for (int off = 0x198; off < 0x1e8; off += 8) {
					char what[16];
					snprintf(what, sizeof(what), "exp+%x", off);
					print_statement(what, *(uint8_t **)(it + off));
				}
				continue;
			}
			char line_buf[1024];
			float *r = (float *)(it + 8);
			int n = snprintf(line_buf, sizeof(line_buf), "item %d %p rect %g %g %g %g:", i, (void *)it, r[0], r[1], r[2], r[3]);
			for (int off = 0; off < 0x200 && n < (int)sizeof(line_buf) - 100; off += 8) {
				if (!readable((uintptr_t)it + off, 8))
					break;
				char what[96];
				describe(*(uint64_t *)(it + off), what, sizeof(what));
				if (what[0] == '"')
					n += snprintf(line_buf + n, sizeof(line_buf) - n, " +%x=%s", off, what);
			}
			log_printf("mem: %s", line_buf);
		}
		return 1;
	}
	if (strcmp(line, "!menus") == 0) {
		// the UI's loaded menus (uiContext rva 0x1a3ab00: menus +0x38, count +0x1438)
		uint8_t *ui = (uint8_t *)GetModuleHandleA(NULL) + 0x1a3ab00;
		int count = *(int *)(ui + 0x1438);
		char buf[1024];
		int n = 0;
		for (int i = 0; i < count && i < 640; i++) {
			uint8_t *m = ((uint8_t **)(ui + 0x38))[i];
			n += snprintf(buf + n, sizeof(buf) - n, "%s ", m ? *(const char **)m : "?");
			if (n > 900) {
				log_printf("mem: menus: %s", buf);
				n = 0;
			}
		}
		log_printf("mem: menus: %s (%d)", buf, count);
		return 1;
	}
	if (strcmp(line, "!weapons") == 0) {
		// the loaded weapons: WeaponCompleteDef *[] at rva 0x3edce0 (name first), WeaponDef *[] at
		// 0x3ee320 (BG_GetWeaponDef 0x85850); ints +0x40..+0x64 of each WeaponDef
		uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
		for (int i = 1; i < 256; i++) {
			uint8_t *complete = ((uint8_t **)(base + 0x3edce0))[i];
			uint8_t *def = ((uint8_t **)(base + 0x3ee320))[i];
			if (!complete || !def || !readable((uintptr_t)complete, 8) || !readable((uintptr_t)def, 0x68))
				break;
			const char *name = *(const char **)complete;
			int *f = (int *)(def + 0x40);
			log_printf("mem: weapon %d %s: %d %d %d %d %d %d %d %d %d %d", i, readable((uintptr_t)name, 1) ? name : "?",
				   f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9]);
		}
		return 1;
	}
	if (strncmp(line, "!rva ", 5) == 0) {
		// "!rva <rva> <size>": like !mem, at an address relative to the exe
		char *end;
		uintptr_t rva = (uintptr_t)strtoull(line + 5, &end, 16);
		size_t size = (size_t)strtoull(end, NULL, 16);
		memdump((uintptr_t)GetModuleHandleA(NULL) + rva, size ? size : 0x80);
		return 1;
	}
	if (strncmp(line, "!mem ", 5) == 0) {
		char *end;
		uintptr_t addr = (uintptr_t)strtoull(line + 5, &end, 16);
		size_t size = (size_t)strtoull(end, NULL, 16);
		memdump(addr, size ? size : 0x80);
		return 1;
	}
	return 0;
}

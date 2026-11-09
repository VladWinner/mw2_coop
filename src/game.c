#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "log.h"
#include "addr.h"

static uint8_t *base;
static int ready;
static unsigned long long image_size; // SizeOfImage, the bound of game_peek

// The 64-bit build is known by its PE timestamp. The 32-bit exe is rewritten by Steam's wrapper for each account, so
// there main32.c identifies the build by the hash of its code in memory before it calls this.
int game_init(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
#ifdef _WIN64
	IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
	uint32_t stamp = nt->FileHeader.TimeDateStamp;
	image_size = nt->OptionalHeader.SizeOfImage;
	ready = stamp == IW4SP_TIMESTAMP;
	// QA: MW2COOP_FORCE_UNKNOWN=1 takes the unknown build path on the known build (the resolver must then find every address where it is)
	int forced = getenv("MW2COOP_FORCE_UNKNOWN") != NULL;
	if (forced)
		ready = 0;
	log_printf("game: exe timestamp 0x%08x, expected 0x%08x -> %s", stamp, IW4SP_TIMESTAMP,
		   ready ? "known build" : forced ? "UNKNOWN BUILD (forced for a test), game calls disabled" : "UNKNOWN BUILD, game calls disabled");
	if (!ready) { // look the addresses of this build up by their signatures (addr.c); the game calls stay off, the log says what was found
		int total = 0;
		int found = addr_resolve_running(&total);
		log_printf("game: addresses found by signature: %d of %d", found, total);
		if (addr_layout_same() && !forced) {
			ready = 1; // the layout of the known build: its addresses hold; hooks verify their own bytes
			log_printf("game: unknown build with the layout of the known one: the mod runs with the known addresses");
		}
	}
#else
	IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
	image_size = nt->OptionalHeader.SizeOfImage;
	ready = 1;
	log_printf("game: 32-bit build identified by main32.c, image %llu bytes", image_size);
#endif
	return ready;
}

void game_cbuf_addtext(const char *text)
{
	if (!ready)
		return;
	((void (*)(int, const char *))(base + RVA_CBUF_ADDTEXT))(0, text);
}

dvar_t *game_find_dvar(const char *name)
{
	if (!ready)
		return NULL;
#ifdef _WIN64
	return ((dvar_t * (*)(const char *))(base + RVA_DVAR_FINDVAR))(name);
#else
	// usercall: the name in EDI, the dvar (or NULL) in EAX; the function takes the dvar lock itself
	dvar_t *dvar;
	const char *arg = name;
	__asm__ volatile("call *%[fn]" : "=a"(dvar), "+D"(arg) : [fn] "r"(base + RVA_DVAR_FINDVAR) : "ecx", "edx", "memory", "cc");
	return dvar;
#endif
}

const char *game_dvar_string(const char *name, char *buf, int size)
{
	dvar_t *d = game_find_dvar(name);
	if (!buf || size <= 0)
		return buf;
	if (!d) {
		snprintf(buf, size, "<missing>");
		return buf;
	}
	/* Keep the historical quoted representation for string dvars, but route all
	   numeric values through the shared formatter so FLOAT2/3/4 and colour
	   vectors are rendered instead of leaking an internal '<type N>' marker. */
	if (d->type == DVAR_TYPE_STRING) {
		const char *value = d->current.string;
		if (!value || IsBadStringPtrA(value, 1))
			value = "";
		snprintf(buf, size, "\"%s\"", value);
	} else {
		game_dvar_value_text(d, &d->current, buf, size);
	}
	return buf;
}

unsigned long long game_peek(unsigned long long rva, int width)
{
	if (!ready || rva >= image_size)
		return 0;
	const uint8_t *p = base + rva;
	switch (width) {
	case 1:
		return *p;
	case 2:
		return *(const uint16_t *)p;
	case 8:
		return *(const uint64_t *)p;
	default:
		return *(const uint32_t *)p;
	}
}

unsigned long long game_peek_ptr(unsigned long long rva, unsigned long long off, int width)
{
	uintptr_t p = (uintptr_t)game_peek(rva, sizeof(void *));
	if (!p)
		return 0;
	const uint8_t *q = (const uint8_t *)(p + off);
	if (IsBadReadPtr(q, width))
		return 0;
	switch (width) {
	case 1:
		return *q;
	case 2:
		return *(const uint16_t *)q;
	case 8:
		return *(const uint64_t *)q;
	default:
		return *(const uint32_t *)q;
	}
}

void game_xuid_add(long long delta)
{
	if (!ready)
		return;
	uint64_t *xuid = (uint64_t *)(base + RVA_XUID_TABLE); // local client 0
	uint64_t before = *xuid;
	*xuid = before + delta;
	log_printf("game: xuid %016llx -> %016llx", (unsigned long long)before, (unsigned long long)*xuid);
}

int game_dvar_raw(const char *name, char *buf, int size)
{
	dvar_t *d = game_find_dvar(name);
	if (!d)
		return 0;
	switch (d->type) {
	case DVAR_TYPE_STRING:
		if (!d->current.string)
			return 0;
		lstrcpynA(buf, d->current.string, size);
		return 1;
	case DVAR_TYPE_BOOL:
		snprintf(buf, size, "%d", d->current.enabled);
		return 1;
	case DVAR_TYPE_INT:
	case DVAR_TYPE_ENUM:
		snprintf(buf, size, "%d", d->current.integer);
		return 1;
	default:
		return 0;
	}
}

void game_dvar_value_text(const dvar_t *d, const dvar_value_t *v, char *buf, int size)
{
	switch (d->type) {
	case DVAR_TYPE_BOOL:
		snprintf(buf, size, "%d", v->enabled);
		break;
	case DVAR_TYPE_INT:
	case DVAR_TYPE_ENUM:
		snprintf(buf, size, "%d", v->integer);
		break;
	case DVAR_TYPE_FLOAT:
		snprintf(buf, size, "%g", (double)v->value);
		break;
	case DVAR_TYPE_FLOAT_2:
		snprintf(buf, size, "%g %g", (double)v->vector[0], (double)v->vector[1]);
		break;
	case DVAR_TYPE_FLOAT_3:
	case DVAR_TYPE_FLOAT_3_COLOR:
		snprintf(buf, size, "%g %g %g", (double)v->vector[0], (double)v->vector[1], (double)v->vector[2]);
		break;
	case DVAR_TYPE_FLOAT_4:
	case DVAR_TYPE_COLOR:
		snprintf(buf, size, "%g %g %g %g", (double)v->vector[0], (double)v->vector[1], (double)v->vector[2], (double)v->vector[3]);
		break;
	case DVAR_TYPE_STRING: {
		const char *s = v->string;
		snprintf(buf, size, "%s", s && !IsBadStringPtrA(s, 1) ? s : "");
		break;
	}
	default:
		snprintf(buf, size, "<type %d>", d->type);
	}
}

void game_for_each_dvar(int (*fn)(dvar_t *dvar, void *ctx), void *ctx)
{
	if (!ready || !RVA_DVAR_HASH)
		return;
	dvar_t **table = (dvar_t **)(base + RVA_DVAR_HASH);
	for (int i = 0; i < 1024; i++) {
		int guard = 0;
		for (dvar_t *d = table[i]; d && guard < 64 && !IsBadReadPtr(d, sizeof(*d)); d = d->hash_next, guard++)
			if (d->name && !IsBadStringPtrA(d->name, 256) && fn(d, ctx))
				return;
	}
}

void game_for_each_command(int (*fn)(const char *name, void *ctx), void *ctx)
{
	if (!ready || !RVA_CMD_FUNCTIONS)
		return;
	int guard = 0;
	// (the list is the game's own and changes under us: a node can be freed while the console walks it, and its name pointer then points nowhere; Vlad's log 2026-11-09:
	// a read fault in _stricmp of the console's completion catalog while the second player was bleeding out)
	for (cmd_function_t *c = *(cmd_function_t **)(base + RVA_CMD_FUNCTIONS); c && guard < 4000 && !IsBadReadPtr(c, sizeof(*c)); c = c->next, guard++)
		if (c->name && !IsBadStringPtrA(c->name, 256) && fn(c->name, ctx))
			return;
}

int game_command_exists(const char *name)
{
	int guard = 0;
	if (!ready || !RVA_CMD_FUNCTIONS || !name)
		return 0;
	for (cmd_function_t *c = *(cmd_function_t **)(base + RVA_CMD_FUNCTIONS); c && guard < 4000; c = c->next, guard++)
		if (c->name && !strcmp(c->name, name))
			return 1;
	return 0;
}

void game_log_commands(void)
{
	if (!ready || !RVA_CMD_FUNCTIONS)
		return;
	cmd_function_t *c = *(cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	char line[1800] = {0};
	int n = 0, count = 0, guard = 0;
	for (; c && guard < 4000; c = c->next, guard++) {
		uintptr_t fn = (uintptr_t)c->function;
		uintptr_t rva = fn >= (uintptr_t)base ? fn - (uintptr_t)base : 0;
		int w = snprintf(line + n, sizeof(line) - (size_t)n, "%s@%llx ", c->name ? c->name : "?", (unsigned long long)rva);
		if (w < 0)
			continue;
		if ((size_t)w >= sizeof(line) - (size_t)n || n + w >= (int)sizeof(line) - 64) {
			if (n)
				log_printf("cmds: %s", line);
			n = 0;
			line[0] = 0;
			w = snprintf(line, sizeof(line), "%s@%llx ", c->name ? c->name : "?", (unsigned long long)rva);
			if (w < 0 || (size_t)w >= sizeof(line)) {
				line[0] = 0;
				continue;
			}
		}
		n += w;
		count++;
	}
	if (n)
		log_printf("cmds: %s", line);
	log_printf("cmds: %d commands registered%s", count, c ? " (truncated; traversal limit 4000)" : "");
}

static int cmd_args_argc(int rva)
{
	if (!ready || !rva)
		return 0;
	uint8_t *args = base + rva;
	int nesting = *(int *)args;
	if (nesting < 0 || nesting >= 8)
		return 0;
	int argc = *(int *)(args + 0x44 + nesting * 4);
	return argc >= 0 && argc <= 1024 ? argc : 0;
}

static const char *cmd_args_argv(int rva, int i)
{
	int argc = cmd_args_argc(rva);
	if (i < 0 || i >= argc)
		return "";
	uint8_t *args = base + rva;
	int nesting = *(int *)args;
#ifdef _WIN64
	return (*(const char ***)(args + 0x68 + nesting * 8))[i];
#else
	return (*(const char ***)(args + 0x64 + nesting * 4))[i];
#endif
}

int game_cmd_argc(void)
{
	return cmd_args_argc(RVA_CMD_ARGS);
}

const char *game_cmd_argv(int i)
{
	return cmd_args_argv(RVA_CMD_ARGS, i);
}

const char *game_sv_cmd_argv(int i)
{
	return cmd_args_argv(RVA_SV_CMD_ARGS, i);
}

void game_add_command(cmd_function_t *node)
{
	if (!ready)
		return;
#ifdef _WIN64
	cmd_function_t **head = (cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	node->next = *head;
	*head = node;
#else
	// Cmd_AddCommand(name, function, node, 0) fills the node itself and links it into the game's list
	((void (*)(const char *, void (*)(void), void *, int))(base + RVA_CMD_ADDCOMMAND))(node->name, node->function, node, 0);
#endif
}

int game_qa_flag(const char *name)
{
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\");
	lstrcatA(path, name);
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

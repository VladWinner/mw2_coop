// The campaign missions: one table (content/release/missions.csv) instead of lists in the Discord, zone and lobby code.
// The built-in rows are compiled in (missions_gen.h); a mw2coop\missions.csv next to the game is read once and its rows
// replace the built-in row of the same map or add a map. Format: CSV with a header row, lines starting with // are comments.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "missions.h"
#include "log.h"

#define MAX_MISSIONS 96

static const struct {
	const char *map, *title, *zone;
	int zone_own, picture;
} built_in[] = {
#include "missions_gen.h"
};

static Mission table[MAX_MISSIONS];
static int count;
static volatile LONG loaded; // 0 not yet, 1 loading, 2 done

static void put(const char *map, const char *title, const char *zone, int own, int picture)
{
	int i;
	for (i = 0; i < count; i++)
		if (_stricmp(table[i].map, map) == 0)
			break;
	if (i == count) {
		if (count >= MAX_MISSIONS)
			return;
		count++;
	}
	lstrcpynA(table[i].map, map, sizeof(table[i].map));
	lstrcpynA(table[i].title, title, sizeof(table[i].title));
	lstrcpynA(table[i].zone, zone, sizeof(table[i].zone));
	table[i].zone_own = own;
	table[i].picture = picture;
}

// the next field of a CSV line (no quotes: the fields hold no commas); advances *p past the comma
static void field(char **p, char *out, size_t size)
{
	char *s = *p, *e = strchr(s, ',');
	size_t n = e ? (size_t)(e - s) : strlen(s);
	if (n >= size)
		n = size - 1;
	memcpy(out, s, n);
	out[n] = 0;
	*p = e ? e + 1 : s + strlen(s);
}

static void read_file(void)
{
	char path[MAX_PATH];
	DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
	char *slash = length && length < sizeof(path) ? strrchr(path, '\\') : NULL;
	if (!slash)
		return;
	slash[1] = 0;
	strncat(path, "mw2coop\\missions.csv", sizeof(path) - strlen(path) - 1);
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	static char buf[1 << 16];
	DWORD got = 0;
	if (!ReadFile(h, buf, sizeof(buf) - 1, &got, NULL))
		got = 0;
	CloseHandle(h);
	buf[got] = 0;
	int rows = 0, header = 1;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\r' || line[n - 1] == ' '))
			line[--n] = 0;
		if (!n || (line[0] == '/' && line[1] == '/') || (unsigned char)line[0] == 0xef)
			continue;
		if (header) { // the column names
			header = 0;
			continue;
		}
		char map[48], title[64], zone[48], own[8], pic[8];
		char *p = line;
		field(&p, map, sizeof(map));
		field(&p, title, sizeof(title));
		field(&p, zone, sizeof(zone));
		field(&p, own, sizeof(own));
		field(&p, pic, sizeof(pic));
		if (!map[0])
			continue;
		put(map, title, zone, atoi(own), atoi(pic));
		rows++;
	}
	log_printf("missions: %s: %d rows read", "missions.csv", rows);
}

const Mission *missions_find(const char *map)
{
	if (!map || !map[0])
		return NULL;
	if (loaded != 2) {
		if (InterlockedCompareExchange(&loaded, 1, 0) == 0) {
			for (size_t i = 0; i < sizeof(built_in) / sizeof(built_in[0]); i++)
				put(built_in[i].map, built_in[i].title, built_in[i].zone, built_in[i].zone_own, built_in[i].picture);
			read_file();
			InterlockedExchange(&loaded, 2);
		} else
			while (loaded != 2)
				Sleep(1);
	}
	for (int i = 0; i < count; i++)
		if (_stricmp(table[i].map, map) == 0)
			return &table[i];
	return NULL;
}

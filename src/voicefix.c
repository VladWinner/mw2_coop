// Optional Russian voice fixes, supplied by the player: [russian] voicefix=1 in settings.ini links the archives found in <game dir>\mw2coop\voicefix\ (named
// localized_russian_iw*.iwd) into <game dir>\main\, where the game loads them after its own (a later archive wins for a file of the same name), and voicefix=0 takes
// back what this made. The mod does not ship the files (the community pack "Исправление локализации CoD MW2" is by Rus-Licenzer over the 1C localization: its readme
// forbids spreading it without the author's name and leaves the rights with 1C). A file already in main\ is never replaced; what was made is listed in
// mw2coop\voicefix\created.txt. A hard link costs no disk space; if the folders are on different drives the file is copied.
#include <windows.h>
#include <string.h>

#include "log.h"
#include "voicefix.h"

#define MAX_NAMES 32
#define BACKSLASH 0x5c

static int dir_of_game(char *out, size_t cap)
{
	DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
	while (n > 0 && out[n - 1] != BACKSLASH)
		n--;
	out[n] = 0;
	return n > 0;
}

static int read_manifest(const char *path, char names[MAX_NAMES][MAX_PATH])
{
	int n = 0;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return 0;
	char buf[4096];
	DWORD got = 0;
	if (ReadFile(f, buf, sizeof(buf) - 1, &got, NULL))
		buf[got] = 0;
	else
		buf[0] = 0;
	CloseHandle(f);
	for (char *p = buf; *p && n < MAX_NAMES;) {
		char *e = p;
		while (*e && *e != '\r' && *e != '\n')
			e++;
		char save = *e;
		*e = 0;
		if (*p && !strchr(p, BACKSLASH) && !strchr(p, '/') && strstr(p, ".iwd"))
			lstrcpynA(names[n++], p, MAX_PATH);
		*e = save;
		while (*e == '\r' || *e == '\n')
			e++;
		p = e;
	}
	return n;
}

static void write_manifest(const char *path, char names[MAX_NAMES][MAX_PATH], int n)
{
	if (!n) {
		DeleteFileA(path);
		return;
	}
	HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return;
	for (int i = 0; i < n; i++) {
		DWORD w;
		WriteFile(f, names[i], (DWORD)lstrlenA(names[i]), &w, NULL);
		WriteFile(f, "\r\n", 2, &w, NULL);
	}
	CloseHandle(f);
}

static int has_name(char names[MAX_NAMES][MAX_PATH], int n, const char *name)
{
	for (int i = 0; i < n; i++)
		if (!lstrcmpiA(names[i], name))
			return 1;
	return 0;
}

void voicefix_apply(const char *settings_path)
{
	char game[MAX_PATH], src[MAX_PATH], main_dir[MAX_PATH], manifest[MAX_PATH];
	static char made[MAX_NAMES][MAX_PATH];
	if (!dir_of_game(game, sizeof(game)))
		return;
	int on = GetPrivateProfileIntA("russian", "voicefix", 0, settings_path) != 0;
	wsprintfA(src, "%smw2coop\\voicefix\\", game);
	wsprintfA(main_dir, "%smain\\", game);
	wsprintfA(manifest, "%screated.txt", src);
	int n = read_manifest(manifest, made);
	if (!on) {
		int left = 0;
		for (int i = 0; i < n; i++) {
			char dst[MAX_PATH];
			wsprintfA(dst, "%s%s", main_dir, made[i]);
			if (GetFileAttributesA(dst) == INVALID_FILE_ATTRIBUTES) // already gone
				continue;
			if (DeleteFileA(dst))
				log_printf("voicefix: removed main\\%s (voicefix=0)", made[i]);
			else
				lstrcpyA(made[left++], made[i]); // still in use: tried again at the next start
		}
		write_manifest(manifest, made, left);
		return;
	}
	char pattern[MAX_PATH];
	wsprintfA(pattern, "%slocalized_russian_iw*.iwd", src);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		log_printf("voicefix: voicefix=1, but there are no localized_russian_iw*.iwd files in mw2coop\\voicefix");
		return;
	}
	int added = 0, linked = 0;
	do {
		char from[MAX_PATH], dst[MAX_PATH];
		wsprintfA(from, "%s%s", src, fd.cFileName);
		wsprintfA(dst, "%s%s", main_dir, fd.cFileName);
		if (GetFileAttributesA(dst) != INVALID_FILE_ATTRIBUTES) {
			if (!has_name(made, n, fd.cFileName))
				log_printf("voicefix: main\\%s is already there and is not ours: left alone", fd.cFileName);
			continue;
		}
		if (CreateHardLinkA(dst, from, NULL))
			linked++;
		else if (!CopyFileA(from, dst, TRUE)) {
			log_printf("voicefix: cannot put %s into main (error %lu)", fd.cFileName, GetLastError());
			continue;
		}
		if (n < MAX_NAMES)
			lstrcpynA(made[n++], fd.cFileName, MAX_PATH);
		added++;
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	if (added)
		write_manifest(manifest, made, n);
	log_printf("voicefix: %d voice archive(s) put into main (%d as links)", added, linked);
}

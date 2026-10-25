// The second player's game process for a split screen session, started from the SAME install: nothing is copied. The
// install's own files are shared with it (hard links for the files of the game folder, junctions for its folders) in
// <game dir>\mw2coop\p2, where it also gets what the game writes: its own settings.ini, players folder, log and
// command file. It runs hidden, joins this process as the partner (the mod's own lobby join on this PC) and, when a
// level starts, both windows go into the shared window (splitscreen.c).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "instance.h"
#include "log.h"
#include "splitscreen.h"

static HANDLE child = NULL;
static char pad_file[2 * MAX_PATH];

// which pad is the second player's: the second process reads it from <its folder>\mw2coop\pad.txt
static void write_pad(int pad)
{
	FILE *f = fopen(pad_file, "w");
	if (f) {
		fprintf(f, "%d%c", pad, 10);
		fclose(f);
	}
	log_printf("instance: pad %d is the second player's", pad);
}

// the lobby's popup asks the second player to press a button on his device; the first pad that does is his
static DWORD WINAPI assign_thread(LPVOID unused)
{
	(void)unused;
	for (int i = 0; i < 2400; i++) { // two minutes
		Sleep(50);
		int pad = splitscreen_poll_any_pad();
		if (pad >= 0) {
			write_pad(pad);
			game_cbuf_addtext("closemenu mw2coop_assign\n");
			return 0;
		}
	}
	log_printf("instance: no pad was pressed, the second player has none");
	return 0;
}

static void parent_dir(const char *path, char *out)
{
	lstrcpynA(out, path, MAX_PATH);
	char *slash = strrchr(out, '\\');
	if (slash)
		slash[1] = 0;
}

static int run_hidden(char *cmdline)
{
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
		return 0;
	WaitForSingleObject(pi.hProcess, 15000);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return code == 0;
}

static int is_dir(const char *path)
{
	DWORD a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// a link to a folder: a junction (no administrator rights needed)
static int link_dir(const char *dst, const char *src)
{
	if (is_dir(dst))
		return 1; // from an earlier start (a junction or a real folder)
	char cmd[2 * MAX_PATH + 40];
	snprintf(cmd, sizeof(cmd), "cmd.exe /c mklink /J \"%s\" \"%s\"", dst, src);
	return run_hidden(cmd);
}

// a link to a file: a hard link (the same volume), else a copy (small files: the game's own dlls)
static int link_file(const char *dst, const char *src)
{
	DeleteFileA(dst);
	if (CreateHardLinkA(dst, src, NULL))
		return 1;
	return CopyFileA(src, dst, FALSE);
}

static void own_address(char *out, size_t n)
{
	snprintf(out, n, "127.0.0.1"); // mw2coop_join maps a loopback address to this PC's own (party.c)
}

int instance_start(int pad)
{
	if (child && WaitForSingleObject(child, 0) == WAIT_TIMEOUT) {
		log_printf("instance: the second process is running already");
		return 1;
	}
	char exe[MAX_PATH], game[MAX_PATH], p2[MAX_PATH], path[2 * MAX_PATH], src[2 * MAX_PATH];
	GetModuleFileNameA(NULL, exe, MAX_PATH);
	parent_dir(exe, game);
	const char *exe_name = strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe;
	snprintf(p2, sizeof(p2), "%smw2coop\\p2\\", game);
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%smw2coop", game);
	CreateDirectoryA(dir, NULL);
	snprintf(dir, sizeof(dir), "%smw2coop\\p2", game);
	CreateDirectoryA(dir, NULL);

	// the game folder: files as hard links, folders as junctions; what the game writes is the second process's own
	WIN32_FIND_DATAA fd;
	snprintf(path, sizeof(path), "%s*", game);
	HANDLE h = FindFirstFileA(path, &fd);
	int failed = 0;
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const char *n = fd.cFileName;
			if (!strcmp(n, ".") || !strcmp(n, "..") || !strcmp(n, "mw2coop") || !strcmp(n, "players") || !strcmp(n, "__iw4sp"))
				continue;
			snprintf(src, sizeof(src), "%s%s", game, n);
			snprintf(path, sizeof(path), "%s%s", p2, n);
			int ok = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? link_dir(path, src) : link_file(path, src);
			if (!ok) {
				log_printf("instance: cannot link %s", n);
				failed = 1;
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	if (failed)
		return 0;

	// players: the settings and bindings of this player's game (files only; saves are not shared)
	snprintf(path, sizeof(path), "%splayers", p2);
	CreateDirectoryA(path, NULL);
	snprintf(path, sizeof(path), "%splayers\\*", game);
	h = FindFirstFileA(path, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			snprintf(src, sizeof(src), "%splayers\\%s", game, fd.cFileName);
			snprintf(path, sizeof(path), "%splayers\\%s", p2, fd.cFileName);
			CopyFileA(src, path, FALSE);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}

	// the mod's folder: its files shared, its settings, log, progress and commands the second process's own
	snprintf(path, sizeof(path), "%smw2coop", p2);
	CreateDirectoryA(path, NULL);
	static const char *shared[] = {"scripts", "patches", "fonts"};
	for (int i = 0; i < 3; i++) {
		snprintf(src, sizeof(src), "%smw2coop\\%s", game, shared[i]);
		snprintf(path, sizeof(path), "%smw2coop\\%s", p2, shared[i]);
		if (is_dir(src) && !link_dir(path, src)) {
			log_printf("instance: cannot link mw2coop\\%s", shared[i]);
			return 0;
		}
	}
	snprintf(src, sizeof(src), "%smw2coop\\settings.ini", game);
	snprintf(path, sizeof(path), "%smw2coop\\settings.ini", p2);
	CopyFileA(src, path, FALSE); // a missing one is fine
	WritePrivateProfileStringA("splitscreen", "slot", "1", path);
	char padtext[16];
	snprintf(padtext, sizeof(padtext), "%d", pad);
	WritePrivateProfileStringA("splitscreen", "pad", padtext, path);
	snprintf(path, sizeof(path), "%smw2coop\\mw2coop.log", p2);
	DeleteFileA(path);
	// the game's lock file: one left by a killed copy makes the next start ask whether to run in safe mode
	snprintf(path, sizeof(path), "%s__iw4sp", p2);
	DeleteFileA(path);

	// what it does once it runs: the mod's own join of this PC as the partner (a second XUID, there is one Steam account)
	char addr[32];
	own_address(addr, sizeof(addr));
	snprintf(path, sizeof(path), "%smw2coop\\commands.txt", p2);
	FILE *f = fopen(path, "w");
	if (!f) {
		log_printf("instance: cannot write the command file");
		return 0;
	}
	fprintf(f, "# the second player of a split screen session\n!sleep 12000\n!xuid_add 1\n!sleep 2000\nmw2coop_join %s\n!sleep 600000\n", addr);
	fclose(f);

	// the game's own checks: started by this process, with its Steam environment
	SetEnvironmentVariableA("SteamAppId", "10180");
	SetEnvironmentVariableA("SteamGameId", "10180");
	char pid[16];
	snprintf(pid, sizeof(pid), "%lu", GetCurrentProcessId());
	SetEnvironmentVariableA("MW2COOP_PARENT_PID", pid); // the second process quits with this one
	splitscreen_host_activate();
	char cmd[2 * MAX_PATH];
	snprintf(path, sizeof(path), "%s%s", p2, exe_name);
	snprintf(cmd, sizeof(cmd), "\"%s\"", path);
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(path, cmd, NULL, NULL, FALSE, 0, NULL, p2, &si, &pi)) {
		log_printf("instance: cannot start %s (error %lu)", path, GetLastError());
		return 0;
	}
	CloseHandle(pi.hThread);
	child = pi.hProcess;
	log_printf("instance: the second process runs (pid %lu) from %s", pi.dwProcessId, p2);
	snprintf(pad_file, sizeof(pad_file), "%smw2coop\\pad.txt", p2);
	DeleteFileA(pad_file);
	if (pad >= 0)
		write_pad(pad);
	else
		CreateThread(NULL, 0, assign_thread, NULL, 0, NULL);
	return 1;
}

void instance_stop(void)
{
	if (child && WaitForSingleObject(child, 0) == WAIT_TIMEOUT) {
		TerminateProcess(child, 0);
		log_printf("instance: the second process was closed");
	}
}

static void split_f(void)
{
	const char *a = game_cmd_argv(1);
	instance_start(a[0] ? atoi(a) : -1);
}

// QA: what the lobby's Start does in a split screen session
static void splitlaunch_f(void)
{
	if (!splitscreen_before_launch())
		game_cbuf_addtext("coopLaunch\n");
}

static cmd_function_t split_cmd = {NULL, "mw2coop_split", NULL, NULL, split_f CMD_NODE_FLAG};
static cmd_function_t splitlaunch_cmd = {NULL, "mw2coop_splitlaunch", NULL, NULL, splitlaunch_f CMD_NODE_FLAG};

void instance_install(void)
{
	game_add_command(&split_cmd);
	game_add_command(&splitlaunch_cmd);
}

// mw2coop: campaign co-op for Modern Warfare 2 (2009), 64-bit Steam build.
// Milestone 2: run console commands from a file and watch dvars (QA remote control).
#include <windows.h>

#include "control.h"
#include "crash.h"
#include "game.h"
#include "log.h"
#include "net.h"
#include "party.h"
#include "progress.h"
#include "scrdebug.h"
#include "scripts.h"

int proxy_init(void);

static BOOL CALLBACK find_game_window(HWND h, LPARAM out)
{
	DWORD pid;
	char cls[32];
	GetWindowThreadProcessId(h, &pid);
	if (pid == GetCurrentProcessId() && GetClassNameA(h, cls, sizeof(cls)) && lstrcmpA(cls, "IW4") == 0) {
		*(HWND *)out = h;
		return FALSE;
	}
	return TRUE;
}

static DWORD WINAPI watcher(LPVOID unused)
{
	(void)unused;
	DWORD start = GetTickCount();
	HWND game = NULL;
	while (!game && GetTickCount() - start < 120000) {
		Sleep(100);
		EnumWindows(find_game_window, (LPARAM)&game);
	}
	if (!game) {
		log_printf("main: no game window after 120 s");
		return 0;
	}
	log_printf("main: game window %p after %lu ms", (void *)game, GetTickCount() - start);
	if (game_init()) {
		crash_install();
		scripts_install();
		scrdebug_install();
		net_install();
		party_install();
		progress_install();
		control_start();
	}
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
	(void)reserved;
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(self);
	log_open();

	char exe[MAX_PATH];
	GetModuleFileNameA(NULL, exe, MAX_PATH);
	HMODULE base = GetModuleHandleA(NULL);
	log_printf("main: attached to %s, image base %p, pid %lu", exe, (void *)base, GetCurrentProcessId());

	if (!proxy_init()) {
		log_printf("main: could not load the real winmm.dll");
		return FALSE;
	}
	CloseHandle(CreateThread(NULL, 0, watcher, NULL, 0, NULL));
	return TRUE;
}

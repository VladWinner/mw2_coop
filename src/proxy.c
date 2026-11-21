// winmm.dll proxy: every export in winmm_stubs.S jumps through winmm_real[], which points at
// the real System32 winmm.dll. The game loads winmm.dll from its own folder first, which is
// how this DLL gets into the process.
#include <windows.h>

#include "log.h"
#include <winmm_names.h>

void *winmm_real[WINMM_EXPORT_COUNT];

// The mod runs only in the single-player game: iw4sp.exe (Steam's, 64 or 32 bits) and iw4x-sp.exe (the iw4x-sp client). Any other program that loads this DLL from the
// game folder (the multiplayer iw4mp.exe, IW4x's iw4x.exe and its launcher, a tool) gets a plain winmm proxy: no log, no hook, no thread, nothing of the mod
// (2026-11-21: until then the DLL captured the Direct3D import and started its thread in whatever process loaded it). QA: MW2COOP_ANYHOST=1 lifts it.
int proxy_host_is_game(void)
{
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return 0;
	const char *name = path + n;
	while (name > path && name[-1] != '\\' && name[-1] != '/')
		name--;
	if (lstrcmpiA(name, "iw4sp.exe") == 0 || lstrcmpiA(name, "iw4x-sp.exe") == 0)
		return 1;
	char v[8];
	return GetEnvironmentVariableA("MW2COOP_ANYHOST", v, sizeof(v)) > 0 && v[0] == '1';
}

int proxy_init(void)
{
	wchar_t path[MAX_PATH];
	UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (n == 0 || n > MAX_PATH - 12)
		return 0;
	lstrcatW(path, L"\\winmm.dll");

	HMODULE real = LoadLibraryW(path);
	if (!real)
		return 0;

	int missing = 0;
	for (int i = 0; i < WINMM_EXPORT_COUNT; i++) {
		winmm_real[i] = (void *)GetProcAddress(real, winmm_names[i]);
		if (!winmm_real[i])
			missing++;
	}
	log_printf("proxy: real winmm at %p, %d exports, %d missing", (void *)real, WINMM_EXPORT_COUNT, missing);
	return 1;
}

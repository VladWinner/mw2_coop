# 32-bit support: plan and state

The mod was made for the 64-bit build of Modern Warfare 2 (2009) that Steam moved the game to in September 2026.
The build before that (32-bit, `iw4sp.exe` linked 2010-02-10) is what most existing mods and many players still use.
Same design (a `winmm.dll` proxy that patches script text in memory and hooks the engine), other addresses and other
structure sizes. The 64-bit build must not break: `sh build.sh` builds it as before, `sh build.sh x86` the 32-bit one.

## Getting a 32-bit game to work on (no download from outside Steam)

Steam keeps the old depots: `download_depot 10180 <depot> <manifest>` in the Steam console puts them in
`steamapps\content\app_10180\depot_<id>` without touching the installed game (a community guide lists the manifests
for depots 10182, 10181 and the language depot, 10189 for Russian). Merge the three folders into a new folder
(never over the installed game), then copy that folder to a test copy.

## What was learnt on the first run (2026-10-25)

- The exe imports `WINMM.dll` (the same proxy slot as the 64-bit game), `WSOCK32.dll`, `steam_api.dll` (Valve's own,
  115,960 B), `mss32.dll`, `binkw32.dll`, `d3d9.dll`. The real `winmm.dll` is `SysWOW64\winmm.dll` (192 exports,
  `tools/gen_winmm_proxy.py` writes the 32-bit stubs into `src/x86/`).
- **The exe is rewritten by Steam's wrapper at its first start**, per account (3,497,984 -> 3,503,672 bytes, new hash),
  and its code is encrypted on disk until the wrapper unpacks it. So the file's hash is not the build's identity, and
  a static analysis of the file is useless. The build is known by the SHA-256 of the executable sections **in memory**
  once the game window exists (`src/x86/main32.c`): `FC9E4843...52D7`, 2,685,679 bytes, image base 0x400000, the same on
  every launch tried.
- A test copy needs `steam_appid.txt` (`10180`) next to the exe, besides what `tools/run_test.ps1` sets (environment,
  the Steam registry pid): without it the wrapper asks Steam to relaunch the game and Steam starts the real install
  (the runner's guard closes it).
- The addresses in iw4x-sp's `symbols.hpp` do **not** fit this build (they point into the middle of functions): it is
  a different version of the exe. They are a guide to what the functions do, not a table.
- To read the code: put a file `mw2coop\dump_image` into the game folder before the start; the proxy writes
  `mw2coop\image32.bin` (the image from memory, 34,930,688 bytes) and deletes the flag. Load it in Ghidra as a raw
  binary (language `x86:LE:32:default`, base address 0x400000).

## Steps

1. Done: the 32-bit proxy loads into the real game, logs, recognises the build, dumps the image.
2. Find the addresses by function, in this order, each checked by the bytes at the address (as the 64-bit hooks do):
   `Scr_AddSourceBuffer` (script patches: with it the campaign scripts of the mod already run, in 32-bit);
   `DB_FindXAssetHeader` and `DB_IsXAssetDefault`; the level zone loader and `DB_LoadXAssets`; the command and dvar
   functions (`Cmd_AddCommand`, `Cbuf_AddText`, `Dvar_FindVar`); `Scr_Error` and `Scr_EndLoadScripts` (script error log);
   then the party and menu structures (the lobby), the heaviest part (`menus.c`, `party.c`, `access.c`, `progress.c`).
3. A per-build profile in the DLL: the addresses and the sizes of the structures come from a table chosen by the
   build, instead of constants in `game.h`; the 64-bit build is one profile.
4. `hook.c` for x86: a 5-byte relative jump and trampolines (the 64-bit helper writes a 14-byte absolute jump).
5. The Steam parts (invite, "friends only", the relay) work with the official copy as in 64-bit (the game's own
   `steam_api.dll`); with a repack that carries a Steam emulator they switch themselves off (join by address).
6. QA on two 32-bit copies, the same scenarios as the 64-bit ones.

## Not to use

A copy of the game from an unknown source (a repack with a Steam emulator was looked at statically only: its
`steam_api.dll` is 1.29 MB, its exe is not Valve's). Not run, not reused.

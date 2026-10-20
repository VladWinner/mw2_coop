# mw2coop: campaign co-op for Modern Warfare 2 (2009)

Two-player co-op for the **Call of Duty: Modern Warfare 2 (2009) campaign**, on the current
**64-bit Steam build**, using the co-op netcode the game already ships for Spec Ops.

> **Alpha. Not playable yet.** Two copies of the game play the first mission (S.S.D.D.)
> together in co-op, with checkpoints, restart and saved progress, but the game does not move
> on to the next mission yet. This is for developers and testers. See [Status](#status).

*Кратко по-русски:* мод для прохождения кампании MW2 (2009) вдвоём на актуальной 64-битной
Steam-версии. Это альфа: первая миссия (S.S.D.D.) уже идёт вдвоём, с чекпоинтами, рестартом
и сохранением прогресса, но перехода на следующую миссию пока нет.

## Status

Verified with two copies of the game on one PC (host + client), last on 2026-10-20, by the
QA scenarios in [Testing](#testing); every run below ended with no script errors on either
machine:

Works:
- `winmm.dll` loads into `iw4sp.exe` and refuses any build it was not made for.
- Console commands from `mw2coop\commands.txt`; game scripts patched at load time from
  `mw2coop\patches`; GSC runtime errors written to `mw2coop\mw2coop.log`.
- The two copies connect by direct IP through the game's own co-op handshake and load a
  campaign mission (S.S.D.D.) together: both report two players in the level, and the
  changed scripts compile byte-identical on both machines.
- Campaign mode in co-op (not Spec Ops), player 2 placed with player 1, matching weapons
  and first-person hands (S.S.D.D. intro).
- Checkpoints: when both players are down (the game's revive system) or one bleeds out, the
  level restarts at the last checkpoint on both machines.
- Mission end is detected and the next mission found.
- Saved progress: the host keeps the last mission and checkpoint in
  `mw2coop\progress.txt`; `mw2coop_continue` starts there next time, and the client is sent
  the same start point automatically.

Not working or not done:
- **Moving on to the next mission**: the host saves it as progress, but the map does not
  change yet. This is the next fix.
- The game's own campaign records (missions unlocked in the menu) are not updated in co-op.
- Only S.S.D.D. has level fixes. Other missions, their in-engine cutscenes for player 2 and
  vehicle sections for two players are not done.
- Joining a friend through a Steam invite (two Steam accounts) is untested.
- No menu: hosting and joining use console commands (below).

## Requirements

- Windows 10/11, x64.
- Call of Duty: Modern Warfare 2 (2009) on Steam (app 10180), **the 64-bit build** from the
  September 2026 update: `iw4sp.exe` PE timestamp `0x6aaaa257`, Steam build 25374375
  (`version` dvar: `IW4 2.0 build 14 Fri Oct 03 16:56:21 2025 win64`). The DLL checks this
  and stays inactive on any other build.
- Each player owns the game. Both players use **the same mw2coop files**: co-op runs in
  lockstep (both PCs run the level scripts), so different scripts desync.

## Install (testers)

Use a **copy** of the game folder, not your Steam install:

1. Copy the game folder somewhere else, e.g. `D:\coop-test\mw2-64`.
2. Put `winmm.dll` and the `mw2coop` folder (`scripts`, `patches`) from the release next to
   `iw4sp.exe` in the copy.
3. Start Steam, then start the copy with the environment variables `SteamAppId=10180` and
   `SteamGameId=10180` set. Without them the Steam DRM wrapper exits and Steam starts your
   **real** install instead. `tools/run_test.ps1` does this and kills any `iw4sp.exe`
   outside your test folder.
4. Logs: `mw2coop\mw2coop.log` in the game copy.

## Hosting and joining (alpha, console)

Commands are read from `mw2coop\commands.txt` (one per line, appended lines run as they
arrive). The game must show its online connection first (about 10 s after start).

Host:
```
set coop_mapName trainer
listen
coopLaunch
```
`coopLaunch` can be given early: the game starts the mission once the partner has joined.

To carry on where you stopped, the host replaces `set coop_mapName ...` with
`mw2coop_continue` (mission and checkpoint from `mw2coop\progress.txt`). The client does
nothing extra: the host sends it the start point with `coopLaunch`. To pick a checkpoint by
hand, the host can `set start <start point>` before `coopLaunch`.

Client (`8305` is port 28960 byte-swapped; the game's `connect` expects it that way):
```
mw2coop_party_join
connect <host IP> 8305
```
Over the internet the host needs UDP 28960 reachable. `mw2coop_party_join` stands in for
the Steam-lobby step of a normal invite; with two Steam accounts a normal Spec Ops invite
may work instead (untested).

## How it works

- Saves: the game would write one save per PC, and loading it would desync the lockstep
  machines, so autosaves are off and progress is a level start point (`start` dvar) plus the
  host's `progress.txt`.
- `winmm.dll` is a proxy: all 180 `winmm` exports go to the system DLL. `iw4sp.exe` imports
  `winmm.dll`, so Windows loads the proxy from the game folder.
- Addresses come from reverse engineering the 64-bit build (Ghidra) and are listed with
  what they are in `src/game.h`, `src/scripts.c`, `src/scrdebug.c`.
- `Scr_AddSourceBuffer` is replaced: `mw2coop\scripts\<file>` adds or replaces a script,
  `mw2coop\patches\<file>.patch` edits the game's own script text when it loads. The mod
  never ships whole game scripts. Patch format: see `src/scripts.c`.
- `content/scripts/maps/_mw2coop.gsc` is the co-op campaign framework: campaign mode in a
  co-op session, keeping player 2 with player 1, weapon sync, checkpoints as level start
  points, restart when both players are down (the game's own revive system), and the end
  of a mission. Scripts ask the host's DLL for actions (restart, save progress) through the
  dvar `mw2coop_cmd`.
- In a co-op session GSC `getdvar` only returns dvars that have certain flags (the engine
  keeps both lockstep machines equal); the DLL flags `start` and the QA dvars so scripts can
  read them.

## Build

Needs [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and a POSIX shell (Git Bash):
```
LLVM_MINGW=/path/to/llvm-mingw-ucrt-x86_64 ./build.sh release
```
`tools/gen_winmm_proxy.py` regenerates the export stubs from the system `winmm.dll`.

## Testing

`tools/qa_pair.sh <scenario>` starts a host and a client copy on one PC, connects them and
runs an in-game check scenario from `content/scripts/maps/_mw2coop_qa.gsc` (`intro`,
`restart`, `end`, `resume`), then prints both logs. `tools/qa_pair.sh resume @continue`
starts the host with `mw2coop_continue` and the client with no start point. Script errors
are logged with the GSC call stack (`file::function +offset`). One Steam account is enough: the QA-only command
`!xuid_add` gives the second copy a different player id. `tools/check_patches.py` checks
the patches against extracted game scripts without starting the game.

## Legal

Not affiliated with or endorsed by Activision or Infinity Ward. Call of Duty and Modern
Warfare are trademarks of Activision. You need your own copy of the game; no game files are
included. mw2coop's own code is MIT licensed (see `LICENSE`). Patch files quote short lines
of the game's scripts only to say where a change goes; those lines belong to their owners
and are not covered by the MIT license. mw2coop uses the game's co-op mode between players
who agree to play together; it is not for multiplayer cheating.

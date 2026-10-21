# mw2coop: campaign co-op for Modern Warfare 2 (2009)

Two-player co-op for the **Call of Duty: Modern Warfare 2 (2009) campaign**, on the current
**64-bit Steam build**, using the co-op netcode the game already ships for Spec Ops.

> **Alpha, work in progress.** Most missions start together and S.S.D.D., Team Player,
> Wolverines!, Takedown's car ride, The Only Easy Day's swim, Cliffhanger's climb and
> snowmobiles, No Russian's elevator, cover and ending, the crash site and ISS of Second Sun,
> Endgame's boat chase and the final knife fight are reworked for two players; enemies fight
> both players. Other set pieces are still written for one player. See [Status](#status).
> Download: [Releases](https://github.com/stxlvn/mw2_coop/releases) (`mw2coop-<version>.zip`,
> see [Install](#install)).

*Кратко по-русски:* мод для прохождения кампании MW2 (2009) вдвоём на актуальной 64-битной
Steam-версии. Альфа: вдвоём переделаны S.S.D.D. (каждый берёт своё оружие), Team Player,
Wolverines!, поездка в машине в Takedown, заплыв и бесшумное убийство в The Only Easy Day,
подъём по льду и снегоходы в Cliffhanger, лифт, прикрытие и концовка No Russian, крушение и МКС
в Second Sun, лодка в Endgame (второй игрок лежит на носу напротив Прайса и стреляет) и
финальный бой на ножах. Враги воюют с обоими игроками. Остальные сцены ещё рассчитаны на
одного. Установка: распакуйте архив из
[Releases](https://github.com/stxlvn/mw2_coop/releases) в папку игры; хост: Кампания ->
СЕТЕВАЯ ИГРА ВДВОЕМ, второй игрок: ПРИСОЕДИНИТЬСЯ К ИГРЕ или приглашение Steam (подробно в
`INSTALL.txt` внутри архива).

## Status

Verified with two copies of the game on one PC (host + client), last on 2026-10-21, by the
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
- Mission end: both machines move on to the next mission together (S.S.D.D. → Team
  Player/`roadkill` checked).
- Test god mode: players take no damage, so nobody is downed. On by default from the
  console (QA); the release `.bat` files set `mw2coop_god 0` (edit `GOD=1` to turn it on).
- Saved progress: the host keeps the last mission and checkpoint in
  `mw2coop\progress.txt`; `mw2coop_continue` starts there next time, and the client is sent
  the same start point automatically.
- Team Player (`roadkill`): both players are in Shepherd's opening scene; at the convoy,
  whoever reaches the glowing seat first is the gunner, the other boards the same door onto a
  seat and the convoy waits for him (30 s, then he is seated); **the passenger holds Use for a
  second to swap places with the gunner**; at the end of the ride and in the crash both get out
  together. Start points `ride`, `ambush` and `ride_later` seat player 2 too.
- Wolverines! (`invasion`): player 2 rides in the opening Humvee (on Worm's rear seat; Worm is
  left out in co-op) and gets into the Humvee at the end with player 1.
- Takedown (`favela`): both ride the opening car (player 2 on the back seat), each ducks on his
  own crouch key; Makarov's shots kill whoever is not ducking (the mission fails, as in single
  player); both get out through the open door.
- The Only Easy Day... (`oilrig`): both ride an SDV underwater (player 2 on the second one),
  both surface by the grate; whoever strikes first does the stealth kill, then the SEALs pull
  both out of the water.
- No Russian (`airport`): player 2 has his own place in the elevator; at the end Makarov
  shoots the player at the van (or the nearer one), then turns and shoots the other.
- Of Their Own Accord (end) and Second Sun (start): both wake up in the crashed Black Hawk,
  each with his own rifle; at the ISS both float side by side; both are let out of the wreck.
  In the street a falling helicopter chases each player; the level goes on once both hide.
- Endgame's ending (knife fight with Shepherd): both wake up after the crash side by side;
  whoever strikes Shepherd first plays the scene (stabbed, the monologue, the crawl to the
  revolver, pulling out the knife, the throw). The other is stunned: his screen goes black
  ("You are stunned. Wait for the other player", in the game's language), he comes to when
  the knife is out and sees the throw, Shepherd's death and Price waking up, from beside the
  fight and never facing the other player (the scene has first-person arms only).
- Cliffhanger: both players climb the ice wall side by side, each with his own icepicks (left
  pick: Attack, right pick: Aim), make the big jump, and Soap catches each of them in turn (the
  second one hangs on until Soap is free); the level goes on once both are at the top. Each
  player gets his own snowmobile for the escape; the helicopter waits for both. Tested with a
  bot playing both players (QA `climb`, `snow`, `snowpick`); real mouse input on the climb is
  untested.
- S.S.D.D.: both start inside the range's sandbag bay (player 1 on the left); each player takes
  his own rifle and pistol off the table (another lies there until both have one) and his own
  frags from the box; the drills go on once both have them.
- Endgame's boat chase: whoever mounts the boat first drives; the other lies at the bow across
  from Price (Price keeps his side), on a body with Price's boat animations, and shoots where he
  looks within the pose's arc.
- Enemies fight both players: the engine already picks either player (QA `targets`: 151 and 129
  of 918 enemy samples on players 1 and 2); enemies a level sends at "the player" (player
  seekers, a favourite enemy, attack triggers) go for the closest player still up; scenes that
  hide the player from enemies, and a level's accuracy and threat settings for the player,
  cover both. No Russian: straying from the squad and grenades count for either player; when
  the cover is blown Makarov shoots both and the mission fails.
- Loose Ends: both players wear the TF141 forest uniform (the level's leftover co-op loadout
  asked for ghillie suits no zone has: red placeholder bodies). Just Like Old Times loads (it
  overflowed the 1350 `loaded_sound` limit with the co-op body zone; the pool is raised to 2000).
- In-game co-op: Campaign -> co-op host lobby (mission select, access: open / Steam friends /
  password), join from the list of games on the network, by address or by Steam invite; the
  host gets an in-game Windows Firewall popup and UPnP port forwarding.
- Last spawn check (2026-10-21, QA `spawn` with weapon and threat checks): S.S.D.D., Team
  Player, Cliffhanger, No Russian, Takedown, Wolverines! pass with no script errors; the other
  levels' runs with this build were not finished.

Not working or not done:
- No on-screen hint for Team Player's seat swap yet.
- Global screen effects show on both screens whoever triggers them: Cliffhanger's climbing
  field of view, depth of field and the slow motion of Soap's catch, The Only Easy Day's water
  vision during the stealth kill.
- No Russian's far ending (nobody at the van door) logs one script error after the second
  shot (`_anim::anim_loop_packet`); the scene plays on.
- Player 2 is only brought along when player 1 jumps far in one step; a slow scripted move can
  still leave him behind.
- The game's own campaign records (missions unlocked in the menu) are not updated in co-op.
- Level fixes so far: S.S.D.D., Team Player, Wolverines!, Takedown (opening), The Only Easy
  Day... (opening), Cliffhanger, No Russian (elevator, ending), Of Their Own Accord (crash),
  Second Sun (opening, street), The Gulag and Takedown seats. Other missions start with
  player 2 next to player 1 but their scripted scenes are written for one player.
- Joining a friend through a Steam invite (two Steam accounts) is untested.

## Requirements

- Windows 10/11, x64.
- Call of Duty: Modern Warfare 2 (2009) on Steam (app 10180), **the 64-bit build** from the
  September 2026 update: `iw4sp.exe` PE timestamp `0x6aaaa257`, Steam build 25374375
  (`version` dvar: `IW4 2.0 build 14 Fri Oct 03 16:56:21 2025 win64`). The DLL checks this
  and stays inactive on any other build.
- Each player owns the game. Both players use **the same mw2coop files**: co-op runs in
  lockstep (both PCs run the level scripts), so different scripts desync.

## Install

For players, from a release archive (`mw2coop-<version>.zip`); `INSTALL.txt` inside says the
same in Russian and English:

1. Steam: Library -> Modern Warfare 2 -> Manage -> Browse local files (the folder with
   `iw4sp.exe`).
2. Copy everything from the archive there: `winmm.dll` and the `mw2coop` folder. No game file
   is replaced. Both players need the same archive.
3. Host: Campaign -> co-op (two players). The lobby opens on the mission you stopped at
   (progress is kept in the host's `mw2coop\progress.txt`); mission select and access (open,
   Steam friends only, password) are in the lobby.
4. Partner: accept the host's Steam invite, or Campaign -> join a game and pick it from the
   list (same LAN or VPN) or type the host's address. Both press Ready, the host Start.

Over the internet the host's router opens UDP 28960 by itself if UPnP is on; else forward the
port or use a VPN. The port is set in `mw2coop\settings.ini` (the same on both PCs).
Uninstall: delete `winmm.dll` and the `mw2coop` folder.

For testers working on the mod: use a **copy** of the game folder, not your Steam install,
and start it with the environment variables `SteamAppId=10180` and `SteamGameId=10180` (without
them the Steam DRM wrapper exits and Steam starts your **real** install instead);
`tools/run_test.ps1` does this and kills any `iw4sp.exe` outside the test folder. Logs:
`mw2coop\mw2coop.log`.

## Hosting and joining by hand (console)

For testing, these lines go into `mw2coop\commands.txt`, which the mod runs in order
(appended lines run as they arrive; `!sleep <ms>` waits). The game must show its online
connection first (about 10 s after start).

Host:
```
set coop_mapName trainer
listen
coopLaunch
```
`coopLaunch` can be given early: the game starts the mission once the partner has joined.
To carry on where you stopped, the host replaces `set coop_mapName ...` with
`mw2coop_continue`. The client does nothing extra: the host sends it the start point with
`coopLaunch`. To pick a checkpoint by hand, the host can `set start <start point>` before
`coopLaunch`; `set mw2coop_god 0` turns the test god mode off (the client follows the host).

Client (`8305` is port 28960 byte-swapped; the game's `connect` expects it that way):
```
mw2coop_party_join
connect <host IP> 8305
```
`mw2coop_party_join` stands in for the Steam-lobby step of a normal invite; with two Steam
accounts a normal Spec Ops invite may work instead (untested).

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
  read them, and every dvar a level script sets itself (equal on both machines), so levels
  that read back their own dvars behave as in single player.
- Diagnostics in `mw2coop.log`: GSC errors with the script call stack, crashes with the
  faulting RVA and stack, and `!trace <rva>` logs every call of a builtin method with stacks.

## Build

Needs [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and a POSIX shell (Git Bash):
```
LLVM_MINGW=/path/to/llvm-mingw-ucrt-x86_64 ./build.sh release
```
`tools/make_release.sh <version>` builds the release DLL (checked for local paths) and packs
`dist/mw2coop-<version>.zip` with the scripts, patches, `content/release` (`INSTALL.txt`,
`settings.ini`, the firewall helper), plus its SHA-256.
`tools/gen_winmm_proxy.py` regenerates the export stubs from the system `winmm.dll`.

## Testing

`tools/qa_pair.sh <scenario>` starts a host and a client copy on one PC, connects them,
runs an in-game check scenario on both (`content/scripts/maps/_mw2coop_qa.gsc`; Cliffhanger's
climb in `_mw2coop_climb_qa.gsc`), prints both logs and closes the copies once the scenario
is done. Usage: `tools/qa_pair.sh <scenario> [start point] [seconds] [map]`. Scenarios:
S.S.D.D. `intro`, `restart`, `end`, `resume` (`@continue`), `god`; Team Player `rkintro`,
`board`, `rideend`, `turret`; Wolverines! `ride`; Takedown `takedown`, `takedown2`; The Only
Easy Day `oilrig`, `oilrig2`; Cliffhanger `climb`, `snow`, `snowpick`; No Russian `nrend`,
`nrend2` (start `escape`); crash site `crashsite` (dcburning, start `crash`), Second Sun
`dcemp`, `street` (start `street`); No Russian `cover` (start `stairs`); S.S.D.D. `pickups`
(start "", `frag`, `pit`); Endgame `boat`, `boat2` (af_chase), `knife`, `knife2` (ending, start
`wakeup`); any map `spawn`, `leash`, `look` (players face each other for screenshots),
`targets` (whom the enemies fight), `buttons` (real Forward and Use on each player's machine).
A scenario can press a player's buttons for real: `press( player, "activate", ms )` makes that
player's own machine run the key binding (`mw2coop_bind +activate`, see `src/control.c`), so
the button reaches the server in his input like a key press (`pickups` takes the weapons this
way and walks up to the frag box). Older scenarios still move the players through script
(button holds, velocity pushes), so mouse and keyboard input on those set pieces is checked by
hand.
Script errors are logged with the GSC call stack (`file::function +offset`). One Steam account
is enough: the QA-only command `!xuid_add` gives the second copy a different player id.
`tools/check_patches.py` checks the patches against extracted game scripts without starting
the game; `tools/gen_*_patches.py` generate the larger patches from the extracted scripts.

## Legal

Not affiliated with or endorsed by Activision or Infinity Ward. Call of Duty and Modern
Warfare are trademarks of Activision. You need your own copy of the game; no game files are
included. mw2coop's own code is MIT licensed (see `LICENSE`). Patch files quote short lines
of the game's scripts only to say where a change goes; those lines belong to their owners
and are not covered by the MIT license. mw2coop uses the game's co-op mode between players
who agree to play together; it is not for multiplayer cheating.

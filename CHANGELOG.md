# Changelog

## Unreleased

- **Russian game: small text is smooth** (font offer from the community). The mod's font atlas had one mip level and the game's sampler for it has the mip filter off (`r_texFilterMipMode` never reached it), so text drawn smaller than the 4x raster was only bilinear-sampled: sharp and jagged. The atlas now has three levels (4x, 2x, 1x) and its sampler uses them (trilinear). `fontmip=0` under `[display]` in `settings.ini` gives the old look. Built on top of the finer glyph grid (PR #8): the packer leaves a transparent 4-pixel margin around each glyph so the 1x level never mixes neighbours. Checked by a frame of the Russian lobby in the 64-bit build; the 32-bit build compiles the same code and was not run.
- **Split screen: the freezes, crashes and "failed memory allocation for script usage" on Roadkill (all builds) are fixed, and the actors' animations no longer reset.** Every client frame the engine saves the animation state of every DObj it frees (about 540 of them, each holding a reference to the string of its notify name) into a global queue; the start of the entity cleanup function applies the queue (restoring the states and giving the strings back) and its caller empties it. Context 1 skipped the whole function, so the empty that follows it in its frame threw away what context 0 had saved: the animation states of all DObjs were lost every other pass (the animation "drawdown" and moonwalking of the AI), and the strings' references were never given back. A string's count is 16 bits: after some 35 to 45 s in a busy level (Roadkill) it wrapped, the string was freed while scripts still held it, and its next release froze the game (a hash chain walk that never ends) or corrupted the script memory (script values of the wrong type, a saved stack of a negative size, "failed memory allocation for script usage"). Context 1 now runs the apply too, so every saved block is applied once (`[native] xferfix=0` turns it off). Found with the script call stacks, a guard on the engine's string release and an event log of the queue (a file `xferlog` in the mw2coop folder). Before it the Roadkill split screen failed in 3 of 4 runs within a minute in every build; after it the 64-bit build (3 of 3) and the iw4x-sp client (4 of 4) ran 95 s and more without a freeze, a script error or a bad release. The 32-bit builds crashed in 3 of 7 runs at a level restart after that (not looked at yet).
- **Script errors in the 32-bit builds no longer flood the log, and the iw4x-sp client logs them with the script call stack.** A script that kept failing wrote one entry with a stack for every hit (522,000 of them, 125 MB, in two minutes on Roadkill in the official 32-bit build). As in the 64-bit build, each error at each place is now logged three times, then at the 10th, 100th, 1000th... hit. The iw4x-sp profile had no script stacks because its script VM addresses were not mapped; they are now (found with tools/map159.py), and the freeze and crash logs of every build name the script function the VM was in. Diagnostics for the split screen's Roadkill freeze and crash (open): `[native] scrwatch=1`, and QA switch files `mw2coop\nomirror`, `norumble`, `nosetdvar`.
- **Split screen: the characters around you no longer stutter.** The jerk of every AI actor (the team, the enemies, the civilians), seen in both halves since the split screen began, is fixed. Cause: in single player the game's server and its client share ONE array of the snapshot's entity states, and the server overwrites it at every snapshot build (20 times a second); the client copies the states of a snapshot out of it when it reads that snapshot, and in the split screen that read comes late, after the next build, so it got the newer snapshot's states: one step of the actor's walk was skipped and the next one read twice (a freeze, then a jump). The mod now keeps a copy of that array per build, tagged with the snapshot's time, and the client reads the copy of the snapshot it is reading (`[native] entring=0` turns it off). Measured on No Russian with the actors' per-frame positions: frames in which a walking actor does not move fall from 20-35% to 0-1% in both halves; the server's own movement was regular all along. The same fix is in the 32-bit and the iw4x-sp builds (in the split screen about half of the snapshot reads were late in all three, none in the plain game).
- **Split screen: the AI no longer holds back for player 2's body** (QA finding, not changed in the game): with player 2 ignored by the AI the server's own per-step stalls of an actor fall from 12% to 4%, so part of the unevenness in the split screen comes from the AI reacting to the second player (blocking, waiting). That is how the game's AI behaves with two players and was left alone.
- **Renamed to Modern Warfare 2 Enhanced.** The GitHub repository is now `stxlvn/mw2_enhanced` (the old address redirects). The `mw2coop` folder, the `winmm.dll` proxy, the settings, the dvars and the archive names stay as they are, so installs and saves keep working.
- **Split screen: the names over the players are Alpha and Bravo.** Both halves showed the profile's nickname (player 2's with " (2)" after it). `[native] p1name` and `p2name` in `settings.ini` set other names.
- **Russian game: the Favela Escape objective no longer names Price** (issue #6). The stock text told the player to help Captain Price reach the landing zone before Price has appeared in the story; it now says Captain MacTavish, as in the h2-mod translation. Only the Russian text is replaced.
- **Split screen: the sun's fade now really stays in its own half, in all three builds.** The renderer writes that state while it draws
  the previous frame, after the game had already switched to the other player's view, so each half held the other view's
  value (the 32-bit and iw4x builds did not even have the per-view state in v0.5.7.5-beta, whatever its note said). The swap now waits for the renderer first (`[native] sunwait=0` turns it off;
  `sunsplit=0` turns the per-view state off). Checked on Boneyard with a turning player 1: his half's value follows his view, player 2's stays his own (64-bit, 32-bit and iw4x-sp). The wait costs about 1 ms per frame in the 32-bit build (146 instead of 169 draws a second in a QA run), nothing measurable in 64-bit.
- Diagnostics only (off by default): `sunlog`, `npclog`, `npcwait`, `npcsnap`, `snaplog`; QA scenario `sunlook`.

## v0.5.7.5-beta (2026-11-02, pre-release: extra fixes of v0.5.7-beta)

- **Split screen: starting it a second time in one game run no longer crashes.** After "quit to the main menu" and a new
  mission the second player's view kept pointers into the previous level; its state is now reset for every session.
- **Split screen: the pause menu (and the objectives) now cover the whole picture.** The game's clock stands still in a
  pause, so the menu frame matched the notes of the last split frames and was put into whichever half was noted last:
  half the time into the bottom half, as if it were "player 2's" pause menu.
- **Split screen: the sun's white-out and dark blind no longer leak between the halves.** The renderer keeps that fade
  per local client and both of our views are local client 0, so a player who looked into the sun brightened or darkened
  the other player's half too ("overexposure in one half, a mask in the other"). Each view now has its own state
  (all three builds). `[native] sunsplit=0` turns the change off.
- Camera watch (log line `CAMERA MISMATCH`) now really runs: the script that feeds it passed a word where a number was
  expected, so it never produced anything in v0.5.7.
- No Russian: player 2 can walk out of the elevator. His place beside player 1 was against the wall next to the doors
  (the doors let a player through only within about 22 units of the middle line): walking forward ran into the wall,
  his half showed the wall and its call panel from a hand's width, and player 1 saw his body still standing in the
  elevator. He now faces the middle of the doors from his place and is let go facing them. (The camera itself was never
  wrong: it matched the body in every test.)
- **Split screen: the other player's body now follows him.** The code that turns the body to face its player's view saved
  the body's origin before each draw and put it back afterwards, undoing the position the game had just worked out from
  the snapshot: each player's body stayed where it stood when the split began (player 1 saw player 2 still in the airport
  elevator after he had walked out; in S.S.D.D. the body "tried to move and could not"). Only the offset it adds is taken
  off now. `[native] posfix=0` goes back to the old behaviour.
- The camera watch ignores a view that is moving (its body position is up to half a second old: a false "57 apart" while
  player 2 walked out of the elevator).
- Test tools: `tools/qa_restart.sh` (second start), `tools/qa_pause.sh` (pause menu, fake pad, scenarios).

## v0.5.7-beta (2026-11-01, pre-release: extra fixes and a developer console)

- **Developer console (new, all three builds)**: the key left of 1 (` or ~) opens a console panel over the top of the
  game picture; the game does not turn or react to keys while it is open. The retail game has no console code, so this
  is the mod's own: any game command runs from it (all of the game's commands work: `map`, `devmap`, `god`, `noclip`,
  `give`, `set`, `toggle`, ...), a dvar's name shows its value and default, `name value` sets it, and the original
  console's lists are back: `cmdlist` / `commands`, `dvarlist`, `dvardump` (a filter is part of a name, or with `*` and
  `?`), `find`, `echo`, `condump`, `clear`, `help`. Tab completes command and dvar names, Up / Down recall lines, PgUp /
  PgDn scroll. The game's own replies (messages of its commands) are not shown: this build no longer prints them.
- **Split screen: the menu button starts it properly.** Campaign → Split screen opens the host popup straight away
  (it used to switch the game to a screen sized window first, a step the test tools skipped, so it was never checked).
  The old two process split screen (a hidden second game process) is removed; the native one is the only one.
- Test tools: the lobby test now goes through the real button command; new QA commands for typing into the console.
- **Known and not fixed in this version:** in the split screen on No Russian (airport) the second player's camera can end up away from his body after the elevator (bottom half shows a wall or the flower beds while he stands elsewhere); the characters around can look jittery in both halves.

## v0.5.6-beta (2026-10-30, pre-release: extra fixes of v0.5.5-beta)

Fixes of the split screen found after v0.5.5-beta; nothing else changes. Same state as 0.5.5: tested with bots and
simulated gamepads, not yet by two people on real gamepads.

- **Split screen HUD as on the console**: ammo and grenades, the compass and stance, the use and mantle hints and the
  low ammo warning sit where the Xbox 360 split screen puts them, in both halves.
- **Scope**: player 1's scope no longer blacks out the bottom half.
- **Sound**: each player now has his own sound listener, so the hearing no longer jumps between the two every frame.
- **Muzzle flash**: a player who fired no longer has an endless flash on his gun in the other half; it shows once
  per shot.
- **Shadows**: each half keeps its own view for the shadows, so the shadow of the camo net over the trainer's grenade
  box no longer follows where the other player looks.
- **Smoother picture**: the picture is shown once per pair of views instead of once per view (with vsync the game
  showed half as many frames per half as it drew), and the QA frames are not written for players.
- **32-bit game**: the split screen no longer crashes at its start (a missing brace after the muzzle flash change of
  the same night; the 64-bit game survived it by luck).
- Checkpoint restarts and the host's actions work in a split session; the test tools got a scenario for the grenade
  box and a check that stops a harness left over from an earlier run.

## v0.5.5-beta (2026-10-29, pre-release of 0.6.0)

A pre-release for people who want to try the split screen and tell us what breaks; the final 0.6.0 follows after more
testing. Everything here was tested with bots on game copies on one PC (and the split screen with frames of both
halves and simulated gamepads); none of it has been played through by two people yet.

### New: native split screen (one game window, one process)

The **Split screen** button of the campaign menu no longer starts a second game; both players are in the one game, the
picture cut top and bottom like the Xbox 360 version: each half has its own view built for its shape (field of view of
the console's split screen), its own weapon, ammo and compass, and text that keeps its proportions. The mission starts
as a normal level in this process, the session ends when you are back in the menus. Pause, the objectives, the
briefing and the console show one full-size picture. Texts that belong to both are shown once: the notices at the top
left (a new objective) in the top half, the subtitles and the mission title lines in the bottom half.

- **Lobby:** a card for each player (call signs Alpha and Bravo, no nicknames); each presses a button on the device he
  will play on (keyboard and mouse, or a gamepad), a choice can be taken back, Start waits for both.
- **Controls:** either player on a gamepad (console layout, XInput) or the keyboard; the second player cannot work the
  game's menus, only pause. Gamepad extras as on the console: aim assist (the turn slows over a target, lock-on while
  moving, the aim-down-the-sight snap), vibration, and the pad's buttons instead of key names in hints.
  Options -> Controls has pages "Gamepad: Alpha" and "Gamepad: Bravo" (look sensitivity, inverted look, aim assist,
  swapped sticks, tactical layout, vibration).
- **Picture:** scopes and red dot sights in both halves; each player sees the other's body, holding the weapon he holds
  and walking where he walks, with his name over it; each half has its own effects (the muzzle flash and smoke of one
  player no longer fill the other's half with a white haze); no dark square around the compass; far NPCs no longer
  jump in the bottom half; no "waiting for the other player" text while a level loads.
- **Builds:** the 64-bit game, the older 32-bit game (archive `-x86`) and, experimental, the iw4x-sp client
  (1.0.159 `iw4sp.exe`, archive `-iw4x`).

How: the game's own two-player machinery runs with two client states swapped per frame; the scene's 2D is drawn into
a layer per half and laid over the squeezed view; the game's 2D placement is made for a half. Details and the research:
`docs/native-splitscreen-plan.md`. Settings under `[native]` in `settings.ini` are for testing (QA only).
Known gaps: the gamepads were tested with simulated gamepads only; one sound listener, no second keyboard (the keyboard
goes to one player); the 32-bit split screen was checked on the Trainer and Cliffhanger only, the iw4x-sp one on the Trainer only.

### Loading screen and briefing of the campaign co-op

The campaign co-op and the split screen now load like single player: the mission's briefing film over the whole picture, the bar at the bottom in the middle. Before, they borrowed Spec Ops'
loading screen (another version of the film with a black panel and the studio logo, the bar at the left, "waiting for the other player"): the Spec Ops lobby they use sets the game's
`specialops` dvar, and the loading screen follows it. The dvar is 0 from the launch to the end of the level; Spec Ops is untouched.

In the online co-op the level used to start the moment both PCs had loaded and cut the briefing off. It now waits for the film, as in single player (the level is paused meanwhile, the film
plays on both PCs); when one player skips the briefing or the film ends on his PC, the other PC lets go at once. (64-bit, 32-bit and iw4x-sp; the split screen has the single player wait already.)
QA runs do not wait (`mw2coop_qa` set; `mw2coop_qa_brief 1` to test the wait).

### Fixed: the Museum, missions in the lobby, a hang

- **Museum for two.** The credits camera carries both players and each sees the museum, not the inside of the other's head (both are hidden from each other during the credits). The museum's civilians play their idles again: the game's animation tables for them were incomplete in the credits mode and the vanilla game raised ten million script errors there (and left civilians in a T-pose), in single player too.
- **All missions can be picked in the co-op lobby** (issue #3): the host sees every mission of the campaign list as open, whatever his save says. Nothing is written to the profile, and his own Mission Select keeps its rules.
- **A rare hang of both PCs after a mission start** (seen once in the 32-bit build): the game's asset database lookup `DB_IsXAssetDefault` did not give back the reader count it took when a name was missing, so the next writer waited for ever. Fixed in both builds.
- **No Russian**: the far ending no longer raises a script error (Makarov's second shot killed the van mate and his idle loop failed; he gets a bullet shield during the shots).
- QA: the checkpoint bots (`cptour`) now run on every campaign mission in 64-bit and 32-bit builds and count the level's own checkpoint remaps.

### Changed: the fonts of the Russian game are the H\*-Mod fonts

The font replacement of v0.5.0-beta now uses the **H\*-Mod fonts** (made by Vlad Loktionov for h2-mod): a Bank Gothic
like face for the headings and the HUD, a Conduit like one for descriptions and subtitles, a bold one for the mission
intro text and subheadings, with smaller glyphs (SIL OFL, shipped in `mw2coop\fonts`), and his fixes of the font
metrics and of the atlas. Works in the 64-bit and the 32-bit build. `fonts=0` under `[display]` in `settings.ini` keeps
the game's own font; the English game is never touched.

### The 32-bit game and the iw4x-sp client (archives `-x86` and `-iw4x`)

`mw2coop-v0.5.5-beta-x86.zip` is the build for the older 32-bit MW2, `mw2coop-v0.5.5-beta-iw4x.zip` the one for the
iw4x-sp client (its 1.0.159 `iw4sp.exe`). Both have the online co-op and, new, everything of the split screen above. The 32-bit build is no longer
experimental; the iw4x-sp one still is.
The 32-bit build also has the Steam relay, the screenshot helper and the script error call stacks (in the iw4x-sp
build the call stacks are off and the Steam relay is not checked). Checked with bots: online co-op on both (iw4x-sp:
a pair on the Trainer), the split screen on the Trainer and Cliffhanger (iw4x-sp: the Trainer).

## v0.5.0-beta (2026-10-25)

Everything here was tested with bots on two game copies on one PC (host + client); none of it has been played through
by two people yet.

### New: fonts closer to the English original (Russian game)

The Russian game draws every text in one generic condensed sans, while the English original uses Bank Gothic (headings,
HUD numbers), ITC Conduit (running text) and T26 Carbon (mission intro screens), none of which has Cyrillic. The mod now
renders the game's own font tables again from free fonts with Cyrillic and puts them into the game's memory: **Tektur**
for the headings, the HUD and the mission intro text, **Fira Sans Condensed** for descriptions and subtitles (SIL OFL,
shipped in `mw2coop\fonts`). Works in the 64-bit and the 32-bit build. `fonts=0` under `[display]` in `settings.ini`
keeps the game's own font; the English game is never touched.

### New: scenes that only the first player had are now for both

These scenes were scripted for one player; the other stood by or was left behind. Each now plays for two, with a
rig of his own where the animation needs one.

- **The Gulag, cellblock rope**: both rappel down, one after the other.
- **The Gulag, the evacuation**: both lie buried, each clips on the Pavelow's rope with his own use key (the
  rope's trigger stays on until both have), both are pulled away. A player who is too late is buried by the
  cave-in, as before.
- **Just Like Old Times, the opening rappel**: whoever hooks up first rappels and kills the guard below; the hook-up
  is free again and the other rappels the same way. The barracks checkpoint waits until both are down (it used to
  pull player 1 up to the top in the middle of his knife kill).
- **Escape from Favela, the big jump over the gap**: whoever leaps (or falls) first starts it; both play the
  jump (Soap reaches for both) and wake up on the ground side by side. Each rig starts 55 units from the other, so
  nobody begins inside a wall or inside the other.
- **Escape from Favela, the chopper's rope ladder**: whoever leaps for the ladder first starts the scene, the other
  catches the same rope. A partner far behind is brought to the rooftop slide, so you reach the ledge together.
  Both screens fade out at the end.
- **The Only Easy Day..., the escape**: both board the little bird, side by side on the bench; it takes off
  when both are on board.
- **Wolverines!, the Humvee at the end**: checked (it was already done): player 2 gets a seat of the parked Humvee.

### Cliffhanger and the other levels, from the playtest and after

- **Cliffhanger**: both players get the level's stealth visibility (player 2 was seen at any distance); the other
  player sees the snowmobile's full third-person model; the rider sits on his snowmobile in his own body (the
  co-op body's mesh on Soap's skeleton, so the AI driving animation fits), hands on the handlebar, upright; on the
  ice climb the partner sees the climber's body, not four arms, and the ice picks stand upright in both hands.
  A swing clicked during the last one counts (the mouse buttons sometimes needed two clicks); this one is a
  guess and easy to revert on its own.
- **S.S.D.D.**: the course's gates close once both players are through; either player's sprint counts.
- **Team Player**: either player can start the seat swap; the passenger starts looking ahead; nobody hurts the
  other during the ride.
- **Lobby**: the password mode has its own Password row, and Start is refused until a password is set.

### The 32-bit build (source only, not in the archive)

The code now also builds for the older 32-bit MW2 (`sh build.sh x86`, official Steam copy of the old depots): two copies
play a co-op session on trainer, joining by address, the script patches apply and give the same patched scripts as the
64-bit build, and a 64-bit host with a 32-bit client was checked on trainer. It is not in this release's archive:
the screenshot helper, Steam relay and some set pieces are not ported yet. See `docs/32bit-plan.md`.

### Fixes in the engine side

- The game leaks a reader count when a model name is unknown, and the next `playsound` then hangs; hooked.
- GSC `GetEntArray( flag, "script_flag" )` finds nothing in this build; the S.S.D.D. gates and the checkpoint leader
  now look at the trigger classes.

### Hardening and housekeeping (code audit, `docs/audit-2026-10-25.md`)

- The host's start packet and the lobby's "ready" packet are acted on only when they come from the partner's
  address; anyone else's is dropped (the first five are logged).
- The command file (`mw2coop\commands.txt`, the QA interface) is read only if it exists when the game starts.
- `mw2coop.log` past 5 MB is set aside as `mw2coop.log.old`; the recorder wrote about 1 MB an hour and nothing
  ever emptied it.
- The buffers of the models made for the game (the co-op rider, the ice pick, No Russian's crew) are freed when the
  level after next loads, instead of never.
- One helper (`src/hook.c`) for all the code patches; its trampoline pages are read-execute, not writable and
  executable.

### Development

- QA scenarios for each new scene, a generic scene watcher, and tools to turn the frames of both screens into
  contact sheets (`tools/shots2png.ps1`, `tools/sheet.ps1`, `tools/spoof_test.py`).
- The patch generators are one per level; `deploy.sh` stops when a patch does not apply.

### Checked by hand

Joining through a Steam invite between two different Steam accounts works (tested by the owner with a friend); it
is off the known issues.

### Not changed

Slow motion still shows on both screens; the Museum epilogue and No Russian's far ending: see the known issues of
the next release.

*Кратко по-русски:* Гулаг (верёвка в блоке и эвакуация к вертолёту), спуск в «Таком же, как в старые времена»,
большой прыжок и лестница вертолёта в «Побеге из фавел», посадка на вертолёт в «Единственном лёгком дне» теперь
проходятся вдвоём; исправлены найденные в игре баги Скалолаза, S.S.D.D. и «Командной игры»; пакеты старта и готовности
принимаются только от напарника; лог больше не растёт бесконечно.

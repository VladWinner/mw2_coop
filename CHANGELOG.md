# Changelog

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

### The 32-bit build (experimental archive)

The code now also builds for the older 32-bit MW2 (`sh build.sh x86`, the official Steam copy of the old depots), and
the release has a second, experimental archive for it: `mw2coop-v0.5.0-beta-x86.zip`. Two copies play a co-op session
on trainer (joining by address), the script patches apply and give the same patched scripts as the 64-bit build, and a
64-bit host with a 32-bit client was checked on trainer. Not ported or not checked: playing over the internet on the
32-bit game, Steam invites and the Steam relay, the screenshot helper; the whole campaign was not played there. See
`docs/32bit-plan.md`.

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

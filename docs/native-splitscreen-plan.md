# Native split screen (one process, two local players): plan

Decision of the owner (2026-10-26): the two-process split screen (`src/splitscreen.c`, `src/instance.c`) is dropped as the way forward; a split
screen inside the game itself is to be built. The two-process code stays in the repo as a fallback until the native one works; it is not polished
any more.

## What the Xbox 360 game does (reference: https://github.com/PaulCombal/mw2-recompiled, docs/gameplay.md and docs/rendering.md)

- One process, one `cg`/`cl` state per local client, up to 4 local players; two players: the screen is cut top and bottom.
- The campaign's scripts (`_load.gsc`) write `cg_fovScale` 0.75 in split screen and 1 otherwise; `cg_fov` is the angle across a 4:3 picture (65), the view is as tall
  as the 4:3 one and as wide as the picture's shape says.
- The 360 code is not available to us: the reference project builds from the player's own disc and carries no engine source.

## What the PC game (`iw4sp.exe`, 64-bit first) has left of it (found so far)

- The script builtin `IsSplitScreen()` is a stub that returns false (rva 0x194720). The local-client machinery is not there.
- UI/HUD leftovers by name: `con_gameMsgWindow%dSplitscreenScale`, `waypointSplitscreenScale`, `adsOverlay{Width,Height}Splitscreen`,
  `coopNameFontSizeSplitscreen`, `splitscreenplayercount`, `splitscreencontrols`, `allsplitscreenprofiles...` (scripts and menus use them).
- **The renderer can draw a sub-window of the picture:** the dvar `r_subwindow` (a vec4 "subwindow to draw: left, right, top, bottom", registered at rva
  0x21b8a) is read by `FUN_140007e40` (rva 0x7e40), which fills the view parameters (viewport rect at +0x130..+0x13c) and corrects the projection matrix
  (scale and offset) for that rectangle. This is the part a split view needs most. Called from rva 0x8150 and rva 0x8210.
- The video setup (`FUN_140010550`, rva 0x10550) stores the picture's aspect (floats at rva 0x4113738 / 0x411373c). A split view has to give each view the
  aspect of its own rectangle (this is the "everything looks stretched" bug of the old two-process version, solved there by writing the real aspect).

## The design to try: two local clients as two contexts in one process

The server already holds both players (the co-op lobby makes two players in one level). What is missing is a second *client*: its own connection, its own
`cg`/`cgs`/`cl`/`clc` state, its own view and HUD. The idea is to run the client code twice per frame with the client-side globals swapped:

1. **Connection.** The process hosts the level (as now) and client context 0 connects as it does today. Context 1 connects to the same server through a second
   UDP socket of ours (`src/net.c` already hooks `sendto`/`recvfrom`), so the server sees a second address and a second client with its own netchan. The
   second client has its own player id (the `!xuid_add` trick of the QA tools is the existing way to a second XUID).
2. **Context switch.** A table of the client-side globals (memory ranges: `cg`, `cgs`, `cl`, `clc`, the snapshot buffers, the view/refdef, the HUD
   state, the per-client dvars if any) is saved and restored when the frame goes from context 0 to context 1 and back. Everything shared (the server, the
   renderer's device, assets, sound) stays single.
3. **Two views in one frame.** In one `SCR_UpdateScreen`-like pass: context 0 draws its 3D view and HUD into the top rectangle, context 1 into the bottom one
   (`r_subwindow` for the projection, the scissor and the 2D placement for the HUD), one `Present`. Post-processing that uses full-screen targets has to run
   per view (or the targets be sized per view).
4. **Input.** One keyboard/mouse (the focused window) and XInput pads (`src/splitscreen.c` has the pad reading and the "press any button" assignment); each
   context gets its own `usercmd` stream.
5. **Game logic.** The scripts of the mod (`_mw2coop*.gsc`, `mirror.c`, `net.c`, `party.c`) assume two machines (host and client: "this machine's player", the partner's
   packets, the mirror of effects). With both clients in one process several of these must see "both players are mine".
6. **Sound** has one listener: player 1's, or a mix; **saves, subtitles, cinematics, loading screens** have to be settled per case.

## Phases, each ends with something that can be checked, and a go/no-go

| Phase | Work | Check |
|---|---|---|
| 0. Map the client side | Ghidra: where `cg`, `cgs`, `cl`, `clc`, `cls`, the snapshot ring, the refdef and the HUD state live; the frame's call order (`CL_Frame`, `SCR_UpdateScreen`, the cgame frame, the renderer's begin/scene/end); how many places read each global; which of them are per client in the engine's own code (arrays indexed by a local client number) and which are plain statics | a table of addresses, sizes, owners; a call graph of one frame |
| 1. A second client over a second socket | no rendering: a second connection of the same process, logged (snapshot arrival, `cg.time`, own player's origin) | both clients in the server's list with different addresses; each receives snapshots |
| 2. Context switch | save/restore of the table; the second client's frame functions run in its own context | the two contexts log different, stable positions with no cross talk for a minute; the first one plays as before |
| 3. Two views | `r_subwindow` per context, scissor, HUD placement, one present | a frame screenshot of both players' first-person views, top and bottom, right proportions (the aspect fix as in the old version) |
| 4. Input and menus | two devices, "press a button" lobby step, no Ready button, the mode in the menu | a QA bot drives both players through a level start |
| 5. Scripts, sound, saves | review of the mod's machine-bound parts | the QA scenarios of the mod pass with both players local |
| 6. Release | docs, changelog, 32-bit build | per the playbook's QA levels |

Go/no-go: after phase 1 (is a second client in the process possible at all), after phase 2 (is a clean context switch possible), after phase 3 (does the
renderer take two views in one frame). If one fails the plan is redone or dropped; the two-process version is the fallback.

## Unknowns that decide the cost

- How many client-side globals there are and whether they are cleanly separable (statics scattered through the code are the main risk).
- Whether the renderer keeps per-frame state that breaks when it is run twice per frame (the shadow maps, the full-screen post-processing targets, the
  per-frame arenas).
- Whether the engine's client code keeps pointers into its own globals (a swap by copying contents is only safe if nothing points into the swapped ranges).
- The 32-bit build needs its own addresses (the address finding is done with Ghidra on a memory dump).

## Rules

The owner's installed game and saves are never touched; test copies live in `D:\coop-test`; nothing is pushed or published without the owner's word.

## Phase 0 results so far (64-bit official build, Ghidra; 2026-10-26)

How the engine keeps the client's state:

- **The code still passes a local client number everywhere** (`CG_CalcFov`, rva 0xbc1e0, takes it; the frame calls the cgame with a literal 0), **but the arrays
  behind it were folded to one element**: the client globals are read at constant addresses (the cgame's `cg` is at rva 0x422440, e.g. `cg+0x14` flags, `cg+0xac`).
  So nothing can be had by "growing an array": the swap of whole memory ranges in the plan above is the way.
- **There is a 2-player table:** `CG_CalcFov` picks `cg_playerFovScale0` or `cg_playerFovScale1` (rva 0x51e1c0 / 0x51e1c8) by looking the local client up in
  a table at rva 0x540bb8 (stride 0x2650 bytes, a type field of 2 and a client id): the Special Ops two-player mode keeps a slot per player.
- **The frame, top down:** the main loop (rva 0x2913a0) -> rva 0x1fbd40 -> rva 0x1fbde0 (the frame's housekeeping, writes config.cfg on demand) -> rva 0xd5b00
  (the client frame, rate limited: `cls` state at rva 0x533cb8, 0 to 7, 6 is active) -> rva 0xd5890 (one client frame: update, the state switch, **rva 0xbca00 =
  the cgame's draw-active-frame for local client 0**, then the 2D and the screen update) -> rva 0xbe710 (the view values) -> rva 0xbc1e0 (the field of view).
  The renderer's view setup with `r_subwindow` (rva 0x7e40) is reached from rva 0x8150 and 0x8210.
- **How much one client frame touches:** the cgame draw and everything it calls (about 4000 functions) read or write about 5800 distinct writable globals, in
  roughly 190 clusters over the whole image. Most of it is the engine at large (renderer, dvars, sound), not per-client state. The client-state candidates are
  the clusters at rva 0x404450-0x44f000 (the cgame's `cgs` at rva 0x40c1a8 and `cg` at rva 0x422440 and their neighbours, about 260 KB), 0x51dca8-0x51e364 (the
  cgame's dvar pointers: shared, not state), 0x533cb0 (`cls` state), 0x540bb8 (the player slots), 0x547e84-0x548d07 and others to be sorted out.
- Tools made for this: `tools/ghidra/CallChain.java` (callers level by level), `tools/ghidra/GlobalsOf.java` (the writable globals a function and its callees touch,
  in address clusters); both run through `GHIDRA_SCRIPT=<name> sh tools/ghidra/run.sh <rva> ...`.

What is still unknown and decides phases 1 and 2: the exact extent of the client state (which of the clusters are per client and which are shared), whether pointers
into those ranges are kept elsewhere (the swap by copying contents is only safe if not), and how the connection state (`cls`, `clc`, the netchan) is laid out for a
second connection.

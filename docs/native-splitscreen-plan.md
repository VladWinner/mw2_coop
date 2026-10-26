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

## Experiment 1 (2026-10-26): can the renderer take two scenes in one frame? No, not as the engine is used now

`src/native.c` (settings.ini `[native] experiment=1`, 64-bit): hooks the cgame's draw (rva 0xbca00) and runs it twice per frame, the second time with the bottom half
as `r_subwindow`; the begin-frame (rva 0xc840) is skipped for the second run. Results:

- Calling only the scene submit (rva 0xca4b0, which `rva 0x941b0` reaches at the end of the cgame draw) twice, and calling the whole cgame draw twice with and without the
  begin-frame skipped: all four variants run for about 30 s (the black intro) and then crash on the worker thread in the renderer's back end, always in
  the draw loop at rva 0x63f50 / 0x64e80 (`IDirect3DDevice9::DrawIndexedPrimitive` fed from a surface stream whose entries are garbage; `rdi` = 1 or 2, the view index).
  So the stream the front end makes for a second view in the same frame is not valid for the back end, or is made by something this build does not run for more than
  one view.
- What the renderer is: the front end is double buffered (`rva 0xc840` toggles two frame data sets of 0x13d700 bytes at rva 0x458fb00, resets the command buffers and the
  view count at +0x419e8); a scene is queued by `rva 0x8210` (view info of 0x150 bytes per view, arena allocation of 0x4708 bytes) and drawn on worker threads.

Conclusion: **two scenes per frame needs the renderer's multi-view data understood far better than one session of reading gives, and it is the riskiest part.**

## The design that avoids it: alternate frames, one scene per frame

Draw the player 1's frame on even frames and the player 2's on odd frames (the Oblivion split screen mod does the same, the "alternate" mode), so the renderer only ever
sees one scene per frame, exactly as it does now:

1. each frame, the context of player `frame & 1` is swapped in (the client-side globals of the plan above) and the normal client frame runs: network receive, usercmd, snapshot, cgame, scene;
2. `r_subwindow` is set to the top half for context 0 and the bottom half for context 1 (the renderer already corrects the projection for it); the 2D (HUD, menus) goes through the viewport/scissor of that half;
3. a Direct3D 9 `Present` hook (the 64-bit `shot.c` finds the device) copies the half just drawn into a texture of its own and, before presenting, draws both textures into the back buffer: top and bottom, always complete;
4. each view runs at half the frame rate (60 fps game: 30 per view); the server's own rate (sv_fps 20) and the client's packet rate (cl_maxpackets 30) are not above that.

What stays unknown (the plan above, phases 1 and 2): the second connection and the extent of the client state to swap. Phase 3 becomes "the Present compositor and the half-frame 2D", which is
much smaller than a multi-view renderer.

## Experiments 2 and 3, and more phase 0 facts (2026-10-26, later)

- **Experiment 2 works (alternate frames + compositor).** `[native] experiment=2`: even frames draw the top half, odd frames the bottom half (`r_subwindow`), and a hook on
  `IDirect3DSwapChain9::Present` (the game presents through the swap chain, once per frame, one view queued per frame; the device's own Present and PresentEx are not used)
  copies the half just drawn into a texture and fills the other half from the other texture. The owner watched it: the two halves are right and there is no flicker.
  What it does not do yet: the two halves are the same player; the 2D (HUD, text) is drawn over the whole picture, so each half shows only its part of it.
- **Experiment 3 fails at once:** `experiment=3` runs the cgame draw for LOCAL CLIENT 1 on the odd frames: the process dies within 0.2 s of the first composed frame (no CRASH line:
  not caught by the exception logger). Local client 1 is not usable by changing the argument alone.
- **The 2-slot table at rva 0x540ba0 (stride 0x2650) is the coop session's connection table, used by both roles:** the server side handler of the out-of-band "coopConnect" message
  (rva 0x22d460) sets up `Netchan_Setup(1 = server socket, slot netchan, address, ...)` for slot `iVar2` taken from the message, marks it with `FUN_1401fe660(slot, 2)` and answers "coopAcceptAck";
  the client's connect handler (rva 0xd23c0) sets up the same table's slot 0 with socket 0. The server-side frame loop (rva 0x22f400) walks both slots and sends to every slot in use.
  So the engine's coop code already thinks in two connection slots; what is needed for a local second player is a slot whose netchan is a local one (the loopback).
- **Some per-client arrays are real 4-entry arrays** (for example the array with stride 0xe80 at rva 0x3cf180, 16 KB = 4 entries, indexed by the local client number in `rva 0x6a570`), while the main
  cgame state (`cg` at rva 0x422440) is addressed at a constant. A mix: some modules keep the console's MAX_LOCAL_CLIENTS = 4 arrays, the main client state is a single static.
- The workflow of six research agents (`docs/native-splitscreen-research/`) stopped on the usage limit before it wrote anything (1.47 million tokens spent, no report): the questions it was given
  (second connection, client state extent, 2D per half, input, server side and mod assumptions, time slicing) are still open and listed above.

## Runtime look at the coop slot table (host, trainer, two copies, 2026-10-26)

`!rva 540ba0 192` and `!rva 5431f0 192` on the host (the two slots), taken 45 s after `coopLaunch` with the partner in:

- **Slot 0 (rva 0x540ba0): the host's own client over the LOOPBACK.** At +0x10 the address: type 2 (NA_LOOPBACK), ip 127.0.0.1; the netchan carries two 0x1000 byte fragment buffers (pointers at +0x30 and +0x48, sizes 0x1000).
- **Slot 1 (rva 0x5431f0): the remote partner by IP.** Address type 4 (NA_IP), the PC's LAN address (192.168.100.4 here: the QA copies join the PC's own address) and its port; its own buffers; a field with value 2 at +0x20.
- `rva 0x52fb5c` (the coop session flag) = 1, `cls` state (rva 0x533cb8) = 6 (active).

So the engine already has what a second LOCAL client needs on the SERVER side: the server talks to a slot over a netchan whose address can be the loopback. What is missing for a second local client is the CLIENT side of that second loopback connection:
the client code (cgame, snapshot parse, usercmd send) is written for one connection (the client connect handler uses slot 0 at a constant) and its state is single statics.
The loopback queue itself (the engine's `NET_` loopback, one client/server pair) would need a second pair or a tag to tell the two clients' packets apart: not examined yet.

## The loopback (phase 1 lead, 2026-10-26)

Found by reading the engine's network layer (64-bit):

- **`NET_SendPacket(sock, length, data, to)` is rva 0x202db0.** For a `to` of type 2 (NA_LOOPBACK) nothing touches a socket: with `sock < 1` (a client sending) the packet goes into **one client-to-server ring** of 64 entries of 0x2008 bytes
  (rva 0x1391690, write counter rva 0x1411894, tagged with the sender's `sock` at entry+0x2004); with `sock >= 1` (the server sending) it goes into **a ring per destination client, selected by the 16-bit field of the
  address (its port)**, 12 entries of 0x2008 bytes each, queue stride 0x18068 (rings from rva 0x14118a0, counters rva 0x1429900/0x1429904 + index * 0x18068).
- **`NET_GetLoopPacket(sock, &from, &msg)` is rva 0x202a60:** `sock == 1` is the server's read (the client-to-server ring; the `from` it gives has the sender's tag as its port, so the server tells loopback clients apart by that port);
  any other `sock` is a client's read of ITS ring `sock * 0x18068`. In this build the client's socket is 0 and the server's is 1, so a second local client could not read its ring with `sock = 1`: the engine was built with one client, but
  its loopback is already shaped for several clients addressed by port. Replacing these two functions (hooks) with a loopback of our own that keys the clients by an explicit context number is small.
- **How the host connects itself over it (the `listen` handler, rva 0xd25e0):** builds the address {type 2, ip 127.0.0.1, port 0}, clears both table slots, marks slot 0 as type 2, calls `Netchan_Setup(0, slot-0 netchan, address, qport 0, buffers 0x1000 x 2)` (rva 0x203190),
  sets the coop flag rva 0x52fb5c, adds the local party member (rva 0xd7220, local client 0, local flag 1) and sets the client state (rva 0x24ee50, 0xb). The partner's connection is made by the server's "coopConnect" handler (rva 0x22d460) on slot 1.
  A second local client would be: slot 1 marked type 2, its netchan set up for the address {type 2, port 1}, its party member added with local client 1, and its client side state brought up in its own context.
- The `listen` handler also runs the console command `nosplitscreen` first; strings `splitscreencontrols`, `nosplitscreen`, `splitscreen` appear in the connect code. Those commands are the console's local split-screen switches: where they are handled is still to be found
  (the PC has the names, the code may be a stub like `IsSplitScreen()`).

So the engine's server side and its loopback are already built for several local clients; the client side is one static copy. The next steps are the hooks on the two loopback functions (queues keyed by context number), a second `Netchan_Setup` and member add for context 1,
and a look at what the client's connect state machine (`cls` state, rva 0x24ee50 and the challenge/connect sequence) needs in its own context.

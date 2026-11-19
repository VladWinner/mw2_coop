# Split screen the way the Xbox 360 game does it: study and plan (2026-11-14)

> **State (2026-11-18).** Built: mode 11 (each player's renderer frame, both shown in one present) is the only split screen in every build (the older modes 1-10, mentioned below as `experiment=<n>`, were removed from the code on 2026-11-18); the console's own view boxes are an option (`[native] viewlayout=1`); Spec Ops takes the console game's split-screen branches; per view since: the shell shock's saved screen, the camera shakes, the hide marks, the effects; sounds are started once. The sections below are the study and the work log in order.

Owner's request (2026-11-14): study how the Xbox 360 version does split screen and build the mod's the same way, instead of fixing the current design
problem by problem. Reference: the owner's own disc (Ghidra project `local/ghidra-proj-x360`, read-only; nothing from it goes into the mod), and the PC
exe. Earlier notes: [native-splitscreen-plan.md](native-splitscreen-plan.md) (the current design and its history).

## How the 360 does it (read from the 360 binary and the PC exe)

1. **One cgame state per local player, as arrays.** The 360's cgame state is an array indexed by the local player number: the shellshock update
   (360 `0x8210aff8`, PC rva `0xb7a20`) addresses `cg` as `0x8247a5e8 + localClientNum * 0x2f1e4`. The PC build folded these arrays into single statics
   (`cg` at rva `0x422440`), but kept the local player number as an argument almost everywhere.
2. **Both views are drawn in the same frame.** Each local player's cgame draw makes its own scene with its own viewport (its rectangle of the screen),
   and the renderer has per-view state: on the PC too, the renderer's view array is still indexed by the local player (`0x4812878 + localClient * 0x2b8`).
3. **The engine asks how many local players there are and adapts.** On the PC that question is a stub: rva `0x7ac10` returns 1, and the per-player
   viewport (rva `0xbd520`) returns one static rectangle. 12 places in the engine ask the count, 4 ask the viewport. Example: the shellshock screen effect
   (rva `0xb75b0`) saves the whole screen with one player (`0xe530`) and only that player's section of the screen with two or more (`0xe580`, the render
   command 3, still in the PC renderer). With one shared saved picture and two players saving the whole screen, each view blends the other's frame: the
   "ghost" (owner: "the lower player's screen duplicated in the upper one").
4. **Each player is his own client:** his own entity number, his own sound listener (the PC engine still has two listeners, `SND_SetListener` rva
   `0x23db90`), his own field-of-view scale (`cg_playerFovScale0` / `1`, rva `0x51e1c0` / `0x51e1c8`), his own effects system.

## What the mod does now, and why the bugs keep coming

The current split screen (`src/native.c`, `[native] experiment=10`) draws **one view per engine frame**: player 1's on even frames, player 2's on odd
frames, each into its own texture, and a compositor puts the two halves together at Present. All per-player client state is one copy, swapped in and out
(about 13 memory ranges, `ranges[]`). It was chosen on 2026-10-26 because drawing two scenes in one frame crashed the renderer's back end (experiment 1:
garbage surface streams for view index 1 in the draw loops `0x63f50` / `0x64e80`).

The bugs that come from that design, not from a missing range:
- each view runs at half the engine's frame rate, and its camera and sound are updated every other frame: the camera jitter of both players
  (Vlad, 2026-11-14, Spec Ops snowmobile races), player 2's sound half a second late;
- anything the engine keeps once per frame or once per process for "the" player (the saved-screen picture of shellshock, the shared targets of
  post-processing, the LOD choice, the sun shadow cull, effects spawned by scripts) belongs to whichever view drew last: the ghost hands of Endgame,
  the AC-130's field of view leaking into the other player's view, shells not going where the crosshair points;
- every new scene with a new engine feature finds another such piece of state.

## The plan: two local players in one frame, as the 360 does

Each phase ends with something checkable in the game; the current design stays the fallback until phase 3 passes.

| Phase | Work | Check |
|---|---|---|
| A. Renderer, two views per frame | Find why the back end got garbage for view 1 (experiment 1): compare the PC renderer's per-view paths (scene submit `0x8210`, the view count at frame data +`0x419e8`, the surface streams of the draw loops `0x63f50` / `0x64e80`, the cull workers) with the 360's, find what was stubbed to one view, and restore it | Trainer: two scenes of the same player in one frame, top and bottom, no crash for 10 minutes |
| B. Local player count and viewports | `0x7ac10` returns 2 while split screen runs; `0xbd520` returns each player's rectangle; go through the 12 + 4 callers and check each behaves per player (shellshock sections, 2D placement, ...) | the Endgame ghost gone with no special case; the HUD in each half from the engine's own placement |
| C. Both cgame draws in one frame | The client frame runs the cgame draw for player 1 and player 2 back to back (the client statics still swapped between them, the PC has no arrays), each with its own local player number for the renderer and sound; the frame alternation and the Present compositor go | both halves at the full frame rate; no camera jitter in the snowmobile races; player 2's sound in time |
| D. Per-player engine state | The remaining shared state the 360 keeps per player (field-of-view scale, effects, sound listener, vision, LOD) is mapped from the 360 arrays to PC addresses and made per player | the AC-130 mission (FOV, aim), Endgame, Cliffhanger, the Spec Ops list without crashes |
| E. Clean-up | the old alternation code, its workarounds (ranges no longer needed, the compositor, fxsplit, sunsplit...) removed; 32-bit port | the regression sweep of all levels and Spec Ops missions in split screen |

## Open questions

- Phase A is the risky one (it is where experiment 1 stopped). The 360 binary shows working multi-view code next to the PC's stubs, which is what
  was missing on 2026-10-26.
- Whether the PC renderer's per-view arrays have room for 2 views everywhere (the view array above has 5 entries) or some were cut to 1.

## Phase A notes (2026-11-14)

- Experiment 1 rerun with the current build (`[native] experiment=1`, Trainer, briefing skipped, 105 s): no crash any more, but no two views either: the
  window shows ONE view stretched over the whole height, and the skinned models (soldiers, the weapons in their hands) have long broken triangles.
  Experiment 1 drew both scenes as local client 0.
- The renderer is built for several scenes per frame: `R_RenderScene` (rva `0x8210`, the refdef and a scene number) takes its scene number from a
  per-frame counter (rva `0x4a40`: global at rva `0x41136f0`, also written to frame data `+0x41a30` / `+0x41a34`), appends a view slot (frame data
  `+0x1100 + n * 0x150`, count at `+0x419e8`) and sets the scene up in its own slot (frame data `+0x41a38` array, stride `0x13a0`, rva `0x5160`).
- So the second scene has its own slots; what breaks is shared per-frame data the second scene reuses: first suspect the skinned-model cache.
- The cgame draw (rva `0xbca00`) calls begin-frame (`0xc840`) itself; the PC's screen update (`0xd5890`) calls the draw with a literal local player 0. A second
  draw in the same frame must skip begin-frame (native.c `second_pass`, already there): then the frame holds 2 scenes and 2 views (logged: "scenes 2 views 2").
- The back end draws every view of the frame whose type (`+0x1380` in the scene slot, set to 2 by `0x5160`) is 2 (`0x453c0`, ported as `ngfr_453c0`). Both
  views were drawn, but over the whole screen: the view's rectangle comes from the per-player view rectangle (`0xbd520` -> rva `0x3bd808`, normalised
  x, y, w, h = 0, 0, 1, 1), not from `r_subwindow`, which only moved the projection. With that rectangle set to the top half for the first draw and the bottom
  half for the second (`[native] experiment=1`, `exp1rect=1`), the renderer draws two real views per frame, and crashes the way the October experiment did:
  view index 1, in the cached static model loop (`0x63f50`, at `0x6400c`) after ~20 s; with `r_smc_enable 0` in the model instance loop (`0x63850`, at `0x63888`);
  with the local player count at 2 (`[native] lccount=2`, the immediate of `0x7ac10`) still at `0x6400c`, after ~70 s. The bad addresses are small offsets:
  a per-view list (the static model surfaces of the view) that is built for view 0 only.
- Next: the front end's per-view static model data (visibility and cache), compared with the 360's (where each local player's view has its own).
- **Cause found and worked around: the static model draw data of the second view overflowed its region.** The four static model list builders
  (`0x3ad40`, `0x3b810`, `0x3be80`, `0x3c440`, called from the scene set-up `0x5160`) take their output chunks from two frame data regions by atomic bumps of
  two counters that begin-frame resets: `+0xb7f80 + [+0x41964]` (0xa000 a chunk) and `+0xd2f80 + [+0x41968]` (0x4000). The first region has 0x1b000 bytes before
  the second starts; one view uses about half of it, two views used 0x1c000-0x24000 (logged) and wrote over the second region. The second region is not free
  either (other data up to `+0x32318`, probe `fdprobe=1`). Fix (`split_smc_redirect`, `[native] exp1smc=1`): before the second view's draw the two counters are
  set so that the bumps land in two 1 MB buffers of the mod, one pair per frame data set, allocated above the frame data (the counter is an unsigned 32-bit
  offset from it). Result: two views per frame on Trainer, top and bottom half, no crash in 150 s (the 10 minute run of the phase A check follows).

## Phase C started: both players in every frame (`[native] experiment=11`, 2026-11-14)

- `client_frame_inner` (mode 11): player 2's client logic runs first every frame, with his own screen update suppressed through the screen update's busy flag
  (rva `0x5db410`, checked by `0xd5b00`); then player 1's frame runs, and its cgame draw (`cg_draw_hook_inner`, mode 11) draws player 1, swaps to context 1 and
  draws player 2 (second draw: begin-frame skipped, static model chunks redirected, player 2's 2D off for now).
- **The view rectangle is the cgame's** (pixels, rva `0x43c630`: x, y, w, h; copied into the refdef by `0xbe710`; inside the context-swapped range, so one per
  player): player 1 (0, 0, W, H/2), player 2 (0, H/2, W, H/2). The normalised per-player rectangle `0x3bd808` (what `0xbd520` returns) is only used by the
  shellshock's screen sections; it is set per view too.
- **The back end drew the 3D of one scene only** (`0x45690` calls the 3D pass `0x5c620` for the frame data's "current scene" `+0x41a30` = the last one submitted).
  The call at `0x456ee` now runs the pass for every scene of the frame, with `+0x41a30` set to the pass's scene (the pass reads it at `0x4df70` / `0x4e030`).
- **The pass clears after its target switch with the whole target as viewport** (`0x5c484`): only the first pass clears now (the whole picture).
- Result (Trainer, D3D9 and native D3D11): the screen is split by the engine itself, player 2's bottom view is right; player 1's top view is drawn but lit
  wrongly (white / black). Cause (next): the scene set-up writes per-scene lighting into fields of the frame data that exist once (the sun at `+0x41a48..+0x41a54`,
  the sun shadow flag `+0x4195c`, `+0x41958`, the light list `+0x421e0` with entries at `+0x423b0` stride 0x1e0 and their shadow bits `+0x41970`): the last scene's
  win. Next: keep them per scene (saved after each scene's set-up, put back before its 3D pass), checked against the 360's scene set-up.
- Tried (off by default, `[native] scenekeep=1`, `scenekeepparts=<bits>`): keeping the frame data block `+0x41940..+0x62980` per scene (saved after each scene's draw,
  after the front end's workers finished: `ctx_wait_workers`) and putting it back before each 3D pass. Putting back the light list part (`+0x421e0..`) crashes at once
  in the light loop (`0x64ac4`); without it there is no crash but player 1's view stays wrong (white / black). So the top view's problem is not (only) those fields.
  Open: what else of the frame is built for one scene (the sun shadow cascades `0x659a0` per pass, the post-processing per view `0x484c0`, the 2D of player 1), to be
  found in the 360's back end (its frame loop over scenes) instead of by trial.

## The 360's renderer front end, read (2026-11-14 evening)

Read from the 360 image (`default_sp.pe`, Ghidra project `mw2x360`; the 360's register save helpers `0x823ddf40..0x823de0e0` had to be taken out of
"no return" first, they cut every function short):
- **The 360's `CG_DrawActiveFrame` is `0x82113110` (PC `0xbca00`).** It stores its local player number in a renderer global, **360 `0x8244b424`**
  (PC rva **`0x51deb8`**, written by `0xbca00` from its argument), and has `lc == 0` / `else` branches that the PC kept (PC `if (param_1 == 0)` in `0xbca00`).
  The per-player begin function `0x821130a8` (PC `0xbd610`) sets the same global.
- **The 360's `R_RenderScene` is `0x8238b158` and its scene set-up `0x8238a288` (PC `0x8210` and `0x5160`).** Same steps in the same order: the
  shellshock for the player (`0x82112298(lc)`, PC `0xbd3d0`), the effects of the refdef's player only (`if (lc == refdef.lc)`, PC `0x20a2c0`), the
  effects' time step for player 0 only (`0x82141e70(lc)` = "lc == 0", PC `0xdb4d0` -> `0x208a10`), the worker waits by number (PC "wait %s").
- **The renderer keeps view data per local player**, indexed by that global: the 360 `0x83dc2080 + lc * 0x2b8` (3 frusta of `0xe8`: view and sun shadow
  cull planes), the PC **`0x4812a40 + lc * 0x2b8`, with room for 4 players** (the next globals start at `0x4813528`). Users on the PC: `0x5160`,
  `0x4780` (shadowed spot lights), `0x17740`, `0x18b60`, `0x19000`, `0x1b9b0`, `0x1bee0`, `0x38cc0`; all run inside `R_RenderScene`.
- **What mode 11 did wrong:** player 2's draw runs through the cgame as player 0 (the PC's client arrays have one element: experiment 3), so his scene
  used player 0's renderer slot too, and the effects' time step ran twice a frame.

### Change (`[native] exp11lc=1`, default on in mode 11)

- The call of `R_RenderScene` (`0xca523`) is hooked: player 2's runs with the renderer global at 1 (as on the 360), then the front end's workers are waited
  for and it goes back to 0. The hook passes every register argument on (`R_RenderScene(refdef, scene)`: a one-argument hook crashed the back end's light
  loop at `0x3f0d9` in every run, with or without the change).
- Readers of the global that index cgame data of one element get 0: the call sites `0x82dd` (shellshock `0xbd3d0`) and `0x6356` (`0x93ac0`,
  `0x51e330 + lc * 0x2cd0`); the per-entity "culled in" byte of `cpose_t` (`+3 + lc`, one byte on the PC: `0xb20d0`, `0xb20e0`, `0x9e547`); the entity
  lookup at `0xa4cbf`; the local entity callbacks of the workers `0x934b0` and `0x94bc0` (with 1: engine error "CG_FreeLocalEntity: not active").
- Result (Trainer, D3D9, 60 s, frames saved with `[native] shot11=<present>`): no crash and no engine error, both views every frame (161 fps).
  **Player 1's top view is still wrong** (black / white) and the skinned models still have long triangles, so the per-player slot was not the cause of those.
  Player 2's effects: the refdef carries local player 0, so `if (lc == refdef.lc)` skips his effects update now (to check).
- Not read yet: the 360's back end frame (the PC `0x45690` -> `0x5c620` / `0x5c240`), which decides how each scene's 3D pass finds its own sun, shadow and
  light state. Next step.

## Why the evening runs crashed: the scene's 2D command list was never finished (2026-11-14, later)

`scr/pc_f1.txt` and its neighbours end the same way: `CRASH: access violation (read 0x10) at rva 0x645bd`, 21-30 s in, right after "split view on". The fault
is in the per-scene 2D command loop (function `0x642e0`, entered from the dispatcher `0x5e620` at `0x5e801` when the command word's type is not 9). It reads
`RBX = *(RSI + 0x20)` (the command's stream context) and then `*(RBX + 0x10)` with `RBX` = 0. `RBX` is the *last `SetStreamSource` target* of the context
(`state + 0x58`) and it is NULL when a `DrawIndexedPrimitive` comes with no stream bound: the command still claims vertices, and the loop crashes fetching them.

Why no stream is bound: the renderer's command list is drawn by **two different functions, chosen by the previous command's type** (`CMP EDI/R12D,9`):
- type 9 (the 3D scene) -> `0x5dde0` (or `0x5da10`), **rewritten** by the native renderer (`ngf_draw_*`);
- type 0..8 (the 2D layer: HUD, console, debug draw, which run **after** every 3D scene of a frame, `0x5e620`'s non-9 branch) -> `0x642e0` / `0x64980`, **not
  rewritten**, so its `SetStreamSource` / `SetIndices` / `DrawIndexedPrimitive` still go through the device pointer at `0x410fd20`, which the native renderer
  leaves empty (`patch_sites()` bridges them to native steps, `w_4f380` acknowledges the sampler states). The `w_4f380` stub is what kept the crash from being
  the first pointer: the stream itself is fine, the vertices are not.

What changed in mode 11 that exposed it:
1. `exp11frames=1` (the last uncommitted change): with one renderer frame per player, `0x453c0` runs **once per player** and resets the 2D state (`0x4e390`)
   and the stream cache twice; player 2's `SetStreamSource` was in the *other* frame's context.
2. `exp11lc=1`: player 2's `R_RenderScene` runs with the renderer's local-player global (`0x51deb8`) at 1 while the dispatcher and the command loops run in
   the client frame; the 3D was ported with that number, the 2D was not.

Before touching any of the two: the 2D command loop has to run without the game's Direct3D 9 device whether the frame is split or not, because the native
renderer has no device at all. Check to run first (D3D11, `experiment=10`, one player view): if the console (`MW2COOP_NATCON=1`) or any 2D debug draw after the
3D scene is drawn at all in a long run, the 2D path is fine and the crash is mode 11's frame split. If it crashes the same way, the 2D draw loop is the bug.

## Checked in the game (2026-11-14 late, 64-bit, D:\coop-test\mw2-64, 1280x720)

- **The committed mode 11 does not crash the way the evening's uncommitted change did.** With `exp11frames` stashed (HEAD `d6e43e9`), `[native] experiment=11`,
  Trainer: two runs of 90 s and 300 s with no exception, "scenes 2 views 2" every frame, both players stepping. So the crash in `scr/pc_f1.txt` and its neighbours
  (rva `0x645bd`, 21-30 s) comes from the last uncommitted change (one renderer frame per player), not from what is committed.
- **The 2D command list of a frame never reaches the engine function in either mode on the native renderer.** With `[native] tracescene=1` (a QA trace of
  `ngfr_453c0` and of `native_final2d_entry`) neither mode 11 nor mode 10 logs a single line, while the split screen runs (mode 10 composes, mode 11 draws both
  views). So the renderer being copied by the native one is not entered the way this doc assumed: the functions of the Direct3D 9 back end that the native
  renderer replaced are not all on the live path, and the 2D the player sees goes somewhere this trace does not cover. Next: find the caller of the 3D view
  setup (`fn_484c0` / `fn_5c010`) and of the 2D dispatch table (rva `0x36f768`, no code reference to it) in the *running* process (a debugger breakpoint on
  `ngfr_453c0` and on `0x5c010` shows who calls them), instead of reading static references.
- Frame capture for QA of mode 11 on the game's own Direct3D 9: `[native] exp11raw=1;exp11rawat=<n>;exp11rawstep=<k>` saves `raw1-<n>.bmp` after player 1's draw.
  It uses `shot_save_now` (the device's back buffer) and does not write a file yet: the "not saved" reason is not logged for it.

## Which renderer the QA runs use (2026-11-15)

Every run in the 2026-11-14 late session and tonight went through `tools/qa_native.sh`, which writes `[native] experiment=<n>` and nothing else: no `[display] directx11=1`,
so the game's own **Direct3D 9** renderer is in use and `natboot` (the mod's rewritten back end) is not installed at all. Consequences:
- The traces added tonight to `ngfr_453c0` / `native_final2d_entry` are silent in these runs because those functions are never called, not because the 2D path is broken. That
  correction is the reason mode 11 "never reached the 2D list": the 2D list in a plain Direct3D 9 run is the engine's own, called by the engine's `0x453c0`, which the mod only
  hooks to get `scene2d_hook` (`0xca523`-style hook set in `native.c`), not to replace.
- Mode 10 (the compositor) and mode 11 in a plain Direct3D 9 run both show real, moving pictures (mode 10 composed both halves tonight, `scratch/native-last.png`), so the engine's
  own frame path is intact in both.
- To test the native renderers (the path where `patch_sites`, `ngf_*` and `exp11lc` matter), `MW2COOP_DISPLAY=directx11` must be added to the QA run: `qa_native.sh` has no switch
  for `[display]` yet, and the last session's "mode 11 on the native renderer" claims were about `[native] experiment=11` with `MW2COOP_NATIVE11=1` in the environment.

The next check is therefore: mode 11 with `directx11=1` (native), which is where player 1's top view was white/black and where the crash in `0x645bd` was found.

## Mode 11 on the native Direct3D 11 renderer, measured (2026-11-15, `MW2COOP_DISPLAY="directx11=1"`)

The QA runner now takes `MW2COOP_DISPLAY` (e.g. `directx11=1`) and writes the `[display]` section. With it, mode 11 in the Trainer:

- **No crash in 100 s** (the whole run, the copy was killed by its timeout). The uncommitted `exp11frames` variant is still the one that died, not this.
- **The back end is entered once per frame after the split starts** (`w_44d00`, `w_453c0`, `w_45060` move together, one per frame). So in mode 11 the engine does **not** run
  the two-view loop of `0xB12` any more: the `end_frame_hook(flags=1)` between the two players' draws ends the first player's frame, `0x453c0` runs, and then the second player's
  draw begins a frame of its own. Two whole renderer frames per engine frame, which is what `exp11frames` was reaching for and what the commit at `d6e43e9` does not do.
- **Player 1's view (top half) is black** in the raw capture taken between the two players' draws (`shots/raw1-300.bmp`): the black covers the top half exactly and the picture
  beneath it is the alpha-numeric overlay of that frame. Player 2's view (bottom) draws correctly. This is the "black/white top view" of the earlier notes, now with a picture of it.
- Before the level, the same run renders the mission's briefing film and text correctly on Direct3D 11 (`native11/scene-9000.bmp`: the AC-130 city map, the Russian subtitles),
  so the native renderer itself is sound; the fault is specific to the split.

Open question that the picture narrows: the top half is not a wrongly-lit view, it is a **cleared** region — the black is laid over the frame the 2D layer is drawn on, so the first
player's 3D pass, its post-process composite (`fn_484c0`) or the frame's clear writes black over exactly that rectangle. Next: print the view rectangle and the clear rectangle the
frame's 3D pass uses for each of the two scenes (the pass's clear is already given the whole target for the first view, `split_smc_redirect`'s neighbour `native_scene_clear`).

## The black top half: what is ruled out, with the logs (2026-11-15, native Direct3D 11, mode 11)

Measured in the game (`clearlog=1`, `rslog=1`, `exp11raw=1`, 1280x720, Trainer):

- **Both players' scenes are submitted**: `rslog after p1: lc 0 ... scenes 1 views 1`, then `rslog after p2: lc 1 second 1 ... scenes 2 views 2`. The per-player renderer
  slot (`exp11lc`) is in effect, and the second scene is in the frame.
- **The view rectangles are set as intended**: at the draw, `view rect 0.000 0.000 1.000 0.500 | cg view 0 0 1280 360 | picture 1280 720` (player 1's top half), and the cgame's
  rectangle is the same.
- **The clear the mod hooks (`0x5c484 -> 0x51070`) is not the 3D pass's clear.** Only 24 such clears happen in a run, all on the cgame draw thread, all with the same signature
  (`flags 0x7 rgba 0 0 0 0 depth 1`, `view rect 0 0 1 1`, `cg view 0 0 0 0`) and always with `scene3d_pass` 0 or -1 — i.e. before the split's rectangles are in place, and never
  from the front end's worker thread that runs the frame's 3D passes (`0x45690`/`0x456ee`, a data reference, so the call is indirect). The log in `php` from an earlier session
  ("the 3D pass, `0x5c484`") names the wrong call: `0x5c620`, the function the `p1`/`p2` logs come from, is the one whose pieces the 3D pass runs, and its clear is reached
  indirectly.
- **Where the black is written is therefore still open**, but the candidates are now narrow: the front end's own frame clear (the `real clear` of the scene target, inside the
  worker's pass), the post-process composite (`fn_484c0`), or the scene target's first switch by `reset_2d` (`ngfr_453c0`). The next instrument is inside `ngfr_453c0` and the
  worker pass: log every `nd_clear` (the native device's clear) with its target and rectangle for two frames of the split, and compare with `raw1-*.bmp`.

## The split's 2D command list crashes with index 0 (2026-11-15 morning)

A native-mode-11 run with the native device's clears traced (`[native] ndclearlog=1`) died 12 ms after `split view on`, on the first clear of the second scene's frame:

```
CRASH: access violation (write 0x0) at address 0x7ffd40e6a1b9
CRASH: stack rvas: 61710 458abe0 0 0 0 458fb00 619b8 4357760 4356bc0 0 458fb00 487d0 4357760 4356bc0 7eb940 7ec138
```

Reading those frames: `0x619b8` -> `0x61710`, both in the scene's 2D command loop (`0x61740`-ish, the loop that iterates the command list). At `0x6170b` the loop calls
`0x61020` and gets back `EAX` = 0; `0x61723`-`0x6172e` does `DEC EAX; CDQE; MOV RAX, qword ptr [0x1441159e8 + RAX*8]` — with 0 that is `[0x1441159e0]`, the guard word *before*
a table of 8-byte entries at `0x1441159e8`, and `[0x1441159e0]` is 0, so the loop writes through a NULL pointer at `RCX = 1`. **So `0x61020` returned an index of 0 where the code
expects 1-based indices (the `DEC` makes the table lookup safe only for `EAX >= 1`).**

`0x61020` is the engine function that appends a command and returns its index; its three callers are `0x6134f`, `0x6137b` (both in the same 2D path) and `0x6170b`. Why it
returned 0 in the split is not yet known — the first candidate is the command arena bookkeeping being run twice per frame (player 1's and player 2's 2D lists) with the
frame's counter reset between them (`0x44d00` resets the arena at the start of every frame, and mode 11 now has two frames per engine frame on the native path).

**Reproduced three times out of three with `ndclearlog=1`, never with the trace off** (same build, `ndclearlog=0` and `ndclearlog=2`/`3` both ran their full minute). So the
logging call is the trigger, not the clear: the trace's `nat_log_hook(...)` runs on the render path between the 3D pass and the second scene's 2D list, and whatever it
disturbs (a stack slot, a timing window, or the log's own lock) is what makes the arena return index 0. It is worth knowing for the owners of other traces in this code that a
log line placed on the frame path can change the frame: the crash was found by the trace and does not happen without it. What the crash does establish is the only clear of the second scene's frame: the 3D pass's colour clear runs with `target scene 1280x720 viewport 0 0 1280 720`, i.e. the
whole picture — so the pass's clear does not carve out a half, and whatever makes the top half black is drawn after it.

## The deferred clear trace (2026-11-15, last change of the morning)

`[native] ndclearlog=1` now records the frame's colour clears into a fixed buffer (`nd_clear_record`, `src/dx/natdraw11.c`) and writes them in one call from `ngfr_453c0`
(`nd_clear_flush()`), instead of logging inside `nd_clear`. **Not proven clean**: its first run crashed at the same place as the logging version (`0x619b8` -> `0x6170b` ->
`0x61020`, the index-0 write through NULL), so the deferral is not enough on its own. Until this is settled, treat every instrument on the frame path in mode 11 as suspect, and
check a claim with the trace off before writing it down. The next owner should either make the trace write nothing at all on the frame path (a raw int array, flushed by the
client thread once a second) or read the same facts from the game's own command list.

## The 360's back end, read, and one renderer frame per player (2026-11-15, `exp11frames`, off by default)

**How the 360 does it (read from the 360 image):**
- Render thread `0x823b0568`; per frame `0x823aede0` (frame begin when bit 0 of the frame's flags at frame data `+0x1740`), `0x823b0468`, `0x823ad698` (on bit 1 = show:
  the command list, executor `0x823ad5d0`, handler table `0x8205b640`, 23 commands: viewport, 2D, debug lines). The 3D passes are recorded by worker job `0x1a`
  (`0x823d9150`, a switch on the pass: 6/7 sun shadow cascades `0x823ddcc0`, 9-12 spot shadows `0x823dae58`), queued by the scene set-up per pass (`0x82388b48`), into
  per-pass buffers of the frame data (`+0x1620 + pass * 0x10`): **one 3D scene per frame data**.
- **The screen update `0x82135818` gives each local player his own renderer frame:** for each player `CG_DrawActiveFrame(lc)` (`0x82113110`), his UI, the view's close
  (`0x823912a8`), and after the first of two views the end-frame `0x82390e20(1)` (begin, not shown); the last view ends with 2 or 3 (shown). So the sun cascades, light
  lists and model chunks are never shared between the two players: each has a whole frame data set. That is why copying parts of one frame per scene (`scenekeep`) never fixed
  the top view.

**The PC kept all of it:**
- end-frame `0xc4c0(flags)`, flags at frame data `+0x41a60`; bit 0 = frame begin in the back end (`0x44d00`: BeginScene, upload of the frame's dynamic data), bit 1 =
  Present (`0x45060`). The screen update `0xd5890` ends with `0xc4c0(3)` at `0xd5aaf`.
- the frame's command list is closed by `0xc800` (end marker `0x40000`, after the view), `0xc7e0` (the final 2D section's start kept at frame data `+0x41a40`), `0xe900`
  (its first command `0x80017`) and `0xe440` (close: end marker, list fields at `+0x41a28`, command buffer index flip, scene counter `0x41136f0` = 0).

**`[native] exp11frames=2` (mode 11):** after player 1's draw: wait for the front end's workers, `0xc800`, `0xc7e0`, `0xe900`, `0xe440`, `0xc4c0(1)`; player 2's draw then
begins its own frame (no `second_pass`), and the screen update's own end-frame is hooked to end it with 3 (on the PC bit 0 is needed in every frame: the dynamic data upload;
with 2 the frame's 2D drew without vertices). `exp11frames=1`: player 2's frame with 2, as on the 360.

**Results (Trainer, D3D9, 1280x720):**
- Without `0xe440`: "scenes 2 views 1" in player 2's frame (the scene counter was not reset) and a crash in the model surface loop (`0x645bd`). Without `0xc800`/`0xc7e0`/
  `0xe900`: the same crash. With the whole close: **player 1's frame completes** (logged inline with `r_smp_backend 0`).
- **Player 2's frame crashes in its 3D pass** (`0x62c70` -> `0x3fee0` -> `0x3fe20` -> `0x462e0` -> dispatcher `0x5e620` -> model surfaces `0x642e0`, at `0x645bd` / `0x64650`):
  a model surface record in *its own* frame data (`fd + 0x474d4`, `+0x4c718`, `+0x4cb58` in three runs) has a garbage vertex pointer (`+0x20`). The same with player 1 drawn
  twice (`exp11same=1`, no context swap), with `exp11lc=0`, with `r_smc_enable 0`, with the back end on the main thread. So **any second renderer frame in the same cgame frame**
  breaks, whoever is drawn.
- With the per-entity cull byte left per player (`exp11pin=0`: `cpose + 3 + lc`, as on the 360, where the PC has one byte and lc 1 writes the next field) player 2's first
  frames complete, and a later one crashes the same way. So per-model state of the cgame frame is the lead: the scene's model records (`0x12d30`: state at `+0x28`, 2 -> 3
  while skinning, offset + 4 when skinned; job 4 via `0x12aa0` from the cgame's entity pass `0x9e540`) carry "already done this cgame frame" from player 1's frame into player 2's.
- Default mode 11 (`exp11frames=0`) runs as before: 60 s, no crash.

Next: find how the 360 makes the second frame re-add and re-skin every model (its per-player cpose bytes and whatever resets the model records per renderer frame), and give the
PC a per-player copy of that state instead of the one byte.

### Player 2's frame fixed: both views right, no crash (2026-11-15, `exp11frames=2` now the mode 11 default)

Two more causes, both from running two renderer frames inside one screen update:
1. **The scene's model records are per screen update, not per frame.** `0x7b60` (from `0xbc1a0`, the PC's version of the 360's `0x82112738` before its player loop) clears
   the records `0x1440dd760` (0x90 each, count `0x1440dd754`, state at `+0x28`: 0 new, 1 posing, 2 posed, 4 culled, then skin-arena offset + 4) once, and the front end's
   loops (cull `0x17740`, `0x16cc0`, `0x16f80`, `0x19000`, `0x319a0`, ...) walk all of them. Player 2's frame drew player 1's records with their skin offsets into player 1's
   frame data (the model-surface records `fd + 0x42980 + (key & 0xffff) * 4`) -> the garbage vertex pointer in `0x642e0`. Fix (`exp11clear=1`): after player 1's frame is
   handed over (and the workers are done), `0x7b60` again, so player 2's frame starts with a clean model list.
2. **Player 2's frame begins the picture again.** Its first `Clear(TARGET|ZBUFFER|STENCIL)` (no rectangles, before its viewport is set; seen in a one-frame D3D9 trace,
   `[native] d3dtrace=1;tracepops=<n>`, now also for mode 11) erased player 1's half: the scene is drawn straight into the back buffer. Fix (`exp11clip=1`): in the device's
   `Clear` hook (`t_clear`), when the back end's frame data (rva `0x4358f58`) is player 2's (recorded at his end-frame), the clear is given player 2's rectangle only (with the
   whole picture as viewport for the call, since a clear's rectangles are clipped to the viewport).

Result (Trainer, D3D9, 1280x720, three runs of 60-70 s, frames from `shot11`): **no crash, both halves live, player 1's top view lit and textured like the game**, ~105 fps for
both players. Open: some frames of player 2's view have large stretched triangles near the camera (looks like the partner's body model; the old "long skinned triangles"),
player 2's 2D is still off (`exp11no2d`), and none of this is checked on the native D3D11 renderer, in other levels, or for sound/FOV/effects per player.

### The shards: the players' bodies skinned under the render thread (2026-11-15, fixed: `exp11rtwait=1`)

- What the shards were (checked, not guessed): with the second entity pass skipped in player 2's context (`[native] p2skip=1`, QA) his view had none, so they were player 1's
  body; player 2's body in player 1's view broke the same way (the "row of soldiers"). Both bodies were placed right (sampled over a whole run: `p2hooklog=1`, entity 0 at
  player 1's pose); only the first frames after the split start placed him at player 2's spawn point. Not the view model (`cg_drawGun 0` changed nothing), not `exp11lc`.
- With the back end on the main thread (`r_smp_backend 0`) both bodies were right: a race. The render thread draws player 1's frame while the main thread already builds
  player 2's, and the back end reads the scene's model records too (`0x46cc0`), which player 2's frame clears (`0x7b60`, `exp11clear`) right after the hand-over.
- Fix: before that clear, wait until the render thread has finished: the engine's count of frames handed over and not done (rva `0x1429b5c`, raised by `0x204230`; the
  begin-frame waits for <= 1 through `0x2039f0`) must be 0, waited for with the engine's own wait that runs worker jobs meanwhile (`0x301e0(cond)`).
- Result (Trainer, D3D9, two runs of 70 s, threaded back end): no shards in 8 frames, both bodies whole, no crash, 214-230 fps.

### Open: the other player's body looks smeared in mode 11 (2026-11-15, owner's report)

Seen at full size (`shot11` frames): player 2's body in player 1's view (and player 1's in player 2's, fainter) is layered, as if several copies of the mesh at slightly
different poses were drawn over each other (the helmet repeated in thin bands). Mode 10 draws the same bodies crisp (Alpha's face, Bravo's helmet). What is ruled out, measured:
- not extra adds: each body is added to the scene once per frame, at the right place, one model record (`[native] bodyaddlog=1`, both contexts, one window of 60 adds);
- not the render thread: the same smear with `r_smp_backend 0`;
- the old one-frame mode 11 (`exp11frames=0`) is broken in other ways (black top half, shards) and cannot be compared.
Candidates, not checked: the body's skeleton and animation, which the cgame now builds twice per screen update (once per context, each before its own frame's
begin: `0xb0860` comes before `0xc840` inside `0xbca00`), against a skeleton memory ring of 4 slots advanced per renderer frame (`0xc840`, `0x14488a2e0`), and the
transplanted body model (`src/transplant.c`). Next: the 360's skeleton and skinning path for two views (owner, 2026-11-15: port it from the 360 and the recompilation),
and a D3D trace that picks out the body's draws.
- 2026-11-15, more checks on the smear: the PC's 4-slot ring advanced by the begin-frame (`0x14488a2e0`) is the per-model lighting cache (`0x29820`, a light-grid
  sample per entity origin kept 3 frames), not skeleton memory; the counter `0x411344c` it also bumps feeds texture/probe caches (`0x27290`, `0x28aa0`). In IW4 a model's
  skeleton memory belongs to the client (reset per local client by the cgame draw), which the 360 keeps per local client and the PC swaps per context: the next thing to read.
  With the mod's body adjustments off (`lagfix=0;yawkeep=0;pitchkeep=0`) player 1's body lands on player 2's camera again, so they stay on. A one-frame trace with every
  indexed draw's geometry (`[native] traceprims=1` with `d3dtrace`) shows every skinned mesh drawn several times from the skinned vertex buffer at different offsets, which the
  rangers (one model, many instances) explain as well: the body's own draws still have to be told apart (by its model, through the scene record).
- 2026-11-15, narrowed: with player 2's draw skipped (`[native] exp11nop2=1`, QA) player 2's body in player 1's view is crisp (one capture, from behind), so the smear needs
  player 2's draw in the same screen update. Each body is one object with two models, added once per frame (`bodyaddlog=2`). The scene record of the body ends player 1's
  frame sometimes posed only (state 2), sometimes skinned (state 23) (`traceprims=1` logs it). Tried and off by default: the bodies' bone controller tables (`0x51dcb0..0x51de80`,
  written by `0x9df60`, outside every swapped range) made per player (`ctlsplit=1`): the shards came back and player 1's view showed bodies from the inside, because the copy
  of player 1's body into context 1 relies on the shared table. Next: what else player 2's draw rewrites that player 1's body uses (skeleton memory of the client, the DObj's
  own cached skeleton), read on the 360 where each local client has its own.

## Why `shot11` produced no files in mode 11: the Present hooks were never installed (2026-11-15, fixed)

Every mode 11 QA capture from the afternoon runs came out empty, and nothing in the log said why. The reason: the mod reaches the game's present path only through the
EndScene bootstrap, and that bootstrap is armed only when the debug console opens (`set_open` -> `present_bootstrap_request` -> `native_present_hooks()` on the console's
setup thread, completed on the render thread). A QA run that never opens the console therefore never wraps `Present` — so no frame queue, no compositor, no `shot11`, no
`[native] d3dtrace` from that path, while the split itself (which does not need Present) looks fine. Fix: `[native] presentkeep=1` arms the same bootstrap from the console's
setup thread at start-up. With it, `shot11=<present>` writes `m11-*.bmp` as documented (`shots/m11-1200.bmp` ... ).

Also: the presenter counts presents from the moment the hooks are in, not from the split, so a `shot11` value has to be small (~1200 is about a second after the level loads,
not 2400+ as the earlier notes assumed); `[native] shot11: present <n>` logs the count every 300 presents to place a capture.

## Ported reading: how the 360 sets up one player's scene beside another (2026-11-15, `8238b158`)

Read from the 360 image, for the phase-D items the PC still shares between the two players. The 360's `R_RenderScene` (`0x8238b158`) does, per player and in order:

1. `0x82391220`: **scene time** written per local client: `0x83c93900` is the client-state table, and the time goes to `[clientState(localClient) + 0x2f4]` — i.e. the
   360's renderer scene record is indexed by the local client (`0x2130` stride). The PC (`native.c` `rs_log`/the scene set-up) has one scene record; a second scene thus
   shares the time field. (The PC's `0x51deb8` is the renderer's local player, set per draw; the 360's equivalent global `0x8244b424` is what `0x82113110` stores and
   `R_RenderScene` reads.)
2. `0x821410c8`, `0x82393cc0` (frame helpers), `0x82112298(lc)` (the shellshock for that player, PC `0xbd3d0`), then the sun and view angles copied into the shared renderer
   block `0x83c934e0` at `+0x154..+0x168` (PC: the renderer state object, rva `0x4357660` — the block the PC's own scene set-up fills from the same refdef fields).
3. `0x82391570` returns the view record; then **the rectangle is transformed through a scale pair** read from the vector table at `0x83c911a8` (`x * v[0] / v[1]`, `y * v[2] /
   v[3]`) into the view's `+0x118..+0x128`; when the refdef's flag `+0x3f54` is set, the window's drawable rectangle (`+0x3f44`, `+0x3f48`, `+0x3f4c`) goes through the same
   scale into `+0x128..+0x130`. On the PC the same maths sits in `0x7e40`/`0x8050` (view parameters `+0x60..+0x74`) against the global rectangle pair at `0x4113718`,
   which `r_subwindow` writes — so the pair is **one global today** and a per-player setup with two rectangles would need it per scene.
4. `0x82389878` (view list), `0x823898f0`, then the scene set-up `0x8238a288` (PC `0x8210` -> `0x5160`).

**Checked on the PC afterwards: this does not cost the split anything.** The PC's view set-up `0x7e40` reads the rectangle from the dvar at `0x4113718` (its `+0x10..+0x1c`)
and multiplies it by the view's own scale (`+0x4c`, `+0x5c`); the split's `experiment=11` code sets that dvar per draw (top half for player 1, bottom for player 2), so the
transform the 360 does per call is applied per call on the PC too. The rectangle is not the smear's source, and no porting is needed there. What remains from the 360 comparison
is the per-client scene record (its time at `+0x2f4`) against the PC's single scene record, which is the next thing to check.

## Scene time: the 360 keeps one per local client, the PC has one (2026-11-15)

Read on both, side by side:
- **360** `R_RenderScene` (`0x8238b158`) calls `0x82391220` first: it loads the renderer's local player (`0x8244b424` is not used here — it reads `0x83c93900 + 0x15b8` for the
  current client record, `0x15c0` for the base, stride `0x2130`) and writes that client's scene time at `+0x2f4`. The scene record is per local client by construction.
- **PC** `0x8210` (`R_RenderScene`) begins with `0x268540` / `0x268740` (the view's command arena), stores the view's command pointer at view `+0x308`, `0xdad10` (the
  post-process), then `0xbd3d0` with the renderer's local player (`0x51deb8`), then copies the sun and angles into the shared renderer block (`0x4113434..`). **No time is
  written for the scene at all**, and there is no per-client index for one: the scene record (`0x13a0` bytes at frame data `+0x41a38 + n * 0x13a0`) is one per submitted
  scene, and the frame's "current scene" (`+0x41a30`) is what the back end's 3D pass reads (`0x4df70`, `0x4e030`).

**Followed up on the PC side, and this lead does not hold either.** The back end's per-scene pass head (`0x4df70`) reads `0x4358e10` (the 2D / view state object) and scales
by the global block `0x4115920` (`+0x1f8..+0x200`, the sun colour); scene records live in a global array `0x4351b0` with stride `0x38`, and `0x4d890` walks them from the frame
counter `0x411375c`. Nothing in `0x8210` / `0x5160` / the pass writes or reads a per-scene time, so the PC simply has no scene clock to make per player: what it has is one
clock for the frame, which both scenes share *legitimately*. The 360's per-client time is part of its per-client scene record, which the PC folded away together with the rest
of the local-client arrays.

Conclusion for the smear: the difference is not a missing clock on the PC. The next candidate in the same family is the shared post-process / 2D state (`0x4358e10`, `0x4357760`),
which the 360 also keeps per player and which the PC's frame end resets once — the frame-end reset (`reset_2d`) runs between the players' frames in mode 11 (`exp11frames=2`)
and the second run rewrites state the first player's scene record still points into.

## The 2D state reset: the mod makes it, the game does not (2026-11-15, `exp11reset`, tried)

A static scan of the reference exe for the three 2D-state globals the port's `reset_2d` writes (`0x4358e90`, `0x4358e8c`, `0x4358e94`) finds **no writer in the game**: the
only references are comparisons in `0x3d890` (the 2D layer setup, called from the composite `0x484c0`) and reads in the post-process `0x48xxx`. So `reset_2d`
(`natframe2.c`, copied there from the engine's frame end by an earlier session) writes state the game never rewrites per frame — and in `exp11frames=2` it now runs
**twice per engine frame**, between and at the end of the two players' frames.

Tried `exp11reset=1` (default): the second frame of an engine frame skips `reset_2d` (`ngfr_skip_reset_2d`, cleared at the screen update's end-frame). Result on the fixed
camera (1280x720, D3D9, `setviewpos -2515 5190 -56 163 0`): no crash, and the top view shows **no visible smear** in the captures (`scratch/reset-no-1200.png`), while the
same view before the change showed the partner's helmet layered in thin bands. Not yet a conclusion: the bodies are behind the camera in this framing, so the next capture has
to look straight at player 2's body, and the same must be checked with `exp11reset=0` on the identical view.

### The smear fixed: player 1's half was never cleared (2026-11-15 night)

Cause, read from the one-frame D3D9 trace (`[native] d3dtrace=1;tracepops=<n>`): each renderer frame starts with `BeginScene` and `Clear(TARGET|ZBUFFER|STENCIL)` **without
rectangles, before its viewport is set**. A D3D9 clear without rectangles clears the current viewport, and the viewport player 1's frame inherits is the last one of the frame
before: player 2's bottom half. So player 1's half was never cleared, colour nor depth: every frame drew only where it was nearer than the old depth, and older frames showed
through in bands (most visible on the bodies, but the scenery too when the camera turned). `exp11noscene=1` / `exp11nop2=1` "fixed" it only because without player 2's scene
the last viewport was the full screen. The 360 has one picture per player frame and no such inheritance.

Fix (`exp11clip=1`, extended): in the device's `Clear` hook a rectangle-less clear of the **back buffer** in mode 11 is given the half of the player whose frame the back end is
drawing (frame data `0x4358f58` == player 2's -> bottom, else top), with the whole picture as viewport for the call. Checked on the reproduction view
(`setviewpos -2580 5212 -56 270 0`) and on a normal 70 s run: both halves crisp, bodies whole (faces, goggles), no crash, 130 fps. (The reproduction view puts player 1 on
player 2's spot, so player 2's camera is inside player 1's body there: that is not a bug.)

### Each player's own FOV scale (2026-11-15, from the 360; `fovsplit=1`)

The game's scripts give each player his own FOV scale: `maps/_ac130.gsc` sets `cg_playerFovScale0` when the gunner is `level.player` and `cg_playerFovScale1` when it is player 2
(`_cobrapilot.gsc` the same). The 360's FOV function (`0x8210fcd8`) multiplies by the scale of the **client slot** of its local player (`0x820d81a8`, the PC's `0xa5ec0`); the PC's
(`0xbc1e0`, the choice at `0xbc2de`) takes it by the local player number, 0 for both players of the split screen (modes 10 and 11): player 1 in the AC-130 zoomed player 2's view too
(Vlad's report) and player 2's own scale was never read. Fix: the 20 bytes of that choice load the dvar pointer from `fov_scale_sel`, which every context swap sets (context 1 ->
`cg_playerFovScale1`); installed at the first swap once the dvars exist (64-bit only). Checked (Trainer, mode 11): `set cg_playerFovScale0 0.5` zooms only the top half,
`set cg_playerFovScale1 0.5` only the bottom one.

### "Which entity is me" per player, as on the 360 (2026-11-15 night, owner: "port this, do not make your version")

The 360 gives each local player his own entity table and his own "me": the cgame of local player 1 has `clientNum` 1 and never draws entity 1 in its own view, and every
"my slot" lookup goes through the client slot (`0x820d81a8`, PC `0xa5ec0`). The PC's own-entity write `0xb28b0` has an inline copy of the old rule (slot = local player number,
`0xb28f3..0xb292d`), so player 2's state went into slot 0 in his context and the mod moved it afterwards. Ported (`ownslot360=1`, 64-bit): those 58 bytes call the slot function
(`local_slot_hook`, slot 1 in context 1), so the write lands in player 2's own slot; `own_write_hook` no longer moves data when the patch is in.

Measured after it (Trainer, mode 11): player 2's context has `clientNum` 1 and, at the write, slot 0 = player 1 from its own snapshots (-2515, 5190), slot 1 = player 2.
Still open, and the cause of the "other player's texture on my camera" (owner's screenshot): (1) something after the snapshot puts player 2's server position into the pose of
slot 0 (with `p0copy=0;p0frame=0;skipent=0` player 1's body is drawn at player 2's spot on every frame, so the old copies stay on for now); (2) player 2's **own** body is drawn at
his own camera in every frame, through an add function other than `0x2bb0` (none of `0x2bb0`'s adds sit near his camera after the first 2 s, `nearlog=1/2`): the PC's first-pass
adders `0x2eb0` / `0x30d0` are the candidates, where the 360's cgame skips its own `clientNum`. QA switches added: camlog, nearlog (1: models near player 2's camera, 2: entity 0's
placements counted every 2 s), exp11nogun.
- 2026-11-15 night, more of the 360's "me" ported: CG_DrawActiveFrame's own inline copy of the old rule (`0xbcc07..0xbcc3e`, the client slot handed to `0x30d0`, which
  replays the second model record list and keeps the records of that slot: flag `0x800000`) now asks the slot function too (`ownslot360`). Measured: player 2's context has
  slot 0 = player 1 and slot 1 = player 2 before and after his draw, his camera at his own slot. The shards on his camera are still there; with the mod's copy of player 1
  (`p0copy=0`) off, player 1's body in his view sits at player 2's own position on every frame (`nearlog=2`: 100 % of the adds), so during player 2's own cgame frame his
  context's entity 0 is filled with player 2's data. On the 360 the client of local player 2 gets player 1 as entity 0 from its own snapshots. **Next: how player 2's context gets
  its snapshot entities** (the mod's snapshot path for local client 1: `0xc99c0 -> 0xb8000(lc)`, the per-context entity ring `entring`), compared with the 360's
  `CL_ParseSnapshot` per local client; then `p0copy`, `p0frame` and the moves in `own_write_hook` can go.
- 2026-11-15 late evening, **correction and the port** (uncommitted when written).
  - **The 360's split:** a `cg_t` per local player, but ONE shared `cg_entities` (`0x824ae9c0`, 0x194 each, indexed by entity number only).
    - The own-entity write `0x82102f28` writes the slot of its client (`0x820d81a8`).
    - The pass over the other players' bodies `0x82100ff8` draws the other slots.
    - So each player's body in the other view is what his own cgame step wrote.
  - **On the PC:** the server's snapshot build (`0x234540`) leaves the client entities (below 2) out of the snapshot list.
    - Player 2's context never got player 1's entity from snapshots: its slot 0 stayed where it was at the split's start for the whole run (measured: player 1 moved from -2515 to -2555, player 2's slot 0 stayed at -2515).
    - The statement above ("slot 0 = player 1 from its own snapshots") was wrong.
  - **Ported:**
    1. `centshare=1` (mode 11): the context swap keeps cg_entities slots 0 and 1 in place, and `p0copy` defaults to 0 with it.
       - Checked (D3D9, Trainer): player 2's slot 0 follows player 1, no entity 0 add near player 2's camera, frames normal.
    2. `p2step=1`: the 360's screen update (`0x82135818`) runs `0x821127a0` (snapshots, own-entity write) for every local player before the draw loop. The PC's `0xbc170`, called at `0xd58e3`, ran for player 1 only.
       - Player 2's step now runs after player 1's, in his context.
       - Built, not yet checked in a run.

### `p2step` checked in the game (2026-11-15, after the evening's other work)

First run with the hook in (`[native] p2step=1`, mode 11, D3D9, Trainer, the smear's reproduction view, `camlog=1`): **no crash**, and the log shows what the port was for:

```
native: p2step #1:   ps origin -2588.0 5212.0 | slot1 -2588.0 5212.0 | slot0 -2515.2 5190.2
native: p2step #121: ps origin -2588.0 5212.0 | slot1 -2588.0 5212.0 | slot0 -2515.2 5190.2
```

Player 2's own entity is written from his own snapshot (`slot1` == his `ps origin`) on every sample, and player 1's slot is left alone. Before the hook his step never ran in mode 11, so his slot was never his own. `[native] p2step=0` turns it off.

Captures from the same run (`shots/m11-1500.bmp`, `scratch/p2step.png`): both halves complete and lit, the world does not drop out on this view, the partner's body is whole. Still open and visible in the owner's own screenshots: the shared effects (smoke, muzzle flash) show on the wrong player, and in some views the environment disappears under player 2 — the effects system and the remaining shared cgame state, which is the phase-D list.

### The effects clock advanced twice per engine frame (2026-11-15 night, `fxclock`)

The cgame's step calls `0xdb4d0` — a one-instruction predicate, `SETZ` on `lc == 0` — and on true calls the effects time step `0x1e80c0` with `cg.time`
(`0xbccdf..0xbccf0`). The 360 has the same rule (`0x82141e70` steps the effects of player 0 only). In mode 11 the second renderer frame runs that same cgame
step again, so the effects system was stepped **twice per engine frame**: particles lived one step ahead of the frame that drew them, which is how smoke and a
muzzle flash ended up on the wrong half (the owner's screenshot). The step is called with the cgame's local client, not the renderer's local player, so pinning
the renderer's player does not touch it.

Fix: hook `0xbccf0` and skip the step while player 2's renderer frame is under way (`fxclock=1`, default; `fxclock=0` off). Measured (D3D9, Trainer, the smear's
view): no crash, both halves complete and lit with the effects drawn where they belong (`scratch/fxclock.png`). Not yet checked: a run with an effect actually
firing in both views at once (a grenade or a weapon at a fixed spot), and the same on the native Direct3D 11 renderer.

### `fxhalf`: one explosion, both halves, checked in the game (2026-11-15 night)

QA scenario `mw2coop_qa=fxhalf` (in `content/scripts/maps/_mw2coop_qa.gsc`): skips the briefing, waits 5 s, computes a spot 250 units in front of player 1, calls
`RadiusDamage(spot, 200, 200, 50, level.player)` so the engine spawns its own explosion effect there, then reports the spot. The trainer's zone registers no
explosion effect by name (`level._effect` has none of the names the packed zones carry), so the engine's own damage effects are the only reliable trigger in it.

Result (D3D9, mode 11, 1280x720, the smear's camera, `shot11=1500;shot11step=40`): captures `m11-1500` and `m11-1580` show **the same explosion in both halves at the
same stage** — same plume shape, same size, no age offset between the views, HUD scaling applied equally. With the effects clock advancing once per engine frame
(`fxclock=1`) the particle system no longer runs a step ahead on one side. Not checked yet: an effect that keeps emitting for longer than one screen update (a smoke
grenade over several seconds), and the same on the native Direct3D 11 renderer.

### Each player's 2D in his own half: the engine's per-client placement (2026-11-16, `exp11place`, built, NOT yet run)

Phase B's open item: player 2's HUD was off (`exp11no2d`) because every 2D command drew with the full-screen placement, and mode 10's half placements (`place_push`, three
structs written by hand) belong to the compositor. The PC engine already has the 360's mechanism:

- `0xdaf10(lc)` is what the cgame's 2D code calls (about 150 call sites) to get its screen placement. With the mode word `0x5e0ccc == 1` it returns the per-client placement
  `0x5e0c60`, otherwise the full-screen one `0x5e0bf0`. The mode is 1 from `0xdad10` (called at `0xbca35` inside `CG_DrawActiveFrame`) until the screen update
  sets 2 (`0xdaf00` at `0xd5aa0`) after the draws; 0 is set by `0xdad20` at `0xd592a` before them.
- `0xdb0b0(placement, x, y, w, h, screen w, screen h)` sets a placement for a viewport at (x, y): the three scales, the safe-area rectangles, the viewable
  rectangles. `0xdb060` calls it for the whole screen (`0x5e0c60`, `x = y = 0`, w and h from `0x548bfc` / `0x548c00`) at the video start (`0xd3d00`) and restart (`0xd4a50`).
- The 360 sets its per-client placement to the half of the player whose view is being drawn, so the hand-written structs of mode 10 are not the 360's way.

Port: `place_client_set` (64-bit) calls `0xdb0b0` on `0x5e0c60` before each player's `cg_draw_original`: player 1 the top half, player 2 the bottom half (`y = h / 2`), and the
whole screen again after player 2's draw. Player 2's 2D is on (`exp11no2d` now defaults to 0 while `exp11place` is on), and the console's split HUD layout (`hud_layout_tick`,
the items of the PC's HUD menus moved like the disc's `hud_splitscreen.txt`, `local/xbox360-ref/hud_ss_*.txt`) now runs in mode 11 as in mode 10 (`hudss=0` off).
`[native] exp11place=0` gives the old behaviour (player 1's HUD placed for the whole screen, player 2's off).

To look at in the first run: the HUD of each player inside his half (ammo bottom right of the half, compass, hints), no HUD of player 2 over player 1's half, the text size
(the half's own scale), and the global texts (subtitles, objective) that both contexts draw.

### Checked: player 2's HUD in his half, and why his 2D first painted the half black (2026-11-16, D3D9, Trainer)

First run with `exp11place`: player 1's half fine, **player 2's half completely black**. Narrowed with QA switches (all kept, off by default):

| Test | Result |
|---|---|
| `exp11no2d=1` (his 2D off, placement on) | both halves fine: the 3D and the placement are not the cause |
| `exp11cycle=6` (placement + 2D, whole-screen placement + 2D, placement + no 2D, every 6 s) | black in both 2D-on states, fine with it off: the content of his 2D, not the placement |
| `skiprot=5` in mode 11 (each of 11 HUD parts skipped in context 1 in turn) | black in every window: not one of those parts |
| `d3dtrace=1` | same structure of the frame with 2D on or off; only 13 more 2D draws after the post-process composite |
| `quadlog=1` (back end 2D quad `0x3cff0`) | a half-screen quad of material **"black"**, colour `ffffffff`, drawn after the post-process in his frame |
| `hudelemlog=1` (hook `0xa3b80`, colour as `0x73eb0(elem, time)` gives it) | both contexts draw the script's `black` HUD element (fade start `0x283c` = 10300 ms, duration 2000 ms); in context 0 the client time is 15514 and the alpha 0, in context 1 the client time is **450** and the alpha 255 |

Cause: `cl.serverTime` (rva `0x51dca8`, inside the swapped cl range) is written only by `0xbc1a0(serverTime, ...)`, which the screen update (`0xd5890`, call at `0xd58dc`) calls
right before the cgame step `0xbc170` (`0xd58e3`). In mode 11 player 2's screen update never runs (busy flag), so his `cl.serverTime` stayed at its first value (450 ms). Every
HUD computation from that time was wrong: here the intro's black fade-in never started in his view. On the 360 the same pair belongs to the per-local-player step.

Fix (`p2time=1`, with `p2step`): `0xd58dc` is hooked to remember the time the screen update sets, and player 2's step writes it to his own `cl.serverTime` before it runs
(`cg_step_hook`). Checked: his time follows player 1's (15550 / 15563), the black element's alpha is 0 in both contexts, and the capture shows both halves live with player 2's
HUD text ("Pick up your weapon from the table", the Trainer's first hint) at the top left of **his** half. The 2D of the HUD menus follows the per-client placement. Not yet seen:
the ammo / compass items (the Trainer has no weapon at that moment), player 1's HUD in the top half, other levels, the native Direct3D 11 renderer, global texts (subtitles, objective)
shown in both halves. `0xbc1a0` also calls `0xb8d30` (snapshot transition), `0x7b60`, `0x8daf0(0)`, `0xa5850` and `0x111c30(0, time)`: only the time is given to context 1 for now.

New QA switches: `exp11cycle=<s>`, `skiprot=<s>` (now also in mode 11), `clearlog11=1`, `quadlog=1`, `hudelemlog=1`.

### Player 1's HUD and post-process were never drawn: his frame had no "show" bit (2026-11-16, `exp11p1show`, checked)

With player 2's HUD fixed, a QA scenario (`mw2coop_qa=hudtest`: a global HUD element, one only for player 1, one only for player 2) showed player 1's half without any 2D: no
HUD elements, no ammo, no compass, and no post-process either. In the traced frame pair only the second frame has the post-process composite and any 2D draw (z = 0).

Reason (back end, `0x453c0` and `0x45060`): the per-scene post-process composite (`0x484c0`) and the 2D (`0x5c010`) run only for a frame whose flags have bit 1 (`& 2`), and the same bit
makes `0x45060` Present. `exp11frames=2` ended player 1's frame with `0xc4c0(1)`, begin without show, as on the 360's end-frame `0x82390e20(1)`; on the PC that also leaves his frame
without HUD and post-process, so the HUD that was visible in his half in the old single-frame design was lost with the per-player frames. Player 2's frame (flags 3) had everything.

Fix: player 1's frame is ended with `0x83` (bit 0x80 only marks it; no code tests it). The back end's call of `0x45060` (at `0x45757`) is hooked: it strips the mark and, for the
duration of that call, the Present hooks skip the Present (`be_skip_present`), so one picture is shown per engine frame and the engine's own frame bookkeeping (the present
queue, `0x45060`'s fence) still runs for both frames. Direct3D 9 only: on the native renderers (`natboot_active`) the old flags stay (not checked there). `exp11p1show=0` off.

Checked (D3D9, Trainer, `hudtest`): the top half shows "GLOBAL ELEM", "PLAYER ONE ELEM", the M4A1 ammo count and the compass, the bottom half "GLOBAL ELEM" and "PLAYER TWO ELEM";
no crash. Each half shows the global element once and its player's own element only in his half.

### The global texts once, as in mode 10 (2026-11-16, `msgwin_hook` in mode 11, checked)

The mode 10 design drew the game's global texts once (docs/native-splitscreen-plan.md, "Global texts once"): the game message windows (rva `0x52f348`, one set outside the swapped
ranges, drawn by `0xcb4f0(local client, window, x, y, ...)`) are drawn by both contexts, so the subtitles (window 2, console channel "subtitle") and the notices at the top left
(window 0, "gamenotify obituary": a new objective, "Pick up your weapon from the table") came twice. The hook only ran in mode 10. Mode 11 draws the windows in both views of a
frame in the same way, so the hook now runs in mode 11 too: context 1 drops windows 0 and 2, the texts are in the top half only, as the 360 has them (its `subtitles_ss` menu is
visible for local player 0 only). `[native] subonce=0` / `subtop=0` as before.

Checked (D3D9, Trainer, `msglog=1`): per context each of windows 0, 1 and 2 is called once per frame; the drops are all of context 1's windows 0 and 2 and none of window 1 or of
context 0. The captures show the subtitle at the bottom of the top half and the objective notice at its top left, nothing in the bottom half.

Decided against porting from mode 10 (and why): the intro's title lines (`hudelem_hook`, sort 1.0078125) were drawn once over the whole picture there because the compositor
could lay one layer over both halves; in mode 11 each half has its own 2D viewport, so each half shows the title at its own half's scale, which is the console's way for a view
without a shared layer. The layer comparison (`dedupe`) stays off. Window 1 ("boldgame", the bold centre messages) stays in both halves as in mode 10. Not checked in mode 11:
the "Follow" waypoint hint in both halves (open since mode 10), the name tags (`namefix`, installed in every mode), the native Direct3D 11 renderer.

### The size of the 2D in a half: `hudsplitscale` (2026-11-16, from the 360, checked on D3D9)

Owner: the HUD is barely visible. The engine's placement scales by the viewport's height / 480, and a half is 0.5 of the screen high, so every 2D element of a half was half the
size of the single picture's. The 360 enlarges the HUD of its split views: `cg_hudLegacySplitscreenScale` (registered in `0x82140e60`; default 1.5, minimum 1, "Screen scale for hud
elements in splitscreen"), with `cg_hudSplitscreenOffsetsUseScale` (default 1: the offsets scale too). `0x82140de8` keeps the active factor in `0x8266be6c` (the dvar while the
screen is split, else 1.0) and the console's `*_ss` menu items flagged `LEGACY_SPLIT_SCREEN_SCALE` (`local/xbox360-ref/hud_ss_menus.txt`) use it; the 360 also has per-element scales
(`cg_hudSplitscreenCompassScale`, `...StanceScale`, `waypointSplitscreenScale`, `con_gameMsgWindow%dSplitscreenScale`, default 0.85).

Port (`place_client_set`, `[native] hudsplitscale=<percent>`, default 150, 100 off): after the engine's `0xdb0b0` has set the half's placement, its scales (`[0]..[3]`) are
multiplied by the factor, the real-to-virtual scales (`[4]`, `[5]`) divided, the virtual rectangles (viewable `[10..13]`, adjustable `[18..21]`) shrink by it and the 4:3 centring
(`[26]`, `(w - 640 * scale) / 2`) is made again, so every 2D element of the half (HUD menus, script HUD elements, message windows, the names over players) grows from its own anchor.
The constants were read from the exe (640, 480, 4/3). Checked: Trainer `p2away` frames, ammo, compass, subtitle and the names (Alpha, Bravo) readable in both halves. The names are
now large (their own size option `nametagscale` is 70 %): the 360 has per-element scales below 1 for some elements; tune `hudsplitscale` or `nametagscale` after a look at the owner's screen.

"The environment disappears" (owner's second report) was looked for on Trainer `p2away` and on Favela's intro with player 1 moving: no missing world in 8 captures. The only matching
older report is "flying glasses, half of the environment disappears" (2026-11-04, the old weapon-FOV/body copy). Still needs a level and a moment from the owner.

### The intro's title lines once, over the whole picture, as in mode 10 (2026-11-16, `exp11titles`, owner's request, checked on D3D9)

The section above kept the title lines per half; the owner asked for mode 10's way: one copy at single player's size and place. Mode 10 drew them in context 0 with the full-screen
placement and laid the layer over both halves. Mode 11 has no compositor, but each half's 2D is placed **relative to its own viewport** (found with the quad and trace logs: the black
quad was at y 0 of player 2's viewport `0,360`), so a whole-screen layout can be drawn in both halves and each clips its part:

- `hudelem_hook` (the HUD element draw `0xa3b80`) catches the elements with sort `1.0078125` (the `_introscreen` patch's marker) in both contexts; per call it sets the **whole-screen**
  placement (`place_client_set(0, 1)`, no `hudsplitscale`), draws a copy of the element and puts the half's placement back;
- player 2's copy has its y (dword 2 of the element) moved by -240 virtual units: half the screen over a scale of height / 480, whatever the resolution, so the part of the layout in
  the bottom half lands at the same screen place; player 1's copy stays where it is.

Result (Favela intro): on the black screen the title lines stand once, at the bottom left of the **whole** screen, at single player's size; during the car scene they run across the bottom
half in the same size. `[native] exp11titles=0` gives each half its own title again (half scale).

What did not work first, kept so it is not tried again: drawing the collected copies in the screen update's final 2D section (the call of `0x94740` at `0xd5a7a`, after `0xe900`, the
placement mode 0): the calls ran and the elements' alpha was 255, but nothing appeared. Neither the HUD menus painted there nor the title text showed in that section in mode 11
(the engine's final 2D after a split frame is not where the halves' 2D goes). QA: `titleshot=1` saves every 80th frame that has title lines.

### Checks after the 2D work (2026-11-16)

- **1920x1080, Trainer (D3D9):** the intro's five title lines stand once at the bottom left of the whole screen at single player's size (the -240 virtual shift of player 2's copy is resolution
  independent as computed); both halves are live. `hudsplitscale` and the titles hold at 1080p.
- **Native Direct3D 11 (`MW2COOP_DISPLAY="directx11=1"`, captures with the console `!shot`, since `shot11` is a Direct3D 9 Present feature):** the title lines draw once over the whole
  picture there too (first capture), but after the intro **the top half is black and the bottom half shows player 2's view** (name "Alpha" over player 1 in his view). This is the known
  native mode 11 failure of the earlier notes (the top half cleared or never drawn); `exp11p1show` is Direct3D 9 only on purpose (the native frame end is the mod's own, `natframe2.c`), so
  player 1's frame there still has no show bit and no 2D. Mode 11 on the native renderers needs its own pass; the Direct3D 9 path is the one checked today.

### Bottom half: aspect (FOV) and the partner's choppy animation (2026-11-16)

- **FOV (`exp11aspect`, fixed):** `RVA_CG_ASPECT` (`0x43c640`, read by CG_CalcFov) lives in each context's cgame block. Player 1's draw was given the half's aspect 3.5556 (live block = context
  0); player 2's block kept the whole screen's 1.7778 (logged: "was 1.7778"), so his field of view was built for another shape. His block gets 3.5556 after `ctx_swap(1)`.
- **Partner's body animation (owner: "partner's body" looks like a lower frame rate in the bottom half, the environment fine):** measured, not reproduced. Context 1's `cg.time` /
  `cl.serverTime` advance every frame (5-9 ms steps, `timelog=1` now logs context 1 too), and two 60-frame bursts (`burst11`, consecutive presents) show both bodies changing in every
  frame, no every-other-frame pattern. The Trainer's intro keeps the players frozen, so no walking could be driven by `fakepad0`, the pad dvar or `press` in these runs.
  Likely cause (not proven): with `centshare` the slot 0 yaw and pitch (the torso/head aim, floats 9 / 41 / 72 before each yaw) are the entity state's, refreshed only at a snapshot
  (20 a second); the earlier copy (`p0_put`: `yawkeep`, `pitchkeep`) put player 1's playerState values in every draw, and `lag_fix_yaw` only fixes the yaw. `p0aim=1` (default) now does
  that for the second entity pass (pass2_hook) from player 1's client block in the context-0 store. Checked only that the body is still drawn right; needs a play test with the partner
  looking up and down and turning. QA kept: `burst11=<present>;burst11n=<n>`, `burst11=-2` (armed by the dvar `mw2coop_qa_burst`), `animlog=1`, `p2away` with `mw2coop_qa_p1walk`.

### The popup friendly name ("Sgt. Foley") in both halves (2026-11-16, `exp11friend`, owner's report, checked)

Owner: the name of Sgt. Foley shows in both halves when the top player looks at him. The popup name under the cross hair is drawn by `0x94e00(local client)`, called from the HUD
(`0x93ac0`, call at `0x93cf0`) once per cgame draw, with local client 0 in **both** contexts. It reads the name the game's server put in two configstrings per local client,
`0xc + 2 * client` and `0xd + 2 * client` (the 16-bit string index table at rva `0x5d8d02`, read through `0xca1e0`); the PC even registers `friendlyNameFontSizeSplitscreen`. So player 2's
draw showed the strings of client 0, the ones player 1's cross hair had set, at his own cross hair. Repro: QA `mw2coop_qa=foleylook` (player 1's view is held on the first friendly actor).

Fix: the call at `0x93cf0` is hooked; in context 1 the entries of client 1 (`0xe`, `0xf`) are swapped into `0xc`, `0xd` for the duration of the call and swapped back. Checked (D3D9, Trainer):
with player 1 on Sgt. Foley the name shows at the top half's cross hair and not in the bottom half. Not checked: whether the server fills `0xe` / `0xf` when player 2's cross hair is on a
friendly (his half shows no name for him until it does). `[native] exp11friend=0` off.

### What the 360 does for the HUD in a split screen, and what the PC mod now takes from it (2026-11-16 night)

**Two layers, as on the console.** (1) The HUD *menus* (`ui/hud_splitscreen.txt`, 22 `*_ss` menus, all flagged `LEGACY_SPLIT_SCREEN_SCALE`) are enlarged by `cg_hudLegacySplitscreenScale`
(default 1.5): the PC mod does this through the per-half placement (`hudsplitscale`, default 150). (2) The HUD elements that *scripts* make (timers, the AC-130's text and cross hair, counters)
are **not** enlarged: the scripts adapt themselves with `IsSplitscreen()` (AC-130: font scale 2.5 -> 1.5, cross hair 80 %, the short right text block `AC130_HUD_RIGHT_BLOCK_SHORT`, the HUD
blocks moved in; the Spec Ops timer at the top of the gunner's half; the download counters of "Download"; no minigun shells and fewer fx in the chopper mission; the sound of the snowmobile
races once). On the PC `IsSplitscreen()` is false in the native split screen (the mod's dvar `mw2coop_native_split` stands in for it), so those branches never ran.

Done:
- **Scripted HUD elements keep the half's own scale** (`hudelem_hook`: the placement scale is 1.0 for their draw, `[native] hudelemk=1` gives them `hudsplitscale` too). The 1.5 scale on top of the
  scripts' own adaptation had cut the AC-130's right text column at the bottom of the half and overlapped the timer.
- **The 360's script branches are taken in the native split screen**: new patches `_ac130.gsc` (2 hunks), `so_ac130_co_hunted.gsc`, `so_download_arcadia_code.gsc`, `so_snowrace_code.gsc` (the fog-clear sound once
  instead of for each player: Vlad's "player 2's sound overlaps with a delay" for the races, if it is this sound), `so_chopper_invasion.gsc` (2), `so_assault_oilrig.gsc`, each with `|| GetDvarInt( "mw2coop_native_split" )`.
  `tools/check_patches.py` applies them to the extracted scripts: all hunks ok. QA runs need `set mw2coop_native_split 1` (the lobby launch sets it; `MW2COOP_PRE='set specialops 1;set mw2coop_native_split 1'`).
- **Names over the other player** ("Alpha", "Bravo") use the dvar `coopNameFontSize` (the loop at `0x93ac0`), which `nametagscale` never touched (only the popup friendly name pair): `guards.c` scales it now.
- **Checked (D3D9, 1280x720):** AC-130: the gunner's HUD fits the top half (text small, short right block, cross hair at its centre, weapon list at the bottom, timer apart), name tag small; `so_bridge` both halves have their minimap
  (the owner's screenshots without it were not reproduced: the HUD may have been fading in); a Spec Ops sweep of all 25 `so_*` levels: no crash, script errors only in `so_ghillies` and `so_showers_gulag`.
- **Not bugs of the mod:** `so_ghillies` is not an operation of the console game (`give_loadout_specialops` lists `so_hidden_so_ghillies` only: `so_ghillies` falls to "No level listing ... giving default guns", whose
  coop path precaches weapons its zone lacks); `so_showers_gulag`: `gulag_code::player_riotshield_threatbias` adds to `level.player.threatbias`, which a Spec Ops start never sets (the 360 has the same script).
- **AC-130 shell aim (Vlad, mode 10: "the game takes the centre of the whole screen"):** with the gunner's trigger held (fake pad) in mode 11 the shells land near the cross hair at the centre of his half (a bit off while the
  plane turns), not at the half's lower edge: not reproduced, the old compositor layout's problem.
- **Snowrace camera / jitter:** player 1's view yaw changes while the sled is driven (the camera turns). A frame-to-frame jitter log exists now (`[native] viewjit=<s>`: origin, pitch, yaw of both contexts for 150 draws); the
  run ended the mission at 24 s without input, so no jitter numbers yet.

- **Snowrace jitter at a standstill (`viewjit=4`, so_snowrace2_cliffhanger, during the start countdown, 1280x720, 210 fps):** for 150 draws of each context the player state's origin, pitch and yaw do not change at all
  (step sd 0.001 units / 0.000 degrees; frame time 4.69 ms, sd 0.6): the view is built from steady data, so Vlad's jitter at the spot is not reproduced in mode 11 (it can be the old compositor's alternate frames, or the sled after
  the start, which this test does not cover).

### Campaign scripts against the 360 (2026-11-16)

The console game has no campaign split screen (only Spec Ops), so the campaign scripts are the stock ones. Compared all 20 campaign level fastfiles (360 against PC, RawFile assets, line ends ignored) and the shared
`common` scripts: 48 level files and 14 common files differ, and **none of the differences is a split screen change** (PC: prone-key hints `is_command_bound`, `forceXPos` of `get_countdown_hud` / `get_download_state_hud`,
precache `&"..."` forms, `triggerer` instead of `level.player` in the grenade pickup, achievements, the credits, the dog move).
- `IsSplitscreen()` is used in the campaign by `_slowmo_breach` (6 levels) and `oilrig/_underwater` only, and by `_coop.gsc` (`level.revive_hud_base_offset` 75 in a split screen, 120 otherwise). All three are already covered
  (`_slowmo_breach.gsc.patch`, `_underwater.gsc.patch` with `mw2coop_native_split`; `_mw2coop.gsc` sets the revive offset 75).
- Scripted HUD elements of the campaign (trainer hints at TOP +105..175, dcburning percentages TOP +30, minigun hints TOPLEFT/TOPRIGHT +50, Cliffhanger icons TOP +175, Predator text CENTER -175 / -120, Estate download
  counters) all sit inside +-240 of the 640x480 HUD space, so they fit a half at the half's own scale (see `hudelem_hook`); nothing to move statically. Which of them are drawn in both halves or in one still needs a
  tour in the game (`hudelemlog`).
- Tools: `local/xbox360-ref/camp-work/{360,pc}/<level>/` hold the unpacked scripts (local only, never shipped).

### Where else the 360 divides into two players: string survey of its single player executable (2026-11-16)

Method: every printable string of `default_sp.pe` (the 360 SP/Spec Ops executable) and `common.zone` that mentions split screen, co-op, a local client or a controller, compared with the PC `iw4sp.exe` and with the mod's sources
(names only; no Ghidra run yet, the game was running). Observations, not yet read in code.
- **Not in the PC executable at all** (the console's split screen layer): `cg_coopSplitscreenView0X`, `View1X`, `ViewWidth`, `ViewHeight` ("2 player split screen coop view 0 x / 1 x / width / height": the console's own view rectangles,
  defaults not read yet: an X and a shared width/height, no Y), `cg_hudSplitscreenCompassScale`, `...CompassElementScale`, `...StanceScale`, `...ScoreboardScale`, `...BannerScoreboardScale`, `cg_hudSplitscreenOffsetsUseScale`,
  `cg_splitscreenSpectatorScaleIncrease`, `con_MiniConSplitscreenScale` (the mod keeps `cg_hudLegacySplitscreenScale` as its `hudsplitscale`), the controller layer (`CL_EnableSplitscreenControls` / `CL_DisableSplitscreenControls`
  with `cl_multi_gamepads_enabled`, "mapping controller %i to localClientNum %i", "Discarding input from controller %i since the mapped client (%i) isn't active", `controllerremoved_ss`), `stopControllerRumble`, the per-player
  UI (`coopPlayer( clientNum, field )` expression is in the PC build too; `coop_player_location*`, the `eog_notify_*_coop1/2` end-of-game menus, `ui_diff` / `ui_diff2`, `mpopmenu_specialops_splitscreen_difficulty_p1/p2`).
- **In both** (so the PC engine already has the layer): `waypointSplitscreenScale`, `con_gameMsgWindow%dSplitscreenScale`, `friendlyNameFontSizeSplitscreen`, `coopNameFontSizeSplitscreen`, `r_scaleViewport`, `resetViewport`,
  `splitscreencontrols`, `splitscreenplayercount`, `IsSplitScreen`, `IsCoop`, and the whole network co-op layer (`coop_start`, `coop_maxFramesClientCanBeBehind`, `setNetCoOpDifficulty`, "exponential moving average smoothing factor for
  anims in network coop", `so_character_select_coop`).
- **Per-player rules the console keeps that the mod handles in its own way:** the difficulty of each player (`g_gameskill_player_1` / `_2`, saved per controller profile; the mod's scripts use the same dvars), profile updates only for local
  client 0 ("UpdateGamerProfile ... not localClient-0"), input from a controller whose client is not active is dropped (the mod's `p2pad` path), "Weaponview linked entity is not in local client #%i's snapshot" (a per-client snapshot check).
- **To read in the 360 code next (Ghidra, when no copy runs):** the defaults and users of `cg_coopSplitscreenView*` (the console's exact view rectangles: they may give the half sizes and the order top / bottom without the
  letterbox guesses), `r_scaleViewport` / `resetViewport`, the sound listener per local client (the "player 2's sound is half a second late" report), per-client rumble, and the controller to client map.

### Read in the 360 code: the console's two view rectangles, the controller map, the sound listeners (2026-11-16)

(`default_sp` image, flat: address = 0x82000000 + file offset; `local/scratch/x360/strrefs.py` finds the code that loads a string, `DecompAt.java` decompiles.)

**The view rectangles are not full-width halves.** `0x82110a70` registers four float dvars; defaults read from the image: `cg_coopSplitscreenView0X` 0.0, `cg_coopSplitscreenView1X` 0.2, `cg_coopSplitscreenViewWidth` 0.8, `cg_coopSplitscreenViewHeight` 0.49
(minimum 0, maximums 1.0 / 1.0 / 1.0 / 0.5). `0x82111cf8(lc, view, count)` turns them into the cgame view: with two views, view 0 = x 0, y 0, w 0.8, h 0.49 of the screen, view 1 = x 0.2, **y 1.0 - 0.49 = 0.51**,
w 0.8, h 0.49 (fractions of the display, pixel = fraction * size + 0.5, truncated), and the view's aspect is `(w / h) * display aspect` (16:9 -> 2.90, not the 3.55 of a full-width half). With one view it uses the whole-screen rectangle.
Nothing in the scripts or zones overrides the dvars (none of the 360 zones mention them), so the console's picture is two staggered 80 % wide boxes: top left and bottom right, a 2 % gap between them. `0x82120800` loops over the two local
clients, skips inactive ones, and numbers the views in order (so a single active client gets the whole screen).
What it means for the mod: the mod's halves are 100 % wide and 50 % high (aspect 3.55 at 16:9), i.e. a flatter vertical field of view than the console's 2.90 and no staggering. Not changed yet: it is a look the owner has to choose (see below);
the cgame view rectangle (`0x43c630`) and `exp11aspect` are where the PC side would take it.

**The controller to client map** (`0x82141280`): with the split screen controls on (`DAT_82411250`, set by `CL_EnableSplitscreenControls`, `0x82141608`; both controllers' "ready" flags `DAT_82648ab8`) the table `DAT_8266be70[controller]`
gives the local client; otherwise only the active controller maps, to client 0. A controller whose client is not active has its input dropped ("Discarding input from controller", `0x82128e28`). The mod's pad routing (`p1pad`, `p2pad`)
is the PC counterpart; nothing to port.

**Sound listeners:** the console keeps one listener per local client (struct 0x3c bytes at `0x83bf1f0c`, active flag at +0x38, a function that clears a client's listener at the end of its frame, `0x8236a4a0`) and a spatialised sound
plays only if some listener is active (`0x82363920`). The PC engine has the same two listeners (`SND_SetListener`) and the mod already feeds listener 1 from context 1 (see the `sndlog` notes above), so the listener count is not
the cause of the reported delay of player 2's sounds; the next suspect is when the sound is started (the cgame's own frame for player 2 starts world sounds one engine frame later than player 1's), not read yet.

Not found: users of `r_scaleViewport` (no code reference in the image; probably a table entry), per-client rumble (`stopControllerRumble` is called from the `resetViewport` command handlers, `0x82134728` / `0x8213ba44`).

### The console's split screen menu flow, and what the mod takes from it (2026-11-16)

Read from the 360's `ui.zone` (names and actions only; the item layout is a binary menu asset, not parsed): Spec Ops -> "Two Player Split Screen" (`@MENU_SP_TWO_PLAYER_SPLIT_SCREEN`) runs
`setdvar splitscreen 1; exec startsplitscreensignin` and opens `menu_splitscreensignin` (two player cards: slot 0 and 1, each with "Join split screen game" / "Leave split screen game",
"Sign in profile" / "Change profile", "Waiting for more players", "Start game"; Back runs `nosplitscreen`, `endsplitscreensignin`, and returns to `main_specops`); then
`menu_splitscreen_setup` (onOpen: `focusFirst; setdvar specialops 1; setdvar g_gameskill_player_1 1; setdvar g_gameskill_player_2 1`; Back opens `leave_lobby_warning_splitscreen`): the Spec Ops lobby
with **one difficulty button for each player**, `popmenu_specialops_splitscreen_difficulty_p1` and `_p2`.
The mod already has the cards (Alpha / Bravo with the picture of each device, "press a button on your device to join") in its split screen lobby. What it lacked in the left column (Start, Select
operation, Friendly fire) were the two difficulty buttons: the whole co-op campaign used the host's single pick.
Done: two rows "Difficulty, Alpha: <level>" / "Difficulty, Bravo: <level>" under Friendly fire in a split screen campaign lobby (access.c makes them, menus.c lays them out and shows them only there; a Special Ops
split lobby keeps the game's own difficulty row), each opening a popup of the four levels (`mw2coop_skill_p1` / `_p2` in `menus.menu`), the pick is `mw2coop_split_skill <player> <0-3>`; the defaults are the profile's
difficulty; the pick reaches the scripts as the flagged dvars `mw2coop_skill_p1` / `_p2` and `maps\_mw2coop::player_gameskill` returns it for that player (so each player has his own damage and
aim assist rules, and the level plays the higher, as the game does); an online session clears them. Checked in the game (1920x1080, lobby by the QA path): the rows show with the right levels, the popup opens with the four names and title "Alpha: difficulty"; Alpha Recruit and Bravo Veteran reached the level scripts as `players 0 3` (QA scenario `skill`). The leave warning (`leave_lobby_warning_splitscreen`) and the 360's separate sign-in screen are not rebuilt (the cards with the devices are the mod's).

**The console's view boxes in the mod (2026-11-16):** `[native] viewlayout=1` (Settings > Enhanced > "Console-style view boxes", read at each split screen start) makes mode 11 draw the 360's two boxes (view 0: x 0, y 0, 80 % x 49 %; view 1: x 20 %, y 51 %): the engine's view rectangle, the pixel rectangle of the cgame view, each cgame's aspect (2.90 on 16:9), the per-half 2D placement and the intro title shift all follow `exp11_view()`. Checked on Trainer at 1280x720: both boxes render, the name tags and the subtitle are placed in their box. Not checked: the intro titles across the boxes, the AC-130 HUD, 4:3 / 21:9 screens, the native DirectX 11 renderer (mode 11 is D3D9 only for now).

### Partner body animation, second look (2026-11-16, D3D9, 1280x720, ~80 engine frames a second): still not reproduced

Owner (screenshot of the bottom half, Alpha's body with a pistol, "all animation frames look like 10-15 fps, the environment is normal"). Boneyard with `p2away` (player 2 still, player 1 placed in front of him), player 1 walking on the
pad dvar (`mw2coop_qa_p1walk`; new `mw2coop_qa_p1circle`: forward plus a turn, a circle) and a 90-frame burst of consecutive presents (`burst11=-2;burst11n=90`): the bottom half's pixels in the body's area differ between **every** pair of
consecutive frames (500-2700 pixels over a threshold, no pair under 150, no every-other-frame or 1-in-5 pattern), with `p0aim=1` and with `p0aim=0` alike; the sky strip of the half does not change at all (player 2 stands still), so the changes are
the body. `[native] animrate=1` (new): the hash of the first eight animation nodes of the other player's body changes in 100 % of the draws, in both contexts. So at this frame rate the body is redrawn with a new pose every frame; what the owner sees
is not that. Not tried: a slower frame rate (60 or 30 a second: the animation clock `fxclock`-style per engine frame may matter there), the native DirectX 11 / 12 renderers, FSR 3 / DLSS (their motion vectors for skinned bodies), mode 10. Need from the owner: the
renderer and upscaler in use, the frame rate in that half, and whether the body is idle, walking or turning when it looks like 10-15 fps.

**Found and fixed (2026-11-16, later): the partner's body turned in steps.** `animlog` now prints the pose (cpose origin +0x18, angles +0x24) per draw: with player 1 turning (fake pad), his body's yaw in player 2's view changed in 1 of 7 draws (about 20 a second: the snapshot rate). `p0aim` wrote the yaw and pitch from the client block's playerState (`CL_BLOCK_PS`), which is the last snapshot's; it now takes context 0's `cg.predictedPlayerState` (rva 0x422440, from the store), built every frame, as the 360's own-entity write (0x82102f28) takes each local player's interpolated state. The yaw now changes in every draw (59 of 59, about 0.016 degrees each). `[native] p0aimsrc=0` puts the old source back. The owner recognises the rest as the older NPC jitter ("airport"), measured next.

**NPC jitter in mode 11 (owner: "the same bug as at the airport"), measured 2026-11-16:** Airport, D3D9, 1280x720, `npclog=1;npctype=13;npcwait=25` (the actors as each view's draw placed them; `npc_log` now also runs after player 2's draw in mode 11). Walking AI actors: zero-step draws 0-2 % and speed cv 0.10-0.26 in **both** views (9 actors), the same as the plain game measured on 2026-11-02 (0 %, cv 0.15-0.2) and unlike the old split (20-35 %, cv 1.1-1.8). `entring`: 0 late snapshot reads of 860 000. So the old position jitter is not back in mode 11; what the owner sees as "the same bug" is not this measure (animation of limbs, or a build without these fixes).

**The partner's body leaned over with the player's view (owner's Airport screenshots, 2026-11-16):** player 1 looking down: his body in the bottom half folded to the floor; looking up: thrown back with the gun pointing up. Cause: `p0aim` (my 2026-11-16 night change) wrote the view's pitch into the slot's pose and state angles, which turn the whole model. The 360's own-entity write (0x82102f28) sets the yaw only (pitch and roll 0). `p0aim` now writes the yaw only (`[native] p0aimpitch=1` the pitch too, as before); the torso's aim stays the game's. Not yet checked in the game with a player looking up or down (the Airport intro holds the view). The search for a worker race on player 2's frame (`exp11wait2`: wait for the renderer workers before the next draw) found none: 0 of 12 000 draws had workers still busy; removed. `npcmax=<n>` (npclog: track only entities up to n): on Airport no walking actor has a number of 134 or lower.

### Smoothness and frame rate of mode 11, measured (2026-11-19)

Owner: "the jitter of the other player and the NPCs is still there" (both builds), and work on the split screen's performance. His settings: Direct3D 9Ex in a
borderless window, MSAA 8x, vsync on a 144 Hz display, no frame cap.

**What was measured** (one copy in mode 11, `tools/qa_native.sh`; `[native] npclog=1` with the new `npcent=<entity>` and a 32-bit form of its line, the camera
in the 64-bit `npccam` line; scripts `jitan.py`, `pacean.py`, `camjit.py`, `fpsav.py` in the local scratch folder; the QA scenario `walkpair`: the players take
turns walking with a real Forward press, the other one watching):

* 64-bit: walking AI actors in both halves (No Russian) 0 % frames without movement, speed cv 0.04-0.26 (the single player range); the partner's body as the other half
  draws it 0 % / cv 0.03-0.18; both render cameras 0 % / cv 0.14-0.16; the two views' cgame times are equal in every frame and follow the wall clock (sd 0.7-0.9 ms).
  The old actor stutter of mode 10 (entring, transfix) does not come back. With the owner's settings at 144 Hz the game holds 144 frames, but a frame takes about
  6.8 of the 6.94 ms: little room before a frame misses the display's refresh.
* Official 32-bit: 32 frames a second on No Russian (19 at worst, 80 % of the frames over 8 ms): the frames were spent inflating sound files (below).

**Fixed:**

1. **The looping sounds' expiry ran in both views' frames (32-bit).** VA 0x49da00 (run from each client frame, VA 0x506b24) calls VA 0x4b24d0, which stops
   every looping channel whose frame stamp is not the previous pass's. Only context 0's entity pass keeps the loops alive (context 1 starts none: `loopsnd1`, the
   doubled emitters of Second Sun), so context 1's pass stopped them all and context 0 started each again in every frame. A looping stream packed in an .iwd
   (No Russian's `emt_airport_traffic1.wav`, `emt_airport_int_flyovers1.wav`) was opened again and set to its play position (`AIL_open_stream`,
   `AIL_set_stream_ms_position`: about 70 and 40 a second), and the game's `FS_Seek` (VA 0x4b2c80) reaches a position inside a packed file by inflating it
   from its start: 300-700 MB every 5 s. Found with `[native] seeklog=1` (the seeks per file, the Miles stream calls by caller and context, the stack of context
   1's stream stops). The pass runs in context 0's frame only now (`loop_expire_hook`; iw4x-sp mapped by the generator, 0x9da00 -> 0xc2e30 checked by its bytes;
   `[native] loopexpire1=1` the old way). The 64-bit build stops no loops this way (its channel stops in context 1 come from new sounds taking a channel).
2. **The sun state of player 1 was held at player 2's.** The render thread writes a view's sun blind / glare state while it draws that view's frame, and player
   2's frame is drawn after his draw has swapped the contexts back: his state went into context 0's copy. QA `sunlook` (Boneyard, player 1 turns once around,
   player 2 stands; field 5 = view forward . sun direction): player 1's value stayed at player 2's 0.181 through the whole turn, in both builds. The sun range now
   stays with context 1 after his draw (`ctx_swap_after_p2_draw`, `[native] sunkeep=0` the old way) and goes back at the next frame's first swap, after the wait.
3. **The 32-bit wait for the render thread** before the sun range moves polled a counter with `Sleep(0)`; it uses the engine's wait now, which runs the workers'
   jobs meanwhile, as on 64 bits (`[native] rtpoll=1` polls again).

After the fixes (one copy, 1920x1080, 45 s runs): official 32-bit No Russian 259-270 frames a second (32 before), Second Sun 95-97, The Pit 94-102; iw4x-sp No Russian 266-274 (243 before); 64-bit with the owner's display settings No Russian 190-196 and Second Sun 125 (unchanged). The Pit gave 166-174 on 32 bits only while player 1's sun state was held at player 2's; with his own state the frame costs more where he faces the sun (measured; that the sun's glare is the cost is inferred, not proven). No crash or hang in about 25 split runs, and context 1 starts no sound (no doubled emitters).

**Left, with the reason:** the mode-11 frame still waits for the render thread twice: before player 2's frame, until player 1's is drawn (`exp11rtwait`: the scene's
model records are cleared for player 2's frame, and the back end reads them while it draws player 1's; without the wait the bodies break into shards), and at the
next frame's first swap for the sun range. Without both, 64-bit No Russian went from 196 to 222 frames a second; a double buffer of the model records would remove
the first. Six context swaps a frame (the cgame block, 306 KB on 64 bits) cost 30-60 ms a second.

**QA tools added:** `[native] npcent=<n>`, the 32-bit npc line, `npccam`; `seeklog=1` (32-bit official exe); `swapsites=1` now gives each call site's worker
wait and copy time; player 2's `sunlog` line in mode 11, labelled by the range's owner; the QA scenario `walkpair`.

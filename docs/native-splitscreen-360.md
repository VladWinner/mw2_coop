# Split screen the way the Xbox 360 game does it: study and plan (2026-11-14)

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

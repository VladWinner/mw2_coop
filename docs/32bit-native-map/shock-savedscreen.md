# Shellshock saved screen ("$savedscreen") in split screen mode 11

Read 2026-11-17 (agent "shock"), static only: no game run. 64-bit = rva in iw4sp.exe (D:\coop-test\mw2-64), 32-bit = VA in image32.bin (rva = VA - 0x400000).
Names in brackets are IW names for orientation, not symbols from the exe.

## The chain (observed)

1. Main thread, `R_RenderScene` (64 0x8210 / 32 0x507b10) stores the command position into the view (64 `view+0x308`, 32 `view+0x2f4`), appends
   command 0x17 (arg 0: 2D projection), then calls the shellshock dispatcher (64 0xbd3d0 at 0x82dd / 32 0x4a31e0 at 0x507b70, cdecl `(int lc)`; mode 11
   passes lc 0 for player 2, `rs_shock_hook`).
2. Dispatcher: `bg_shock_screenType` 1 -> flashed (64 0xb7670 / 32 0x452f30), else blurred (64 0xb75b0 / 32 0x4df4e0). Both take the view rectangle from the
   getter (64 0xbd520 / 32 0x4a6fd0) = the static **vrect** (64 0x3bd808 / 32 0x72f3ec, normalised x, y, w, h; native.c `M11_VIEWRECT`, set per view by `exp11_view`).
3. They write render commands (no stretch-pic call in the cgame):

| cmd | size | layout | writer 64 / 32 | who emits |
|---|---|---|---|---|
| 0x02 save screen | 8 | +4 timer slot (= lc) | 0xe530 (ecx = lc) / 0x50eeb0 (cdecl) | when lc count < 2 |
| 0x03 save section | 0x18 | +4 x, +8 y, +0xc w, +0x10 h (vrect), +0x14 slot | 0xe580 (xmm0..3, [rsp+20] lc) / 0x50ef00 (cdecl) | when lc count >= 2 (never on PC: 64 0x7ac10 / 32 0x407980 return 1) |
| 0x12 blend blurred | 0x1c | +4 fade ms, +8 x, +0xc y, +0x10 w, +0x14 h (vrect), +0x18 slot | 0xe5e0 (ecx fade, xmm1..3 x y w, [rsp+20] h, [rsp+28] lc) / 0x50ef60 (cdecl fade, x, y, w, h, lc) | blurred, every frame while `hasSaved` |
| 0x13 blend flashed | 0x20 | +4 byte, +8 white, +0xc grab, +0x10 x, +0x14 y, +0x18 w, +0x1c h (vrect); no slot | 0xe660 / 0x50efe0 | flashed, every frame while `hasSaved` |

   Blurred (64 0xb75b0): `if hasSaved: cmd 0x12; if skipFlag: return; save (cmd 2 or 3); hasSaved = 1` -> it **saves every frame** (a feedback trail).
   Flashed (0xb7670): `if hasSaved: cmd 0x13 else save once` -> saves **once at the shock start**.
   Flags: 64 0x43b5c4 / c8 (hasSaved) / cc (skip), 32 0x78d5cc / d0 / d4; inside the swapped cgame range 0 in both builds (per player already).
4. Back end, per view, after that view's composite: 64 0x453c0 -> `0x484c0(view)` then **0x5c010(view)** (32: 0x553140 -> **0x553050**, ESI = view). It
   switches to target 1 (back buffer), sets the 2D state's viewport to the view's pixel rect `view+0x160..+0x16c` (64 0x4358e78..e84, 32 0x2163b1c..28),
   mode 0 (64 0x4358e8c / 32 0x2163b30), W/H = target 1 size (64 0x4358e90/94, 32 0x2163b34/38), then runs the view's command list through the handler table.
   The 2D flush (64 0x26a60 / 32 0x523c30 -> 0x542030) sets the D3D viewport to that rect when mode != 1; 0x4ec20 (cmd 0x17) builds the ortho projection from its w, h.
   **So 2D in a view's list is view-local: (0,0) = the view's top-left, extent = the view's size.**

### Handler table (index = type)
- 64: base rva 0x3705b0 (`[type]`, 8 bytes): type 2 slot **0x3705c0** -> 0x41160, type 3 0x3705c8 -> 0x41260, type 0x12 **0x370640** -> 0x412b0, type 0x13 0x370648 -> 0x41430.
- 32: base VA 0x71b198 (4 bytes; the 0x71b19c of inputui.md is type 1): type 2 slot **0x71b1a0** -> 0x536330, type 3 0x71b1a4 -> 0x5363a0, type 0x12 **0x71b1e0** -> 0x5363e0, type 0x13 0x71b1e4 -> 0x536530.
- slot bytes: 64 0x3705c0 `60 11 04 40 01 00 00 00 60 12 04 40 01 00 00 00`, 0x370640 `b0 12 04 40 01 00 00 00 30 14 04 40 01 00 00 00`;
  32 0x71b1a0 `30 63 53 00 a0 63 53 00 50 62 53 00 e0 75 53 00`, 0x71b1e0 `e0 63 53 00 30 65 53 00 50 6c 53 00 90 73 53 00`.
- handlers are `void (cmd **)`, cdecl in 32, advance `*p += size`.

### Capture - type 2 handler [RB_SaveScreenCmd] - 64 rva 0x41160, 32 VA 0x536330
- `if (savedTime[cmd+4] != viewTime) StretchRect(dev, surf[curRT], NULL, level0($savedscreen), NULL, D3DTEXF_LINEAR); savedTime[cmd+4] = viewTime`.
  **Whole current target (target 1 = back buffer, both halves) -> whole $savedscreen, no rectangles.**
- 64: state = `[0x36fc78]`, device `state+0xd0`, curRT `state+0xb98`, src `[0x4359008 + curRT*0x20]`, $savedscreen GfxImage* `[0x4359000]` (target entry 0),
  savedTime int[4?] at 0x4115a60, viewTime 0x4358e48 (= state+0x16e8, copied from `view+0x180` by 0x4f070). StretchRect = `call [r10+0x110]` at 0x41214 (`41 ff 92 10 01 00 00 48 8b 03 48 8b cb ff 50 10`), R8 = src rect (0, `xor r8d,r8d` 0x411d1), [rsp+20] = dst rect (0, 0x411ee).
- 32: helper **0x5340c0** usercall EAX = GfxImage* (`[0x244a300]`), stack (`[0x71e640]`, `[0x71e644]` = state), caller cleans 8; device `state+0x90`, curRT `state+0xb14`,
  src `[0x244a304 + curRT*0x14]`, StretchRect `call edx` at 0x534102 (vtable +0x88). Call site 0x53636f `e8 4c dd ff ff 83 c4 08 8b 4f 04 8b 15 f4 3a 16`.
  savedTime 0x1c8c034, viewTime 0x2163af4.
- bytes: 64 0x41160 `48 89 6c 24 18 48 89 74 24 20 57 48 83 ec 40 83`; 32 0x536330 `55 8b ec 83 e4 f8 83 3d 4c a2 44 02 00 56 8b 75`, 0x5340c0 `51 8b 00 8b 08 56 8d 54 24 04 52 6a 00 50 8b 41`.
- type 3 [RB_SaveScreenSectionCmd] 64 0x41260 / 32 0x5363a0: **only stamps `savedTime[cmd+0x14]`, copies nothing** (PC stub).
- $savedscreen size: the native port (src/dx/natbring.c) makes it display size like target 1; the D3D9 builder 0x39420 was not re-read: likely the same.
- confidence: verified (both decompiles + disassembly)

### Draw - type 0x12 handler [RB_BlendSavedScreenBlurredCmd] - 64 rva 0x412b0, 32 VA 0x5363e0
- `dt = viewTime - savedTime[cmd+0x18]; if 0 <= dt < fade: codeImage = $savedscreen; a = min(0.99, 0.01^(dt/fade));`
  **`DrawStretchPic(mat "shellshock", x 0, y 0, w W*cmd.w, h H*cmd.h, s0 cmd.x, t0 cmd.y, s1 cmd.x+cmd.w, t1 cmd.y+cmd.h, color a<<24|0xffffff)`**.
  The texture coordinates ARE the vrect of the cgame; the quad is placed at the view's origin with the view's size (2D is view-local).
- 64 call **0x413e7** -> 0x3cff0 [RB_DrawStretchPic]: Win64 RCX material (`[0x4115a28]`), XMM1 x, XMM2 y, XMM3 w, [rsp+20] h, +28 s0, +30 t0, +38 s1, +40 t1, +48 color.
  bytes `e8 04 bc ff ff 44 0f 28 4c 24 60 44 0f 28 44 24`. code image slot 0x4358a60.
- 32 call **0x536514** -> 0x533890: material in **ESI** (`mov esi,[0x1c8c018]` at 0x5364f1), 9 stack dwords x, y, w, h, s0, t0, s1, t1, color, caller cleans 0x24.
  bytes `e8 77 d3 ff ff 83 c4 24 8b 07 0f b7 48 02 03 c8`. code image slot 0x2163780. (ESI as material: read at the call; 0x533890's own use of ESI not decompiled: likely.)
- flashed type 0x13: 64 0x41430 call **0x4155c** (`e8 8f ba ff ff 48 8b 06 4c 8d 9c 24 80 00 00 00`), 32 0x536530 call **0x536636** (`e8 55 d2 ff ff 8b 03 0f b7 50 02 83 c4 24 5f 03`);
  same quad and s/t from cmd +0x10..+0x1c; material "shellshock_flashed" (64 [0x4115a30], 32 [0x1c8c01c]) or, byte +4 set, "feedbackreplace" (64 [0x4115928], 32 [0x1c8bf98]), colour from +8/+0xc. No time check.
- cgame emit sites (to change s/t at the source): 64 0xb7603 call e5e0 (`e8 d8 6f f5 ff 83 3d bd 3f 38 00 00 74 15 c7 05`), 0xb7799 call e660; 32 0x4df535 call 0x50ef60
  (`e8 26 fa 02 00 83 c4 18 83 3d d4 d5 78 00 00 74`), 0x453028 call 0x50efe0.
- confidence: verified

### Other users of $savedscreen
- Target entry 0 (`[0x4359000]` / `[0x244a300]`) is put into the code image slot only by the two blend handlers; "shellshock" / "shellshock_flashed" pointers are read only by them.
  The slot itself is shared with post-process passes (64 0x484c0, 0x479e0, 0x47ef0, 0x618e0), which bind other targets (blur 0x61bd0 with targets 4, 9; ping-pong 6/7). verified (references).

## Xbox 360 (default_sp.pe, read only, for orientation; addresses do not carry over)
- 0x8210ace8 (= b75b0) reads per-lc cgame flags (`0x8244c2c0 + lc*0x2f1e4`) and a **per-player rectangle** `0x82111bd8(lc)` (a table by player count); with >1 player it emits
  **command 3** (0x8210ab70 -> 0x82393508).
- Handler table at 0x8205b644 (23 entries, same order). Type 3 = 0x823a9d30: if `savedTime[lc] != time` -> **0x823a7b68: s/t x target size -> pixel rect -> resolve of that
  rectangle only** (0x823a7780). Type 0x12 = 0x823a9de8: the same quad as the PC (0, 0, W*w, H*h, s/t from the command). likely (disassembly read).
- So the 360 gives each player his own timer slot (his lc), copies only his rectangle into the one $savedscreen, and samples the same rectangle.

## What mode 11 does today (concluded)
- s/t are already per view (vrect per draw), and the quad fills the view. Not a coordinate bug as long as vrect is the player's rect at his `R_RenderScene`.
- Both players' commands carry slot 0. Player 1's list saves the whole back buffer (his half fresh, player 2's half = whatever is there before player 2's composite:
  with `exp11frames=2` his last final frame, HUD included). In player 2's list `savedTime[0] == viewTime` (if both views have the same time): his save is skipped and his
  blend gets dt 0, alpha 0.99: his half is covered by that stale picture. If his time is lower, dt < 0 and he gets no blend at all.
- A squeezed whole picture in one half needs s/t = 0..1 (vrect 0,0,1,1) or a 2D viewport that is not the view's; neither found in the static code. `unknown`: check with a QA
  log of cmd 0x12 +8..+0x14 and 0x4358e78..e84 (32: 0x2163b1c..) per view.

## Proposed hook (concluded, the 360's way)
1. **Own timer slot for player 2**: after `rs_shock_original` returns in player 2's call, walk the commands it appended (64 `[0x458fab0]` -> {+0 base, +8 used}, 32
   `[0x1c93268]` -> {+0 base, +4 used}; each `{u16 type, u16 size}`) and set the slot to 1: type 2 `+4`, type 3 `+0x14`, type 0x12 `+0x18`. The cgame keeps lc 0.
   Slots 1..3 have no other reference (64 0x4115a64..6c, 32 0x1c8c038..44).
2. **Section capture**: replace handler slot type 2 (64 0x3705c0, 32 0x71b1a0; .rdata, VirtualProtect) with a handler that in split screen does the same stamp test and a
   `StretchRect(src = surf[curRT], &r, dst = level 0 of $savedscreen, &r, LINEAR)` with `r` = the view's pixel rect (64 ints 0x4358e78/7c/80/84 = x, y, w, h; 32
   0x2163b1c/20/24/28; r = {x, y, x+w, y+h}), and calls the original otherwise. (Equivalent: fill the type 3 stub and patch the count test, 64 `jg` 0xb7630 and in 0xb7d90,
   32 `jg` 0x56ddb9 in the shared 0x56ddb0.) Native DX11/12: `ngfr_stretch_current_to_saved` needs the same rect (nd_stretch takes one).
3. No s/t hook: they come from vrect. Timing: the save sits right after 0x17 and the blend in each view's list, which runs after that view's composite and before its HUD,
   so each half of $savedscreen holds its own player's last frame without HUD, and the other half is not touched (as single player and as the 360).

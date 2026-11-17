# Package: mode 11 2D and HUD (native.c place_client_set, place_push / hudss, hudelem_hook, msgwin_hook, name loop, exp11titles, exp11friend, the mode 11 view rectangle, fxentfix, pass2 in mode 11)

Conventions as in the other files: VA = 32-bit virtual address (image base 0x400000), rva = VA - 0x400000. All bytes are from `image32.bin` (read only).
"verified" = both decompiles (or both disassemblies) compared; "likely" = one side read closely, the other matches in role and neighbours; "unknown" = not found.
Nothing was run. The line numbers are those of `src/native.c` at 9392c82.

## Summary (read this first)

| 64-bit (native.c) | 32-bit VA (rva) | convention / kind | confidence |
|---|---|---|---|
| 0xdb0b0 set a placement (place_client_set) | **0x4a7320** (0xa7320) | cdecl `void (float *pl, float x, float y, float w, float h, int sw, int sh)`, plain `ret` | verified |
| 0x5e0c60 per-client placement | **0x937500** (0x537500), 27 floats, mode dword at +0x6c (0x93756c) | data | verified |
| 0x548bfc / 0x548c00 screen w / h | **0x894374 / 0x894378** (0x494374 / 0x494378) | int | verified |
| 0xdaf10 placement getter (not called by native.c) | 0x448a30 | cdecl `(int lc)` | verified |
| 0xdad10 / 0xdaf00 / 0xdad20 (mode 1 / 2 / 0) | 0x4bb380 / 0x4f6c20 / 0x45be10 | void(void) | verified |
| 0x51e330 cgame UI context (hudss, CG_DC_RVA) | **0x754d28** (0x354d28), menus +0x38, count **+0xa38** | data | verified |
| 0xa3b80 HUD element draw (hudelem_hook) | 0x564b40 (already in inputui.md) | cdecl `(int lc, hudelem *e)` | verified |
| 0x73eb0 element colour (hudelemlog) | **0x44e520** (0x4e520) | cdecl `void (hudelem *e, int time, uint8_t out[4])` | verified |
| 0xb0610 element material name (hudelemlog) | **0x4a5e00** (0xa5e00) | cdecl `int (int lc, int index, char *out, unsigned size)` | verified |
| 0x51dca8 cl.serverTime | 0x86a5b4 (render.md) | int | verified |
| 0xcb4f0 message window draw (msgwin_hook) | 0x4713b0 (inputui.md) | cdecl, the same 12 arguments as 64 | verified |
| 0x93daa / 0x93dbb / 0x93edc name loop self test | 0x55ea9e / 0x55eab1 / 0x55eb95 (inputui.md; 32-bit stub already in native.c) | patch site | verified |
| 0x94e00 popup friendly name, call 0x93cf0 (exp11friend) | **0x55e7d0**, call **0x4f319a** | cdecl `void (int lc)` | verified |
| 0x5d8d02 configstring index table (exp11friend) | **0x92443a** (0x52443a), u16 per configstring | data | verified |
| 0x43c630 cgame view rectangle (ints x y w h) | **0x75f008** (0x35f008) | data | verified |
| 0x43c640 cgame aspect (RVA_CG_ASPECT) | 0x75f018 (0x35f018) (render.md said likely; now verified) | float | verified |
| 0x3bd808 view rectangle (floats, what 0xbd520 returns) | **0x72f3ec** (0x32f3ec); getter 0x4a6fd0 | data (.data, 0,0,1,1) | verified |
| 0x4113728 / 0x411372c picture size | 0x1c8eda4 / 0x1c8eda8 (render.md) | int | verified |
| 0x115110 / 0x1151d0 (fxentfix) | **0x4f4230 / 0x453670** | cdecl, 4 / 6 args | verified |
| 0x42d310 refdef view origin (ctx_camera_yaw, camlog) | **0x77fae8** (0x37fae8); forward axis 0x77faf4 | data | verified (origin), likely (axis) |

What is different from 64 bits:
1. **The cgame view rectangle 0x75f008 and the cgame aspect 0x75f018 lie OUTSIDE the 32-bit context range 0** (`{0x35f280, ...}` starts at VA 0x75f280); in 64 bits
   0x43c630 / 0x43c640 are inside range 0 (0x404450..0x44f000), one copy per context. In 32 bits there is one copy for both contexts. Mode 11 writes both before each
   player's draw, so the draws get the right values, but (a) after player 2's draw the live values stay player 2's (bottom half) until the next frame's mode 11 block, where 64
   gets player 1's back from `ctx_swap(0)`; (b) **`ctx_store[1][0] + (RVA_CG_ASPECT - RANGE_RVA(0))` (lines 7426, 7440, video_push / video_pop, mode 9) is a negative offset in
   32 bits: it writes 0x268 bytes before context 1's block.** Either give the island its own range or handle it in code; do not carry those two lines over as they are.
2. The HUD element is 0xa8 bytes in both builds (array stride 0x2a dwords: 64 at 0x4228d8, 32 at 0x7750c4). `TITLE_BYTES 0xc0` is larger than the element in 64 too; use 0xa8 in 32.
3. The placement setter 0x4a7320 passes the placement to its safe-area helper 0x57e170 in **EBX** (register), but its own entry is plain cdecl and it saves EBX: a C call works as in 64.
   Do not hook it.
4. New code must write the official 32-bit rva literally inside `XR()` (tools/gen_addr159.py reads the source for the iw4x-sp table, as `hud_menu` does with `XR(0x354d28)`);
   the `XR64(0x4113728)` / `XR64(0x411372c)` reads at lines 4299 and 4325 are dead blocks in 32 bits and must become `XR(0x188eda4)` / `XR(0x188eda8)`.
5. The message window draw has the same 12 arguments in both builds (read from both callers), so `msgwin_hook`'s prototype and its mode 11 test carry over unchanged.

## 1. place_client_set (native.c 3419-3462), exp11place and hudsplitscale

### Set a placement - 64-bit rva 0xdb0b0, native.c line 3445: (placement, x, y, w, h, screen w, screen h)
- 32-bit: VA 0x4a7320, rva 0xa7320, convention **cdecl**, arguments `(float *pl, float x, float y, float w, float h, int sw, int sh)` (x87 floats as 4-byte stack slots), void.
  Prologue `fld [esp+0x10]; push ebx; mov ebx,[esp+8]`, epilogue `pop ebx; ret` at 0x4a7400 (`5b c3`). EBX is saved by the callee.
- bytes at VA 0x4a7320: d9 44 24 10 53 8b 5c 24 08 d9 53 20 d9 44 24 18
- callers (observed): 0x4f4ce0 (the whole-screen set-up = 64 0xdb060: `push sh; push sw; 4 floats (0, 0, (float)sw, (float)sh); push 0x937500; call; add esp,0x1c`), 0x43beb0 and
  0x4f1160 (= 64 0xdb2b0's two calls, for 0x937578 and 0x9375e8). 0x4f4ce0 is called from the video start 0x4fd8d0 and at 0x57a59e only (64: 0xd3d00, 0xd51d0, 0xd4a50), not per frame.
- body (observed, both): p[6..9] = x, y, w, h; `t = min(w, h * 4/3 / pixelAspect)` (32: double 1.3333 at 0x6d4910, pixel aspect float at 0x89438c = video config +0x18, 64: 0x548c14);
  the two safe-area rectangles (64: two calls of 0xdad30 with the dvars at 0x5e0b60/68/70/78; 32: one call of **0x57e170**, which takes the placement in EBX and calls 0x57dfc0 twice with
  the dvars at 0x937574 / 0x9374fc and 0x937570 / 0x9374f8, outputs EBX+0x28, +0x30, +0x38, +0x40 and EBX+0x48, +0x50, +0x58, +0x60);
  p[0] = t/640, p[1] = h/480, p[2] = w/640, p[3] = h/480, p[4] = 640/t, p[5] = 480/h, p[26] = (w - t) * 0.5 (32 constants: 640.0 at 0x6b2d70, 480.0 at 0x6b2838, 0.5 at 0x6d9e60).
- confidence: verified

### The placement layout (hudsplitscale writes p[0..5], p[10..13], p[18..21], p[26]; reads p[8])
| floats | meaning | 64 offset | 32 offset | how known |
|---|---|---|---|---|
| p[0..5] | scales virtual-to-real, virtual-to-full, real-to-virtual | 0x00..0x14 | 0x00..0x14 | stores in 0x4a7320 `[ebx]..[ebx+0x14]` |
| p[6..9] | viewport x, y, w, h | 0x18..0x24 | 0x18..0x24 | `[ebx+0x18]..[ebx+0x24]` |
| p[10..13] | virtual viewable min / max | 0x28..0x34 | 0x28..0x34 | 0x57e170 `lea edx,[ebx+0x28]`, `lea ecx,[ebx+0x30]` |
| p[14..17] | real viewable min / max | 0x38..0x44 | 0x38..0x44 | `lea edi,[ebx+0x38]`, `lea esi,[ebx+0x40]` |
| p[18..21] | virtual adjustable | 0x48..0x54 | 0x48..0x54 | second call, `lea eax,[ebx+0x50]` / +0x48 |
| p[22..25] | real adjustable | 0x58..0x64 | 0x58..0x64 | same |
| p[26] | 4:3 side margin | 0x68 | 0x68 | `fstp [ebx+0x68]` |
- image values at 0x937500 (1920x1080): 2.25 2.25 3 2.25 0.444 0.444 0 0 1920 1080 64 36 789.3 444 144 81 1776 999 0 0 853.3 480 0 0 1920 1080 240 (render.md has the same).
- confidence: verified

### Screen size - 64-bit rva 0x548bfc / 0x548c00 (native.c 3436, 8605)
- 32-bit: VA 0x894374 (w) / 0x894378 (h), rva 0x494374 / 0x494378, ints (0x780 / 0x438 in the image). Evidence: 0x4f4ce0 reads both as the int and float arguments of 0x4a7320,
  as 64 0xdb060 reads 0x548bfc / 0x548c00 (render.md found the same pair for 0x4f4ce0). confidence: verified

### The per-client placement - 64-bit rva 0x5e0c60 (native.c 3444)
- 32-bit: VA 0x937500, rva 0x537500 (render.md). Mode dword 0x93756c (64: 0x5e0ccc). One element in both builds (`lc * 0x6c` with lc 0 only).
- Who makes the cgame use it (observed; native.c relies on it, does not call it): getter **0x448a30** `(int lc)`: `mode == 1 ? 0x937500 + lc*0x6c : 0x937578` (64 0xdaf10; ~145 callers,
  64 ~150; 0x427d10 returns 0x937500 + lc*0x6c always, 64 0xdaf50). Mode 1: **0x4bb380** (`c7 05 6c 75 93 00 01 00 00 00 c3`), called at **0x4666d6** at the start of the cgame draw 0x4666c0
  (64: 0xdad10 at 0xbca35) and at 0x507b5f in the scene submit 0x507b10. Mode 2: 0x4f6c20, called at 0x57ae7e in the screen update 0x57adc0 (64: 0xdaf00 at 0xd5aa0). Mode 0: 0x45be10, called
  at 0x57ac90 (64: 0xdad20 at 0xd592a). So, as in 64, each `cg_draw_original` sets mode 1 first and every 2D draw of the cgame takes 0x937500.
- Concluded: `place_client_set` ports with three numbers changed (`XR(0x494374)`, `XR(0x494378)`, `XR(0x537500)`, setter `XR(0xa7320)`); the placement maths and the hudsplitscale writes
  carry over unchanged.
- confidence: verified

## 2. place_push (native.c 7009-7046) and the console HUD layout (hudss, hud_layout_tick 7347)

### place_push / place_pop
- No new address: `PLACE_PTR(n)` already has the 32-bit list `{0x537500, 0x537578, 0x5375e8}` (render.md). The 28-float save (`place_saved[3][28]`) reads one dword past 0x937500's
  27 floats: the mode dword 0x93756c (64 the same, 0x5e0ccc). Harmless, restored with the same value. confidence: verified

### cgame UI context - 64-bit rva 0x51e330 (CG_DC_RVA, native.c 7055)
- 32-bit: VA 0x754d28, rva 0x354d28 (native.c already has it; now checked). Menus array at +0x38 (4-byte pointers), count at **+0xa38** (64: +0x1438), at most 0x280 menus.
- per local client stride: 64 0x2cd0, 32 **0x1684**; only one element exists in 32 SP: 0x754d28 + 0x1684 = 0x7563ac is the `coopNameFontSize` dvar pointer (inputui.md).
- evidence (observed): 0x565880 pushes "ui/hud.txt" (string 0x6a3b48), then `imul edi,edi,0x1684; lea ebx,[edi+0x754d28]; push 0; push list; push ebx; call 0x417640` (at 0x5658a4);
  64 0xa6b7f: `imul rdx,rcx,0x2cd0; add r14,rdx; call 0x25b5a0(dc, list, 0)`. 0x417640 = 0x25b5a0: `menus[count] = def` at `dc + 0x38 + count*4` / `*8`, count at +0xa38 / +0x1438, limit 0x27f.
- not in any context range in either build: both contexts draw the same menus (as the comment in native.c says).
- confidence: verified

### Item offsets the HUD layout uses (64 -> 32)
| field | 64 | 32 | how known |
|---|---|---|---|
| rect x, y (ITEM_RECT) | 0x08, 0x0c | 0x04, 0x08 | msgwin callers: 32 0x624029/0x624034 `fld [esi+8]`, `fld [esi+4]`; 64 0x255183/0x255158 `[rsi+8]`, `[rsi+0xc]`; verified |
| rect align h / v | 0x18 / 0x19 | 0x14 / 0x15 | 32 `movzx [esi+0x14]`, `[esi+0x15]`; 64 `movzx [rsi+0x18]`; verified |
| rectClient (ITEM_RECT + 0x14) | 0x1c | 0x18 | menus_ui.h ITEM_RECT_CLIENT_Y 0x20 / 0x1c; likely |
| textAlignMode | 0xd4 | 0xc8 | msgwin callers (arg 11); verified |
| **textscale (ITEM_TEXT_SCALE)** | 0xe0 | **0xd4** | between textAlignMode / align x / align y and textStyle (32 `[esi+0xd8]` = arg 10, 64 `[rsi+0xe4]`); menus.c's 32 ITEM_FONT 0xc4 + 0x10 is the same offset; likely |
| textStyle, window index, window mode | 0xe4, 0xe8, 0xec | 0xd8, 0xdc, 0xe0 | msgwin callers; verified |
| ownerDraw (ITEM_OWNER_DRAW, QA log only) | 0x40 | 0x38 | concluded from the windowDef order (dynamicFlags 0x50 / 0x48 in menus_ui.h, ownerDraw 0x10 before it); likely |

## 3. hudelem_hook (native.c 8556-8633): hudelemlog, the mode 10 titles, exp11titles, hudelemk

### HUD element draw - 64-bit rva 0xa3b80 (hook)
- 32-bit: VA 0x564b40, rva 0x164b40, cdecl `(int lc, hudelem *e)` (inputui.md; native.c's 32 install already checks `a1 50 86 75 00`). Arguments read at `mov ebx,[esp+0x348]` /
  `mov ebp,[esp+0x350]` after `sub esp,0x340` and two pushes.
- bytes at VA: a1 50 86 75 00 81 ec 40 03 00 00 80 78 10 00 53
- the element comes from the sorted list of **pointers into the per-context array 0x7750c4** (0xa8 each, 256, collected and sorted by 0x4beed0, which calls 0x564b40 right before its `add esp,8` at 0x4befc5); 64: 0x4228d8,
  stride 0x2a dwords = 0xa8 too (0xa2640). Both arrays are inside range 0 of their build.
- **copy-to-stack is safe in 32 (exp11titles draws a modified copy)**: the only code that refers to the array base 0x7750c4 is 0x56e8c0, 0x4aa9b0 and 0x4beed0; the sub-draws of 0x564b40
  (0x563da0, 0x563a80, 0x563e30, 0x564110, 0x563f90, 0x564a10) take the element in a register (EAX / EDI / ESI), never as an index.
- confidence: verified

### HUD element fields (64 -> 32, all 4-byte, no pointer inside: identical)
| field | offset (both) | where seen in 32 |
|---|---|---|
| type | +0x00 | 0x564b75 `cmp [ebp],0xd` |
| x, y (exp11titles moves dword 1 and 2) | +0x04, +0x08 | 0x5636f0 `[esi+4]`, `[esi+8]` (from-x / from-y +0x60 / +0x64, move start / time +0x70 / +0x74) |
| font | +0x24 | 0x563a80 `in_EAX[9]` |
| colour, from-colour, fade start, fade time | +0x30, +0x34, +0x38, +0x3c | 0x44e520 |
| label | +0x40 | 0x563a80 |
| material index (hudelemlog `+0x4c`) | +0x4c | 0x564118 `mov ecx,[ebx+0x4c]` |
| value, text | +0x80, +0x84 | 0x563a80 |
| sort (title marker 1.0078125) | +0x88 | inputui.md (comparator 0x564ce0) |
| flags | +0xa4 | 0x564b62 `test byte [ebp+0xa4],0x80` |
| size (TITLE_BYTES) | 0xa8 | stride 0x2a dwords in 0x4beed0 |
- confidence: verified

### Element colour at a time - 64-bit rva 0x73eb0 (hudelemlog, native.c 8576)
- 32-bit: VA 0x44e520, rva 0x4e520, **cdecl** `void (hudelem *e, int time, uint8_t rgba[4])`; `push ecx` (scratch), `pop ecx; ret` (0x44e673 / 0x44e67e). Called in 0x564b40 at 0x564bb0
  with `push &out; push [0x86a5b4]; push ebp(e)`, `add esp,0xc` (64: 0xa3b80 calls it with DAT_14051dca8).
- bytes at VA: 51 8b 4c 24 08 8b 44 24 0c 2b 41 38 8b 51 3c 85
- body (both): lerp of the four colour bytes +0x34 -> +0x30 over (time - [+0x38]) / [+0x3c], else copy +0x30.
- confidence: verified

### Element material name - 64-bit rva 0xb0610 (hudelemlog, native.c 8570)
- 32-bit: VA 0x4a5e00, rva 0xa5e00, **cdecl** `int (int lc /* unused */, int index, char *out, unsigned size)`, returns 1 / 0 in EAX, plain `ret`. Called at 0x56412c (case 4 of the draw:
  `push 0x40; push buf; push [ebx+0x4c]; push lc`). The configstring of the name is `index + 0xae8` in 32 (64: `index + 0xee7`): internal to the function, nothing for the caller.
- bytes at VA: 8b 44 24 08 8d 48 ff 83 f9 7e 77 46 05 e8 0a 00
- confidence: verified (map.txt's guess `b0610 -> 4a5e00` is right)

### cl.serverTime - 64-bit rva 0x51dca8 (native.c 8574)
- 32-bit VA 0x86a5b4 (render.md, verified); seen again here: 0x564b9d `mov eax,[0x86a5b4]` is the time 0x564b40 gives the colour function, as 64 passes DAT_14051dca8.

### exp11titles (native.c 8597-8618), hudelemk (8622-8630), mode 10 titles (8586-8596)
- need: the screen size pair (0x894374 / 0x894378), the element's x +4 / y +8 / sort +0x88, `TITLE_BYTES` = **0xa8**, and `place_client_set`. Nothing else.
- concluded: `IsBadReadPtr(e, TITLE_BYTES)` and the copy are fine with 0xa8; the -240 virtual shift is resolution independent in 32 too (same 640 x 480 virtual space: 0x4a7320 constants).
- confidence: verified (inputs), concluded (behaviour; not run)

## 4. msgwin_hook in mode 11 (native.c 8442-8478)

### Game message window draw - 64-bit rva 0xcb4f0 (hook), 32-bit VA 0x4713b0 (inputui.md)
- The mode 11 lines (8449 `once`, 8450-8462 msglog, 8463-8469 msglog >= 2) use only `window`, `ctx_current`, `split_on` and print the arguments: **no address**.
- **The arguments are the same in both builds** (observed in the only caller of each, 32 0x623f50 at 0x624047, 64 0x254820 at 0x2551a3):
  `(lc = [edi], window = item+0xdc / +0xe8, x = (int)item+4 / +8, y = (int)item+8 / +0xc, horz = byte item+0x14 / +0x18, vert = item+0x15 / +0x19, font, float scale, const float *colour,
  textStyle = item+0xd8 / +0xe4, textAlignMode = item+0xc8 / +0xd4, how = gameMsgWindowMode item+0xe0 / +0xec)`. The 32 body uses `how` (its last argument to 0x577df0) but has no switch on it.
  So native.c's prototype `(int, int, int, int, int, int, void *, float, const float *, int, int, int)` is right for cdecl 32 (12 dwords; `add esp,0x30` at 0x62404c).
- windows of local client 0 (both contexts draw as lc 0): 32 `0x885f80 + window * 0x34`, outside every 32 context range, like 64's 0x52f348: one set, both views draw it, so the hook is
  needed in 32 for the same reason.
- difference (observed, no effect on the hook): 32 has no test of the key catchers' menu bit before drawing (64 tests `0x533cb0[lc] & 0x10`).
- bytes at VA 0x4713b0: a1 c0 a7 86 00 83 ec 10 83 78 10 00 0f 85 99 00
- confidence: verified

## 5. name_self_install (native.c 8480-8551)

### The name loop's self test - 64-bit 0x93daa (patch, 17 bytes), 0x93dbb (name drawn), 0x93edc (next client)
- 32-bit (inputui.md; native.c already has the 32-bit stub): patch **VA 0x55ea9e** (rva 0x15ea9e, 19 bytes), "name drawn" **0x55eab1**, "next client" **0x55eb95**. Loop function 0x55ea00, cdecl `(int lc)`.
- bytes at 0x55ea9e: 84 d2 8b c5 74 03 0f b7 01 39 44 24 3c 0f 84 e4  (the 19 bytes end `00 00 00`)
- bytes at 0x55eab1: f6 86 64 01 00 00 01 0f 84 d7 00 00 00 d9 46 f8
- bytes at 0x55eb95: 81 c1 3c 26 00 00 81 c6 98 01 00 00 83 c5 01 81
- bytes at 0x55ea00: 83 ec 28 56 8b 74 24 30 56 e8 02 f2 f8 ff 83 c4
- re-checked: at 0x55eaa7 the stack holds 0x38 bytes below the return address (`sub esp,0x28`, push esi, push ebx/ebp/edi; the slot of 0x448a30's argument is reused as the
  third argument of 0x4c4bd0 by `fstp [esp]` at 0x55ea3f, and the three are removed by `add esp,0xc` at 0x55ea67), so `[esp+0x3c]` is the loop's argument `lc`. DL = coop byte 0x88897c, EBP = client index, ECX = slot + 0x18.
  The loop takes its placement from 0x448a30(lc) at 0x55ea1d, so the names follow the per-client placement in mode 11 as in 64.
- mode 11 needs nothing more: the stub tests `ctx_current`, the same in every mode.
- confidence: verified

## 6. exp11friend (native.c 10616-10660, 10752; not in the package list, read because it is the same HUD code)

### Popup friendly name - 64-bit rva 0x94e00, call 0x93cf0 in 0x93ac0
- 32-bit: function **VA 0x55e7d0** (rva 0x15e7d0), **cdecl `void (int lc)`** (`mov esi,[esp+0x2c]` after `sub esp,0x24; push esi`); found by its string "Friend Name" (VA 0x6e0060), body as 64:
  `cl_paused` dvar 0x86a7c0, configstrings `lc*2 + 0xc` and `lc*2 + 0xd` through 0x48b8a0, the placement from 0x448a30(lc), two text draws 0x4a4800.
- **call site VA 0x4f319a** (rva 0xf319a), the only caller, in the HUD function 0x4f3020 (64's 0x93ac0 twin; it calls 0x55ed30, 0x55e7d0, 0x55ea00 in a row with `push esi` = lc each and one
  `add esp,0xc` at 0x4f31a5).
- bytes at 0x4f319a: e8 31 b6 06 00 56 e8 5b b8 06 00 83 c4 0c 83 3d  (target 0x4f319f + 0x6b631 = 0x55e7d0)
- bytes at 0x55e7d0: a1 c0 a7 86 00 83 ec 24 83 78 10 00 0f 85 16 02
- configstring string-index table - 64-bit rva 0x5d8d02: **32 VA 0x92443a** (rva 0x52443a), u16 per configstring. 0x48b8a0 (= 64 0xca1e0): `mov eax,[esp+4]; movzx ecx,word [eax*2+0x92443a];
  mov [esp+4],ecx; jmp 0x4e7440`. Outside every context range in both builds, so the swap of entries 0xc..0xf for the call works the same.
- confidence: verified

## 7. The mode 11 view rectangle (native.c 4297-4302, 4318-4333, 4440-4447)

### cgame view rectangle in pixels - 64-bit rva 0x43c630 (`cvr`, int x, y, w, h)
- 32-bit: **VA 0x75f008**, rva 0x35f008 (x 0x75f008, y 0x75f00c, w 0x75f010, h 0x75f014); the aspect follows at 0x75f018 as in 64 (0x43c640).
- evidence (observed): 0x571120 (= 64 0xbe710) copies 0x75f008 / 0c / 10 / 14 to the refdef 0x77fad0 / d4 / d8 / dc (0x571215 .. 0x571283), as 64 copies 0x43c630..63c to 0x42d2f8..304
  (0xbe860 .. 0xbe8f2). 0x433f30 (= 64 0xa6680, the HUD / cgame init) does `mov [0x75f008],0` and `0x476490(&0x75f010, &0x75f014, &0x75f018)` at 0x43416e..0x434187, as 64 does
  `mov [0x43c630],ebp` and `0xca230(&0x43c638, &0x43c63c, &0x43c640)` at 0xa69e4..0xa69ff. This also settles RVA_CG_ASPECT = 0x75f018 (render.md had "likely").
- **not in the 32-bit context range 0** (starts 0x75f280); the neighbours (0x75efe0..0x75f27f) are referenced by the dvar registration 0x47c0f0 and a dozen cgame functions. See summary point 1.
- confidence: verified

### View rectangle (floats) - 64-bit rva 0x3bd808 (`vrect`), the static that 0xbd520 returns
- 32-bit: **VA 0x72f3ec**, rva 0x32f3ec, 4 floats, in .data (0x72f000..0x2535e78, writable), image value (0, 0, 1, 1).
- getter 0xbd520 -> **0x4a6fd0** (`b8 ec f3 72 00 c3`). Callers, one to one: 0xb75b0 -> 0x4df4e0, 0xb7670 -> 0x452f30, 0xb7d90 -> 0x56ddb0 (each pair has the same shape: the blur /
  double-vision render commands, the local player count 0x7ac10 -> 0x407980). Their dispatcher 0xbd3d0 -> **0x4a31e0**, called by the scene submit with the local client global
  (64: `mov ecx,[0x51deb8]` at 0x82d7, `call 0xbd3d0` at 0x82dd in 0x8210; 32: `0x4a31e0(DAT_0075e688)` in 0x507b10).
- not in any context range in either build (one rectangle, rewritten before each draw by native.c).
- confidence: verified

### Picture size - 64-bit 0x4113728 / 0x411372c (native.c 4299, 4325, read with XR64)
- 32-bit: VA 0x1c8eda4 / 0x1c8eda8 (rva 0x188eda4 / 0x188eda8) (render.md, verified). In 32 `XR64()` returns a dead block: these two lines need `XR()` with the 32 numbers.

### Not in this package
- The rest of the 4318-4500 block (the per-player renderer frames: 0xc800, 0xc7e0, 0xe900, 0xe440, 0xc4c0, 0x301e0, 0x7b60, 0x410fd00, 0x40dd754 / 0x40dd760, `p1_frame_flags`, `split_smc_redirect`,
  `ctx_wait_workers`, `fd_scene_save`, `cg_draw_vis`) and the QA logs in it (`timelog` 0x422430 / 0x42242c / 0x51dcb0, `camlog`, `viewjit_log`, `vehjit_log`): unknown here.

## 8. fxentfix in the entity cleanup (native.c 9833-9862, `#ifdef _WIN64`; `fxsplit` defaults to 0 in mode 11, so it normally does not run there)

### Save the effects of an entity's DObj - 64-bit rva 0x115110 (called at 0xb9440 in ent_clean 0xb9320)
- 32-bit: VA **0x4f4230**, rva 0xf4230, **cdecl** `void (void *buf, DObj *obj, bool flag, uint16_t key)`; called at 0x41a44c in ent_clean 0x41a3a0 (`push key; push flag(dl); push obj; push buf`).
- bytes at VA: 53 8b 5c 24 0c 56 33 f6 3b de 75 23 8b 44 24 0c
- buffer: 32 0x108 bytes (`local_108[264]`), 64 0x210 (`local_238[528]`): native.c's 528-byte buffers are large enough.
- confidence: verified

### Re-attach / kill them - 64-bit rva 0x1151d0 (called at 0xb94d9)
- 32-bit: VA **0x453670**, rva 0x53670, **cdecl** `void (void *buf, int zero, int ent, DObj *obj, bool flag, uint16_t key)`; called at 0x41a4d5 (`add esp,0x24` = 6 args + the 3 of the two calls before).
  map.txt's guess `1151d0 -> 453670` is right.
- bytes at VA: 53 8b 5c 24 10 56 8b 74 24 0c 80 be 04 01 00 00
- entity offsets in the same code: `slot + 0x16c` (== 0xffffff) -> **+0x160**, `slot + 0x164` (u16 key) -> **+0x158** (observed `cmp [edi+0x160],0xffffff`, `movzx ecx,[edi+0x158]` at 0x41a42b / 0x41a435,
  EDI = `ent * 0x198 + 0x792848`), as entities.md has. The flag set (RVA_ENT_FLAGS 0x86b280) and the DObj getter 0x4f3c70 are entities.md's.
- confidence: verified

## 9. pass2_hook in mode 11 (native.c 8774-8790 p0aim, 8795-8920 bodylerp): what they need

- cg.predictedPlayerState 0x422440 -> **0x774c2c** (clientctx.md); its fields: origin **+0x1c**, pitch **+0x108**, yaw **+0x10c** (roll +0x110), the same in 32. Evidence (observed): 0x571120 copies
  0x774c48 / 4c / 50 (ps + 0x1c) to 0x783a64 / 68 / 6c and 0x774d34 / 38 / 3c (ps + 0x108) to 0x783a7c / 80 / 84; 64 0xbe710 copies 0x42245c.. to 0x431a54.. and 0x422548.. to 0x431a6c..
  (render.md had these as unknown). 0x774c2c is inside 32 range 0, so `ctx_store[n][0] + (XR(0x374c2c) - RANGE_RVA(0))` works. confidence: verified
- refdef view origin 0x42d310 (ctx_camera_yaw, camlog) -> **0x77fae8** (refdef 0x77fad0 + 0x18, rva 0x37fae8): written by the cgame draw 0x4666c0 (0x46670e) and 0x571120 (0x5712ef), as 64 writes
  0x42d310 in 0xbca00 and 0xbe710. The forward axis `+0xc` -> 0x77faf4 (concluded: the refdef has no pointer before +0x58, whose time field is at +0x58 in both, render.md). Inside range 0.
  confidence: verified (origin), likely (axis)
- the centity offsets (CENT_SIZE 0x198, CF_YAW0/1/2 = float 9 / 39 / 70, pitch one float before each, pose origin 0x18 -> 0x14, current origin 0x80 -> 0x74, next origin 0xfc -> 0xf0, pose yaw 0x28 -> 0x24,
  current yaw 0xa8 -> 0x9c, entity number 0x15e -> 0x152) are entities.md's. Base: **0x792848** (confirmed again: `lea edi,[eax+0x792848]` at 0x41a420); clientctx.md's table says 0x792844 with the
  origin at +0x18, which is 4 off.
- `CL_BLOCK_PS` (p0aimsrc=0) is clientctx.md's.

## Open / not done

- ITEM_TEXT_SCALE 0xd4 and ITEM_OWNER_DRAW 0x38 are likely (neighbours observed), not read in a 32 function that uses them directly.
- The refdef forward axis offset (+0xc after the origin) is concluded.
- Whether anything reads the cgame view rectangle / aspect between player 2's draw and the next frame (relevant to summary point 1): not traced.
- iw4x-sp 1.0.159 numbers for the new rvas: not looked at (the XR table generator handles them once they are written in the source).
- The Xbox 360 build was not used for this package.

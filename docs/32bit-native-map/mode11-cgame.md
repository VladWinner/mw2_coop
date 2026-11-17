# Package MODE 11 CGAME PER PLAYER: 32-bit counterparts

Build: 32-bit official, image base 0x400000 (Ghidra project base 0: VAs below are real VAs, rva = VA - 0x400000). All "bytes at VA" are the first 16 bytes
of `image32.bin`. Agent: m11cg. Method: every 64-bit site was disassembled and its function decompiled next to the 32-bit candidate (same calls in the same
order, same constants, same globals), the 32-bit candidates were found through the references to the renderer's local player global (all 29 of them classified),
through the callers of the paired functions and through strings ("Friend Name", "cg_playerFovScale0/1"). Static analysis only: nothing was run.
"observed" = read in a decompile or disassembly; "concluded" = my reading of what I observed.

Covers `native.c`: the exp11lc pins (lines 10705-10744), `rs_shock_hook` (3185), `rs_cgents_hook` (3204, the stride note at 3087), `fx_step_hook` / fxclock (3196),
`friend_hook` (10621), `cg_time_hook` / `cg_step_hook` (10647 / 10653), `own_slot_install` (9303), `fov_scale_install` (5028), the `mode == 11` parts at 5068, 5207,
5237, 5374, 7710. Also, because the pins and the two R_RenderScene callbacks only make sense with it: the R_RenderScene call (`render_scene_hook`, 64 0xca523) and the
screen update's end-frame call (`end_frame_hook`, 64 0xd5aaf).

## Summary: what is different from 64 bits (read this first)

1. **The renderer's local player global** (64 rva 0x51deb8) is **VA 0x75e688, rva 0x35e688** (render.md found it too; confirmed here by its 3 writers and 26 readers).
   It is not in a swapped range in either build. The 32-bit readers are absolute-address loads: `a1 88 e6 75 00` (`mov eax,[0x75e688]`, 5 bytes, address at +1) or
   `8b 0d 88 e6 75 00` (`mov ecx,[0x75e688]`, 6 bytes, address at +2). The 64-bit pin table (7-byte `movsxd`, rip-relative check) does not apply: use the 32-bit table below.
2. **Five of the six pins have a 1:1 counterpart** (the same tiny functions, the read at the same place). **0xa4cbf has none**: 64's 0xa4c10 inlines the "height of an
   entity" code that 64 also has as its own function 0xa2e10 (with its own read at 0xa2e38, not pinned in 64). In 32 bits the code is split three ways:
   0x564730 (= 0xa4c10) -> 0x4788a0 (= 0xa2e10) -> **0x5646c0** (the tag lookup), and the one read at 0x5646c0 stands for both 64-bit reads. The value it reads is only passed
   as the unused second argument of the client DObj lookup (0x4f3c70 reads `[esp+4]` only; 64 0x1fece0 also takes one argument): pinning it changes nothing in either build.
3. **`own_slot_install` needs no patch in 32 bits.** Both 64-bit inline copies of the slot rule (0xb28f3 in the own-entity write, 0xbcc07 in CG_DrawActiveFrame) are
   calls of the slot function 0x4215a0 in 32 bits (0x4904ab, 0x4667ff), and `hooks_apply32_entities` already points both calls at `local_slot_hook` (`slot_sites` 0x904ab, 0x667ff).
4. **`fov_scale_install` needs no patch in 32 bits.** The 32-bit CG_CalcFov (0x440d20) chooses the FOV scale by the client slot, exactly the 360's rule: `if (0x4215a0(lc) == 0)
   cg_playerFovScale0 else cg_playerFovScale1`, and that call (0x440e0e) is `slot_sites` 0x40e0e, already hooked. Consequence: in 32 bits `fovsplit` and `ownslot360` cannot be
   switched off on their own; they follow `ownfix` (the slot hook).
5. **Per-local-client cgame UI context** (64: 0x51e330 + lc * 0x2cd0) = **VA 0x754d28 + lc * 0x1684**; one element in both builds (the next global at 0x7563ac is a dvar
   pointer). `native.c` already has it as the 32-bit `CG_DC_RVA` (0x354d28): this package confirms it independently.
6. **The configstring index table** (64 0x5d8d02, `uint16`) = **VA 0x92443a**; entries 0xc..0xf at 0x924452 / 0x924454 / 0x924456 / 0x924458. The tables have different
   lengths (64: 0xfa8 entries to 0x5dac52; 32: 0xba9 entries to 0x925b8c), the indices the friend name uses (0xc + 2 * lc, 0xd + 2 * lc) are the same constants.
7. **Every new call site is a plain cdecl call, the caller cleans** (often with one batched `add esp, N` after several calls), every callee ends with plain `ret`, none takes a
   register argument: the C signatures of the 64-bit hooks work unchanged (extra declared arguments only read the caller's frame, harmless under cdecl).
8. **No patch site lies in the engine's checked code** (docs/32bit-plan.md: 0x400ffc-0x408000, 0x417ffc-0x428000, 0x42fffc-0x440000). Some *callees* do (0x41d550,
   0x425820, 0x4215a0, 0x421830): leave their bodies alone.
9. 32-bit only: a developer console command at 0x45cb50 ("must be in a map to use this command", a "lighting" shot command; referenced as data from 0x4f3480) runs its own
   time / step / draw sequence (0x45cd8c, 0x45cd93, 0x45cd9c). No 64-bit counterpart (64 0xbca00 and 0xbc170 have one caller each); not part of play, leave it.
10. The 64-bit hooks for these sites live in `#ifdef _WIN64` blocks (`fx_step_hook`, `friend_hook`, `cg_step_hook`, `cg_time_hook`, `own_slot_install`, `fov_scale_install`),
    and the client frame's mode 11 branch reads the busy flag through `XR64(0x5db410)` (a dead block in 32 bits): the port needs the 32-bit value **VA 0x931ee0 (rva 0x531ee0)**.

## Table: 64-bit site -> 32-bit

| what (native.c) | 64-bit | 32-bit VA (rva) | convention | confidence |
|---|---|---|---|---|
| renderer local player global | 0x51deb8 | **0x75e688** (0x35e688) | data, int | verified |
| pin: cull byte = 2 (`cpose+3+lc`) | instr 0xb20d0 in fn 0xb20d0 | instr **0x489134** in fn 0x489130 (6 bytes, `8b 0d`) | fn cdecl (cpose*) | verified |
| pin: cull byte 0 -> 1 | instr 0xb20e0 in fn 0xb20e0 | instr **0x4e2800** = fn start (5 bytes, `a1`) | fn cdecl (cpose*) | verified |
| pin: cull byte reset | instr 0x9e547 in fn 0x9e540 | instr **0x4ea374** in fn 0x4ea370 (5 bytes, `a1`) | fn cdecl (record*) | verified |
| pin: entity tag lookup | instr 0xa4cbf in fn 0xa4c10 | **no 1:1**; instr **0x5646c0** = fn start of 0x5646c0 (5 bytes, `a1`), also 64's 0xa2e38 | fn usercall (ESI = centity, stack float*) | verified (split), pin is a no-op |
| pin: local entities callback | instr = fn 0x934b0 (-> 0xa5f10) | instr = fn **0x4e6a90** (-> 0x41d550) (5 bytes, `a1`) | thunk: `[esp+4] = lc; jmp` | verified |
| pin: HUD type-13 callback | instr = fn 0x94bc0 (-> 0xa23c0) | instr = fn **0x4e7460** (-> 0x4aa9b0) (5 bytes, `a1`) | thunk: `[esp+4] = lc; jmp` | verified |
| their caller (worker callback) | 0x121490 | 0x4431e0 | cdecl (struct*) | verified |
| rs_shock: call | 0x82dd -> 0xbd3d0 | call **0x507b70** -> **0x4a31e0** | cdecl (int lc) | verified |
| rs_cgents: call | 0x6356 -> 0x93ac0 (CG_Draw2D) | call **0x506b3c** -> **0x4f3020** | cdecl (int lc) | verified |
| per-lc UI context | 0x51e330 + lc*0x2cd0 | **0x754d28 + lc*0x1684** | data | verified |
| R_RenderScene call (render_scene_hook) | 0xca523 -> 0x8210 | call **0x4496b7** -> 0x507b10 | cdecl (refdef*, int scene) | verified |
| cg_time: call | 0xd58dc -> 0xbc1a0 | call **0x57adff** -> **0x44a530** | cdecl (serverTime, b, c) | verified |
| cl.serverTime (p2time) | 0x51dca8 | **0x86a5b4** (0x46a5b4) | data, int | verified |
| cg_step: call | 0xd58e3 -> 0xbc170 | call **0x57ae05** -> **0x44a1b0** | cdecl (int lc) | verified |
| cg.nextSnap ptr (camlog) | 0x40cad8 | 0x75f2c8 | data, ptr | verified (clientctx / entities) |
| fx_step: call | 0xbccf0 -> 0x1e80c0 | call **0x4668b2** -> **0x49d120** | cdecl (int 0, int time) | verified |
| "lc == 0" predicate | 0xdb4d0 | 0x409540 | cdecl (int) -> bool | verified |
| friend: call | 0x93cf0 -> 0x94e00 | call **0x4f319a** -> **0x55e7d0** | cdecl (int lc) | verified |
| configstring index table | 0x5d8d02 | **0x92443a** | data, u16[] | verified |
| configstring getter | 0xca1e0 | 0x48b8a0 | cdecl (int) | verified |
| ownslot360 part 1 | patch 0xb28f3 (58 bytes) | **no patch**: call 0x4904ab -> 0x4215a0 (already `slot_sites`) | - | verified |
| ownslot360 part 2 | patch 0xbcc07 (55 bytes) | **no patch**: call 0x4667ff -> 0x4215a0 (already `slot_sites`) | - | verified |
| fovsplit | patch 0xbc2de (20 bytes) | **no patch**: call 0x440e0e -> 0x4215a0 (already `slot_sites`) | - | verified |
| cg_playerFovScale0 dvar ptr | 0x51e1c0 | **0x75f298** (0x35f298) | data, ptr | verified |
| cg_playerFovScale1 dvar ptr | 0x51e1c8 | **0x85e8e0** (0x45e8e0) | data, ptr | verified |
| screen update busy flag (7710) | 0x5db410 | **0x931ee0** (0x531ee0) | data, byte | verified |
| end-frame call (end_frame_hook) | 0xd5aaf -> 0xc4c0 | call **0x57ae8a** -> 0x50d630 | cdecl (uint flags) | verified |

## The exp11lc pins (hooks_apply, native.c lines 10705-10744)

What the readers are (observed in both builds): the renderer's local player is used as an index into **one-element cgame data**: the `cull` byte of a pose (`cpose + 3 + lc`;
the PC has one byte there), and the cgame's per-client local entity list and HUD element pass. In 32 bits the six reads are the same kind of instruction at the same place
except where noted. The 64-bit pins write `xor reg,reg` + `nop`s; the 32-bit equivalents are below. `xor` changes the flags; in each place the next flag use is preceded by its own
`cmp` / `test` (checked in the disassembly), as in 64.

### cull byte := 2 - 64-bit rva 0xb20d0 (fn 0xb20d0, `movsxd rax,[lc]; mov byte [rax+rcx+3],2; ret`), native.c line 10711
- 32-bit: fn **VA 0x489130, rva 0x89130**, cdecl `void (cpose *p)`: `mov eax,[esp+4]; mov ecx,[0x75e688]; mov byte [ecx+eax+3],2; ret`. **Pin site: VA 0x489134, rva 0x89134**,
  `8b 0d 88 e6 75 00` (6 bytes, absolute address at +2) -> `31 c9 90 90 90 90` (xor ecx,ecx).
- bytes at VA 0x489134: 8b 0d 88 e6 75 00 c6 44 01 03 02 c3 83 3d 2c 43
- bytes at VA 0x489130 (fn): 8b 44 24 04 8b 0d 88 e6 75 00 c6 44 01 03 02 c3
- evidence: same body instruction for instruction. Callers 32: 0x50f74e (0x50f700), 0x52f1f3 (0x52f170), 0x54f76b (0x54f660); 64: 0x2fc51 (0x2f8d0), 0x12d89 (0x12d30), 0x55830 (0x556d0).
- confidence: verified

### cull byte 0 -> 1 - 64-bit rva 0xb20e0 (fn 0xb20e0, `movsxd rdx,[lc]; ...`), native.c line 10712
- 32-bit: fn **VA 0x4e2800, rva 0xe2800**, cdecl `void (cpose *p)`: `mov eax,[0x75e688]; mov ecx,[esp+4]; mov dl,[eax+ecx+3]; test dl,dl; jnz; mov byte [eax+ecx+3],1; ret`.
  **Pin site: VA 0x4e2800 (the first instruction)**, `a1 88 e6 75 00` (5 bytes, address at +1) -> `31 c0 90 90 90`.
- bytes at VA: a1 88 e6 75 00 8b 4c 24 04 8a 54 08 03 84 d2 75
- evidence: same body. Callers 32: 0x50f735, 0x52f1a5, 0x52f1df, 0x54f6ad, 0x54f7af, 0x605fdc, 0x60666e (7); 64: 0x12d6b, 0x55724, 0x5589d, 0x209242, 0x209063, 0x2fc12 (6). Every 32-bit caller
  calls the function, so the pin covers them all (no inlined copy: all 29 references to 0x75e688 are classified at the end).
- confidence: verified

### cull byte reset - 64-bit instr 0x9e547 in fn 0x9e540, native.c line 10713
- 32-bit: fn **VA 0x4ea370, rva 0xea370**, cdecl `void (record *r)`: `mov edx,[esp+4]; mov eax,[0x75e688]; mov ecx,[edx+0x70]; cmp byte [ecx+eax+3],0; jz; mov byte [ecx+eax+3],0;
  if (dvar [0x8941e4]->int == 0) tail-jump 0x50f870` (64: `[rcx+0x80]`, dvar 0x5dacc0, tail 0x12aa0). **Pin site: VA 0x4ea374, rva 0xea374**, `a1 88 e6 75 00` -> `31 c0 90 90 90`.
- bytes at VA 0x4ea374: a1 88 e6 75 00 8b 4a 70 80 7c 01 03 00 74 19 c6
- bytes at VA 0x4ea370 (fn): 8b 54 24 04 a1 88 e6 75 00 8b 4a 70 80 7c 01 03
- offsets: scene model record -> cpose pointer: 64 +0x80 -> 32 **+0x70** (record size 0x90 -> 0x78, see QA notes).
- evidence: callers 32 0x503c00 (scene add DObj, = 64 0x2bb0 by entities.md), 0x503eb0 (= 0x2eb0), 0x504070 (= 0x30d0, decompiles compared: the same 0x1800000 / 0x800000 record test, the 0x200 cap, the copy, then this call).
- confidence: verified

### entity tag lookup - 64-bit instr 0xa4cbf (`mov edx,[lc]`) in fn 0xa4c10, native.c line 10727
- **No 1:1 counterpart.** 64 0xa4c10 (HUD element / waypoint origin of an entity: `0x7ff` = fixed point, else entity origin + head height) inlines the head-height code that 64 also
  has as its own function 0xa2e10 (read of the global at 0xa2e38, not pinned in 64). In 32 bits:
  - 0xa4c10 = **0x564730** (`__fastcall`-like: ECX = the HUD element, ESI = output vec3; same `0x7ff` test, same `valid & 1` test at centity +0x180, origin at +0x14), which calls
  - 0xa2e10 = **0x4788a0** (cdecl `float (centity *)`, result in ST0; same type 1 / 13 test at +0xd4 (64 +0xe0), same three dvar fallbacks, flags +0x64 (64 +0x70)), which calls
  - **0x5646c0** (usercall: ESI = centity, stack: `float *out`): `mov eax,[0x75e688]; movzx ecx,word [esi+0x152]; ...; push eax; push ecx; call 0x4f3c70; add esp,8` then the tag
    search ("Cannot find tag [%s] on entity" on failure).
- **Pin site: VA 0x5646c0, rva 0x1646c0** (the function's first instruction) `a1 88 e6 75 00` -> `31 c0 90 90 90`. It covers both 64 reads (0xa4cbf and 0xa2e38).
- bytes at VA: a1 88 e6 75 00 0f b7 8e 52 01 00 00 83 ec 0c 57
- observed: the value only becomes the second argument of the client DObj lookup **0x4f3c70, which reads `[esp+4]` only** (`8b 44 24 04 0f b7 04 45 28 c1 4a 01 ...; ret`); 64 0x1fece0
  decompiles with one parameter too. Concluded: this pin has no effect in either build; keep it for parity or drop it.
- callers: 0x4788a0 <- 0x55ea00 (names loop), 0x55ec60, 0x564730 <- 0x564a10, 0x564d30 (the type-13 HUD body of 0x4aa9b0).
- confidence: verified (the split, the read); "no effect" concluded from the callee's code

### local entities callback - 64-bit fn 0x934b0 (`mov ecx,[lc]; jmp 0xa5f10`), native.c line 10727
- 32-bit: fn **VA 0x4e6a90, rva 0xe6a90**: `mov eax,[0x75e688]; mov [esp+4],eax; jmp 0x41d550` (the argument slot is overwritten with lc, then a tail jump: cdecl, one stack argument).
  **Pin site: VA 0x4e6a90** `a1 88 e6 75 00` -> `31 c0 90 90 90`.
- bytes at VA: a1 88 e6 75 00 89 44 24 04 e9 b2 6a f3 ff cc cc
- evidence: 0x41d550 = 64 0xa5f10 (decompiles compared): walks the local entity list of lc (32: `0x754ca0 + lc * 100`, free list `0x751a9c + lc * 4`; 64: `0x4007c0 + lc * 0x78`, `0x400838 + lc * 8`),
  compares with cg.time, Com_Error(1, ...) on a broken link. One element per list in both builds, which is why 1 gives "CG_FreeLocalEntity: not active".
  Caller: 0x4431e0 (= 64 0x121490: `if (dvar && lc == p[1]) 0x598830(...); 0x4e6a90(p[1]); tail 0x4e7460(p[1])`, the same two calls in the same order as 64 `0x934b0(p[1]); 0x94bc0(p[1])`).
- note: 0x41d550 lies in a checked range (0x417ffc-0x428000); the pin site does not.
- confidence: verified

### HUD type-13 callback - 64-bit fn 0x94bc0 (`mov ecx,[lc]; jmp 0xa23c0`), native.c line 10727
- 32-bit: fn **VA 0x4e7460, rva 0xe7460**: `mov eax,[0x75e688]; mov [esp+4],eax; jmp 0x4aa9b0`. **Pin site: VA 0x4e7460** `a1 88 e6 75 00` -> `31 c0 90 90 90`.
- bytes at VA: a1 88 e6 75 00 89 44 24 04 e9 42 35 fc ff cc cc
- evidence: 0x4aa9b0 = 64 0xa23c0: collects the HUD elements (32: 0x7750c4 .. 0x77f8c4, 0xa8 each; 64 0x4228d8, 0xa8 each, 256), qsorts them, handles type 13. The 32-bit build outlined the
  type-13 body into 0x564d30(lc) (which reaches 0x564730 = 0xa4c10, as 64 0xa23c0 calls 0xa4c10 at 0xa257d).
- confidence: verified

### How the 64-bit install code must change for these (concluded)
- table for 32 bits: `{VA 0x489134, len 6, want 8b 0d <0x75e688>, put 31 c9 90 90 90 90}`, `{0x4e2800, 5, a1 <0x75e688>, 31 c0 90 90 90}`, `{0x4ea374, 5, same}`, `{0x5646c0, 5, same}`, `{0x4e6a90, 5, same}`, `{0x4e7460, 5, same}`.
  Compare the 4-byte absolute address with `base + XR(0x35e688)` (it is absolute, not rip-relative). The 64 code does not go through `j_save` here; the 32 one should (see the observations at the end).

## The R_RenderScene callbacks (`rs_shock_hook`, `rs_cgents_hook`, `render_scene_hook`)

### rs_shock_hook - 64-bit call at rva 0x82dd (in R_RenderScene 0x8210) to 0xbd3d0 (shellshock of a local player), native.c line 3185
- 32-bit: **call site VA 0x507b70, rva 0x107b70**, inside 0x507b10 (= 64 0x8210, render.md). Target **VA 0x4a31e0, rva 0xa31e0**, **cdecl `void (int lc)`**, plain `ret`.
  The caller: `mov ecx,[0x75e688]; push ecx; call 0x4a31e0` and frees the word later (`add esp,4` at 0x507b8b, after two other instructions): caller cleans.
- bytes at the call site: e8 6b b6 f9 ff 8b 5d 08 d9 43 18 8d 43 18 d9 1d (rel32 -> 0x4a31e0)
- bytes at the function: 80 3d d4 e4 78 00 00 a1 8c e6 75 00 75 0a 80 78
- evidence: decompiles compared: two dvar / flag early outs, `0x41b850(lc)` (64 0xb7820), `iVar == 1 -> 0x452f30(lc)` (64 0xb7670), `0x570d20` (64 0xc48f0), flag `& 8` of cg.ps, then `0x4df4e0(lc, max)`
  (64 0xb75b0). Only caller of 0x4a31e0 (64: only caller of 0xbd3d0 besides data refs).
- confidence: verified

### rs_cgents_hook - 64-bit call at rva 0x6356 (in the scene fill 0x5160) to 0x93ac0 (CG_Draw2D), native.c line 3204; the stride note at line 3087
- 32-bit: **call site VA 0x506b3c, rva 0x106b3c**, inside 0x5064b0 (= 64 0x5160, render.md). Target **VA 0x4f3020, rva 0xf3020** = CG_Draw2D, **cdecl `void (int lc)`**, plain `ret`
  (at 0x4f32a6). Sequence (observed, 32): `push 0x71fe18; push -1; call 0x5258d0` (a profiler marker, no 64 counterpart), `mov ecx,[0x75e688]; push ecx; call 0x4f3020; add esp,0xc`
  (cleans the marker's two words and lc together).
- bytes at the call site: e8 df c4 fe ff 83 c4 0c 68 20 fe 71 00 6a ff e8 (rel32 -> 0x4f3020)
- bytes at the function: 56 be a0 f2 75 00 e8 95 bb 06 00 83 3d a8 f2 75
- **the matcher's `93ac0 -> 55ef10` is wrong**: 0x55ef10 is only the "COOP_WAITINGFORPLAYER" piece. CG_Draw2D 0x4f3020 = 64 0x93ac0 by: the same branch on `0x75f2a8` (64 0x40caa8) and
  the cg_draw2D-like dvar, the calls `0x4beed0(lc, 0 / 1 / 2)` (64 0xa2640), `0x48cc60(&cgs 0x75f2a0)` (64 0x98160(&0x40caa0)), then **0x55ed30, 0x55e7d0 (friend name), 0x55ea00 (names
  loop)** (64: 0x94760, 0x94e00, inline loop), `0x4117f0(&0x754d28 + lc * 0x1684)` (64 `0x258df0(&0x51e330 + lc * 0x2cd0)`), 0x55ef10 (waiting text), 0x4c5980 (64 0xb5f00) at the end.
  Only caller of 0x4f3020 is this site.
- **the per-lc stride (line 3087): 64 0x51e330 + lc * 0x2cd0 -> 32 VA 0x754d28 + lc * 0x1684** (observed: `imul ecx,ecx,0x1684; add ecx,0x754d28; push ecx; call 0x4117f0` at 0x4f320c).
  One element: 0x754d28 + 0x1684 = 0x7563ac is the `coopNameFontSize` dvar pointer (inputui.md). The users pair up: 64 0xa6680 / 32 0x433f30, 0xb5f00 / 0x4c5980, 0x936e0 / 0x55e700.
  (`native.c` line 7057 already uses 0x354d28 + 0x1684 as the 32-bit `CG_DC_RVA`.)
- the scene fill's other reads of the global, in order (32 / 64): `cmp [lc], refdef.lc` -> `0x444980(lc)` (64 0x20a2c0, the refdef's player's effects) at 0x506ae9 / 0x6316;
  `0x409540(lc)` ("lc == 0") -> `0x445780()` (64 0x208a10, the effects time step) at 0x506afa / 0x6327; this call at 0x506b35 / 0x6350; `0x499c60(lc)` -> `0x40cf10(lc)` (64 0xaa1b0 / 0xa650)
  at 0x506f0b / 0x506f1d / 0x6e05 / 0x6e14. The same steps as 64 in the same order.
- confidence: verified

### render_scene_hook - 64-bit call at rva 0xca523 (in 0xca4b0) to R_RenderScene 0x8210 (native.c line 3158, installed with the two above)
- 32-bit: **call site VA 0x4496b7, rva 0x496b7**, inside 0x449660 (= 64 0xca4b0: the same `if (lc global == 0)` copy of the view origin and angles into 0x894418.. / 0x89440c..
  (64 0x548cdc.. / 0x548cd0..), then 0x505f50 (64 0x4a40), then the call). Target **0x507b10** (render.md), **cdecl `(refdef *, int scene)`**: `push ecx (scene number from 0x505f50's output);
  push esi (refdef); call 0x507b10; add esp,0x10` (cleans the two words of the earlier call too).
- bytes at the call site: e8 54 e4 0b 00 83 c4 10 5e 59 c3 cc cc cc cc cc (rel32 -> 0x507b10)
- bytes at the function: 55 8b ec 83 e4 f8 83 ec 1c 53 56 57 68 38 3f 00
- the hook writes the global: `int *rlc = base + XR(0x35e688)`.
- confidence: verified

## The client time and the cgame step (`cg_time_hook`, `cg_step_hook`, p2step / p2time)

### cg_time_hook - 64-bit call at rva 0xd58dc (in the one-client-frame function 0xd5890) to 0xbc1a0, native.c line 10647
- 32-bit: **call site VA 0x57adff, rva 0x17adff**, inside 0x57adc0 (= 0xd5890, clientctx). Target **VA 0x44a530, rva 0x4a530**, **cdecl `void (int serverTime, int b, int c)`**, plain `ret`
  at 0x44a570. The caller pushes `[0x894340]` (c), `-(x != 0) & 2` (b), `[0x1b2ac7c]` (serverTime) and cleans with `add esp,0x14` at 0x57ae10 (together with the next two calls' words).
  The first argument is the server time in both builds, so `cg_time_arg = t` is right.
- bytes at the call site: e8 2c f7 ec ff 56 e8 a6 f3 ec ff 56 e8 c0 f2 f6 (rel32 -> 0x44a530)
- bytes at the function: 8b 44 24 08 8b 4c 24 0c 56 8b 74 24 08 89 35 b4
- what it stores (observed): arg1 -> **0x86a5b4 (cl.serverTime, 64 0x51dca8)**, arg2 -> 0x86a5c4 (64 0x51dcb8), arg3 -> 0x86a5c0 (64 0x51dcb4); then 0x459d50 (64 0xb8d30), 0x507410, 0x4bd2b0(0),
  0x45d4f0, 0x462b70(0, serverTime) (64: 0xb8d30, 0x7b60, 0x8daf0(0), 0xa5850, 0x111c30(0, a)).
- p2time writes `*(int *)(base + XR(0x51dca8))`: **32-bit rva 0x46a5b4** (VA 0x86a5b4). It is inside the swapped range 1 (0x86798c..0x86a5bc) as in 64. render.md and clientctx agree on the address;
  render.md's "written by 0x4b74e0" is a second writer (0x4b74e0 also sets it), the function at this call site is 0x44a530.
- second caller: 0x45cd8c in the developer command 0x45cb50 (no 64 counterpart, see the summary).
- confidence: verified

### cg_step_hook - 64-bit call at rva 0xd58e3 (in 0xd5890) to 0xbc170, native.c line 10653
- 32-bit: **call site VA 0x57ae05, rva 0x17ae05** (right after the time call). Target **VA 0x44a1b0, rva 0x4a1b0**, **cdecl `void (int lc)`**, plain `ret`. The caller does `push esi`
  (ESI = 0, `xor esi,esi` at 0x57adcb) and cleans at 0x57ae10.
- bytes at the call site: e8 a6 f3 ec ff 56 e8 c0 f2 f6 ff 83 c4 14 85 c0 (rel32 -> 0x44a1b0)
- bytes at the function: 56 8b 74 24 08 56 e8 e5 28 07 00 56 e8 5f b6 fd
- body (observed): `0x4bcaa0(lc); 0x425820(lc); 0x4ef900(&0x75f2a0); 0x4904a0(lc)`; 64: `0xb2860(); 0xb8c50(lc); 0x97ea0(&0x40caa0); 0xb28b0(lc)`. 0x425820 = 0xb8c50 (cg.time from cl.serverTime,
  render.md), 0x75f2a0 = 0x40caa0 (clientctx), 0x4904a0 = 0xb28b0 own-entity write (entities.md). So p2step in 32 bits runs `own_write_hook` (function hook at 0x4904a0) and, through
  `slot_sites` 0x904ab, writes slot 1 in context 1: the same end state as 64 with `ownslot360`.
- note: 0x425820 is in a checked range (callee only).
- camlog (QA, line 10668): `XR(0x40cad8)` -> 32 rva 0x35f2c8 (cg.nextSnap, the ps origin at +0x1c in both); the slot origins `RVA_CENT + 0x18` must be **+0x14** in 32 bits.
- second caller: 0x45cd93 (developer command, as above).
- confidence: verified

## The effects clock (`fx_step_hook`, fxclock) - 64-bit call at rva 0xbccf0 (in CG_DrawActiveFrame 0xbca00) to 0x1e80c0, native.c line 3196
- 32-bit: **call site VA 0x4668b2, rva 0x668b2**, inside CG_DrawActiveFrame 0x4666c0. Target **VA 0x49d120, rva 0x9d120**, **cdecl `void (int a /* 0 */, int time)`**, plain `ret` at 0x49d141.
  Sequence (observed): `push ebp (lc); call 0x409540` ("lc == 0", 64 0xdb4d0); `add esp,8`; `test al,al; jz`; `mov ecx,[0x774c20] (cg.time); push ecx; push ebx (0); call 0x49d120; add esp,8`.
  64: `mov ecx,esi; call 0xdb4d0; test al,al; jz; mov edx,[cg.time 0x422434]; xor ecx,ecx; call 0x1e80c0`. Same test, same arguments.
- bytes at the call site: e8 69 68 03 00 83 c4 08 e8 e1 dc 09 00 8b 15 3c (rel32 -> 0x49d120)
- bytes at the function: 83 ec 08 8b 44 24 0c 8b 4c 24 10 8d 14 24 52 6a
- body (observed, both builds): packs the two arguments and queues worker command 0x14 (`0x52b0c0(0x14, &args)`; 64 `0x307f0(0x14, &args)`). The other caller is the effects step of the scene fill
  (0x445780 at 0x4457b7; 64 0x208a10 at 0x208a56), which only runs for renderer player 0 (`0x409540(lc)` at 0x506b00), as in 64.
- the matcher's `1e80c0 -> 409540` is wrong (0x409540 is the predicate 0xdb4d0: `33 c0 39 44 24 04 0f 94 c0 c3`).
- `fx_step_hook(int *ctx, int time)` works as cdecl in 32 bits (both 4-byte stack words).
- confidence: verified

## The popup friendly name (`friend_hook`) - 64-bit call at rva 0x93cf0 (in CG_Draw2D 0x93ac0) to 0x94e00, data 0x5d8d02, native.c line 10621
- 32-bit: **call site VA 0x4f319a, rva 0xf319a**, inside CG_Draw2D 0x4f3020: `push esi; call 0x55ed30; push esi; call 0x55e7d0; push esi; call 0x55ea00; add esp,0xc` (caller cleans the three).
  Target **VA 0x55e7d0, rva 0x15e7d0**, **cdecl `void (int lc)`**, plain `ret` at 0x55e9fb.
- bytes at the call site: e8 31 b6 06 00 56 e8 5b b8 06 00 83 c4 0c 83 3d (rel32 -> 0x55e7d0)
- bytes at the function: a1 c0 a7 86 00 83 ec 24 83 78 10 00 0f 85 16 02
- evidence: the only user of the string "Friend Name" (VA 0x6e0060); body = 64 0x94e00: `cl_paused`-dvar test (0x86a7c0), `0x4edc10(lc)` (64 0xb77f0), a flag, then
  `0x48b8a0(lc * 2 + 0xc)` and later `0x48b8a0(lc * 2 + 0xd)` (64 `0xca1e0(lc * 2 + 0xc / + 0xd)`), the same draw calls. Matcher guess 94e00 -> 55e7d0 confirmed.
- the configstring getter **0x48b8a0** (= 0xca1e0): `mov eax,[esp+4]; movzx ecx,word [eax*2 + 0x92443a]; mov [esp+4],ecx; jmp 0x4e7440` (64: `0x21a370((&0x5d8d02)[n])`).
- data: **table VA 0x92443a (rva 0x52443a)**, `uint16`. `friend_hook`'s `t[0xc..0xf]` = VA 0x924452, 0x924454, 0x924456, 0x924458. The table's users pair up with 64's six (0xca1e0 / 0x48b8a0,
  0xd2270 / 0x432800, 0xca530 / 0x4467f0, 0xd5510 / 0x479040, 0xc97e0 / 0x5766b0, 0xd4d60 / 0x57a430). Not in a swapped range in either build.
- confidence: verified

## "Which entity is me" (`own_slot_install`, native.c line 9303) - 64-bit patches at rva 0xb28f3 (58 bytes) and 0xbcc07 (55 bytes)
- **No 32-bit patch.** Both 64-bit patch sites are inline copies of the slot rule 0xa5ec0. In 32 bits they are calls of 0x4215a0:
  - own-entity write 0x4904a0: `push ebp; call 0x4215a0` at **VA 0x4904ab** (bytes e8 f0 10 f9 ff 8b 3d c8 f2 75 00 d9 47 1c 8b f0); the result is the slot (entities.md, observed).
  - CG_DrawActiveFrame 0x4666c0: `push ebp (lc); call 0x4215a0` at **VA 0x4667ff** (bytes e8 9c ad fb ff 50 e8 66 d8 09 00 83 c4 1c e8 8e), then `push eax; call 0x504070`. 0x504070 = 64 0x30d0
    (decompiles compared: replays the second model record list, keeps records whose `(flags & 0x1800000) != 0x800000` matches the argument; 32 counts 0x1c5c49c / 0x1c5c498, records 0x1c5c4a4,
    0x78 each; 64 0x40dd758 / 0x40dd754 / 0x40dd760, 0x90 each).
  - Both calls are in `hooks_apply32_entities`' `slot_sites` (0x904ab, 0x667ff), redirected to `local_slot_hook`: the 360's rule is already in place in 32 bits.
- `own_write_hook` already has the 32-bit branch (`own_write_original(lc)`, no moves): consistent with this.
- confidence: verified

## Each player's FOV scale (`fov_scale_install`, native.c line 5028) - 64-bit patch at rva 0xbc2de (20 bytes), dvars 0x51e1c0 / 0x51e1c8
- **No 32-bit patch.** 32-bit CG_CalcFov **VA 0x440d20** (render.md: twin of 0xbc1e0, result in ST0; observed here: the local client is its stack argument, `mov esi,[esp+0x1c]` after
  `sub esp,0xc` and three pushes = `[esp+4]` at entry, so render.md's "local client in a register" does not hold) does (observed, 0x440df8..0x440e2d):
  `push esi (lc); call 0x4215a0; add esp,4; test eax,eax; jz -> fld [[0x75f298]+0x10] else fld [[0x85e8e0]+0x10]; fmul ...`. That is the 360's rule (the scale of the client **slot**), and the call at
  **VA 0x440e0e** (bytes e8 8d 07 fe ff 83 c4 04 85 c0 74 0a a1 e0 e8 85) is `slot_sites` 0x40e0e, hooked: context 1 reads `cg_playerFovScale1`.
- 64 for comparison (observed): the non-coop path at 0xbc2de is the inline slot rule's "slot = lc" shortcut (`mov edx,ebx; mov rax,[0x51e1c8]; test edx,edx; jnz; mov rax,[0x51e1c0]`), the coop path is
  the inline loop at 0xbc39c that jumps back to 0xbc2e0 / 0xbc2eb.
- dvar pointers: **cg_playerFovScale0 = VA 0x75f298 (rva 0x35f298)**, **cg_playerFovScale1 = VA 0x85e8e0 (rva 0x45e8e0)**. Evidence: the registration function 0x5beaa0 pushes the name
  "cg_playerFovScale0" (VA 0x6b795c) at 0x5bf559, calls 0x49f700, stores EAX at 0x5bf57a to 0x75f298; "cg_playerFovScale1" (VA 0x6a66f8) at 0x5bf58e, store at 0x5bf5af to 0x85e8e0. Each has
  exactly one reader, 0x440d20 (64: each has one writer in 0x171a90 and one reader in 0xbc1e0). The store at 0x5bf545 to 0x85e854 belongs to the dvar registered before (the `cg_fovScale`-like
  multiplier CG_CalcFov reads first).
- `fov_scale_follow` is already a no-op stub in 32 bits: nothing to port. Side effect: `fovsplit=0` does not exist in 32 bits (it follows `ownfix`).
- confidence: verified

## The `mode == 11` parts

### line 5068 - `cent_share_on` (centshare): the players' slots 0 and 1 kept through a swap of range 0
- needs `RVA_CENT` / `CENT_SIZE`: 32 VA 0x792848, 0x198 (already the 32-bit macros). Slots 0..1 = [0x792848, 0x792b78), inside the 32-bit range 0 ([0x75f280, 0x7a0000)): the keep-and-restore
  works the same as 64. Note: clientctx.md's table gives the base as 0x792844; the code says 0x792848 (CG_DrawActiveFrame at 0x46687c `imul esi,esi,0x198; add esi,0x792848; push esi`; 0x564730 reads
  the origin at `0x79285c + n * 0x198` = base + 0x14, entities.md's value), so 0x792848 is right.
- confidence: verified

### line 5207 - `fxbody_install` (64-bit only code, runs only with `fxsplit=1`, which mode 11 does not use by default)
- needs: call sites 0x11c65f (-> 0x1e5de0, physics object create for an effect element, world 1) and 0x119382 (-> 0x1e5f40, destroy).
- 32 bits: destroy 0x1e5f40 = **0x40c790** (likely: same body, `0x487b20`, lock 0x17 where 64 has 0x15, the `+0x54 == 3` test where 64 has `+0x5c == 3`, two frees to pools chosen by `world == 3`).
  Create 0x1e5de0 = **0x421830** (likely: cdecl, 5 stack args (world, 3 vector pointers, def), the same lock 0x17 pair; it lies in a checked range, so only call sites may be hooked).
  Its effects call site is likely **0x5956c5** in 0x595570 (`push 1; call 0x421830; add esp,0x14`; 64 `mov ecx,1; call 0x1e5de0` at 0x11c65f): not compared with 64 0x11c4c0. The destroy site of the
  effects free (64 0x119382 in 0x1192c0) is **unknown**; candidate: the helper 0x4d03b0 (`push [esi+4]; push 1; call 0x40c790` at 0x4d03bb), not checked.
- confidence: likely / unknown as marked

### line 5237 - `fx_on = cfg("fxsplit", mode == 11 ? 0 : 1)` in `ctx_swap_mask`
- needs `RANGE_FX` (32: range 10, VA 0x983e80..; already in native.c), `RVA_WAIT_TYPE` with type 19 (32 0x52ac60, the type list is the same: render.md), `RANGE_SUN` + `render_wait_idle` (already
  32-bit). Nothing new.

### line 5374 - fxseed (64-bit only code; `fxsplit=1` and `fxseed=1`, an experiment that is off)
- needs FX_InitSystem 0x119a00 -> **0x49e390** (verified: the same body: system header `0xb18 * lc` -> `0xad0 * lc` at 0xd83e80, pool `0x10ea80 * lc` -> `0x107a80 * lc` at 0xd89200, the
  `0x4810` block 0xcd6900 -> 0xd84980, the same field writes `0, -1, 1, 1`, the "lc" byte, the marks reset call).
- the impact marks the experiment keeps: the reset is 64 0x114b10 = **32 0x4d6490** (verified: pools per lc 0x4b000 -> **0x47000** at 0xb7ac00 -> **0xd3ab80**, sub-pools +0x9000 / +0x15000 -> +0x8000 /
  +0x14000, entry sizes 0x48 / 0x18 / 0x48 -> 0x40 / 0x18 / 0x44, 0x1000 bytes of 0xff, the "ready" flag 0xbc6280 -> 0xd82e08).
  The globals range 64 [0xbc6280, 0xbc72e0) -> 32 **[0xd82e04, 0xd83e40)** (likely: the lowest written global is FX_InitSystem's lc store 0xd82e04 (64 0xbc72d4, inside the 64 range), the
  highest 0xd83e3c; what lies in 0xd83e40..0xd83e80, before the effects header, is unknown). Pool: 64 0xb7ac00 + 0x4b000 -> 32 **0xd3ab80 + 0x47000**.
- confidence: verified (functions), likely (the globals' upper edge)

### line 7710 - the client frame's mode 11 branch (player 2's client logic under the screen update's busy flag)
- needs the busy flag 64 0x5db410 -> **VA 0x931ee0 (rva 0x531ee0)**: read by 0x4c3660 (`80 3d e0 1e 93 00 00 75 59`: `cmp byte [0x931ee0],0; jnz`) and written by it at 0x4c36ae / 0x4c36ba, plus
  0x601e34 (observed). The code reads it through `XR64` (dead block in 32 bits): it needs a per-build value.
- also `np_dvar` (cg_nopredict pointer 0x85e8e8, clientctx), `client_frame_original` (0x4c3660, call 0x496dd7, clientctx): already mapped.
- confidence: verified

### end_frame_hook (exp11frames, installed in the same mode 11 block) - 64-bit call at rva 0xd5aaf (`mov ecx,3; call 0xc4c0`)
- 32-bit: **call site VA 0x57ae8a, rva 0x17ae8a** in 0x57adc0: `push 3; call 0x50d630; add esp,8` (also frees the `push 0` of the call at 0x57ae79). Target **0x50d630**, **cdecl `void (uint flags)`**: it writes
  the flags to `frame data + 0x41a38` (render.md: the 64 +0x41a60 flags field) and runs the back end parts when `& 1` / `& 2`, as 64 0xc4c0.
- bytes at the call site: e8 a1 27 f9 ff 83 c4 08 e8 19 26 f9 ff 85 c0 74 (rel32 -> 0x50d630); at the function: 51 56 e8 49 f0 ff ff 84 c0 0f 84 aa 00 00 00 8b
- the hook also reads `XR64(0x410fd00)` (the frame data pointer): 32 0x1c93238 (render.md).
- confidence: verified (site, convention); the body compared in outline only

## All 29 references to the global 0x75e688 (the reason the pin list is complete)
- writes (3): 0x4666f2 (CG_DrawActiveFrame stores its argument; 64 0xbca46), 0x4443a4 (setter 0x4443a0; 64 0xbe0f0, likely), 0x432cdc (0x432cd0; 64 0xbd61f in 0xbd610).
- cgame-side reads (15): the six pins (0x489134, 0x4e2800, 0x4ea374, 0x5646c0, 0x4e6a90, 0x4e7460), 0x4431f0 (64 0x1214a9), 0x507b69 (shellshock argument; 64 0x82d7), 0x506ae9 / 0x506afa / 0x506b35 /
  0x506f0b / 0x506f1d (scene fill; 64 0x6316 / 0x6327 / 0x6350 / 0x6e05 / 0x6e14), 0x449661 (64 0xca4b6), 0x50f802 (64 0x2fb24 in 0x2f8d0, likely: both compare the global with a field before
  queueing a worker command; not decompiled side by side).
- renderer reads (11, the per-player view slot `* 0x2b8`): 0x5133d9, 0x5147c3, 0x516076, 0x5167e5, 0x516820, 0x51689d, 0x516950, 0x516a14, 0x516320, 0x52ed4e, 0x5143f8 (64: 0x4780, 0x5160 x4,
  0x17740, 0x18b60, 0x19000, 0x1b9b0, 0x1bee0, 0x38cc0).
- 64 has 30: the extra one is 0xa2e38 (0xa2e10), whose 32-bit read is the same instruction as 0xa4cbf's (0x5646c0).

## QA-only offsets met on the way (not hooks)
- `body_rec_log` (traceprims, line 3125): scene model records 64 0x40dd760, 0x90 each, entity bits at +0x70, DObj at +0x78, count 0x40dd754 -> 32 records **0x1c5c4a4, 0x78 each, entity bits at +0x68**
  (observed in 0x504070); the DObj offset in 32 is unknown; count 0x1c5c498 (the second list's count 0x1c5c49c, 64 0x40dd758). The scene list start / count (64 0x40f09e4 / 0x40f09e8): unknown.
- camlog / p2hooklog / pslog: slot origin +0x18 -> +0x14 (float index 6 -> 5).
- rslog / clearlog (frame data fields): render package.

## Observations about the 64-bit code (read in native.c and the 64-bit disassembly, not run)
1. `fov_scale_install` replaces 0xbc2de..0xbc2f1, but CG_CalcFov's coop path (taken when the coop byte 0x52fb5c is set) jumps back to **0xbc2e0** (now inside `mov rax, imm64`) and **0xbc2eb**
   (now the `nop`s, then `movss xmm0,[rax+0x10]` with a stale RAX). The patch is installed at every session start (line 10870) and is not journalled (`j_save` is not called), so it stays after
   the session: a later session with the coop byte set (network co-op) in the same process would run those bytes. Worth a check.
2. `own_slot_install` and the exp11lc pins also bypass `j_save`. `own_slot_install` replaces the whole inline rule (both paths), so it is safe; the pins keep the engine's behaviour for player 0.
3. The 64-bit pin at 0xa4cbf has no effect: 0x1fece0 takes one argument (its decompile has no second parameter).

## Not done / unknown
- fxbody: the effects free call site (64 0x119382) in 32 bits; the create site 0x5956c5 is likely, not compared.
- fxseed: what lies in 0xd83e40..0xd83e80.
- The back end's frame end call (64 0x45757 -> 0x45060, `be_end_hook`, exp11p1show) and the scene 2D / final 2D / clear / 3D-pass sites of the same block: render package, not looked at here.
- iw4x-sp 1.0.159: none of the new addresses is in `src/addr159.c` yet (generated from the source by `tools/gen_addr159.py`); not checked.
- The Xbox 360 cross-check was not needed (every offset was read in the 32-bit code directly).

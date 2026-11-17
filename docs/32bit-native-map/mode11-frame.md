# Package: mode 11 frame and back end (native.c be_end_hook, end_frame_hook, scene3d_hook, scene_clear_hook, render_scene_hook, begin_frame_hook, the exp11frames sequence, the mode 11 clear/present branches)

Conventions as in [render.md](render.md): VA = address in the official 32-bit exe (image base 0x400000), rva = VA - 0x400000 (what `native.c` adds to `base`). Ghidra's
32-bit project has image base 0, so the addresses it prints are VAs. "bytes at VA" are from `image32.bin` (read only). "verified" = both decompiles (and the
disassembly where a register or a call site matters) compared; "likely" = one side read closely and the other matches in role; "unknown" = not found.
Every site below was read in the disassembly, and every relative call's target was recomputed from its bytes.

## Summary: what differs from 64-bit (read this first)

Nothing found makes a straight port impossible. Four places need a different hook form, and one 64-bit assumption does not hold:

1. **be_end_hook has no site in 32 bits.** The 32-bit back-end frame (0x5397e0, 64: 0x45690) does not pass the flags to the frame end. It loads the flags into EBX, tests
   `bl & 2` itself and *tail-jumps* (`e9` at 0x5398cf) to the frame end 0x5394e0 (64: 0x45060), which takes **no argument**. So there is no call that receives `flags`, and
   no 0x80 bit to strip. Port: wrap the back-end frame itself (its three `e8` calls in the render thread loop 0x5398e0, or its entry). Read the flags from
   `[[0x2534c74] + 0x41a38]` before calling it, and set `be_skip_present` while it runs. The Present that the tail jump reaches happens inside the wrapped call. Details below.
2. **scene3d_hook: the scene pass (0x5538e0, 64: 0x5c620) takes the scene in EAX.** The call at 0x539851 is an ordinary `e8`, but the hook needs a naked entry stub
   (`push eax; call scene3d_hook; add esp,4; ret`), and the original has to be called with `mov eax, slot`. The ESI stubs for scene2d/final2d in native.c already use this pattern.
3. **scene_clear_hook: the clear wrapper 0x51070 is inlined in 32 bits.** There is no `e8` to hook. The site is a D3D9 `IDirect3DDevice9::Clear` call through the vtable
   (`mov edx,[edx+0xac]` at 0x5535c1, `call edx` at 0x5535d7) with the colour already converted to a D3DCOLOR. Port: patch the 6-byte vtable load into
   `mov edx, imm32; nop` that points at a stdcall function with Clear's signature. The hook then receives `(dev, 0, NULL, flags, D3DCOLOR, 1.0f, 0)`, not a float rgba pointer.
4. **The engine's "wait while helping workers" (64: 0x301e0, predicate in RCX) is 0x52afc0 in 32 bits, and its predicate goes in ESI.** It is called as
   `mov esi, pred; call 0x52afc0`. The `exp11rtwait` call needs an asm wrapper (`"+S"(pred)`). The predicate is `int (void)`, and its EAX is tested whole.
5. The other hooks port directly. The end-frame (0x50d630) is cdecl `(unsigned flags)`. R_RenderScene (0x507b10) is cdecl `(refdef *, int scene)`: **two** stack
   arguments, so declare two, not four. The shellshock (0x4a31e0) and the cgame 2D (0x4f3020) are cdecl `(int lc)`. The list helpers c800/c7e0/e900/e440 and 7b60 are
   cdecl `void (void)`.
6. **Frame data offsets move** (table below). The scene stride is 0xe40 (64: 0x13a0), and a frame still holds at most 4 scenes. **Trap:** in 32 bits `fd + 0x41a38` is the
   *frame flags*. In 64 bits `fd + 0x41a38` is the *scene array pointer*, and the flags are at +0x41a60.
7. **The cgame's view rectangle (64: 0x43c630) and aspect (0x43c640) are not inside the 32-bit cgame context range.** In 32 bits they are at 0x75f008..0x75f018. The
   range 0a of [clientctx.md](clientctx.md) starts at 0x75f280, so the two contexts share one copy in 32 bits. In 64 bits there is one copy per context.
   The mode 11 code writes both values before each player's draw, so the draw is right either way. Only the value left between frames differs (player 2's instead of player 1's).
8. **Numbering:** this code uses `XR64(...)` (a dead block in 32-bit builds) and `XR(0x...)` with 64-bit numbers (`XR(0xc4c0)`, `XR(0x3bd808)`, `XR(0x51deb8)`,
   `XR(0x301e0)`, `XR(0x1429b5c)`, ...). Every one needs a per-build define. The new 32-bit rvas also have to be visible to tools/gen_addr159.py for the iw4x-sp profile.

## The sites (quick table)

| native.c | 64-bit | 32-bit VA (rva) | convention | confidence |
|---|---|---|---|---|
| be_end_hook | call at 0x45757 -> 0x45060(flags) | **no equivalent call**; wrap the back-end frame 0x5397e0 (0x1397e0) at its calls 0x5399a6, 0x539a02, 0x539a67 | 0x5397e0: cdecl void(void); frame end 0x5394e0 void(void), reached by a tail `jmp` | verified (addresses); the wrapper is derived |
| end_frame_hook | call at 0xd5aaf -> 0xc4c0(3) | call **0x57ae8a** (0x17ae8a) -> **0x50d630** (0x10d630) | cdecl (unsigned flags), `push 3` | verified |
| scene3d_hook | call at 0x456ee -> 0x5c620(scene) | call **0x539851** (0x139851) -> **0x5538e0** (0x1538e0) | **scene in EAX**, no stack args | verified |
| scene_clear_hook | call at 0x5c484 -> 0x51070(dev, flags, rgba*, depth, stencil) | **inlined**: vtable load **0x5535c1** (0x1535c1), `call edx` **0x5535d7**, returns to 0x5535d9 | IDirect3DDevice9::Clear, stdcall, 7 args | verified |
| render_scene_hook | call at 0xca523 -> 0x8210(refdef, scene) | call **0x4496b7** (0x496b7) -> **0x507b10** (0x107b10) | cdecl (refdef *, int scene) | verified |
| rs_shock_hook | call at 0x82dd -> 0xbd3d0(lc) | call **0x507b70** (0x107b70) -> **0x4a31e0** (0xa31e0) | cdecl (int lc) | verified |
| rs_cgents_hook | call at 0x6356 -> 0x93ac0(lc) | call **0x506b3c** (0x106b3c) -> **0x4f3020** (0xf3020) | cdecl (int lc) | verified |
| begin_frame_hook | 0xc840 (function start) | call 0x466831 (0x66831) -> 0x50d800, already in native.c (render.md) | cdecl void(void) | verified (render.md + rechecked) |
| exp11lc pins (6) | 0xb20d0, 0xb20e0, 0x9e547, 0xa4cbf, 0x934b0, 0x94bc0 | 0x489134, 0x4e2800, 0x4ea374, 0x5646c0, 0x4e6a90, 0x4e7460 | byte patches | verified (5), likely (0x5646c0) |

## Hook entries

### be_end_hook - 64-bit call at rva 0x45757 (in 0x45690) to 0x45060, native.c line 1370 / install 10755: player 1's frame ends with flags 0x83, and its Present is skipped
- 64 (observed): `0x45690` (back-end frame, render thread) reads `uVar5 = *(fd + 0x41a60)` and calls `0x45060(uVar5)` at 0x45757 (`mov ecx,ebx; call`). 0x45060 tests `flags & 2`, calls
  the swap chain's Present (vtable +0x18), issues the frame's event query, and sets the gamma ramp (device +0xa8). The single-thread path of the end-frame (c4c0 at 0xc77d) calls 0x45060 directly, and native.c does not hook that path.
- 32 (observed): the back-end frame is **VA 0x5397e0, rva 0x1397e0**, cdecl void(void). Bytes `51 53 56 8b 35 74 4c 53 02 c7 05 24 e8 21 02 01`: push ecx; push ebx; push esi;
  `mov esi,[0x2534c74]` (the handed-over frame data); `mov [0x221e824],1`. The flags are read twice: `test byte [esi+0x41a38],1` at 0x53981b (begin, 0x539370) and
  `mov ebx,[eax+0x41a38]` at 0x539878. At the end: `test bl,2` (0x5398c5, bytes `f6 c3 02 74 0a`), then `pop esi; pop ebx; add esp,4; jmp 0x5394e0` (0x5398cf, bytes `e9 0c fc ff ff 5e 5b 59 c3 cc ...`).
  So the frame end is a **tail jump with no argument**, and EBX (the flags) is restored before it.
- 32 frame end: **VA 0x5394e0, rva 0x1394e0**, void(void), bytes `e8 5b ff ff ff a1 38 9f f1 01 80 78 0d 00 75 18`. It starts with `call 0x539440` (the Present:
  `[0x1c8ed58 + idx*0x10]`->vtable **+0xc** = IDirect3DSwapChain9::Present, error text "Direct3DDevice9::Present failed", then the query Issue). It then runs the gamma part
  (0x4358d0 x3, `jmp 0x50abd0`). Callers: the tail jump at 0x5398cf, and `0x50d630` at 0x50d6df (the single-thread path, after its own `& 2` test). 64 has the same two (0x45757, 0xc77d).
- The render thread loop (64: 0x45770, 32: **0x5398e0**) does the same in three places: 64 `DAT_1448c30e0 = 0x203ad0(); if (!= 0) 0x45690();`, 32 `[0x2534c74] = 0x4cf770(); if (!= 0) call 0x5397e0`.
  The 32 sites, in order:
  - store 0x53999f, call **0x5399a6** (rva 0x1399a6), bytes `e8 35 fe ff ff e8 50 f8 f1 ff 68 cc 84 72 00 6a`
  - store 0x5399fb, call **0x539a02** (rva 0x139a02), bytes `e8 d9 fd ff ff c7 05 20 ee c8 01 01 00 00 00 c6`
  - store 0x539a5c, call **0x539a67** (rva 0x139a67), bytes `e8 74 fd ff ff e9 03 ff ff ff cc cc cc cc cc cc`

  Each rel32 was recomputed: all three point to 0x5397e0.
  0x4cf770 returns the frame data that the main thread stored at 0x1551adc in the hand-over 0x40e3c0 (64: 0x204230, which stores it at 0x1429c58).
- Who touches the flags dword (FindDisp, observed): only 0x50d630 and 0x5397e0 in 32 bits, and only 0xc4c0 and 0x45690 in 64 bits. In 32 bits nothing tests bit 0x80.
- Port (derived, not run): `static void be_frame_hook(void) { uint8_t *fd = *(uint8_t **)(base + 0x2134c74); if (fd && (*(uint32_t *)(fd + 0x41a38) & 0x80)) be_skip_present = 1;
  be_frame_original(); be_skip_present = 0; }`, with jhook_call on the three call sites. As an alternative, put a 9-byte entry hook on 0x5397e0: its first four instructions,
  `51 53 56 8b 35 <abs32>`, hold no relative operands.
  - Stripping 0x80 is not needed.
  - The Present reaches the mod through the swap chain hook (`sc_present_hook`, via 2), which calls `before_present` and so sees `be_skip_present`.
  - The `#ifdef _WIN64` guards around `be_skip_present`, `be_end_installed` and `p1_frame_flags()` (which returns 1 in 32-bit builds) have to be lifted.
- Rejected alternative: hooking the `call 0x539440` at 0x5394e0. The frame data is gone from 0x221e5b4 by then (cleared at 0x53987e), and on the single-thread path
  [0x2534c74] is stale.
- confidence: verified for every address and byte; the wrapper is derived.

### end_frame_hook - 64-bit call at rva 0xd5aaf to 0xc4c0, native.c line 3098 / install 10746: the screen update's end-frame ends player 2's frame with end_frame_flags
- 32: screen update **0x57adc0** (64: 0xd5890; its tail is split into 0x57ac90). Site **VA 0x57ae8a, rva 0x17ae8a**: `6a 03` (push 3) at 0x57ae88, then the call. Bytes at 0x57ae8a:
  `e8 a1 27 f9 ff 83 c4 08 e8 19 26 f9 ff 85 c0 74`. The rel32 resolves to 0x50d630, and the `add esp,8` cleans this push and the push 0 before it.
- 32 end-frame: **VA 0x50d630, rva 0x10d630**, **cdecl, void (unsigned flags)** (`push ecx; push esi; call 0x50c680; test al,al; jz; mov eax,[esp+0xc]`). Bytes
  `51 56 e8 49 f0 ff ff 84 c0 0f 84 aa 00 00 00 8b`. Body (observed):
  - `*(fd + 0x41a38) = flags` (64: +0x41a60).
  - If `flags & 2`: 0x50c2c0, the wait for fewer than 3 frames in flight (64: an inline loop).
  - If 0x50d5c0 returns true: the multithreaded hand-over 0x50d4e0 ("wait cmds", "wait renderer", "wake renderer" = 0x40e3c0(fd), then `fd = 0`).
  - Otherwise the single-thread path: 0x539370 (begin, bit 0), 0x539640, 0x539680, 0x5394e0 (bit 1).
  - At the end, `DAT_01c93238 = 0`.

  The order matches 64 c4c0. Callers in 32: 0x57ae8a, 0x45cdad (in 0x45cb50, the other caller of the cgame draw) and 0x518d63 (in 0x518b20). 64 c4c0 has one caller (0xd5aaf).
- The hook body reads `p2_fd = *(void **)(base + XR64(0x410fd00))`. In 32 bits that is **0x1c93238 (rva 0x1893238)**, the current frame data of the main thread
  (64 c4c0 `*(DAT_14410fd00 + 0x41a60) = param_1`, 32 `*(DAT_01c93238 + 0x41a38) = param_1`).
- confidence: verified

### scene3d_hook - 64-bit call at rva 0x456ee (in 0x45690) to 0x5c620, native.c line 3025 / install 10703: every scene of the frame gets its 3D pass
- 64 (observed): `mov rdx,[0x4358f58]; cmp [rdx+0x41a34],ebx; ...; mov eax,[rdx+0x41a30]; imul rcx,rax,0x13a0; add rcx,[rdx+0x41a38]; call 0x5c620`.
- 32 (observed): `mov ecx,[0x221e5b4]` (set at 0x539824); `cmp [ecx+0x419c4],ebx`; `mov eax,[ecx+0x419c0]; imul eax,eax,0xe40; add eax,[ecx+0x419c8]; call 0x5538e0` at
  **VA 0x539851, rva 0x139851**. Bytes `e8 8a a0 01 00 eb 16 39 1d 54 7e c8 01 74 0e e8`; the rel32 resolves to 0x5538e0.
- 32 scene pass: **VA 0x5538e0, rva 0x1538e0**, **scene in EAX**. Bytes `55 8b ec 83 e4 f8 51 8b 0d a8 e7 21 02 83 e9 00`. It switches on the draw path [0x221e7a8]:
  - 0: clear only
  - 1: `push eax; call 0x552ec0`
  - 2: `call 0x553280`
  - else: `push eax; call 0x5533f0` (render.md)

  It preserves EBX/ESI/EDI/EBP. Its other caller is 0x539661 in 0x539640, the single-thread path (64: 0x4539c in 0x45370); native.c does not hook it in either build.
- The pass reads the "current scene" again in 32 bits too: `mov ecx,[0x221e5b4]; mov eax,[ecx+0x419c0]; call 0x543520` (0x55365b) and the same for 0x543570 (0x553712),
  as 64 `0x4df70(*(fd+0x41a30))` / `0x4e030(...)`. So writing +0x419c0 = i per pass works as in 64 bits.
- offsets: back end's frame data 0x4358f58 -> **0x221e5b4** (rva 0x1e1e5b4); count +0x41a34 -> **+0x419c4**; scene array +0x41a38 -> **+0x419c8**; current +0x41a30 -> **+0x419c0**;
  scene stride 0x13a0 -> **0xe40**. The array holds 4 scenes in both builds (begin-frame: 64 `0x4585d60 + i*0x4e80`, 32 `0x1eea900 + i*0x3900`), so `i < 4` stays.
- Port: an entry stub for EAX, and `__asm__ volatile("call *%1" : "+a"(slot) : "r"(target) : "ecx", "edx", "memory", "cc")` for the original.
- confidence: verified

### scene_clear_hook - 64-bit call at rva 0x5c484 (in 0x5c240) to 0x51070, native.c line 3056 / install 10701: only the first pass clears
- 64 (observed): after `0x128c0(&rgba)` the code tests `|| (flags & 0xfe)`, then `0x51070(DAT_144356c90, flags=BL, &rgba, XMM3 = DAT_140385a54, 0)`. 0x51070 converts the
  float rgba to D3DCOLOR and calls `dev->vtbl[+0x158]` (slot 43, Clear) with `(dev, 0, NULL, flags, color, depth, stencil)`.
- 32 (observed, in the scene draw 0x5533f0, render.md): `call 0x50cbd0; test al,al; jnz; test bl,0xfe; jz 0x5535d9`. Then:
  - `mov esi,[0x2163be0]` (the back end's device, 64: 0x4356c90)
  - `lea edx,[esp+0x14]; lea ecx,[esp+0x18]; call 0x540f60`: the colour conversion, **fastcall: ECX = float rgba *, EDX = D3DCOLOR * out**
  - `fld1; mov eax,[esp+0x14]; mov edx,[esi]`
  - **`mov edx,[edx+0xac]` at VA 0x5535c1** (slot 43 x 4 = Clear)
  - pushes: 0 (stencil), z = 1.0, color, `movzx ecx,bl` (flags), 0, 0, esi (dev)
  - **`call edx` at VA 0x5535d7**, which returns to 0x5535d9

  Same place in the pass: before 0x558640 (64: 0x62c70).
- bytes at 0x5535c1: `8b 92 ac 00 00 00 6a 00 51 d9 1c 24 50 0f b6 cb`. At 0x5535d7: `ff d2 a1 88 c0 c8 01 8b f5 89 44 24 14 e8 57 50`.
- The first clear of the pass (the scene+0x1ac branch, 64: the 0x51070 call with flags 6) is `mov eax,[ecx+0xac]` at 0x5534e7 and `call eax` at 0x5534fb. native.c does not hook its 64 twin.
- Port (derived), option A, site-exact: rewrite the 6 bytes at rva 0x1535c1 into `ba <imm32> 90` (mov edx, hook; nop). The hook is
  `HRESULT __stdcall (IDirect3DDevice9 *dev, DWORD n, const D3DRECT *r, DWORD flags, D3DCOLOR c, float z, DWORD stencil)`. It returns D3D_OK when `scene3d_pass > 0`, and
  otherwise calls `dev->lpVtbl->Clear(...)`, which reaches t_clear when that hook is in. The QA `clearred` then passes 0xffff0000 instead of a float colour.
- Option B: the device Clear hook (t_clear) skips when `__builtin_return_address(0) == base + 0x1535d9`. This breaks if another layer (overlay, wrapper) sits between.
- confidence: verified (site, arguments, device global); the port is derived.

### render_scene_hook (+ rs_shock_hook, rs_cgents_hook) - 64-bit calls at rva 0xca523, 0x82dd, 0x6356, native.c lines 3158-3205 / install 10766: player 2's R_RenderScene runs as renderer player 1
- R_RenderScene call: 64 `0xca4b0` (refdef) calls `0x8210(refdef, scene)` at 0xca523. In 32 bits the wrapper is **0x449660** (cdecl (refdef *); `push ecx` = scene number from
  0x505f50, `push esi` = refdef). Site **VA 0x4496b7, rva 0x496b7**, bytes `e8 54 e4 0b 00 83 c4 10 5e 59 c3 cc cc cc cc cc`; the rel32 resolves to 0x507b10. Both read
  refdef +0x18..+0x2c when the renderer's player is 0.
- 32 R_RenderScene: **VA 0x507b10, rva 0x107b10**, **cdecl void (refdef *, int scene)**: two stack arguments, `[ebp+8]` and `[ebp+0xc]` after `push ebp; mov ebp,esp; and esp,-8`.
  Bytes `55 8b ec 83 e4 f8 83 ec 1c 53 56 57 68 38 3f 00`. 32 callers: 0x4496b7 and 0x518d39 (in 0x518b20, which also calls the end-frame; 64 has no such second caller of 0x8210).
  Declare the 32-bit hook with two arguments. A four-argument cdecl hook would only read and pass on two words of the caller's frame, which is harmless but not needed.
- shellshock: in 0x507b10, `mov ecx,[0x75e688]; push ecx; call 0x4a31e0` (64: `mov ecx,[0x51deb8]; call 0xbd3d0` at 0x82dd).
  - Site **VA 0x507b70, rva 0x107b70**, bytes `e8 6b b6 f9 ff 8b 5d 08 d9 43 18 8d 43 18 d9 1d`. The argument is cleaned by the later `add esp,4` at 0x507b8b.
  - **0x4a31e0** (rva 0xa31e0), cdecl (int lc), bytes `80 3d d4 e4 78 00 00 a1 8c e6 75 00 75 0a 80 78`. It matches 64 bd3d0: the same two flag/dvar early-outs, the
    `dvar == 1` branch to 0x452f30 (b7670), and the final call 0x4df4e0(lc, value) (b75b0). verified.
- cgame 2D inside the scene fill: 64 `0x5160` at 0x6350 `mov ecx,[0x51deb8]; call 0x93ac0`. 32 scene fill 0x5064b0: `mov ecx,[0x75e688]` (0x506b35), `push ecx`,
  `call 0x4f3020` at **VA 0x506b3c, rva 0x106b3c**, bytes `e8 df c4 fe ff 83 c4 0c 68 20 fe 71 00 6a ff e8`.

  The neighbours match in order: `if (lc == refdef.lc) 0x444980` (64 0x20a2c0), `0x409540(lc) -> 0x445780` (db4d0 -> 208a10, the effects step), and
  `[0x1c868c8] -> 0x49da00` (64 [0x410fc80] -> d5190).

  **0x4f3020** (rva 0xf3020), cdecl (int lc), bytes `56 be a0 f2 75 00 e8 95 bb 06 00 83 3d a8 f2 75`. It is the 64 0x93ac0, verified by:
  - the same dvar ladder
  - `0x4beed0(lc, 0/1/2)` = a2640(lc, 0/1/2)
  - `0x4117f0(&0x754d28 + lc*0x1684)` = `258df0(&0x51e330 + lc*0x2cd0)`, the one-element cgame array that is the reason for pinning lc to 0
  - 0x55ef10 (COOP_WAITINGFORPLAYER, render.md)
- renderer local player: 0x51deb8 -> **0x75e688** (rva 0x35e688) (render.md; 29 references, written by the cgame draw at 0x4666f2). The renderer's per-player block
  indexed by it is `0x1ef9bcc + lc*0x2b8` with 4 slots up to 0x1efa6ac. Observed in 0x5133d9, 0x5147c3, 0x5167e5, 0x516820, 0x51689d, 0x516950, 0x516a14, 0x516361,
  0x52ed4e and 0x5143f8, all `imul reg,reg,0x2b8`. So lc = 1 has room in 32 bits as in 64.
- confidence: verified

### exp11lc pins - native.c install lines 10705-10740: the readers of the renderer's player that must see 0
| 64 site (want bytes) | 32 function | 32 site (VA / rva) | 32 bytes now | 32 patch | evidence | confidence |
|---|---|---|---|---|---|---|
| 0xb20d0 `48 63 05 ..` | 0x489130 (`*(lc + 3 + p) = 2`) | 0x489134 / 0x89134 | `8b 0d 88 e6 75 00` | `31 c9 90 90 90 90` | same body as b20d0 | verified |
| 0xb20e0 `48 63 15 ..` | 0x4e2800 (`if 0, = 1`) | 0x4e2800 / 0xe2800 | `a1 88 e6 75 00` | `31 c0 90 90 90` | same body as b20e0 | verified |
| 0x9e547 `48 63 05 ..` | 0x4ea370 (`+3 + *(p+0x70)`, 64 `*(p+0x80)`) | 0x4ea374 / 0xea374 | `a1 88 e6 75 00` | `31 c0 90 90 90` | same body as 9e540 | verified |
| 0xa4cbf `8b 15 ..` | 0x5646c0 (DObj by the entity's handle +0x152 with lc; "Cannot find tag '%s' on entity") | 0x5646c0 / 0x1646c0 | `a1 88 e6 75 00` | `31 c0 90 90 90` | 64 a4c10's middle part is split in 32 into 0x4788a0 (type 1 / 0xd test, fallback dvars by flags 8 / 4, as a4c10) + 0x5646c0; ESI = entity | likely |
| 0x934b0 `8b 0d ..` (-> a5f10) | 0x4e6a90 (-> 0x41d550) | 0x4e6a90 / 0xe6a90 | `a1 88 e6 75 00` then `mov [esp+4],eax; jmp` | `31 c0 90 90 90` | 64 121490 calls 934b0 then 94bc0; 32 0x4431e0 calls 0x4e6a90 then 0x4e7460; 0x41d550 = a5f10 (local entity list per lc, stride 0x64 vs 0x78, same free-list code) | verified |
| 0x94bc0 `8b 0d ..` (-> a23c0) | 0x4e7460 (-> 0x4aa9b0) | 0x4e7460 / 0xe7460 | `a1 88 e6 75 00` then `mov [esp+4],eax; jmp` | `31 c0 90 90 90` | order in 0x4431e0; 0x4aa9b0 sorts and walks type 0xd like a23c0 | verified |
- The 32 patch check should compare the abs32 in the instruction with `base + 0x35e688`, not a rel32 as in 64.

### begin_frame_hook - 64-bit RVA_BEGIN_FRAME 0xc840, native.c line 3210: mode 11 parts
- The 32-bit hook is already the call site **0x466831** (rva 0x66831) in the cgame draw -> 0x50d800 (`#define RVA_BEGIN_FRAME_CALL`; render.md). Bytes at 0x466831:
  `e8 ca 6f 0a 00 68 48 3a 78 00 e8 c0 5b 0b 00 e8` (rel32 -> 0x50d800). The function's bytes are `51 53 56 e8 88 67 f6 ff 33 db 84 c0 74 1b 38 1d`.
- Mode 11 with exp11frames >= 1: `second_pass = 0` during player 2's draw, so player 2's begin-frame runs. Observed in 0x50d800:
  - it waits "wait_renderer2" with the predicate **0x4d5ba0** (`[0x1551b60] <= 1`, 64: 0x2039f0 `[0x1429b5c] < 2`)
  - it flips 0x1c93264 and sets the frame data 0x1c93238 = 0x1c93300 + idx*0x12bb00
  - it sets +0x419c4 = the scene count (0x1c931c8, 64 0x41136f0), +0x419c8 = the scene array, +0x419bc = the list fields (64 +0x41a28)
  - unlike 64, it calls 0x4a07f0 when the scene count is 0, *before* the flip. The mod's mid-frame close (0x50edc0) zeroes that count, so player 2's begin also runs it. Not checked for side effects.

  No new address for the hook itself.
- confidence: verified

## The exp11frames sequence in the cgame draw hook (native.c lines 4381-4440, 4512)

All functions are called from the main thread inside the cgame draw (hooked at 0x4666c0).
| 64 | role | 32 VA (rva) | convention / bytes | evidence | confidence |
|---|---|---|---|---|---|
| 0xc800 | write end marker 0x40000 | **0x50d7c0** (0x10d7c0) | cdecl void(void); `8b 0d 68 32 c9 01 8b 51 04 a1 60 32 c9 01 2b c2` | writes halfwords 0, 4 = dword 0x40000 into the ring at [0x1c93268] (64 0x458fab0) | verified |
| 0xc7e0 | 2D section start -> fd+0x41a40 | **0x50d7a0** (0x10d7a0) | cdecl void(void); `a1 68 32 c9 01 8b 48 04 03 08 8b 15 38 32 c9 01` | writes `fd + 0x419cc` (64 +0x41a40) | verified |
| 0xe900 | command 0x80017 | **0x50f1f0** (0x10f1f0) | cdecl void(void); `8b 0d 68 32 c9 01 8b 51 04 8b 41 08 56 8b 35 60` | 8-byte command 0x17, 8, 0 | verified |
| 0xe440 | close the list | **0x50edc0** (0x10edc0) | cdecl void(void); `53 33 db 38 1d 80 ef c8 01 74 72 a1 38 32 c9 01` | end marker; clears `*(fd + 0x419bc) + 4..0xc` (64 `*(fd+0x41a28) + 8..0x18`); flips 0x1c9326c (64 0x458fa60); zeroes the scene count 0x1c931c8 (64 0x41136f0). **32 only**: the whole body is under `if ([0x1c8ef80])` (renderer up) and it first copies the debug plumes into `fd + 0x419e4` (0x51e730) | verified |
| 0xc4c0 | end-frame | **0x50d630** | cdecl (unsigned) | above | verified |
| 0x301e0 + rt_frames_done | wait for the render thread with the worker help | **0x52afc0** (0x12afc0), **predicate in ESI** | `51 53 57 ff d6 85 c0 75 4d 8b 3d 68 11 69 00 8b`; never writes ESI; preserves EBX/EDI; clobbers EAX/ECX/EDX | callers set `mov esi, pred` (0x5398a5, 0x50d838, 0x506f01) | verified |
| 0x1429b5c | frames handed to the render thread and not finished (rt_frames_done) | **0x1551b60** (rva 0x1151b60) | dword | 0x40e3c0 `push 0x1551b60; call [0x691168]` (an import taking the counter's address, where 64 204230 does `LOCK inc`); 0x4d5ba0 tests `<= 1` as 64 2039f0 `< 2`; 0x4287b0 also references it and sits where 64 calls 203aa0 in the back-end frame (likely the decrement; not decompiled) | verified (address); likely (inc/dec roles) |
| 0x7b60 | clear the scene's model records | **0x507410** (0x107410) | cdecl void(void); `a1 98 c4 c5 01 8b c8 c1 e1 04 2b c8 03 c9 56 03` | the same five memsets in the same order: 64 0x40dd760 (n 0x40dd754 * 0x90), 0x40ef760 (n 0x40dd758 * 0x90), 0x40f09f8 (* 0x58), 0x4108600 (* 0x30), 0x410ec00 (0x1000) -> 32 0x1c5c4a4 (n 0x1c5c498 * **0x78**), 0x1c6b4a4 (n 0x1c5c49c * 0x78), 0x1c6c67c (* 0x48), 0x1c80284 (* 0x28), 0x1c85884 (0x1000); the same conditional 0xff fills | verified |
| 0x410fd00 | current frame data (main thread) | **0x1c93238** (0x1893238) | pointer | above | verified |
| 0x4113728 / 0x411372c | window width / height | 0x1c8eda4 / 0x1c8eda8 | int | render.md | verified |
| 0x3bd808 | per-player view rectangle (normalised x, y, w, h; what 0xbd520 returns) | **0x72f3ec** (0x32f3ec) | 4 floats, image holds 0, 0, 1, 1 | 64 bd520 `return &0x3bd808`, 32 **0x4a6fd0** `return &0x72f3ec` (`b8 ec f3 72 00 c3`); 0x4a6fd0's callers are the shellshock pieces 0x4df4e0 (= b75b0, same body), 0x452f30, 0x56ddb0 | verified |
| 0x43c630 / 634 / 638 / 63c | cgame view rectangle in pixels (x, y, w, h) | **0x75f008 / 0x75f00c / 0x75f010 / 0x75f014** (0x35f008..) | ints | 64 a6680 writes x = 0 and passes &w, &h, &aspect to ca230 (video w, h, aspect); 32 0x433f30 (= a6680, render.md / clientctx.md) writes `[0x75f008] = 0`, pushes 0x75f010 / 0x75f014 / 0x75f018 to 0x476490 (the same body as ca230: 0x894374, 0x894378, 0x894384). 64 be710 copies the 4 into refdef 0x42d2f8..0x42d304; 32 0x571120 copies 0x75f008 -> 0x77fad0, 0x75f00c -> 0x77fad4, 0x75f010 -> 0x77fad8, 0x75f014 -> 0x77fadc | verified |
| 0x43c640 | cgame aspect (RVA_CG_ASPECT) | 0x75f018 (already in native.c) | float | render.md | verified |
- Derived: 0x75f008..0x75f018 are below 0x75f280, the lower edge of the 32-bit cgame range 0a in [clientctx.md](clientctx.md), so `ctx_swap` does not swap them in 32 bits. The 64-bit
  comments "one per context" and "part of each context's cgame block" do not hold. The code still sets the values before each draw (lines 4321-4329, 4449-4460), so the draws are right.
  After the frame the shared copy holds player 2's rectangle until the next frame's `exp11_pixels(0, ...)`. If that matters, save and restore it around player 2's draw.

## The D3D clear and present branches (native.c lines 1180, 2171-2200, 2367, 2556-2580)
- `before_present_inner` mode 11 (line 1180: shot11, burst11, skip_rotation, exp11_cycle_shot) and the tracepops branch (2367) use no game address.
- `t_clear` (2171-2200, exp11clip and clearlog11): back end's frame data `XR64(0x4358f58)` -> **0x221e5b4** (rva 0x1e1e5b4); `XR64(0x4113728)` / `XR64(0x411372c)` -> 0x1c8eda4 / 0x1c8eda8.
  `p2_fd` is the main thread's 0x1c93238 at player 2's end-frame. The back end's 0x221e5b4 holds the same object while it draws that frame: it is set from the handed-over
  pointer at 0x539824 (render thread) or 0x50d6bb (single thread). The image holds 0x01c93300 there, the first frame data set. So the comparison carries over.
- `before_present` / `be_skip_present`: see be_end_hook. In 32 bits the Present the back end makes is the swap chain's (0x539440, vtable +0xc), as in 64 bits (+0x18).
- `native_nat_clear_rect`, `native_nat_frame_hidden`, `native_final2d_entry` (0x5c010), `ng_skip_reset_2d`: native renderer (natboot) only, 64-bit only. Not needed for 32 bits.
- confidence: verified

## Frame data fields (fd = 0x1c93238 on the main thread, 0x221e5b4 in the back end)
| field | 64 | 32 | evidence |
|---|---|---|---|
| current scene index | +0x41a30 | **+0x419c0** | 45690 / 5397e0; the pass reads it at 0x55365b / 0x553712 |
| scene count | +0x41a34 | **+0x419c4** | same; begin-frame writes it |
| scene array pointer | +0x41a38 (8 bytes) | **+0x419c8** | same; begin-frame `0x1eea900 + i*0x3900` |
| 2D section start | +0x41a40 | +0x419cc | c7e0 / 0x50d7a0 |
| command list fields pointer | +0x41a28 | +0x419bc | e440 / 0x50edc0; begin-frame |
| **frame flags** (bit 0 begin, bit 1 show; 0x80 the mod's mark) | **+0x41a60** | **+0x41a38** | c4c0 / 0x50d630 store; only 2 functions touch it in each build |
| view count | +0x419e8 | +0x41980 | render.md; 0x507b10 at 0x507bc4 |
| view infos (0x150 each), rectangle at +0x130 | +0x1100, +0x130 | +0x1100, +0x130 | render.md; 0x507b10 `imul edx,0x150; lea esi,[edx+eax+0x1100]` |
| scene stride / max scenes | 0x13a0 / 4 | **0xe40** / 4 | above |
| scene's 2D command list pointer | scene+0x308 | scene+0x2f4 | 0x507b10 at 0x507b58 (render.md) |
| the pointer c4c0 tests `> 0x40ffff` | +0x419c0 (pointer!) | +0x4196c | c4c0 / 0x50d630 / 0x50d4e0 (**do not confuse with 32's +0x419c0, the scene index**) |
| light count (scenekeep only) | +0x421e0 | +0x41fc0 | 5c240 / 0x5533f0 `if (*(fd+0x41fc0)) 0x558db0` (one site) |

## QA-only and off-by-default parts (not needed for the port)
- `scenekeep` (FD_SCENE_LO/HI 0x41940..0x62980, fd_scene_parts, glob 0x4113400, default 0): **unknown** in 32 bits. Only the light count above was seen; the block
  bounds and the parts list would have to be redone. Keep it off in 32 bits.
- `p2skindump`, `traceprims` / `body_rec_log`: the records are 0x1c5c4a4 + i*0x78, count 0x1c5c498 (verified above). The record fields (+0x28 state, +0x30 pose, +0x70 entity
  bits, +0x78 DObj in 64) are **unknown**, and so are 0x40f09e4 / 0x40f09e8.
- `rslog`: fd +0x41ea8 (spot lights) -> likely **+0x41c8c**: the single store right before the same "wait" in the scene fill, 32 0x506ef6 against 64 0x6dfa. Not
  verified. +0x41a68 is **unknown**, and so is the spot dvar 0x4115fe0.
- `camlog`: refdef origin 0x42d310 -> 0x77fae8 (refdef 0x77fad0 + 0x18, render.md; derived, likely).
- `exp11frames=0` (`split_smc_redirect`: +0x41964 / +0x41968, 0xb7f80 / 0xd2f80): not mapped. Port only exp11frames >= 1 (the default is 2).
- `exp11raw`: RVA_D3D_DEVICE -> 0x1c8c088 (render.md).

## 64-to-32 function pairs used above (for the next agent)
45770 -> 5398e0, 45690 -> 5397e0, 45060 -> 5394e0 (+ 539440 Present), 44d00 -> 539370 (fastcall ECX = fd), 45370 -> 539640, 453c0 -> 539680, 5c620 -> 5538e0, 5c240 -> 5533f0,
51070 -> inlined (540f60 + vtable Clear), d5890 -> 57adc0 (+ tail 57ac90), c4c0 -> 50d630, 204230 -> 40e3c0, 2039f0 -> 4d5ba0, 301e0 -> 52afc0 (ESI), 45010 -> 5393f0,
c800 -> 50d7c0, c7e0 -> 50d7a0, e900 -> 50f1f0, e440 -> 50edc0, daf00 -> 4f6c20, c840 -> 50d800, 7b60 -> 507410, ca4b0 -> 449660, 8210 -> 507b10, 5160 -> 5064b0,
bd3d0 -> 4a31e0, b75b0 -> 4df4e0, b7670 -> 452f30, bd520 -> 4a6fd0, 93ac0 -> 4f3020, a2640 -> 4beed0, a6680 -> 433f30, ca230 -> 476490, be710 -> 571120,
b20d0 -> 489130, b20e0 -> 4e2800, 9e540 -> 4ea370, 121490 -> 4431e0, 934b0 -> 4e6a90, 94bc0 -> 4e7460, a5f10 -> 41d550, a23c0 -> 4aa9b0, a4c10 -> 4788a0 + 5646c0 (split).
The matcher had 7b60 -> 507410 and ca4b0 -> 449660 right. Its d5890 -> 57ac90 is only the tail of the screen update, and 44d00 -> 524700 is a callee of the frame begin (0x539370 calls 0x524700), not 44d00 itself.

## Open (unknown)
- The scenekeep block and its parts in 32 bits. The model record fields. 0x40f09e4 / 0x40f09e8, 0x4115fe0, 0x4113400, fd+0x41a68.
- Whether 0x4a07f0, which 32's begin-frame runs when the scene count is 0, does something a second begin per engine frame should not repeat (64 c840 has no such early call).
- Whether 0x51e730 (the debug plume copy, 32-only in the list close) matters when the list is closed twice per engine frame. It looks harmless (it copies into each frame's own data).
- Nothing here was run in the game.

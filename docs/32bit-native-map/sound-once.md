# Sound once ("evsnd1"): world event sounds and effect sounds started once in split screen

64-bit fix: `src/native.c` (`snd_count_stub`, `ev_check_hook0/1`, `fx_sound_queue_hook`, around lines 9670-9840; install around line 10500, all under `#ifdef _WIN64`).
Read 2026-11-17 from the Ghidra projects (64: iw4sp.exe; 32: image32.bin, official build) and `D:\coop-test\mw2-32\mw2coop\image32.bin` (bytes, read only). Nothing was run.
RVA below = VA - 0x400000 (what `base + XR()` takes in the 32-bit build).

Tool note: `tools/ghidra/run.sh` writes every run's output to one log, `${TMP}/mw2coop-ghidra.log`. Runs started at the same time (other agents, even with their own
`G_AGENT`) overwrite each other's log: one of my first outputs had another agent's decompile appended. Give each parallel run its own `TMP` (I used a folder in my scratchpad).

---

### world sound start (SND_PlaySoundAlias-like, 13 arguments) - 64-bit rva 0x23a9b0, native.c line 9681 (`snd_count_stub`), install line 10507
- 32-bit: **VA 0x66d250, rva 0x26d250** (map.txt guess `23a9b0 66d250 c`, confirmed).
- convention: **cdecl, all 13 arguments on the stack, no register inputs, plain `ret` (caller cleans up)**. Return value in EAX (-1 = no sound).
- arguments (stack offsets at entry, [esp] = return address): +0x04 alias, +0x08 alias (second), +0x0c float lerp, +0x10 float volume, +0x14 float pitch,
  +0x18 entity number, +0x1c origin (float *), **+0x20 handle pointer (int *, may be NULL)**, +0x24 .. +0x34 the last five (+0x34 = recursion depth).
- bytes at VA: `83 ec 40 80 3d 50 0b bf 01 00 c7 04 24 ff ff ff`
  = `sub esp,0x40` (3) ; `cmp byte ptr [0x1bf0b50],0` (7) ; `mov dword ptr [esp],-1` (7, ends at +0x11) ; then `jnz` (relative, must not be copied).
  A hook pattern of the first **10 bytes** (`83 ec 40 80 3d 50 0b bf 01 00`) holds two whole instructions with no relative operand: the 32-bit `hook_function` trampoline can copy them as they are.
- observed (32, disassembly): `mov eax,[esp+0x60]` right after `sub esp,0x40` (= entry +0x20), `test eax,eax; jz; mov dword ptr [eax],0xffffffff`; the "sound off" exit is
  `or eax,0xffffffff; add esp,0x40; ret`. Every exit seen is a plain `ret` (0x66d269, 0x66d338, 0x66d37a, 0x66d4c8). The caller 0x4b3060 pushes 13 dwords and does `add esp,0x34` after the call.
  At entry ECX is only pushed as a stack slot and overwritten (`push ecx` then `fstp [esp]`), EAX is written before it is read, EBX/EBP/ESI/EDI are pushed: no register inputs.
- evidence (both decompiles compared):
  - same start: sound-system flag (64 `DAT_141a23f40`, 32 `DAT_01bf0b50`) == 0 -> return -1; `if (handle) *handle = -1` with the handle as the 8th argument in both.
  - same call of the 8-argument helper with arguments 1..8 (64 0x237c70, 32 0x66ce10; 32 pushes arg8, arg7, arg6, three floats, arg2, arg1) and on success the chained-alias call with
    `depth + 1` (64 0x23a770, 32 0x66d150) and `return 0`.
  - same flag decode `alias->flags >> 9 & 0x3f` into a 0x50-stride table (64 0x141a2902d, 32 0x1bf5c29), same `>> 7 & 3` dispatch to 1 (64 0x2acdf0/0x2aced0, 32 0x66caf0, handle in EDI) and 2 (64 0x23ddd0, 32 0x66cbc0).
  - same callers' argument shapes: the pending-queue player (64 0x23a1f0, 32 0x4b3060) calls it with `(alias, alias, 0, 1.0, 1.0, entity, &origin, NULL, 0, 0, 1.0, 1, 0)` in both;
    the alias wrapper (64 0x23a940, 32 0x40ae30) `if (!alias) return -1; return start(alias, alias, 0, 1.0, a2, a3, a4, NULL, a5, 0, 1.0, a6, 0)` in both.
  - the event handler reaches it in both: 64 0x9f9a0 -> 0xa6e60 -> 0x23a940 -> 0x23a9b0; 32 0x49a4d0 -> 0x4d3b90 / 0x4f56c0 / 0x4f1d00 -> 0x40ae30 -> 0x66d250 (CallChain depth 3).
- callers (CallChain depth 1): 64 has 13 code callers, 32 has 11 (0x40ae30, 0x66d150, 0x66d5d0, 0x4db7c0, 0x488750, 0x4dfdd0, 0x4b3060, 0x4d9190, 0x418850, 0x66e660, 0x66e7c0). The two missing
  ones were not mapped (unknown whether inlined in 32 or reached otherwise).
- not through this function in either build: 64 0x23c370 calls 0x23ddd0 directly, 32 0x66f970 calls 0x66cbc0 directly (the same one other path in both; not muted in 64 either).
- conclusion: the 32-bit mute stub can return early with a plain `ret`: `mov eax,[esp+0x20]; test eax,eax; jz 1f; mov dword ptr [eax],-1; 1: or eax,-1; ret`. It must touch no
  callee-saved register and leave the x87 stack alone. For the count path (`snd_note(alias, ret)`): alias at [esp+4], return address at [esp] at entry; EAX/ECX/EDX may be clobbered before the
  jump to the trampoline (no register inputs).
- confidence: **verified**

---

### entity event checks of the snapshot entities - 64-bit calls at rva 0x9c924 -> 0x9f840 and 0x9c954 -> 0x9f940 (`ev_check_hook0/1`), native.c line 9727, install line 10512
- containing function: 64 rva 0x9c810 (331 bytes, called from the cgame draw 0xbca00) = **32 VA 0x4f6220, rva 0xf6220** (map guess `9c810 4f6220 c`, confirmed; called from 0x4666c0 = 64 0xbca00).
- **site A** (type < 0x10, the regular entity events; 64 0x9c924 -> 0x9f840, `ev_check_hook0`):
  - 32: call at **VA 0x4f6321, rva 0xf6321**, target **VA 0x438f80, rva 0x38f80** (map guess confirmed; also confirmed in render.md).
  - bytes at site: `e8 5a 2c f4 ff 8b 0d 90 79 86 00 83 c4 08 83 c7` (0x4f6326 - 0xbd3a6 = 0x438f80)
  - bytes at target: `56 8b 74 24 0c 8b 8e 88 01 00 00 8b 86 64 01 00`
- **site B** (type >= 0x10, the event entities; 64 0x9c954 -> 0x9f940, `ev_check_hook1`):
  - 32: call at **VA 0x4f631a, rva 0xf631a**, target **VA 0x4f4d20, rva 0xf4d20** (map guess `9f940 4f4d20 p`, confirmed).
  - bytes at site: `e8 01 ea ff ff eb 05 e8 5a 2c f4 ff 8b 0d 90 79` (0x4f631f - 0x15ff = 0x4f4d20)
  - bytes at target: `56 8b 74 24 0c 83 be 88 01 00 00 00 75 38 0f b6`
  - **order differs from 64**: in 32 the type >= 0x10 call (B) comes first in memory (0x4f631a), then A (0x4f6321). Do not pair them by position.
- convention (both targets): **cdecl `(int lc, centity *cent)`**, both on the stack, plain `ret` (caller cleans up). Observed: the caller pushes once before the branch
  (`push eax` = centity, `push 0` = lc at 0x4f6315/0x4f6316), then `jc` picks the call, and `add esp,8` at 0x4f632c after the join. Both targets start `push esi; mov esi,[esp+0xc]` (arg 2),
  no register input read. lc is the constant 0 in both builds (64: `xor ecx,ecx`).
  The 64 hook functions `void hook(int lc, uint8_t *cent)` fit as cdecl C functions; the 32-bit `hook_call` writes the rel32 straight to them.
- centity offsets used by these functions (observed in both decompiles):

  | field | 64 | 32 |
  |---|---|---|
  | last event handled (per context) | +0x194 | **+0x188** |
  | event sequence | +0x170 | **+0x164** |
  | event bytes [4] | +0x174 | **+0x168** |
  | event parms [4] | +0x178 | **+0x16c** |
  | eventParm (swapped in during the call) | +0xe8 | **+0xdc** |
  | eType byte | +0xe0 | **+0xd4** |
  | entity stride / array | 0x1a8 / 0x441070 | **0x198 / VA 0x792848** |
  | the caller's skip test (type 8): byte, ushort, global | +0x157, +0x160, 0x40caa0 | **+0x14b, +0x154, VA 0x75f2a0** |
  | "events enabled" global before the loop | 0x51debc | **VA 0x85e8cc** |
  | snapshot pointer (entity count +8, numbers from +0xc) | 0x51b080 | **VA 0x867990** |
- evidence: 0x9c810 / 0x4f6220 decompile line for line alike (first loop 0x7ff + add-entity 0x9c960 / 0x46df50, second loop +0x100c/+0x1010 with 0x122b40 / 0x43e7b0, third loop with the
  event calls under the same type tests). 0x9f840 / 0x438f80 alike (the `seq - last` clamp to 4, `& 3` ring, 0x73ca0 / 0x4a8e10 with `event + 0x10`, the event handler 0x9f9a0 / 0x49a4d0
  `(lc, cent, event, 0)`). 0x9f940 / 0x4f4d20 alike (`if (last == 0) { last = 1; 0x73ca0/0x4a8e10(eType); 0x9ce30/0x4e2130(cent); handler(lc, cent, eType - 0x10, 0); }`).
- other callers of 0x438f80 (not hooked, as in 64 where 0x9f840 is called from the pass2 and the cgame draw too): 0x4202d0 (pass2), 0x4666c0 (cgame draw). 0x4f4d20 has only the one caller.
- confidence: **verified**

---

### pending sound queue of the effects system - 64-bit rva 0x237a70 (replaced whole by `fx_sound_queue_hook`), native.c line 9785, install line 10524
- 32-bit: **VA 0x40a3c0, rva 0xa3c0** (no map guess; found from the queue player 0x4b3060 = 64 0x23a1f0 and the references to its count).
- convention: **cdecl `(void *alias, int ent, const float *origin)`**, all on the stack (+4, +8, +0xc at entry), plain `ret`, no register inputs.
- bytes at VA: `8b 0d c4 96 bf 01 83 f9 20 75 19 89 4c 24 0c c7`
  = `mov ecx,[0x1bf96c4]` (6, absolute operand: a trampoline can copy it) ; `cmp ecx,0x20` ; `jnz` ...
- data:

  | | 64 | 32 |
  |---|---|---|
  | count (dword) | rva 0x1a2cf58 | **VA 0x1bf96c4, rva 0x17f96c4** |
  | entry array | rva 0x1a2d270 | **VA 0x1bf96c8, rva 0x17f96c8** |
  | entries | 32 (0x20) | 32 (0x20) |
  | stride | 0x18 | **0x14** |
  | alias pointer | +0x0 (8 bytes) | **+0x0 (4 bytes)** |
  | entity (int) | +0x8 | **+0x4** |
  | origin (3 floats) | +0xc | **+0x8** |

  32 array end: 0x1bf96c8 + 0x20 * 0x14 = 0x1bf9948. Neither the count nor the array lies in a 32-bit context range of native.c (`ranges[]`, `#else` branch): one shared queue, as in 64.
- **difference from 64**: when the queue is full, 32 does not return silently: it rewrites its own stack arguments and tail-jumps to VA 0x461af0 (print error, arguments `(0x14, "ERROR: too many FX sounds %d\n", count)`,
  string at VA 0x6e3974). 64 just returns. A whole replacement in 32 drops that message (harmless); hooking and calling the original keeps it.
- caller: only **VA 0x44c4e0, rva 0x4c4e0** (= 64 0xa6280: `if (DAT 0x774c24 <= a4) { alias = 0x480280(a3); if (alias) queue(alias, 0x7fe, a2); }`, 64 the same with 0x26aca0 and `_DAT_140422438`),
  itself called from 0x595800 (= 64 0x11be20, the effects system's sound element, by position in the chain). Call site **VA 0x44c509, rva 0x4c509**: `e8 b2 de fb ff 83 c4 0c c3 cc cc cc cc cc cc cc`
  (0x44c50e - 0x4214e = 0x40a3c0; `add esp,0xc` after it).
- other users: player VA 0x4b3060 (= 64 0x23a1f0: reads the entries, calls 0x66d250, sets count 0); reset VA 0x4fa580 (= 64 0x239840: count = 0).
- conclusion: two safe ways in 32: (a) `hook_call` at the one call site 0x44c509 with a cdecl `void hook(void *alias, int ent, const float *origin)` that returns for context 1 and otherwise calls the
  original; or (b) `hook_function` at 0x40a3c0 with the 6-byte pattern `8b 0d c4 96 bf 01`. A copy of the 64 whole replacement would need the 32 offsets above (stride 0x14, entity +4, origin +8).
- confidence: **verified** (decompiles of 0x237a70 / 0x40a3c0, 0x23a1f0 / 0x4b3060, 0xa6280 / 0x44c4e0, 0x239840 / 0x4fa580 compared; same 0x20 limit, same 0x7fe entity, same field order)

---

## Risks for the 32-bit port (conclusions, not tested)
- The 64 stub is x64 asm (`%rip`, `0x40(%rsp)` for the 8th argument): 32 needs its own stub, handle at `[esp+0x20]`, plain `ret`, no `ret N`.
- The expected-byte patterns hold absolute addresses (0x1bf0b50, 0x1bf96c4). On a build whose addresses moved (the iw4x-sp 159 profile through `XR`), the compare fails and the hook is refused:
  safe, but the 159 addresses of all three parts are unknown here.
- Sites A and B are in the opposite order from 64 (B at 0x4f631a, A at 0x4f6321); the install check must test each site's own target.
- `snd_mute` is one global flag set around context 1's event call: a sound started by another thread during that call would be muted too (same in 64). Whether the 32-bit build starts
  sounds from another thread at that time: unknown.
- The 64 comment measured the double queue in mode 10, where each context runs its own effects system. The 32 `ranges[]` has the effects range (RANGE_FX 10, `[native] fxsplit`; in 64
  `fxsplit` defaults to off in mode 11). Whether, with one shared effects system, context 1's queue entries are duplicates or the only copy of some effect sounds: unknown; check
  with a sound count (`sndcount`) per context before turning the queue part on in 32.
- 32 has 11 direct callers of the sound start where 64 has 13; the two 64 callers without a 32 counterpart were not identified. Any 32 sound start path that does not pass through 0x66d250 is not muted (unknown whether one exists besides 0x66f970 -> 0x66cbc0, which 64 has too).

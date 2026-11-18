# Finding the game's addresses by signature

The mod patches `iw4sp.exe` at addresses it learned from one build of the game (the *reference build*: the official 32-bit Steam build, the
64-bit Steam build). A build with other addresses used to mean "unknown build: nothing is patched". This page is about how the mod now looks
the addresses up in a build it has not seen: by byte signatures of the code around them.

## What is there

| Piece | Where | What it does |
|---|---|---|
| Signature files | `mw2coop/addresses-x86.txt`, `addresses-x64.txt` (`content/release/`) | for every address the mod uses, byte patterns of the code around it, `??` for bytes that depend on where things are |
| Generator | `tools/addrsig.py gen` | makes a signature file from the reference build (an image dump or the exe) |
| Python check | `tools/addrsig.py verify` | resolves a signature file against another build, optionally against a table of known answers |
| Resolver | `src/addr.c`, `src/addr.h` | the same rules in C, run by the mod at start on a build it does not know; a hash index of the code makes a full run take 0.1 to 0.4 s |
| Test program | `tools/test_addr.c`, `tools/test_addr.sh` | runs the resolver on a game exe file |

Known builds are not affected: the resolver runs only when the code hash (32-bit) or the PE timestamp (64-bit) is not a known one.

## The signature file

One signature per line (the header of `tools/addrsig.py` has the details):

```
CODE 0x00cf10 off=16 sig=8B 44 24 04 ?? ?? ?? ?? 56 ...     code point at reference rva 0xcf10, 16 bytes into the pattern
DATA 0x33ba88 abs cap=4 sig=A1 ?? ?? ?? ?? ...              a global: the 4 bytes at offset 4 are its absolute address
DATA 0x1b21e90 rip cap=3 tail=0 sig=48 8D 0D ?? ?? ?? ?? ... a global: a rip-relative displacement at offset 3 (64-bit)
DATA 0x2e9fd0 abs cap=8 delta=4 sig=...                      the code reaches the global through a field 4 bytes into it
DATA 0x331aec derive                                         no signature: taken from its neighbours
```

A signature is made only if it matches exactly one place in the reference build. Several signatures (up to 24 for code, 16 for data) are made per
address from windows of different size and position around it.

## The rules (the Python check and the C resolver are the same)

1. A signature counts if it matches exactly **one** place in the executable sections of the running build.
2. An address is accepted if two signatures agree on it, or, for a code address, one signature with 40 or more fixed bytes; and no other
   candidate has as many votes. Anything else stays unresolved. **The mod must not use an unresolved address.**
3. A global that no signature found is derived from the two nearest accepted globals below it and the two nearest above it (within 16 MB), if all of them moved by the same amount.
   On the 1.0.159 build this added 3 right answers and no wrong one; on the 64-bit build it takes 74 of 120 to 115.
4. The window for finding a global through the code that uses a field of it is limited to -0x20 .. +0x100 bytes. A wider window found 3 wrong
   addresses in a test (the code referenced another entry of the same table in the other build), so it is not used.

## Same layout

If every address found is the reference address itself, at least 90% of the code addresses (and 40) and at least half of the data addresses
were found, the build has the reference build's layout: the same addresses with some bytes changed. The mod then runs with the known addresses
(`addr_layout_same()`); each hook still checks the bytes it patches and refuses if they differ.

## Measured

| Test | Result |
|---|---|
| 32-bit reference (official) -> the 1.0.159 exe of the iw4x client; the 241 addresses with known answers (`src/addr159.c`) | **215 right, 0 wrong**, 26 not resolved |
| 2026-11-10, the signature file after the guards, climbads, gfxopts and level-start hooks were added (312 addresses), offline on four exes | official image: 305 found, all identity (the 7 not found are the builtin entries, which `game_builtin_entry` finds by name, and 0x29116c); the second official-layout exe: 304 of 312; the iw4x-sp 1.0.159 exe: 272 of 312, **219 right, 0 wrong** against the answers known by hand; the COD-MOD exe (3514008 bytes): 272 of 312 (no known answers to compare) |
| 2026-11-11, in the game: a 64-bit pair (Trainer `bay`, then the Endgame boat in the real briefing mode) with `MW2COOP_FORCE_UNKNOWN=1 MW2COOP_MOVED=1 MW2COOP_FORCE_RESOLVED=1` (the resolver's tables in use, 14 addresses on the zero block) | no crash on either copy, `bay` PASS 5 on both; rumble builtins found by name (3 of 6), the briefing wait switched itself off (film flags not found), the level started 4 s after loading |
| the same in C (`test_addr.exe`) | identical to the Python result, 0.26 s |
| 32-bit reference -> a second official-layout exe (D:\ install of the owner, 33 patched places, 174 differing bytes; the layout is identical, so every answer must be identity) | **235 of 241 found, all identity**; in the game the mod recognised the layout, installed all its modules and started split screen with the same hooks as on the official exe |
| 64-bit reference -> itself, inside the running game (forced) | 115 of 120 found in 110 ms (code 55 of 55, data 60 of 65), all identity; this is the "same layout" case, so a 64-bit exe with the reference layout now runs |

## A build whose addresses moved (experimental)

`[native] movedbuilds=1` (or `MW2COOP_MOVED=1`) lets the mod, 32-bit or 64-bit, run on an exe whose addresses it had to look up (85% or more found). The code asks `addr_xr(reference rva)`:
the same number on a known build, the resolved one on this build, and for an address that was not found the offset of a private block (a block of `ret` bytes for code, zeros for
data; `# code <rva> <rva>` lines of the signature file give the reference build's code ranges) so a hook sees the wrong bytes and refuses, a read gives zeros and a call returns.
The player-method table of mirror.c is found by the method names instead of by address. Tried on the iw4x-sp 1.0.159 exe: 213 of 241 found, no crash, most hooks refused (that exe's
code differs from the official one: the iw4x profile build is for it). The 64-bit code goes through `XR()` the same way since 2026-11-10 (the `RVA_` macros, every `base + XR(0x...)`, the tables of ranges, call sites and hook sites; 304 addresses in
`addresses-x64.txt` (2026-11-13), 290 of them found with signatures on the reference exe, all themselves; the other 14 are renderer and dvar globals that no signature reaches, the neighbour
rule may place them). Not covered: diagnostics that are off by default (the `scenescan`, `npcsnap` and other test switches read raw addresses) and `crash.c`'s check of one place of
the known build. **The 64-bit moved-build path has only been proven on the reference exe** (every address resolves to itself; the compiled code of the 64-bit DLL was byte-identical
before `XR()` became the resolver): no second 64-bit build exists here to run it on.

## The native renderers need the known build (2026-11-13)

`natboot_install()` runs from DllMain, before `game_init()` has looked at the build, and the native Direct3D 11 / 12 renderers replace the engine's back end function by function with
hundreds of addresses and structure layouts of the reference exe (`src/dx/`, no byte check at the hook sites, outside the signature file). On another 64-bit build they would patch the wrong
bytes, so `install_all()` now requires the reference exe's timestamp (`IW4SP_TIMESTAMP`) and otherwise logs "is not the build the native renderer was written against", stays off and leaves the game
on its own Direct3D 9. The same timestamp is not enough (a patched or unpacked copy): the first 14 bytes of each of the 67 functions `install()` replaces are hashed and compared with `NAT_SITE_HASH`
(`tools/gen_natsites.py <reference exe>` makes `src/dx/natsites_gen.h`; `--check` in `pr_checks.py` keeps its list equal to the `install()` calls); a mismatch also leaves the game on Direct3D 9. The other 64-bit hooks that still used a raw offset (`crash.c`, `guards.c`, the DObj add hook of `native.c`, the QA IWNet switch of `menus.c`) go through `XR()` now.
The new XR addresses of the sign-in hooks (issues 21 and the LAN case) are in both signature files; `tools/gen_addr159.py` collected the 64-bit `XR()` literals of shared code since the 64-bit
routing (it now reads only the lines the 32-bit build compiles, plus the `REF_` macros, and skips 30 known 64-bit literals) and the iw4x table has the four new 32-bit addresses.

## Audit of 2026-11-13 (independence, second round)

* **64-bit literals in shared code.** About 60 uses of 64-bit addresses stood in code that the 32-bit build compiles too (diagnostics, the 64-bit compositor, hooks the 32-bit build never installs). They are `XR64()` now
  (`game.h`: `XR()` on 64-bit; on 32-bit a private block of zeros, `addr_dead_block()`, so a stray use reads zeros instead of an address far outside the image); `tools/gen_addr159.py` no longer needs a skip list.
* **A check for raw offsets.** `tools/check_raw_addresses.py` (run by `pr_checks.py`) fails when 64-bit code of `src/*.c` / `src/*.h` uses `base + 0x....` instead of `XR()` (four scan windows and image bounds carry
  `raw-ok:`). This is the static stand-in for running on a second 64-bit exe, which does not exist here: a moved build is only handled if every address goes through the resolver.
* **Unresolved addresses (14 on 64-bit).** Every consumer was read. Harmless on the zero block: diagnostics (`loadprof`, the dobj tree log, video mode 9), the Enhanced options' reads, the briefing wait (`brief_addresses_ok`,
  already gated), the screenshot and console device lookups (they test for NULL). Fixed: **the entity table guard** read the renderer's device pointer (0x4115920): unresolved it read as NULL and the guard handed out empty entries for every lookup
  of the game, so it is not installed then; **the split screen** swaps ranges of the game's memory (two of them, 0x4812878 and 0x45851b0, are among the unresolved) and draws through the device pointer (0x410fd20): `activate()`
  now refuses with a log line listing what was not found (`split_addresses_ok`), and the rest of the mod runs. Tried on the known build (split works) and with the resolver's tables forced (`MW2COOP_FORCE_UNKNOWN=1`, refused as designed).
* **iw4x-sp.** The 1.0.159 table is static and complete for what the mod uses (316 addresses mapped, `gen_addr159.py`); a different iw4x exe goes through the signature path like any unknown build (277 of 316 found on 1.0.159;
  the split screen is refused when a range is missing), and `python -I tools/gen_addr159.py <image32.bin> <new iw4sp.exe>` makes a new table.
* **Hard-coded UI scripts.** The scripts every button repeated (`click`, `click and close`, `hover`, `leave`) are `UI_S_*` constants in `menus_ui.h`; the other `ui_script_set` strings are single-use. The texts, the mission list, the console
  texts, the menu button tables and the settings reference are files (`.str`, `.csv`, `.menu`) since 2026-11-09 and 11-13.

## Third round (2026-11-17): the 32-bit code, and names that hid a raw number

* **The check covers the 32-bit build.** `tools/check_raw_addresses.py` reads, for each build, only the lines that build compiles (by the `#ifdef _WIN64` nesting): `src/*.c` / `src/*.h` for the 64-bit
  build, the files `build.sh` gives the 32-bit build (with `src/x86`) and the headers for the 32-bit one. Besides `base + 0x....` it finds a cast of a 32-bit VA (`(int *)0x738c6c`) and
  `base + NAME` where `NAME` is a `#define` of a raw number (the form that hid the last ones: the snapshot entity ring of `native.c`, the text measuring hooks of `padglyphs.c` and the rumble
  builtin check of `rumble.c` on 64-bit; the 32-bit Steam join hook of `steamnet.c`, whose expected first bytes held the official build's VA of the co-op flag and are now filled in at start).
  What stays raw carries `raw-ok:` and a reason: image bounds, scan windows, iw4x-sp 1.0.159 addresses in diagnostics that only run on that client.
* **The iw4x-sp table covers every 32-bit file.** `tools/gen_addr159.py` read six source files; `guards.c` and `steamnet.c` use `XR()` too, so on the iw4x-sp client their addresses had no
  entry (`addr159()` gave 0) and `guards.c` carried that client's two addresses by hand. 348 addresses, all mapped; the hand-written pair is gone (the table gives the same numbers).
* **Signature files.** `addresses-x64.txt`: 421 addresses, 409 found on the reference exe (the others are globals no signature reaches; the neighbour rule may place them).
  `addresses-x86.txt`: 348 addresses, 341 found on the official image; on the iw4x-sp 1.0.159 exe 305 found, 297 equal to the hand-checked table, 7 found where the table has no entry
  (features that are off on that client) and one global placed 0x80 off by the neighbour rule (`RVA_VM_STACK_TOP`, read for a log line only).
* **Runs on the unknown-build path** (`MW2COOP_FORCE_UNKNOWN=1 MW2COOP_MOVED=1`, plus `MW2COOP_FORCE_RESOLVED=1` on a build with the reference layout so the tables are used):
  * 64-bit, network pair, Estate, `nativesave`: 409 of 421 found on both machines, every check passed (the checkpoint loaded after a failed mission, both players back on their places).
    The split screen refuses on this path, as designed: two of the ranges it swaps (0x4812878, 0x45851b0) have no signature.
  * 32-bit official exe, split screen, Trainer, `p2walk`: 340 of 348 found, split screen on, player 2 walks with the pad in all four directions.
  * The iw4x-sp 1.0.159 exe through the signatures instead of its table (a build whose code really moved): 302 of 348 found. It **crashed** 14 s in: `padopts.c` registered its
    dvars through two functions that were not found (the `ret` block), got no dvar back and read through it. Fixed: the gamepad options check `addr_resolved()` first and stay at
    their defaults; the block a call to an unresolved function lands in now returns 0 (`xor eax, eax; ret`) instead of whatever `eax` held. Run again: no crash, the split screen
    refuses itself (the cgame draw's bytes differ), the menus, the console and the rest of the mod run.
  * The same exe as a network pair crashed both machines when the level loaded: the briefing wait (`menus.c`, `brief_install`) wrote its jump over a function that was not found,
    that is into the one block every unresolved function shares, so every unresolved function ran the briefing hook and returned 1; the `setdvar` wrapper then read the script
    argument through Scr_GetString (not found on that exe) and copied a string from address 1. Fixed: `brief_install` checks `addr_resolved()`; `hook_jump()` writes nothing
    into those blocks (`addr_in_scratch()`), and `addr_scratch_check()` puts the code block back and logs who wrote it, after each install step and in the menu tick.
    Run again (200 s): both machines load Estate and run to the end of the test time, nothing writes into the block; the scenario's own checks did not report in that time (not verified).
* **Builtins found by name in `.data`.** `find_builtin_entry()` (the fallback when a script builtin's table entry has no address) tested a whole section with one `VirtualQuery`
  region; `.data` is many regions once the game writes to it, so the functions' table there was never searched, and the three rumble functions were "not found" on the 64-bit
  unknown-build path. It walks the regions now and searches only the initialised part of a section; all six rumble builtins are found by name.

## Fourth round (2026-11-18): every address found, moved builds on by default

* **Every address the mod uses is in the files.** The generators collected `XR(0x..)` literals, `RVA_` / `REF_` defines and a fixed list of tables. A name given to `XR()`
  (a `#define` like `P2C_SCMD_CALL`, an array the code walks like `XR(movs[i])`) was missed: six 64-bit addresses (the player-2 server command calls, the clc newest command,
  the cg.localClientNum pins) had no line, and on the unknown-build path they pointed at the harmless blocks, so the split screen hung at the level start (a wait on a zero
  flag). `tools/addrsig.py` (64 bits) and `tools/gen_addr159.py` (32 bits) now take any such name (an array of `{rva, ...}` entries by its first members; nothing under
  0x1000). x64 468 addresses, x86 407.
* **Globals no instruction names directly.** 64-bit code reaches some tables through the image base (`[reg + rva]`): a new `img` kind of DATA line (the captured dword is
  the RVA itself), read by both resolvers; and on 64 bits a second, wider window (±0x800) for a reference into a field of the table. Not on 32 bits: on the iw4x-sp exe
  it placed seven `.rdata` globals 0x80 off; unresolved is safer.
* **Import table slots.** The 32-bit dump's flat `.text` holds the IAT, and the asset lookup's `[0x69116c]` (the bytes `localize.c`'s hook expects) got a code signature
  of the slot's contents (system DLL addresses of the machine the dump was made on): never found elsewhere, so that hook (texts, the model transplant) was off on every build
  found by signature. A slot (4-aligned, it and both neighbours pointers into 0x60000000..0x80000000) now gets data signatures from the code that reads it.
* **Measured.** On the reference builds: x64 468 of 468, x86 407 of 407 (`tools/addrsig.py verify ... --list reference`: none unresolved, none elsewhere). The iw4x-sp
  1.0.159 exe through the signatures: 349 of 407, 336 equal to the hand-checked table (the others: 7 the table marks off on that client, a call site the hook checks by its
  target, a log-only global 0x80 off).
* **A build whose addresses moved runs by default when 95% or more are found** (`addr_moved_allowed()`, both builds); `[native] movedbuilds=1` runs it from 85%, `0`
  never. Checked on the unknown-build path (`MW2COOP_FORCE_UNKNOWN=1 MW2COOP_FORCE_RESOLVED=1`, without `MW2COOP_MOVED`): 64-bit 468 of 468 found, the default lets it
  run, mode 11 split screen on Trainer (5200 draws, no address left to a harmless block but the builtin table entries found by name); 32-bit 407 of 407, the asset hook
  on, mode 11 split screen on Trainer.

## Fifth round (2026-11-18): after player 2's mouse look, the shell shock per view and the knife hand

The day's fixes (`5b08ac8..cd9b74d`) added 10 64-bit and 8 32-bit addresses. Checked again (owner: "check the mod's independence of the game's version after these edits"):

* **Every new address is in the files and goes through the resolver.** `tools/check_raw_addresses.py`: no raw offsets. One was hidden from it: the QA log `cmdlog1` read frame_msec as
  `RVA_KEY_CATCHERS - 0x4140` (a distance between two globals that a moved build need not keep); it reads `XR(0x52fb70)` now. The knife hand's three functions are `REF_` numbers
  (the generators take them; the files came out unchanged), and **the hook is not installed unless all three were found** (`addr_resolved`): the hook calls `DObjGetBoneIndex`
  and `SL_GetString` from inside the animation calc, and an unresolved one would have returned 0 from the harmless block (`SL_GetString` is not found on the iw4x-sp exe through
  the signatures). The shell shock's 64-bit patch and handler slot compare their bytes and the slot's old value first; the 32-bit call goes through `m11_call32`, which checks
  the call and its target: unresolved, both refuse.
* **frame_msec is no longer needed by the split screen.** `split_addresses_ok()` refused the split screen when any swapped range was not found; frame_msec's range (`RANGE_MSEC`)
  only serves player 2's mouse look, so without it the split screen runs as before 2026-11-18 (the range is left out of the swap, a log line says that player 2's mouse does
  not turn the view).
* **Addresses cut by a signature's edge.** frame_msec's 32-bit address was not found on the iw4x-sp exe: one of its two signatures ended in `81 C3 E0 5B 92`, an `add ebx, 0x925be0`
  without the address's last byte, so the generator did not see it as an address and kept it fixed. `tools/addrsig.py` (x86) now looks at 3 bytes on each side of the window
  when it marks wildcards. Regenerated `addresses-x86.txt`: on the official image 415 of 415, on the owner's other exe 415 of 415, on the iw4x-sp 1.0.159 exe through the signatures
  **370 of 415 (was 354), 362 equal to the hand-checked table (was 346)**, the same one wrong (`RVA_VM_STACK_TOP`, log only), frame_msec found. 19 globals of `.rdata` that had a
  single signature (it held such a cut address) are now placed by their neighbours only; a single data signature never resolved on its own, so nothing is lost.
* **The C resolver's test builds again** (`tools/test_addr.sh`: `addr_moved_allowed()` read the settings file with a Windows call the test does not link; under `ADDR_TEST` it
  takes the default rule): x64 477 of 477, x86 415 of 415, the iw4x-sp exe 369 of 415.
* **Runs on the unknown-build path** (Endgame's wake-up in split screen, `knifeshots`, 40 s; QA `knifehand` for the hand):
  * 64-bit, `MW2COOP_FORCE_UNKNOWN=1 MW2COOP_FORCE_RESOLVED=1`: 477 of 477 found, the default lets it run, mode 11 split screen, the shell shock's saved screen per view
    (`shocksect` installed, each view's rectangle, player 2's commands on timer slot 1), no crash.
  * 32-bit official exe, the same: 415 of 415, "12 of 12 calls hooked" (the shock copy among them), each view's rectangle copied, no crash.
  * The iw4x-sp 1.0.159 exe through the signatures (`MW2COOP_FORCE_UNKNOWN=1 MW2COOP_MOVED=1`, code that really moved): 366 of 415 found in game, no crash; the knife hand is
    refused with its log line; the split screen does not start (its context hook's call is not found, as in the third round) and the game stays in the main menu.
  * QA `knifehand` (map `ending`, player 1 looks at player 2's left fist): 64-bit and 32-bit on the unknown-build path and the iw4x-sp exe on its own table (`addr159.c`,
    where `addr_resolved` reads that table): the hook turns the two wrist bones, the knife in the left fist as before, the scenario runs to its end. The first iw4x-sp
    start opened no game window in 120 s; the same run again started normally (cause not found).

## What is not done

* No 64-bit build whose addresses moved exists here to run it on. The path is run on the reference
  exe with the lookups forced (`MW2COOP_FORCE_UNKNOWN=1 MW2COOP_MOVED=1 MW2COOP_FORCE_RESOLVED=1`): every address resolves to itself, so what that proves is that every use goes through the
  resolver and that the unresolved ones are survived.
* The renderer (`src/dx`) is still tied to the reference build as a whole (its build hash, above): on any other 64-bit build the game keeps its own Direct3D 9.
* The official 32-bit exe hashes its code on some loads (exit 0x8000dead); the mod does not get around that check, so the saves stay off there on any build.

## Regenerating

```sh
python -I tools/addrsig.py gen x86 <image32.bin or iw4sp.exe> content/release/addresses-x86.txt
python -I tools/addrsig.py gen x64 <64-bit iw4sp.exe>        content/release/addresses-x64.txt
python -I tools/addrsig.py verify content/release/addresses-x86.txt <other build> [--truth table]
sh tools/test_addr.sh && build/test_addr.exe content/release/addresses-x86.txt <other build>
```

The 32-bit reference is a memory dump of the official build (`mw2coop\dump_image`, docs/32bit-plan.md); the 64-bit generator needs
`llvm-objdump` for instruction boundaries. QA: `MW2COOP_FORCE_UNKNOWN=1` takes the unknown-build path on a known build.

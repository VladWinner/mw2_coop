# 32-bit port of the native split screen: where everything is

`src/native.c` (4,400 lines when the port started; about 12,000 now) is written against the 64-bit build: about 230 addresses and many structure offsets then. The 32-bit official build
(`FC9E4843...52D7`, image base 0x400000) is the same engine compiled again, so every function and structure has a counterpart. This folder holds what
was found for each one, a file per work package, so `native.c` can be given an address table for both builds. The port is done (the 32-bit build and the iw4x-sp
client run the split screen in mode 11 since 2026-11-17); the later files are notes of single fixes made for both builds.

## Files

| File | What |
|---|---|
| [aimrumble.md](aimrumble.md) | aim assist, rumble, player state fields and session control |
| [clientctx.md](clientctx.md) | the second client context and the frame |
| [entities.md](entities.md) | entities, DObjs and animation |
| [inputui.md](inputui.md) | input, pads, text and menus |
| [render.md](render.md) | the renderer and the compositor of the first split screen mode |
| [mode11-frame.md](mode11-frame.md) | mode 11: each player's renderer frame and the back end |
| [mode11-cgame.md](mode11-cgame.md) | mode 11: the cgame per player |
| [mode11-hud.md](mode11-hud.md) | mode 11: the 2D, the HUD and the view rectangles |
| [sound-once.md](sound-once.md) | world event sounds and effect sounds started once in split screen |
| [shock-savedscreen.md](shock-savedscreen.md) | the shell shock's saved screen per view (2026-11-18) |
| [weapon-animtype.md](weapon-animtype.md) | the third-person knife pose and the knife hand of Endgame (2026-11-18) |

Address independence of what is mapped here: [../address-resolution.md](../address-resolution.md) (the signature files cover the 32-bit addresses; the iw4x-sp
1.0.159 exe has its own table, `src/addr159.c`).

## How to find the 32-bit counterpart of a 64-bit function

- Ghidra projects (read only for you; set `G_AGENT=<your name>` so runs do not wait for each other's lock):
  `G_AGENT=x sh tools/ghidra/g.sh @<rva>` (64-bit decompile), `MW2COOP_GHIDRA_BITS=32 G_AGENT=x sh tools/ghidra/g.sh @<VA>` (32-bit).
  **In the 32-bit project the image base is 0, so you pass the VA (0x4666c0), not the RVA (0x666c0).** `native.c` takes `base + RVA`, RVA = VA - 0x400000.
  Other helpers: `tools/ghidra/g.sh -s CallChain.java <addr> [depth]`, `-s FuncAt.java <addr>`, `-s GlobalsOf.java`, `-s RipRefs.java`, `-s LeaUse.java`,
  `XrefStrings.java` modes (`"<string>"`, `-decomp "<string>"`, `ref:<addr>`, `dis:<addr>:<count>`). Read the scripts' headers.
- `local/scratch/map32/map.txt`: an automatic matcher's guesses, `<rva64> <VA32> <how>` (how: s = shared unique string, c = callee of a matched
  function, p = caller). About 27% of the 64-bit functions have a guess; **a guess is a lead, not a fact**: compare the decompiled code of both (same calls in the same
  order, same constants, same strings) before you trust it. `tools/match_funcs.py` makes it from `f64.txt` / `f32.txt` (ExportFuncs.java).
- Bytes of the 32-bit code: `D:\coop-test\mw2-32\mw2coop\image32.bin` is the exe's image from memory laid out from the base 0x400000
  (file offset = VA - 0x400000). Read it (never write it).
- The 64-bit bytes: `tools/rvabytes.py` (exe on disk).

## Rules

- Do not run the game, do not touch `D:\coop-test`, do not edit `src/`, `build.sh`, or other packages' files. Write only your own file here.
- Structure layouts differ (pointers are 4 bytes, many structs are re-packed): never assume an offset carries over. For each offset `native.c` uses, find the
  32-bit one by reading code that touches the same field, and say how you know.
- Calling conventions differ too (the 32-bit build has cdecl, stdcall, thiscall and compiler-made register conventions): give it for every function a hook has to replace or call.
- For every address a hook will patch give the first 16 bytes from `image32.bin`, so the hook can check them like the 64-bit hooks do.
- Say `unknown` where you did not find it. Keep what you read in the code (observed) apart from what you concluded. A wrong address costs a crash hunt later; a gap costs nothing.

## Entry format

```
### <name> - 64-bit rva 0x..., native.c line N: what it does
- 32-bit: VA 0x......, rva 0x......, convention: ..., arguments: ...
- bytes at VA: .. .. .. (16)
- offsets / sizes (if a structure): field -> 64-bit offset -> 32-bit offset
- evidence: ...
- confidence: verified (both decompiles compared) | likely | unknown
```

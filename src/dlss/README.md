# DLSS bridge (`mw2coop_dlss.dll`)

NVIDIA's DLSS Super Resolution for the mod's upscaling path (`[display] dlss=1`, `src/fsr3.c`). It is a separate DLL on purpose.

## Why it is apart from the mod

NVIDIA's SDK (headers, libraries, `nvngx_dlss.dll`) is under the **NVIDIA RTX SDKs License** (`LICENSE.txt` in [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS)), which is not an open source license.
Among other things it does not let the SDK be distributed as source, or become subject to an open source license (clauses 4b, 4e), and it asks for terms at least as protective as its own
when NVIDIA's binaries are passed on (2c). So:

- nothing from NVIDIA's SDK is in this repository, and the main `winmm.dll` has no NVIDIA code in it: it only loads `mw2coop_dlss.dll` at run time (`dlss_dir`);
- `dlss_bridge.cpp` and `msvc_shims.c` here are the mod's own code (MIT) and include NVIDIA's headers only at build time, from a local copy of the SDK;
- the GitHub nightly build does not build the bridge (it has no SDK);
- the bridge DLL and `nvngx_dlss.dll` are not part of the default release archives. Whoever passes them on has to follow NVIDIA's license (its distribution requirements, 2a-2d).

## Build (needs the SDK on the machine)

```sh
# the SDK: a checkout of https://github.com/NVIDIA/DLSS (folders include and lib/Windows_x86_64 are enough), pinned by commit and checked by hand;
# the DLL is signed by NVIDIA (Get-AuthenticodeSignature)
DLSS_SDK=../third-party/dlss sh src/dlss/build_dlss.sh      # -> build/mw2coop_dlss.dll
```

`nvsdk_ngx_s.lib` is built with MSVC; it links into the llvm-mingw build with `msvc_shims.c`, which provides the 12 MSVC runtime symbols the library imports (the stack cookie, the
thread-safe statics support, `operator new` / `delete` and two STL failure helpers) and with the MSVC default-library directives switched off in the link line.

## Use

Put `mw2coop_dlss.dll` and NVIDIA's `nvngx_dlss.dll` (from the SDK's `lib/Windows_x86_64/rel`) into one folder and set in `settings.ini`:

```ini
[display]
dlss=1
dlss_dir=C:/path/to/that/folder
upscale_width=1920
upscale_height=1080
```

The game's own resolution (Options > Video or `r_customMode`) is the render size, as with FSR 3. A GeForce RTX card and a current NVIDIA driver are needed; the log (`mw2coop.log`, lines
starting with `dlss:`) says why DLSS did not start.

## What it does

`dlss_bridge_init` starts NGX on the game's Direct3D 12 device (the Direct3D 9 on 12 layer) and checks that DLSS is available; `dlss_bridge_create` makes the feature for the render and output
sizes (low-resolution motion vectors, DLSS's own exposure, LDR color); `dlss_bridge_evaluate` records one evaluation on the mod's command list with the color, the depth, the motion
vectors in render pixels (without jitter), the output and the jitter offsets. Everything else (the camera, jitter in the projection, depth and vector passes, history resets) is the code
shared with FSR 3 in `src/fsr3.c`.

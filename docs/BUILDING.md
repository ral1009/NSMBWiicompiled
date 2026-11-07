# Building and developing NSMBWiicompiled

This page is for building the port from source and working on it. To play, download a release instead; see the [README](../README.md).

The game's PowerPC code is translated ahead of time into C++ by [WiiCompiled](https://github.com/patchzyy/Wiicompiled)'s translator, compiled natively, and rendered through [aurora](https://github.com/encounter/aurora). [`docs/MASTER_PLAN.md`](MASTER_PLAN.md) is the full plan and dated progress log; [`docs/issues.md`](issues.md) lists every bug fixed (Symptom / Root cause / Fix / Scope); [`CLAUDE.md`](../CLAUDE.md) has the working norms and the most useful debugging notes.

## How this differs from decompilation

- **Recompilation is the deliverable.** WiiCompiled's translator pipeline (PowerPC decode → IR/SSA lift → C++ emit → native compile) produces the actual playable build.
- **[NSMBW-Decomp](https://github.com/NSMBW-Community/NSMBW-Decomp) is reference material, not the goal.** Its symbol maps and verified source are used as ground truth to make the recompilation correct; this project isn't trying to extend or finish that decomp itself.

## Repository layout

| Path | What it is |
| --- | --- |
| `translator/` | The game-agnostic PowerPC → C++ translator (decoder, IR/SSA lifter, emitter), from WiiCompiled; NSMBW added the multi-module shard emitter |
| `runtime/` | Shared runtime: HLE implementations of Wii SDK calls (GX, OS, audio, input, DVD, etc.) plus NSMBW-specific product code (`runtime/src/product/`). NSMBW-only branches are `#ifdef MKW_RUNTIME_PRODUCT_NSMBW` |
| `aurora-main/` | Vendored [aurora](https://github.com/encounter/aurora): GX-to-modern-GPU (D3D12/Vulkan via Dawn/WebGPU) compatibility layer |
| `projects/nsmbw/` | NSMBW's manifest (`nsmbw.yml`), per-REL manifests (`nsmbw-d_*.yml`, linked by `modules.txt`), `function_map.txt` (real NSMBW addresses; also seeds translation), native overrides (`native/*.cpp`, each commented with the evidence for its binding; files ending `_diag.cpp` are diagnostics, not fixes), and `tools/` (run, screenshot, packaging and asset scripts) |
| `projects/mkwii/` | The original Mario Kart Wii project this was forked alongside, kept for reference |
| `docs/` | This page, the plan and progress log, the issues index |

## Building

Prerequisites (from `translator/README.md`): .NET 8 SDK, CMake ≥ 3.16, Ninja, Clang/LLVM (LLVM-MinGW targeting `x86-64-v3` is the tested path; MSVC is not), and a checkout of NSMBW-Decomp *inside* this directory (`NSMBW-Decomp/`, gitignored) holding `original/wiimj2d.dol` and the four `original/*.rel` files from your own dump. The manifests read `NSMBW-Decomp/original/`.

There's no one-command build yet. From the repo root:

```bash
# 1. Translate main.dol (start = the DOL entry point) and each REL (start = its _prolog).
dotnet run --project translator/src/Translator.Cli -- translate-recursive 0x80004050 \
  --project projects/nsmbw/nsmbw.yml --outdir build/nsmbw/functions \
  --output-metadata build/nsmbw/base_translation_output.json --threads 8
# repeat per REL with --project projects/nsmbw/nsmbw-<rel>.yml,
# --outdir build/nsmbw-<rel>/functions, --output-metadata build/nsmbw-<rel>/base_translation_output.json

# 2. One combined data initialiser for main.dol + all four RELs.
dotnet run --project translator/src/Translator.Cli -- generate-nsmbw-data-init \
  --project projects/nsmbw/nsmbw.yml \
  --rel-projects projects/nsmbw/nsmbw-d_basesNP.yml,projects/nsmbw/nsmbw-d_en_bossNP.yml,projects/nsmbw/nsmbw-d_enemiesNP.yml,projects/nsmbw/nsmbw-d_profileNP.yml

# 3. Build-shard manifest. Rerun whenever a PPC_NATIVE_OVERRIDE is added or removed under
#    projects/nsmbw/native/, or the build crashes with missing_target at that address.
dotnet run --project translator/src/Translator.Cli -- emit-nsmbw-build-shards \
  --project projects/nsmbw/nsmbw.yml --modules-file projects/nsmbw/modules.txt \
  --native-source-dir projects/nsmbw/native

# 4. Configure once, then build.
cmake -S runtime -B build_nsmbw -G Ninja -DCMAKE_BUILD_TYPE=Release -DMKW_BUILD_NSMBW=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++   # LLVM-MinGW on PATH
cmake --build build_nsmbw --target NSMBWCompiled
```

Step 1 also has to be re-run when a guest function that is only reachable through a function pointer is added to `function_map.txt` (symptom: `missing_target - guest address 0x... was called but is not translated`).

A dev build (`build_nsmbw`) has a console and no first-launch setup: set `dvd_root` in `%LOCALAPPDATA%\WiiCompiled\Config.toml` to an extracted disc. Run it with the helper, which captures the stderr log and periodic window screenshots:

```powershell
.\projects\nsmbw\tools\run_nsmbw.ps1 -Tag test -Seconds 120 -ShotEvery 10 -EnvVars @('NSMBW_AUTO_PRESS_SELFTEST=1')
```

Output lands in `build_nsmbw\nsmbw_test.err.log` and `build_nsmbw\shots\`. Scene changes appear as `createRoot(profile=0x..)` lines (0 BOOT, 3 world map, 5 title *and* levels, 6/7 course-in, 8 cutscene, 10 file select). The NAND save (`%LOCALAPPDATA%\WiiCompiled\NAND\title\00010004\534d4e50\wiimj2d.sav`) is kept between runs, so a started file resumes at the world map; `NSMBW_RESET_SAVE=1` deletes it at launch. With the self-test presses on, a resumed save gets from boot to 1-1 in about 30 seconds.

## Packaging

```powershell
.\projects\nsmbw\tools\package_nsmbw.ps1
```

This writes `dist\NSMBWCompiled-v<version>-<commit>.zip` from the release build (`build_release\`, configured with `-DNSMBW_RELEASE=ON`). The version is set in `package_nsmbw.ps1` and `runtime/cmake/NsmbwProduct.cmake`. The zip holds everything the exe loads from its own folder:
- `NSMBWCompiled.exe`
- the DLLs it imports that Windows doesn't ship: SDL3, Dawn, the two DirectX shader compiler DLLs, libc++, libunwind, libpng and zlib
- `dsp_coef.bin` (the audio mixer's resampling table), `initial_pipeline_cache.db` (the starting shader list), `initial_texture_preload.txt` (which HD-pack files each scene loads, so they are loaded before the scene fades in), `DolphinTool.exe` and its licenses
- `wii_bootstrap\`, the files a fresh emulated NAND is seeded with
- `PLAYING.txt`, setup instructions for the player

Release builds ask for the disc image on first launch (`runtime/src/product/nsmbw_disc_setup.cpp`), then offer to install an HD texture pack; `NSMBW_SETUP_IMAGE=<rvz>` and `NSMBW_SETUP_TEXTURES=<dir>` script those dialogs. They have no console window and log to `nsmbw_data\NSMBWCompiled.log`, and they need an AVX2 CPU (the build targets `x86-64-v3`) and a D3D12 or Vulkan GPU. `package_nsmbw.ps1 -Private` builds the developer's own no-setup folder instead, which contains the game and must never be shared.

Test a package from a folder **outside** this repo. When a file is missing next to the exe, the runtime looks for it in `runtime/assets/` by walking up from the working directory, so inside the checkout a missing file goes unnoticed.

## Where the features live

- **Options menu** (`runtime/src/product/nsmbw_options_menu.cpp`, `nsmbw_menu_draw.cpp`, `nsmbw_menu_art.cpp`): drawn with the game's own window, buttons, cursor, fonts and sounds, read from the player's disc. F10 opens it; Shift+F10 opens the older ImGui settings bar.
- **Options button** in the pause and world-map menus: the layouts are patched from the player's disc at startup (`runtime/src/product/nsmbw_layout_patch.cpp`) and the game code that counts buttons is overridden (`projects/nsmbw/native/nsmbw_pause_options.cpp`, `nsmbw_map_options.cpp`).
- **Exit** in uncleared courses: `projects/nsmbw/native/nsmbw_pause_exit.cpp`. **Full save** from the world map: `nsmbw_wm_full_save.cpp`.
- **Category volumes**: `projects/nsmbw/native/nsmbw_sound_volumes.cpp`.
- **Controls**: per-context action bindings in `runtime/src/product/nsmbw_controls.cpp`, saved to `%LOCALAPPDATA%\WiiCompiled\nsmbw_controls.ini`. `NSMBW_LEGACY_PAD_MAPPING=1` restores the old fixed table.
- **Button prompts**: [Kenney's Input Prompts](https://kenney.nl/assets/input-prompts) (CC0), built into the executable (`runtime/src/product/nsmbw_button_glyphs.cpp`, generated from `projects/nsmbw/tools/nsmbw_button_glyphs.cpp.in` by `gen_button_glyphs.py`). Keyboard keys with long names are drawn as symbol keycaps (`projects/nsmbw/tools/draw_key_icons.py`). `NSMBW_GLYPH_TEST=xbox|playstation|switch|keyboard` pins a set; `NSMBW_LOG_GLYPHS=1` logs each change.
- **HD texture packs**: aurora's Dolphin-compatible replacement (`aurora-main/lib/gfx/texture_replacement.cpp`). By hand: copy the pack's `SMN` folder into `nsmbw_data\texture_replacements\` next to the exe and turn the option on, or set `texture_replacements = true` under `[video]` in Config.toml.

## Debugging notes worth knowing before touching this code

Three bug classes account for almost every fix so far (full write-ups in `CLAUDE.md`):

- **Dead override at MKW's address.** An SDK function is natively overridden at *Mario Kart Wii's* address in the shared runtime, so it's silently dead for NSMBW. Symptoms look like rendering/logic bugs, but the function ran as translated code with no host-side effect. Check `projects/nsmbw/function_map.txt` for NSMBW's real addresses before suspecting the host implementation.
- **Inlined leaf.** The translator inlines small leaf functions into their callers, which bypasses a native override bound at that leaf's address. If a bound function's HLE never logs a call, check the generated shards for `inline leaf 0x<addr>`; handle the effect at a level the translator can't inline away (an SPR write, an MMIO register).
- **Override drops guest-side bookkeeping.** A native override replaces an SDK function *including* its stores to guest globals that other, still-translated SDK code reads later (`GXBeginDisplayList`, `GXLoadTexObj`). Read the translated body before binding and mirror those stores.

All diagnostics are off unless their environment variable is set. `CLAUDE.md` has the complete list; the most used:

| Variable | Shows |
| --- | --- |
| `NSMBW_LOG_STATE_CHANGES=1` | every scene state-machine transition by name (`dScGameSetup_c::StateID_...`) |
| `NSMBW_LOG_IDLE_THREADS=1` | thread states when the scheduler idles, plus the sleeping thread's call chain |
| `NSMBW_LOG_DRAW_TEXGEN=<profile>` (+ `_TICK=<min VI tick>`, `_MINVP=<min viewport height>`, `_MAX=<draws>`) | per draw: VI tick, bound texture, texgen, TEV, blend, alpha test, fog, cull, projection, viewport/scissor, SU scale |
| `NSMBW_LOG_TEXFMT=<profile>` | each `GXLoadTexObj` (format, size, filter) interleaved with raw draws |
| `NSMBW_DUMP_TEXTURES=<dir>` (+ `NSMBW_DUMP_TEXTURES_SCENE=<profile>`) | decoded textures as PPM/PGM (`projects/nsmbw/tools/ppm2png.ps1` converts) |
| `NSMBW_LOG_INTERP=1` | presented vs real FPS, frame pacing, game-thread spare time |
| `AURORA_GPU_TIMING=1` | GPU time per render pass every 2 s |
| `AURORA_LOG_TEXTURE_STATS=1` | live GPU textures and texture-cache sizes every 2 s |
| `NSMBW_AUTO_PRESS_SELFTEST=1`, `NSMBW_AUTO_PRESS_TICKS=<n>` | synthesise a press every 60 ticks: A everywhere except the world map, where it taps right once and then presses 2 |
| `NSMBW_LOG_NAND=1` | every NAND `IOS_*` call with path, size and result |
| `NSMBW_LOG_INPUT=1` | each change in the keyboard sample handed to `KPADRead` |
| `NSMBW_LOG_OPTIONS_MENU=1`, `NSMBW_FIRST_LAUNCH_OPTIONS=1` | options menu opens / closes; force the first-launch open |
| `NSMBW_SHOW_BOOT_SCREENS=1` | keep the Wii Remote strap and controller screens at boot |

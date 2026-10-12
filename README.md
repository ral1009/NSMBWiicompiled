# NSMBWiicompiled

A native PC port of *New Super Mario Bros. Wii* (NSMBW, PAL v1) via **static recompilation** — the same technique [WiiCompiled](https://github.com/patchzyy/Wiicompiled) uses for Mario Kart Wii, applied to a different game. No emulator, no interpreter, no JIT: the game's PowerPC code is translated once, ahead of time, into real, compilable C++, and rendered through [aurora](https://github.com/encounter/aurora).

This is a student side project, built with AI assistance, and doubles as a from-scratch education in C, PowerPC, and the recompilation toolchain. See [`CLAUDE.md`](CLAUDE.md) for the working norms and [`docs/MASTER_PLAN.md`](docs/MASTER_PLAN.md) for the full phase plan, learning curriculum, and dated progress log — that file is the single source of truth on scope and status; this README just summarizes it.

**You need your own legally dumped PAL NSMBW disc.** Nothing here works without it, and no Nintendo assets, code or data are bundled — translation runs locally against your own disc image.

## Status: v0.1.0 released — playable, no-setup first launch (2026-10-12)

Release builds ask for your disc image on first launch, extract it with the bundled DolphinTool, and optionally install a Dolphin HD texture pack. The table below is the per-stage evidence from the earlier bring-up; later work (controls, glyphs, interpolation, HD packs, release packaging) is in the sections further down and in the progress log.

Verified with screenshots (the evidence for each row is in the progress log):

| Stage | State |
| --- | --- |
| Boot, Wii Strap screen, "save data has been created" dialog | renders correctly |
| Title screen (logo, hills, ground tiles, circle-wipe transition) | renders; sky colour not yet confirmed, Mario / "PRESS 2" not yet checked |
| File Selection (cards, Erase/Copy, Free Mode / Coin Battle, background) | renders correctly |
| Intro cutscene | plays to the end, including the item-rain section, without crashing |
| Input | keyboard and SDL gamepads, bound per context (see Controls). One Wii Remote per connected gamepad, up to 4 players (2026-10-11); single player verified, a second controller joining **not yet verified** |
| Audio | AX/DSP frames now run, so sound-driven game logic advances; audible output not yet verified |
| World map | renders and runs; enter a level with **2** (A opens the map's free-look mode) |
| Levels (1-1, 1-2, 1-Tower, 1-Castle) | sky, tiles, Mario, enemies, bosses and background all render; playable with keyboard input; tilesets swap correctly between levels (2026-09-21) |
| Save data | `wiimj2d.sav` is kept between runs and loaded at boot, so a started file resumes at the map without replaying the intro (`NSMBW_RESET_SAVE=1` wipes it) |

## How this differs from decompilation

- **Recompilation is the deliverable.** WiiCompiled's translator pipeline (PowerPC decode → IR/SSA lift → C++ emit → native compile) produces the actual playable build.
- **[NSMBW-Decomp](https://github.com/NSMBW-Community/NSMBW-Decomp) is reference material, not the goal.** Its symbol maps and verified source are used as ground truth to make the recompilation correct; this project isn't trying to extend or finish that decomp itself. Its licence terms need confirming before any release that incorporates its code.

## Repository layout

| Path | What it is |
| --- | --- |
| `translator/` | The game-agnostic PowerPC → C++ translator (decoder, IR/SSA lifter, emitter), from WiiCompiled; NSMBW added the multi-module shard emitter |
| `runtime/` | Shared runtime: HLE implementations of Wii SDK calls (GX, OS, audio, input, DVD, etc.) plus NSMBW-specific product code (`runtime/src/product/nsmbw_product.cpp`). NSMBW-only branches are `#ifdef MKW_RUNTIME_PRODUCT_NSMBW` |
| `aurora-main/` | Vendored [aurora](https://github.com/encounter/aurora) — GX-to-modern-GPU (D3D12/Vulkan/OpenGL via Dawn/WebGPU) compatibility layer, with env-gated NSMBW diagnostics in `lib/gx/command_processor.cpp` |
| `projects/nsmbw/` | NSMBW's manifest (`nsmbw.yml`), per-REL manifests (`nsmbw-d_*.yml`, linked by `modules.txt`), `function_map.txt` (real NSMBW addresses; also seeds translation), native overrides (`native/*.cpp`, each commented with the evidence for its binding — files ending `_diag.cpp` are diagnostics, not fixes), and `tools/` (`run_nsmbw.ps1` timed run + screenshots, `shot.ps1`, `ppm2png.ps1`) |
| `projects/mkwii/` | The original Mario Kart Wii project this was forked alongside, kept for reference |
| `docs/MASTER_PLAN.md` | Project scope, phase breakdown, learning curriculum, and the dated progress log |
| `docs/issues.md` | Every bug fixed so far: Symptom / Root cause / Fix / Scope (general runtime bug vs NSMBW-specific vs unconfirmed) |

## Building

Prerequisites (from `translator/README.md`): .NET 8 SDK, CMake ≥ 3.16, Ninja, Clang/LLVM (LLVM-MinGW targeting `x86-64-v3` is the tested path; MSVC is not), and a checkout of NSMBW-Decomp *inside* this directory (`NSMBW-Decomp/`, gitignored) holding `original/wiimj2d.dol` and the four `original/*.rel` files from your own dump — the manifests read `NSMBW-Decomp/original/`.

There's no packaged one-command build yet. From the repo root:

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

Run it with the helper, which captures the stderr log and periodic window screenshots:

```powershell
.\projects\nsmbw\tools\run_nsmbw.ps1 -Tag test -Seconds 120 -ShotEvery 10 -EnvVars @('NSMBW_AUTO_PRESS_SELFTEST=1')
```

Output lands in `build_nsmbw\nsmbw_test.err.log` and `build_nsmbw\shots\`. Scene changes appear as `createRoot(profile=0x..)` lines (0 BOOT, 3 world map, 5 title *and* levels, 6/7 course-in, 8 cutscene, 10 file select). The NAND save (`%LOCALAPPDATA%\WiiCompiled\NAND\title\00010004\534d4e50\wiimj2d.sav`) is kept between runs so a started file resumes at the world map; `NSMBW_RESET_SAVE=1` deletes it at launch, and the window config is still reset unless `NSMBW_KEEP_STATE=1`. With the self-test presses on, a resumed save gets from boot to 1-1 in about 30 seconds.

### Running a build on another PC

```powershell
.\projects\nsmbw\tools\package_nsmbw.ps1
```

This writes `dist\NSMBWCompiled-<commit>.zip`, which contains everything the exe loads from its own folder:
- `NSMBWCompiled.exe`
- the 8 DLLs it imports that Windows doesn't ship: SDL3, Dawn, the two DirectX shader compiler DLLs, libc++, libunwind, libpng and zlib
- `dsp_coef.bin`, the audio mixer's resampling table
- `wii_bootstrap\`, the files a fresh emulated NAND is seeded with
- `PLAYING.txt`, setup instructions for the player

The player still needs their own PAL disc (SMNP01, revision 1). Release builds (`build_release\`, configured with `-DNSMBW_RELEASE=ON`; zip `dist\NSMBWCompiled-v0.1.0-<commit>.zip`) also ship `DolphinTool.exe`, ask for the disc image on first launch, then offer to install an HD texture pack; dev builds still need `dvd_root` in `%LOCALAPPDATA%\WiiCompiled\Config.toml` pointing at an extracted disc. Release builds have no console window and log to `nsmbw_data\NSMBWCompiled.log`. They also need an AVX2 CPU (the build targets `x86-64-v3`) and a D3D12 or Vulkan GPU.

Test a package from a folder **outside** this repo. When a file is missing next to the exe, the runtime looks for it in `runtime/assets/` by walking up from the working directory, so inside the checkout a missing file goes unnoticed.

## HD textures (Dolphin texture packs)

Dolphin custom-texture packs work unchanged. A release build's first launch offers to install one (pick the pack folder; it finds `SMN` inside, copies it and turns the option on; `NSMBW_SETUP_TEXTURES=<dir>` is the test hook). By hand: copy the pack's `SMN` folder (the one Dolphin expects in `Load\Textures\`) into `build_nsmbw\nsmbw_data\texture_replacements\`, then tick **Graphics → HD texture pack** in the F10 menu (or set `texture_replacements = true` under `[video]` in `%LOCALAPPDATA%\WiiCompiled\Config.toml`). The checkbox switches the pack on and off while the game runs. Tested with the full *NSMBW HD v2.81 (DDS)* pack and with [nymo's HD HUD Tweaks](https://gamebanana.com/mods/480785) by **nymo642**. Packs contain Nintendo-derived art, so none are included in this repository; download them from their authors.

## Controls

NSMBW is played with a keyboard or any SDL gamepad; there is no Wii Remote support, so the boot goes straight to the title screen (the strap warning and "Hold the Wii Remote sideways" screens are skipped; `NSMBW_SHOW_BOOT_SCREENS=1` keeps them).

Buttons are bound to **actions, separately for courses, the world map and menus** (F10 → Controller settings → NSMBW controls): Jump and Confirm are both the Wii Remote's 2 button to the game, but they can sit on different buttons. Click a slot and press the button or key to bind it; right-click clears it. Bindings are saved in `%LOCALAPPDATA%\WiiCompiled\nsmbw_controls.ini`. Each connected gamepad is its own Wii Remote (player N = Nth pad, up to 4); the keyboard is player 1's. Multiplayer with two pads is not yet tested. Defaults:

| | Gamepad | Keyboard |
|---|---|---|
| Jump / Confirm | South (A / Cross) | Z, Space / Enter |
| Run, fireball / Back | West / East | X / Backspace |
| Spin jump | Shoulders | C |
| Pause | Start | = or Esc |
| Move | D-pad, left stick | Arrows |
| Tilt remote (tilt lifts, remote wire; courses only) | Right stick left / right (proportional) | A / D |

## Changes to the game

- **Exit** in the pause menu works in every course, cleared or not (enemy courses excepted, as in the original). `projects/nsmbw/native/nsmbw_pause_exit.cpp`.
- **Save** on the world-map menu is always a full save. The original offers only Quick Save (save and quit to the title) until the final boss is beaten. The saved "final boss beaten" flag itself is untouched. `projects/nsmbw/native/nsmbw_wm_full_save.cpp`.

## Controller button glyphs

Button prompts ("Press (2) to Start", the (2) on the controller screen, Erase/Copy on file select, the map's Menu/Items hints) show the button you actually press, for the device you last used (Xbox, PlayStation, Switch or keyboard) and for the current context's bindings. **F10 → Controller settings → Button icons** pins one set or keeps the original Wii icons. The icons are [Kenney's Input Prompts](https://kenney.nl/assets/input-prompts) (CC0), built into the executable, and work with or without a texture pack. Keyboard keys with long names (Backspace, Enter, Shift, Space, Tab) are drawn as symbol keycaps (`projects/nsmbw/tools/draw_key_icons.py`), and without a pack the glyphs are downscaled with multi-sample averaging so thin outlines stay smooth (not yet re-checked on screen). `NSMBW_GLYPH_TEST=xbox|playstation|switch|keyboard` pins a set for testing; `NSMBW_LOG_GLYPHS=1` logs each change.

## Debugging notes worth knowing before touching this code

Three bug classes account for almost every fix so far (full write-ups in `CLAUDE.md`):

- **Dead override at MKW's address.** An SDK function is natively overridden at *Mario Kart Wii's* address in the shared runtime, so it's silently dead for NSMBW — symptoms look like rendering/logic bugs, but the function ran as translated code with no host-side effect. Check `projects/nsmbw/function_map.txt` for NSMBW's real addresses before suspecting the host implementation.
- **Inlined leaf.** The translator inlines small leaf functions into their callers, which bypasses a native override bound at that leaf's address. If a bound function's HLE never logs a call, check the generated shards for `inline leaf 0x<addr>`; handle the effect at a level the translator can't inline away (an SPR write, an MMIO register).
- **Override drops guest-side bookkeeping.** A native override replaces an SDK function *including* its stores to guest globals that other, still-translated SDK code reads later (`GXBeginDisplayList`, `GXLoadTexObj`). Read the translated body before binding and mirror those stores.

All diagnostics are off unless their environment variable is set:

| Variable | Shows |
| --- | --- |
| `NSMBW_LOG_STATE_CHANGES=1` | every scene state-machine transition by name (`dScGameSetup_c::StateID_...`) |
| `NSMBW_LOG_IDLE_THREADS=1` | thread states when the scheduler idles, plus the sleeping thread's call chain |
| `NSMBW_LOG_DRAW_TEXGEN=<profile>` (+ `_TICK=<min VI tick>`, `_MINVP=<min viewport height>`, `_MAX=<draws>`) | per draw: VI tick, bound texture, texgen, TEV, blend, alpha test, fog, cull, projection, viewport/scissor, SU scale |
| `NSMBW_LOG_TEXFMT=<profile>` | each `GXLoadTexObj` (format, size, filter) interleaved with raw draws |
| `NSMBW_DUMP_TEXTURES=<dir>` (+ `NSMBW_DUMP_TEXTURES_SCENE=<profile>`) | decoded textures as PPM/PGM (`projects/nsmbw/tools/ppm2png.ps1` converts) |
| `NSMBW_LOG_FIFO_DESYNC=1` | ring of recent raw-FIFO draws, dumped when the parser loses sync |
| `NSMBW_LOG_DISPLAY_LIST=1` | each guest `GXBeginDisplayList` / `GXEndDisplayList` with byte counts |
| `NSMBW_AUTO_PRESS_SELFTEST=1`, `NSMBW_AUTO_PRESS_TICKS=<n>` | synthesise a press every 60 ticks (for the first `n` ticks): A everywhere except the world map, where it taps right once and then presses 2 |
| `NSMBW_LOG_NAND=1` | every `IOS_Open/Read/Write/Seek/Close/Ioctl/Ioctlv` on the NAND with path, size and result |
| `NSMBW_LOG_INPUT=1` | each change in the keyboard sample handed to `KPADRead` (WPAD hold/trig/release bits, raw PAD bits, SDL scancodes down) |
| `NSMBW_GPU_PEEK*`, `NSMBW_TEST_TRIANGLE`, other `NSMBW_LOG_*` / `NSMBW_DUMP_*` | older GX bring-up probes; see the source for each |

## License

Licensed under [GPL-3.0](LICENSE), inherited from WiiCompiled. Third-party components (aurora, and anything else bundled or linked) are listed with their own licenses in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). No Nintendo assets, code, or data of any kind are shipped here.

# NSMBWiicompiled

A native PC port of *New Super Mario Bros. Wii* (NSMBW, PAL v1) via **static recompilation** — the same technique [WiiCompiled](https://github.com/patchzyy/Wiicompiled) uses for Mario Kart Wii, applied to a different game. No emulator, no interpreter, no JIT: the game's PowerPC code is translated once, ahead of time, into real, compilable C++, and rendered through [aurora](https://github.com/encounter/aurora).

This is a student side project, built with AI assistance, and doubles as a from-scratch education in C, PowerPC, and the recompilation toolchain. See [`CLAUDE.md`](CLAUDE.md) for the working norms and [`docs/MASTER_PLAN.md`](docs/MASTER_PLAN.md) for the full phase plan, learning curriculum, and dated progress log — that file is the single source of truth on scope and status; this README just summarizes it.

**You need your own legally dumped PAL NSMBW disc.** Nothing here works without it, and no Nintendo assets, code or data are bundled — translation runs locally against your own disc image.

## Status: Phase 6/7/8 — playable into World 1 (2026-09-21)

Verified with screenshots (the evidence for each row is in the progress log):

| Stage | State |
| --- | --- |
| Boot, Wii Strap screen, "save data has been created" dialog | renders correctly |
| Title screen (logo, hills, ground tiles, circle-wipe transition) | renders; sky colour not yet confirmed, Mario / "PRESS 2" not yet checked |
| File Selection (cards, Erase/Copy, Free Mode / Coin Battle, background) | renders correctly |
| Intro cutscene | plays to the end, including the item-rain section, without crashing |
| Input | keyboard as one sideways Wii Remote (channel 0): **Z** = 2 (jump), **X** = 1 (run), **Enter** = A, **arrow keys** = d-pad, with held / pressed / released state delivered through `KPADRead`. B, +, −, HOME and the pointer are not bound yet |
| Audio | AX/DSP frames now run, so sound-driven game logic advances; audible output not yet verified |
| World map | renders and runs; enter a level with **2** (A opens the map's free-look mode) |
| Levels (1-1, 1-2) | sky, tiles, Mario, enemies and background all render; playable with keyboard input; tilesets swap correctly between levels (2026-09-21) |
| Save data | `wiimj2d.sav` is kept between runs and loaded at boot, so a started file resumes at the map without replaying the intro (`NSMBW_RESET_SAVE=1` wipes it) |

What changed on 2026-09-20, evening (details and evidence in the progress log):

1. **Invisible level geometry** (black sky, invisible ground/Mario/enemies while the HUD and far hills drew): every tile/model draw ran with a 0×0 viewport and an all-zero scissor. The native `GXSetViewport`/`GXSetScissor` never wrote the `__GXData` fields the game's translated `GXGetViewportv`/`GXGetScissor`/`__GXSetViewport` read back, so each save/restore around a render-to-texture pass restored zeros. Mirroring the SDK's six float stores and two BP words fixed the whole level.
2. **Save never resumed**: the boot scene types the save file with `ISFS_ReadDir` and expects `EINVAL` ("this is a file"); the HLE answered `ENOENT` for any non-directory, so the game re-created the save on every launch.
3. Also fixed: the world-map crashes (REL `.bss` was initialised from file bytes instead of zeros; 77 cross-module and 21 vtable call targets the recursive translator couldn't see) and the item-rain crash in the cutscene (a fixed 4 KB raw-FIFO staging buffer dropped the tail of large draws).

What changed on 2026-09-20, earlier (three root causes, all the same bug class — see below):

1. **Cutscene crash** (`[aurora] unmapped vtx attr 9`): `GXBeginDisplayList` / `GXEndDisplayList` were never bound for NSMBW, so the host executed display-list *recordings* as live draws and later replayed stale memory. Bound both; guest memory-error lines per run went 98 → 0.
2. **Stuck after choosing the number of players**: the scene waits in `VoiceEndWait` for a voice clip to finish, and audio never ticked because only `AIInit` was bound. Bound the AI DMA family, translated the AX task callbacks the recursive translator couldn't see, and stopped the scheduler's idle loop from starving VI once audio was live.
3. **Every 2D screen drawn as smeared gradients** (file select, strap, title elements): the native `GXLoadTexObj` didn't cache the texture size where the guest's `__GXSetSUTexRegs` reads it, so every layout draw sampled a one-texel sliver magnified across the pane. Two guest-memory stores fixed all of it.

Earlier milestones, in order: guest game thread running (2026-09-01) → GX render-state + startup-overlay fixes (2026-09-04) → input (WPAD), scene creation, and the first 3D frame (2026-09-19) → cutscene, file select and the audio path (2026-09-20) → world map and a playable 1-1 (2026-09-20, evening).

**Not yet started:** audible audio output verification (Phase 8), a correctness-validation harness (Phase 9), and any compatibility pass across the 8 worlds (Phase 10).

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

Output lands in `build_nsmbw\nsmbw_test.err.log` and `build_nsmbw\shots\`. Scene changes appear as `createRoot(profile=0x..)` lines (0 BOOT, 3 world map, 5 title *and* levels, 6/7 course-in, 8 cutscene, 10 file select). The NAND save (`%LOCALAPPDATA%\WiiCompiled\NAND\title\00010004\534d4e50\wiimj2d.sav`) is kept between runs so a started file resumes at the world map; `NSMBW_RESET_SAVE=1` deletes it at launch, and the window config is still reset unless `NSMBW_KEEP_STATE=1`. With the self-test presses on, a resumed save gets from boot to 1-1 in about two minutes.

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

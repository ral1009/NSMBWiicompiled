# NSMBW Static Recompilation — Master Plan

Single source of truth for this project. Replaces separate plan/curriculum files — everything lives here.

---

## 1. What this project actually is

**Goal:** produce a native PC executable of New Super Mario Bros. Wii using static recompilation — the same technique WiiCompiled uses for Mario Kart Wii. No emulator, no interpreter, no JIT: the game's PowerPC code gets translated once, ahead of time, into real compilable C++.

**Recomp vs decomp — don't confuse these:**
- **Recompilation is the deliverable.** WiiCompiled's translator pipeline is what produces the actual playable build.
- **Decompilation (NSMBW-Decomp) is reference material, not the goal.** It produces human-verified C/C++ that rebuilds byte-identical to the original *Wii* binary — still meant to run on a Wii/emulator, not natively. We use its symbol maps and verified source as ground truth to make the recompilation easier and more correct. We are not trying to finish or extend the decomp project itself.

**Legal baseline:** requires owning a legally dumped copy of your own PAL NSMBW disc. Nothing here works, or should work, without that. No Nintendo assets/code are ever bundled — translation runs locally against your own disc image.

**License note for later:** WiiCompiled is GPL-3.0 (anything derived from it must be too, if distributed). NSMBW-Decomp's license terms need direct confirmation with that project before any public release incorporating their code.

---

## 2. What's already solved vs. what's new work

### Reused from WiiCompiled
- PowerPC decoder + IR/SSA lifter + C++ emitter (game-agnostic translator core)
- The 4-command pipeline: `translate-recursive` → `generate-data-init` → `emit-build-shards` → CMake/Ninja/Clang build
- The YAML manifest system for pointing the generic translator at a new game
- **aurora** — GX → modern GPU (D3D12/Vulkan/OpenGL via Dawn/WebGPU) translation layer
- Dolphin-derived Wii DSP audio coefficients (console-wide, reusable as-is)
- Runtime scaffolding: settings bar, `Config.toml` persistence, window/resolution handling

### Reused from NSMBW-Decomp / NSMBW-Maps
- Partial (~8%) verified C/C++ reconstruction of real game logic — ground truth reference
- `syms.txt` / address maps — feeds directly into the translator's `function_map` input
- A build system that verifies byte-exact matches — good sanity check tooling

### New work specific to NSMBW (not solved by either repo)
- Manifest authoring (memory base, SDA bases, entry point for `wiimj2d.dol`)
- **Multi-REL handling** — four separately-loaded `.rel` modules vs. MKWii's one; untested whether the translator generalizes here
- Instruction coverage gaps (different compiler output patterns than MKWii may hit unsupported PowerPC instructions)
- Data section initializers across `main.dol` + all four RELs
- **Input runtime** — WPAD (Wii Remote/Nunchuk) emulation, entirely new vs. MKWii's GameCube-passthrough model
- Rendering verification — 2D tilemap/sprite GX usage vs. MKWii's 3D tracks (likely simpler, unproven)
- Audio — NSMBW's specific sound bank format and streaming setup
- OS/misc call stubs (threading, save data) specific to what NSMBW calls
- **Correctness validation harness** — no free oracle like MKWii's ghosts; must be built from scratch (Super Guide sequences help as a narrow smoke test only)
- Compatibility QA across 8 worlds + co-op mode

---

## 3. Phases

### Phase 0 — Environment setup + first milestone (do this first)

**Install:** Git, .NET 8 SDK, CMake ≥3.16, Ninja (`pip install ninja`), Clang/LLVM (LLVM-MinGW), Python 3, CodeWarrior for Embedded PowerPC (+ `wibo`/WINE if not on Windows).

**Clone:**
```bash
git clone https://github.com/patchzyy/Wiicompiled.git
git clone https://github.com/NSMBW-Community/NSMBW-Decomp.git
```

**Game files:** dump your own PAL disc, verify MD5 checksums against NSMBW-Decomp's README before proceeding — wrong revision means nothing downstream lines up.

**Milestone: build NSMBW-Decomp standalone.**
```bash
./configure.py     # generates build.ninja: real C source → compile; undecompiled regions → slice raw bytes from original/
ninja              # compiles decompiled source with the actual original CodeWarrior compiler; slices the rest; links into rebuilt .dol/.rel files
./progress.py --verify-bin   # byte-compares your rebuilt files against original/ — pass or fail, no ambiguity
```

**What you'll see if it works:** a clean verification report, no diff output.
**What you'll see if it fails:** a report naming the specific file/offset that mismatches — check open issues on the repo first, this may not be your fault on a fresh clone.

**Why this phase matters (it's training, not a detour):**
- You watch a real compiler + linker succeed once, so you'll recognize what "working" looks like later.
- You read real, verified-correct C for this exact game before writing or evaluating any yourself — `source/` and `include/` become your first C textbook, `slices/` shows you concretely which systems are still opaque.
- Byte-exact verification gives an unambiguous pass/fail signal — rare to get this early, worth using while available.
- The failure space here is narrow (environment/setup mismatches), a much gentler place to practice debugging with AI's help than anything in later phases.

---

### Phase 1 — Foundational learning (concurrent, ongoing throughout)

Not blocking — keep doing this in parallel with every phase below. Full breakdown in Section 4.

---

### Phase 2 — Get WiiCompiled's translator running (no game data yet)

1. `dotnet build translator/Translator.sln -c Release`
2. `dotnet test translator/Translator.sln -c Release` — confirm default suite passes (no binaries needed)
3. Read `translator/README.md` and `projects/examples/generic-dol.yml` closely
4. Run the pipeline once against the generic example manifest just to see it work end-to-end, zero stakes

---

### Phase 3 — Author the NSMBW manifest

- Determine memory base/size, SDA bases (r13/r2 — found via `lis`/`ori` pairs in `__init_registers`), entry point for `wiimj2d.dol`
- Cross-reference NSMBW-Decomp's `docs/INTRODUCTION.md` and `syms.txt`
- Point `translation.function_map.path` at NSMBW-Decomp's symbol data
- Get **only `main.dol`** translating first — ignore the four RELs until this works in isolation

---

### Phase 4 — Handle the four REL modules

- Investigate whether `translate-recursive` handles multiple independently-loaded RELs cleanly, or needs manifest/translator changes
- Get each REL translating individually before combining
- Iterate on unsupported-instruction failures: fails → identify instruction → extend decoder support (or check if decomp'd source already covers that function) → retry

---

### Phase 5 — Data init and build graph

- `generate-data-init` for `main.dol` and each REL
- `emit-build-shards` to produce the CMake graph
- **Milestone: a compiling executable**, even if it crashes on launch — proves decode → lift → emit → compile → link works end-to-end for this game

---

### Phase 6 — Runtime bring-up

Order matters:
1. Stub Wii OS calls causing immediate crashes (log-and-dummy-return is fine initially)
2. Get aurora initialized against NSMBW's actual GX init calls — confirm a window opens / a frame clears
3. Hardcode/stub input entirely just to get past boot to a title screen or first level frame

---

### Phase 7 — Real input (WPAD emulation)

- Identify what NSMBW reads from WPAD: button state, pointer position, possibly shake detection
- Scope decision: mapping modern gamepad/keyboard to sideways-Wiimote-equivalent input is achievable for v1; full motion/pointer emulation is a stretch goal, not a requirement

---

### Phase 8 — Audio

- Same DSP hardware as MKWii (coefficient handling already in WiiCompiled's runtime)
- NSMBW's sound bank format/streaming setup is new, game-specific work

---

### Phase 9 — Correctness validation harness

- Record fixed input sequences on real hardware/Dolphin, capture per-frame state
- Replay same inputs against the recompiled build, diff frame-by-frame
- Use Super Guide's scripted sequences as an easy first smoke test — narrow coverage, not a substitute for the full harness

---

### Phase 10 — Compatibility pass

- Play through all 8 worlds, single-player and co-op, log and fix crashes as found
- Manual QA — no repo does this for you

---

### Phase 11 — Polish and release scaffolding

- Settings UI (reuse WiiCompiled's pattern)
- Installer/wrapper if distributing
- Confirm no bundled Nintendo assets/code
- Final license check before any public release

---

## 4. Learning curriculum, mapped to phases

| Phase | Learn before/during |
|---|---|
| Phase 0 | Git basics, what compiler/linker do, terminal comfort |
| During Phase 0 build | **C fundamentals** — variables, control flow, **pointers** (non-negotiable), structs, arrays, function pointers, manual memory model. Use NSMBW-Decomp's `source/`/`include/` as your textbook while it builds. |
| Before Phase 2 | Computer architecture basics: registers, the stack, **endianness** (PowerPC is big-endian, your PC is little-endian), calling conventions |
| Before Phase 3 | **PowerPC assembly reading** (not writing) — loads/stores, arithmetic, branches, `lis`/`ori` constant-building pattern, condition register basics, Small Data Area concept. DOL/REL file structure (`.text`/`.data`/`.rodata`/`.bss`/`.sdata`) |
| Before Phase 4 | Reverse engineering literacy — disassembler navigation, symbol maps in practice, type-inference reasoning (why "loaded with `lfs`" implies float) |
| Concurrent, deepen before Phase 6 | C++ basics: classes, inheritance, **virtual functions** (source of the "indirect call" problem — vtables), IR/SSA concept, interpretation vs. JIT vs. static recompilation distinction |
| Before Phase 6 | GX conceptual basics (draw calls, vertex formats, texture binding), one modern graphics API's basic vocabulary (Vulkan or D3D12 — command buffers, pipelines, shaders) |
| Before Phase 7 | WPAD specifics — button bitmask, accelerometer, IR pointer data |
| Before Phase 8 | Digital audio basics — sample rate, PCM, streaming vs. one-shot, mixing |
| Before Phase 9 | Testing/validation methodology — deterministic inputs, automated frame-by-frame state diffing vs. eyeballing, why non-determinism (uninitialized reads, timing) can cause phantom mismatches |

**Study approach:** learn each item right before the phase that needs it, not all up front — retention is better when immediately applied, and most of this will click once you've seen the concrete problem it solves. When AI explains or generates something, ask "why" until you could explain it back yourself — that's the actual point, since your judgment on whether AI output is correct is the real bottleneck on this project.

---

## 5. Realistic expectations

- Phases 0–2: days, mostly setup/orientation
- Phases 3–5 (translation working): weeks — symbol maps save real time over blind reverse-engineering
- Phases 6–9: the bulk of the project, months — this is where genuine understanding matters most, since verifying a crash fix or validation mismatch requires actually reasoning about what's happening
- Phase 10+: ongoing, can run in parallel with continued fixes

**Given competing academic workload (EE, IAs, English essays), the realistic target is reaching a documented, explainable milestone — not a finished playable port.** Phase 0 through a compiling build (Phase 5) is an honest, presentable goal on its own: it requires genuinely learning C, PowerPC basics, and a real build toolchain, and produces a concrete artifact (repo + commit history + devlog) regardless of whether the game is playable yet.

---

## 6. Progress log

Keep entries here as work happens. Since 2026-09-19 these are written by Claude at the end of each working session (see CLAUDE.md's documentation policy); entries before that date were written by the developer. Either way the standard is the same: what was observed, what was ruled out and how, what the cause was, what was verified.

Each individual bug fixed also gets a short Symptom / Root cause / Fix / Scope entry in `docs/issues.md` (started 2026-09-21, backfilled from every session to date). That file is the index; this log is the narrative.

### Template for each entry
```
## [Date] — Phase X
What I did:
What broke / what I didn't expect:
What I learned:
What's next:
```

*(Log entries go below this line as the project progresses.)*

## 2026-08-30 — Phase 0
What I did:
1. Obtained .rvz file of NSMBW, ensuring it was PAL (Europe) V1
2. Used Dolphin to extract main.dol, plus four .LZ files
3. Pasted into original folder
4. Downloaded and extracted compiler (had issue where there was an extra folder just needed to remove that)
5. Updated pip, downloaded ninja, ninja -version doesn't run because it isnt in the environment variables list, decided to just stick with leaving it how it is
6. Ran the test-path, obtained True

What broke / what I didn't expect:

What I learned:
Ok so configure plans what needs to be compiled, ninja compiles the source file and dumps it in bin, then progress.py verifies the files in the bin

What's next:
Test that the translator works

## 2026-08-30 — Phase 2
What I did:
Used a general.dol file to test the translator

What broke / what I didn't expect:
1. README said the SDA_BASE files were only required when using RuntimeConfig.h, but actually was immediately demanded and required for the translator to run.
2. SDA_BASE needs lis/oris to build, so looked through the raw bytes and found the numbers and pasted it into where it was expected.
3. The SDA_BASEs worked, but the translator got lost and computed something that wasn't valid, will be fixed in the future when function map is implemented.

What I learned:

What's next:
start translating NSMBW's main.dol and use NSMBW-decomp's function map and syms.txt

## 2026-08-30 — Phase 3
What I did:
Created manifest file to describe the NSMBW main.dol, converted the syms.text but discovered that its format had a 0x prefix, which had to be removed, and the file order needed to be reordered.

What broke / what I didn't expect:
A function at depth level 3 is computing 0x60 address, which forces that to be translated but it is invalid.

What I learned:

What's next:
This will be addressed in Phase 4. The other 4 REL files are going to be handled next.

## 2026-08-30 — Phase 4
What I did:
Used similar way to translate all four REL files, made manifests, used each prolog address to start the recursion, then tested all 4.

What broke / what I didn't expect:

What I learned:
All four succeeded. 0x60 is not part of main.dol or any REL file - it's outside anything we dumped, likely part of the Wii's own OS/boot code. Not a translator bug. Will be addressed in Phase 6.

What's next:

## 2026-08-30 — Phase 5
What I did:
Added stub for 0x60 issue.

What broke / what I didn't expect:
Investigated building, but MKWii's cmake is built entirely around MKWii's own product (its own dependencies, its own product list) - not a generic harness that accepts any game's generated shards. Will need heavy rebuilding to add NSMBW as its own product.

What I learned:

What's next:
Build our own working version of the CMake product setup for NSMBW before committing anything.

## 2026-09-01 — Phase 6
What I did:
Got the guest's main game thread running under the recompiled runtime (commit `8a13af0`).

What broke / what I didn't expect:
Boot then hung in an infinite loop polling the EXI (external interface) queue — the guest was waiting for hardware that the host never emulates.

What I learned:
"Runs" and "progresses" are different milestones. Every SDK subsystem the guest touches (EXI, WPAD, DVD, VI, GX) needs either a real HLE implementation or a stub that returns what the guest expects, or the guest spins forever waiting.

What's next:
Get past the EXI loop and reach the first frame.

## 2026-09-04 — Phase 6
What I did:
Two fixes, both in commit `0a4d08a`:
1. GX register-ID seeding. `GXInit()`'s native override seeds a table that tells `GX_WRITE_RAS_REG` which BP register each state write targets. NSMBW never called that override (it was bound only at MKW's address), so the table stayed unseeded and writes like `GXSetColorUpdate`/`GXSetBlendMode` landed on BP register 0x00 (genMode) instead of 0x41 (cmode0). Net effect: colour writes masked off and cull mode corrupted by unrelated bits. Now seeded natively at boot regardless of whether the guest calls GXInit.
2. Startup overlay dismissal. The host draws an opaque "WiiCompiled" card until a hook at MKW's `StrapScene::CheckInput` (0x800077C8) dismisses it. NSMBW never reaches that address, so the card stayed up over the (now correctly rendering) output. Added an NSMBW hook at `dScBoot_c::finalizeState_WiiStrapDispEndWait` (0x8015CFB0).

What broke / what I didn't expect:
The black screen had two independent causes stacked on top of each other. Fixing the first (register misrouting) produced no visible change because the second (the overlay) was still covering the output.

What I learned:
First instance of what turned out to be the dominant bug class in this port: an SDK function natively overridden at *Mario Kart Wii's* address in the shared runtime is silently dead for NSMBW. Symptoms look like rendering or logic bugs; the actual cause is that the function ran as translated PowerPC and its host-side side effect never happened. `projects/nsmbw/function_map.txt` has NSMBW's real addresses.

What's next:
Strap screen → title screen transition.

## 2026-09-05 → 2026-09-18 — Phase 6/7 (backfilled from the uncommitted diff on 2026-09-19)
What I did:
This stretch was several sessions that were never committed; summarised here from the working-tree diff rather than from notes taken at the time, so it's less granular than the entries around it.

1. **Input works (Phase 7, minimal).** `nsmbw_wpad_overrides.cpp`: the real un-overridden WPAD entry points (confirmed by disassembling `WPADProbe` at 0x801E1080) read the `_wpdcb[chan]` control-block struct directly rather than going through the HLE callback contract. Because `WPAD::Init` (0x801DFB90) is replaced wholesale by the HLE, the linking loop at 0x801DF930 that points `_wpdcb[i]` at its backing storage (`_wpd[]` @0x8039F660, stride 0x9C0) never ran, and `_wpdcb[0]` read as null. Fix seeds channel 0 at boot as an already-synced Wii Remote (status = WPAD_ERR_NONE, handshakeFinished = true, devType = Core). Every WPAD reader now sees a connected controller without needing the game's Bluetooth pairing state machine (`dConnect_c`).
2. **Scene creation works.** `nsmbw_preseeded_rel_link.cpp`: `dScene_c::createNextScene()` could never build the RESTART_CRSIN scene because `fProfListMg_c::m_data_p->profileList[1..8]` read as zero at runtime. Traced: the translator's embedded data blob for `d_profileNP` is correct, and `RestoreRelImages()` copies it correctly at boot; the corruption happens later when the guest's own `DynamicModuleControlBase::link()` (0x80160080) runs `do_load()`/`do_link()` on the four pre-seeded RELs. Those four exist on disc only as `.LZ` files, which this runtime's DVD layer can't decompress, so the real load path fails and zeroes memory the pre-seed had already set up. Override skips `do_load()`/`do_link()` for exactly those four module names and reports success.
3. **Translator: state-free ABI mismatch handling** (`NsmbwMultiModuleShardEmitter.cs`). When modules are merged and a caller's `_statefree` forward declaration disagrees with whichever module's copy of the target won, the emitter used to drop the whole caller. Now it rewrites only the affected call site(s) to the always-correct `InvokeDirectCpu` fallback and keeps the rest of the caller intact.
4. Host startup card (`settings_overlay.cpp`) made opt-out per product via `DisableStartupScreen()` — NSMBW's strap warning is itself the first thing the guest draws, so the card was covering real content rather than a gap.
5. A run of `TEMPORARY` diagnostics in `os_scheduler.cpp`, `os_thread.cpp`, `vi.cpp`, `nsmbw_wiistrap_dismiss.cpp` and ~15 `*_diag.cpp` override files under `projects/nsmbw/native/`, all from chasing the post-input black screen (does the scheduler go idle and never resume? does `GXCopyDisp` stop being called? does `VISetBlack(TRUE)` ever get its matching `FALSE`?). Each of these ruled something out; none found the cause.

What broke / what I didn't expect:
Roughly two weeks of chasing individual GX state setters (blend modes, TEV colours, alpha, UV data) with no visible progress. Every 2D element that did render (strap screen, save-file dialog with its confirm button, a blue rectangle, a wipe circle) was either partially drawn or drawn in the wrong place, and no 3D draw was ever correct.

What I learned:
Investigating one GX setter at a time was the wrong strategy when there was no known-good draw anywhere in the pipeline to compare against. Without a baseline, every anomaly looked equally plausible as the cause. See the next entry for what worked instead.

What's next:
Bisect the pipeline from the bottom up instead.

## 2026-09-19 — Phase 6 (first 3D frame)
What I did:
Changed approach: instead of asking "why is draw X wrong", asked "what is the lowest layer that can be shown to work, and where is the first layer above it that fails". Then fixed each boundary in turn.

1. **Baseline: one triangle through the real backend.** `NsmbwDrawTestTriangle()` in `runtime/src/hle/gx/gx_copy.cpp`, gated on `NSMBW_TEST_TRIANGLE`. Submits a single solid-colour triangle at known coordinates through the normal GX FIFO → aurora path, just before the display copy. It rendered on the first try. That proved host vertex submission, aurora decode, pipeline compile, draw, and present were all fine — the problem had to be in what the *guest* was feeding in, not in the host.

2. **Texture copies were clobbering the display-copy stride.** With a known-good draw to compare against, the "143-pixel-wide strip" symptom on 2D content resolved quickly. BP register 0x4D is the EFB-copy destination stride and it is shared between display copies and texture copies; BP 0x49/0x4A are the shared copy source rectangle. NSMBW's `GXSetTexCopySrc` (0x801C5AA0), `GXSetTexCopyDst` (0x801C5B10) and `GXCopyTex` (0x801C63D0) were running as translated code and overwriting those registers before the display copy read them. Bound all three to the runtime's existing HLE wrappers in `nsmbw_gx_texcopy_overrides.cpp` → full-frame 2D.

3. **Indexed matrix loads were dead.** `GXLoadPosMtxIndx` (0x801C9AD0) and `GXLoadNrmMtxIndx3x3` (0x801C9B60) — the calls nw4r::g3d uses to load node matrices from a palette by index — were the same dead-override class. Bound in `nsmbw_gx_mtxindx_overrides.cpp`.

4. **3D projection never reached aurora.** NSMBW's `GXSetProjection`/`GXSetCurrentMtx` don't write XF registers immediately; they cache into the `__gx` struct (`*(0x80433360 − 0x4EF8)`) and set a dirty bit at `__gx+0x5FC`, which `__GXSetDirtyState` (0x801C5430) flushes when `GXBegin` or `GXCallDisplayList` runs. The runtime's `GXCallDisplayList` HLE didn't know about the guest's dirty-state cache, so the 3D scene's projection was set but never flushed before its display lists executed. `NsmbwCallDisplayList_801C9720` in `nsmbw_gx_overrides.cpp` now invokes 0x801C5430 when the dirty bit is set, then runs the display list.

5. **Locked-cache DMA was never happening — the actual root cause of "no 3D".** nw4r::g3d computes node world/view matrices in the locked L1 cache (0xE0000000) and DMAs them back to main RAM via `LCLoadBlocks`/`LCStoreBlocks`/`LCStoreData`. Binding those functions to the runtime HLE (`nsmbw_os_lockedcache_overrides.cpp`) was necessary but *not sufficient*: the translator inlines small leaf functions into their callers (`inline leaf 0x<addr>` in the generated shards), so at the inlined call sites the override was bypassed and the guest wrote the DMAU/DMAL special-purpose registers (SPR 922/923) directly. Nothing on the host honoured those writes, so the view-matrix palettes in main RAM stayed zero and every 3D vertex collapsed. Fix in `runtime/src/ppc_helpers.cpp`, `PPC_WriteSpr`: SPR 922 stores the DMAU value; SPR 923 with the trigger bit (0x2) decodes block count/length, maps the physical main-RAM address to virtual, `memmove`s between main RAM and the locked-cache window, and calls `GxNotifyGuestRamDmaWrite` so aurora sees the updated matrices. Result: first ever correct 3D frame — the World 1-1 title backdrop (hills, bushes, flowers, clouds, fog) at `dma_t30.png`.

6. **Crash on restart, and the reset-on-launch feature.** After one clean run the game was killed mid-frame, and every subsequent launch aborted with `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN, from ucrtbase — this is what an `abort()` looks like in the Windows event log) right after "Using framebuffer size 640x528". Ruled out in order: `Config.toml` (reset, no change), the NAND save (moved aside, no change), the diagnostic env vars (unset, no change). Cause: the SQLite pipeline/shader cache (`nsmbw_data/Cache/pipeline_cache.db*`, `dawn_cache.db*`) had a half-written WAL from the abort, and the pipeline prewarm at boot crashed reading it. Moving `Cache/` aside → immediate boot. `ResetPersistentStateForCleanRun()` in `nsmbw_product.cpp` now runs at the top of `main()`: removes the NSMBW NAND save (`NAND/title/00010004/534d4e50`), resets the `[video]` window keys in `Config.toml` to windowed 640×480 @1×, and wipes the pipeline cache **only** if a `.last_run_unclean` marker survives from a previous run (written at start, removed by an `atexit` handler on clean exit). `NSMBW_KEEP_STATE=1` opts out. Verified across two runs: run 1 removed the save and reset the config; run 2 after a forced kill wiped 6 cache files and booted.

What broke / what I didn't expect:
- The first 3D frame appeared and then vanished when a blue rectangle (a 2D layout element) drew over it — the 2D layer's depth/blend state is still wrong, so 2D elements paint over the 3D scene instead of compositing with it.
- On that frame the sky is black, the ground tiles are a flat grey strip, and Mario, the logo and the "press 2" prompt are not visible. Those are now bugs against a *working* 3D scene, which is a different situation from before.
- The pipeline-cache crash was mistaken at first for a regression from the day's changes; it was pure state.

What I learned:
- **Bisect at boundaries with a known-good input.** The test triangle located the first broken stage in one run after two weeks of guessing. Now recorded in CLAUDE.md as the default approach.
- **Inlining defeats address-based overrides.** A native override at a leaf function's address only fires at non-inlined call sites. If a bound function's HLE never logs a call, check the shards for `inline leaf` before assuming the binding is wrong. Handling the effect at the lowest level the translator can't inline away (here, the SPR write itself) is more robust than binding the wrapper functions.
- **Locked cache** (concept, for the curriculum): the Wii CPU can lock part of its L1 data cache and treat it as 16 KB of scratch RAM at 0xE0000000, with a DMA engine that copies blocks between it and main RAM. Games use it for hot math like skinning matrices. A recompiler has to emulate the DMA explicitly, because the "cache" is just another array on the host and nothing moves data out of it otherwise.
- **The guest's own SDK state caches matter.** Even when a GX function is correctly HLE'd, the guest's copy of that SDK may defer the actual register write into a dirty-flag mechanism that fires from a *different* function. Overriding the setter without overriding the flusher leaves the state stranded.
- Windows crash forensics: `Get-WinEvent -LogName Application` gives the faulting module and exception code when the process dies with no console output.

What's next:
- Sky (black), ground tiles (grey strip), Mario, logo and "press 2" prompt on the title screen.
- 2D-over-3D compositing (the blue rectangle overwrite).
- Consider pruning the `*_diag.cpp` files whose area is now stable (each removal needs the shard manifest regenerated).

## 2026-09-19 (late) → 2026-09-20 — Phase 6/7/8 (cutscene, file select, audio path)
What I did:
Started from six reported issues: title-screen elements not rendering, no colours/textures in the cutscene, crash mid-cutscene, blocked after selecting number of players, all UI broken, Wii Strap never appearing. Ordered them by evidence before touching anything, then fixed them in that order. Four commits in `MKWiicompiled/` (`246213a`, `5c4baa9`, `b4f0f8d`, `0db7c99`).

1. **Cutscene crash → display-list recording was a dead override** (`5c4baa9`). The late-09-19 session had added a FIFO-desync ring (`NSMBW_LOG_FIFO_DESYNC`, committed separately as `246213a`) to catch the crash `[aurora] unmapped vtx attr 9` (a draw whose vertex descriptor has no position). The ring showed draws with `LR=0x801C9690`, a return address *inside* NSMBW's `GXEndDisplayList` (0x801C9670, identified by structure: GXFlush → GXGetCPUFifo → GXSetCPUFifo → clear `__gx+0x5F8`; its partner `GXBeginDisplayList` is 0x801C95B0). Neither was bound for NSMBW. The host only redirects gather-pipe writes into the guest's display-list buffer while `g_dlRecordState.active`, which only MKW's bound Begin ever set; so every command NSMBW *recorded* was executed live, and the buffer later replayed by `GXCallDisplayList` was stale memory: garbage CP writes cleared POS from the VCD (the FATAL) and float bytes were dereferenced as pointers (the ~98 `GX guest pointer: memory error` lines per run). Fix: NSMBW overrides for both that mirror the guest bookkeeping (dirty flush via 0x801C5430, `__gx+0x5F9` state save at 0x80390850, `__gx+0x5F8` flag) and start/stop host recording; the record state now carries the guest fifo-object address (NSMBW's 0x803907D0; MKW unchanged) so the wrap/count publish does not land on MKW's addresses, which are live NSMBW `.data`. Result: memory-error lines 98 → 0, desync events 10+ → 0, no FATAL in any later run.
   *Concept:* a display list is a block of GX commands the guest records into RAM once and replays many times. On hardware `GXBeginDisplayList` re-points the CPU FIFO at that buffer; since our host intercepts every gather-pipe store, it has to be told when recording starts or it draws the recording instead of storing it.

2. **Blocked after player-count select → audio never advanced** (`b4f0f8d`). New diagnostic `NSMBW_LOG_STATE_CHANGES` overrides the one non-template `sStateMethod_c::changeStateMethod` (0x8015FD50) and prints every state ID by name (they are strings like `dScGameSetup_c::StateID_VoiceEndWait`). It showed GAME_SETUP's last transition was `LowBatteryCheck → VoiceEndWait`: the scene waits for Mario's voice clip to *finish*, and nothing ever reported a sound ending. Cause: only `AIInit` was bound; `AIRegisterDMACallback` / `AIInitDMA` / `AIStartDMA` / the getters ran translated against flat register storage, so the host's per-block audio pump never had AX's DMA callback, `__AXNextFrame` never ran, nw4r::snd never ticked. Three pieces were needed: (a) bind the AI DMA family (0x8019F1F0/F240/F2C0/F2E0/F2F0/F310, identified from their register accesses); because `AIStartDMA` and the three getters are inlined into their single AX call sites, `dsp.cpp` now serves the AI-DMA registers (0xCC005030–3A) from the AI HLE and starts DMA on the CTRL_LEN bit-15 write. (b) The four AX DSP-task callbacks (0x801A1D90/1DA0/1E00/1E10) are only reachable through a struct of function pointers, so the recursive translator never emitted them (`missing_target 0x801A1DA0` on the first AX frame); added to `function_map.txt` as seeds and the base translation re-run. (c) With audio live, the scheduler's idle loop broke out right after `Audio_HLE_Poll` on every pass (the guest runs slower than real time, so an audio block is always due), starving `VI_HLE_PollRetrace`; the main thread sat WAITING forever in `EGG::AsyncDisplay::beginFrame` (found with `NSMBW_LOG_IDLE_THREADS`, which dumps thread states at idle plus a stack walk of the sleeper from `SelectThread`). Removed the early break. Result: `VoiceEndWait → NextSceneWait → MOVIE`, and the intro cutscene plays for 60+ s, fully textured.

3. **All 2D UI broken / strap invisible / title elements missing → SU texture-size registers** (`0db7c99`). With 3D correct after (1), the file-select screen still drew smeared gradients with a hard band. Bisection: `NSMBW_DUMP_TEXTURES` showed the decoded textures were right (pastel hills, gradient ramps); `NSMBW_LOG_TEXFMT` showed sane formats/filters and correct load→draw order; a new per-draw dump `NSMBW_LOG_DRAW_TEXGEN` showed correct texgen, TEV, blend and projection, but `suScale=1/1` on every 2D draw against 628x96 / 198x164 textures. Aurora samples at `uv × SU_TS scale / texture size`; the SU_TS0/1 registers (BP 0x30/0x31) are written by the guest's `__GXSetSUTexRegs` (0x801C7A10 → `__SetSURegs` 0x801C7980) from the per-texmap image0/mode0 words the SDK's `GXLoadTexObj` caches at `__gx+0x564` / `+0x584`. Our native `GXLoadTexObj` replaced that body without those two stores, so the guest always computed 1x1 and every pane showed a one-texel sliver magnified across it. Fix: the override writes both words. Result: File Selection renders correctly (title text, three cards, Erase/Copy, Free Mode/Coin Battle, hill background), plus the "save data has been created" dialog, the Wii Strap screen, and the title logo/hills/ground through the circle wipe.

What broke / what I didn't expect:
- Binding the AI DMA family first made the game stall *before* the strap: three faults were stacked (missing translated callbacks, then scheduler starvation), each only visible once the previous one was fixed.
- Re-running `translate-recursive` from 0x80004000 instead of the DOL entry 0x80004050 silently dropped the entry point (`missing_target 0x80004050`).
- A "one-draw texture lag" I thought I saw in aurora was an artefact of reading `g_gxState.textures[]` (the previous draw's resolved binding) instead of `loadedTextures[]`.
- The strap "alpha 0 for the whole run" finding from the 09-05→18 stretch was the same SU-scale bug seen through a different lens (the pane sampled a transparent sliver of its texture), not a broken fade animation.

What I learned:
- Third bug class for this port (now in CLAUDE.md): a native override replaces an SDK function *including its guest-side bookkeeping*. Both `GXBeginDisplayList` and `GXLoadTexObj` bit us because other still-translated SDK code (`GXCallDisplayList`, `__GXSetSUTexRegs`) reads `__gx` fields the replaced body would have written. Read the translated body before binding and mirror every store to guest globals.
- Name-based state logging beats guessing: one run of `NSMBW_LOG_STATE_CHANGES` answered "where is GAME_SETUP stuck" exactly, where earlier sessions had built force-advance experiments around the same question.
- Verify with counts, not impressions: memory-error lines per run (98 → 0) and desync events (10+ → 0) made the display-list fix checkable before the cutscene was even reachable.
- *Concept — SU_TS registers:* GX texture coordinates are normalised (0..1) when generated, and the hardware multiplies them by the texture's size (held in the SU_TS registers) before sampling. Aurora reproduces that step, so a stale size register shows up as magnification, not as a missing texture.

What's next:
- Title screen: the sky may still be black behind the logo (only seen through the wipe circle this session; needs a run that idles on the title). Mario and the "press 2" prompt not yet checked post-fix.
- Play forward from the cutscene into the world map and 1-1; log crashes with `NSMBW_LOG_STATE_CHANGES` on.
- Boot never auto-advances the strap without a button press (8400 frames in BOOT in a no-input run); the decomp's 1200-frame `mAutoAdvanceTimer` path should. Low priority, but a real divergence.
- Prune the pre-input `NSMBW_FORCE_GAMESETUP_ADVANCE` / `NSMBW_CHAIN_ADVANCE` experiments and the `*_diag.cpp` files whose areas are now stable (each removal needs the shard manifest regenerated).

## 2026-09-20 (later) — Phase 7 (real input through KPAD) + repo restructure
What I did:
1. **Input now goes through `KPADRead`** (`nsmbw_kpad_overrides.cpp`). The previous scheme polled the keyboard in the VI tick pump and OR'd "just pressed" bits into `EGG::CoreController`'s field at `+0x1C` via a native `downTrigger` override, so only press edges ever existed and nothing could be *held*. Traced the game's real sampling: `func_802BD0D0` (`EGG::CoreController::beginFrame`, virtual, no static callers) calls `KPADReadEx(mNum, this+0x18, 16, &err)`, so `+0x18/+0x1C/+0x20` are `KPADStatus[0].hold/trig/release` — the old hack was poking one field of that struct. Both `KPADRead` (0x801ED4E0, `r6=0; r7=0`) and `KPADReadEx` (0x801ED4F0, `r7=1`) tail into a shared internal at 0x801ED500; that is now overridden to return one real sample per frame for channel 0 (hold/trig/release computed from a per-VI-tick keyboard snapshot so every read in a frame sees the same edge) and "no controller" for channels 1–3. The `downTrigger` override and the PAD code in the tick pump are gone; the self-test press is delivered as one frame of "A held" through the same path.
   Keys (the remote is sideways, so the arrows are rotated to what the game expects): **Z = 2**, **X = 1**, **Enter = A**, Left/Right/Up/Down → WPAD UP/DOWN/RIGHT/LEFT.
   Verified with injected key events and `NSMBW_LOG_INPUT` (`build_nsmbw/nsmbw_keytest.err.log`): scancode 29 → hold 0x0100 (2), 79 → 0x0004 (d-pad DOWN = screen right), 81 → 0x0001, 80 → 0x0008, each with matching trig/release; the self-test presses still carry the game strap → title → File Selection; and the developer confirmed live that keys work.
2. **One repo instead of two.** The outer docs-only wrapper repo was removed and the fork's contents lifted to the top of `Wiicompiled/` (git history and the `ral1009` remote intact). Removed `Launcher/` (MKW installer tooling), `.github/ISSUE_TEMPLATE/` and the `function_map.txt.bak*` files; kept `projects/mkwii/` because translator tests use it. Manifests now read `NSMBW-Decomp/original/` (the clones sit inside the repo directory, gitignored). Verified by a fresh CMake configure + full rebuild + a run to the cutscene.
   The folder was **not** renamed to match the GitHub repo: Claude Code keys its per-project memory and session transcripts to a slug of the absolute path, so a rename would have re-keyed all of it for a cosmetic gain.

What broke / what I didn't expect:
- Four local, untracked artefacts had the old absolute path baked in and each failed in turn after the move: the CMake cache (source dir is `runtime/`, not the repo root), the FetchContent sub-builds under `build_nsmbw/_deps`, the MKW shard manifest under `generated/`, and the NSMBW data initializer (`incbin` paths). Regenerated or reconfigured all of them; nothing tracked changed.
- The first injected-key run showed a "wrong" bit for Z; on the next run with raw scancodes logged every key mapped correctly, so it was most likely a real key pressed at the same time, not a mapping bug.

What I learned:
- Inject input where the guest *samples* it (the SDK read function), not where it *consumes* it (a game class's field): the SDK struct is the contract, and everything the game derives — held state, edges, button repeat, the sideways d-pad rotation — then comes from its own code.
- Absolute paths hide in build caches, not in the repo. After moving a tree, expect the cache, sub-builds and generated initializers to need regeneration even when `git status` is clean.

What's next:
- Bind B (Shift?), + (pause) and − / HOME; decide whether the pointer needs emulating for any menu.
- Idle on the title screen to check the sky / Mario / "PRESS 2"; then play from the cutscene into the world map and 1-1 with real input and `NSMBW_LOG_STATE_CHANGES`.
- Audible audio; strap auto-advance without input; prune stale `*_diag.cpp` overrides.

## 2026-09-20 (evening) — Phase 6/7 (world map, save resume, playable 1-1)
What I did:
1. **Cutscene item-rain crash.** The raw-FIFO staging buffer (`g_hleGxState.fifoBytes`, `runtime/src/hle/gx/gx_internal.h`) was a fixed 4096-byte array; when the intro spawns dozens of Penguin/Propeller suits, single raw draws exceed 4 KB, `pushBytes` in `gx_fifo.cpp` hit the overflow branch and *reset* the buffer, so the parser resumed mid-packet (symptoms: `GX guest pointer memory error 0x58000004`, XF PosMtx sub-copy warnings, then an access violation). It is now a growable `std::vector` (doubles on demand; observed growing to 16384 bytes in the cutscene, guest memory errors 0, cutscene completes).
2. **World-map crashes** (`missing_target 0x80B6E7A0`, `0x80B73330`, then `missing_target 0x0` from 0x808DC34C at the level-entry wipe):
   - Cross-module calls between RELs (d_basesNP ↔ d_en_bossNP ↔ d_enemiesNP) go through relocations the recursive translator doesn't follow, so 77 targets were never translated. Seeded as `cross_module_call_target_<addr>` entries in `function_map.txt` (re-translating a REL needs `--rel-projects` with all four `.yml`s, otherwise "imports from module 2, but no module registry").
   - The null vtable at 0x808DC34C came from `RelFile.BuildImage` (`translator/.../Rel/RelFile.cs`) copying the REL's *file* tail — the relocation tables — over the `.bss` region instead of zeroing it, so `*(0x8099FDFC)` read 0x000254E8 at boot. `OSLink` zeroes `.bss` on real hardware; the loader now stops the copy at `bssOffset`. Translator tests still pass (570).
   - `extract-data-function-pointers` returned zero candidates for RELs because it only scanned main.dol's data; it now also runs `CollectDataSectionFunctionPointerTargets()` over REL data (vtables), which produced the missing 0x800FE020 and 20 more (`rel_vtable_target_func_<addr>` seeds).
   Verified by a 330 s run boot → map → 1-1 → map → 1-1 with 0 crashes and 0 memory errors.
3. **Save resume.** The persisted `wiimj2d.sav` was being re-created at every launch even though it was on disk. `NSMBW_LOG_NAND=1` (new trace of every NAND `IOS_*` call) showed `dScBoot_c::ExistFileCheck` going `→ NandSpaceCheck → CreateFile` right after `IOS_Ioctlv(fd=1, cmd=4 READDIR, path='/title/00010004/534d4e50/wiimj2d.sav') -> -106`. The SDK's `NANDGetType` probes a path with `ISFS_ReadDir` and classifies the result: `OK` = directory, `EINVAL (-101)` = file, `ENOENT (-106)` = nothing. `HandleIsfsReadDir` (`nand_isfs.cpp`) returned `ENOENT` for anything that wasn't a directory. It now returns `EINVAL` for an existing file; the next boot went `ExistFileCheck → Load → ProcEnd` and the file select led straight to the world map with no MOVIE scene. (`ResetPersistentStateForCleanRun` no longer deletes the save; `NSMBW_RESET_SAVE=1` does.) Note NSMBW only marks a slot as started when you first reach the map, so a save wiped mid-intro stays "NEW" — that was the earlier "the save file doesn't exist" observation, not a write failure.
4. **Level graphics — the main result.** In 1-1 the HUD, far hills and clouds drew, but the sky was black and the ground, Mario and enemies were invisible. `NSMBW_LOG_DRAW_TEXGEN` (now with a VI-tick gate, a minimum-viewport filter, a draw budget, fog/cull and the viewport/scissor per draw) captured 351 level draws: every tile/model draw (triangle strips, 256×256 CMPR tileset textures, 3-stage TEV) ran with **`vp=(0,0 0x0)` and `scissor=(-342,-342 1x1)`**. The scissor value is the tell: the BP scissor registers store coordinates +342, so "−342, 1×1" means the raw registers were all zero — and a 0×0 viewport rasterises nothing. Root cause is bug class 3 again: `nsmbw_gx_setters_diag.cpp` binds `GXSetViewport` (0x801C9D50) and `GXSetScissor` (0x801C9DA0) natively but never wrote the `__GXData` fields the SDK bodies store — the six viewport floats at `__gx+0x544` and `suScis0/1` at `__gx+0x148/0x14C` — while translated readers still consume them: `GXGetViewportv` (0x801C9D80), `GXGetScissor` (0x801C9E10, inlined into `d2d::Multi_c::draw`, `LytBase_c::SetScissorMask` and the save/restore helper 0x8008A230) and `__GXSetViewport` (0x801C9C80, run from `__GXSetDirtyState`). The level's render-to-texture passes (the 640×72 strips and 32×32 tile-animation viewports seen at the top of the log) save the viewport/scissor, draw, then restore them — restoring zeros. The two overrides now mirror the SDK stores exactly (float stores + `GX_DIRTY_VIEWPORT`; the +342/`<<12` BP encoding + `bpSentNot`). Developer confirmed live: sky, tiles, Mario, enemies and background all render and 1-1 is playable.
5. **Self-test on the map**: a resumed save starts Mario on the start node, and on the map A opens free-look ("Back to Mario"), not the level. The auto-press now taps screen-right once (ticks 30–33 of the map visit), suppresses presses for 90 ticks, then presses **2** on the map (A elsewhere). Also `g_nsmbwCurrentViTick` is published from the tick pump for the draw-log gate.

What broke / what I didn't expect:
- My first 160-draw capture was entirely render-to-texture work (640×72 strip, 32×32 tile viewports) and a second one, filtered to viewports ≥ 400 px, was entirely HUD — the 3D draws were failing the *viewport* filter because their viewport was the bug. Negative results from a filter are evidence too.
- A 90-tick right *hold* on the map put it into free-look mode instead of moving Mario; and pressing A there does the same. Neither was documented anywhere I looked; the developer knew.
- The shell tool unescapes `\\n` in heredocs, so `printf`-style format strings written through Python heredocs got real newlines twice before I switched to `chr(92)` / a script file.
- I killed the developer's running game once by stopping `NSMBWCompiled` before a rebuild. Don't do that when someone may be playing; let the link fail instead.

What I learned:
- **"Raw register = 0" fingerprints.** Scissor `(-342,-342 1x1)` and viewport `0x0` together mean *someone restored zeroed GX bookkeeping*, which points at an override that skipped its guest-side stores — faster than reasoning about which object is missing. Worth remembering alongside the two earlier class-3 cases.
- The SDK encodes "is this a file?" as an *error code* from `ISFS_ReadDir`. When emulating an API, the distinctions between failure codes are part of the contract, not detail.
- One concept used above: `.bss` is the zero-initialised data section — it occupies no bytes in the file, so a loader must clear that memory itself; copying "the rest of the file" into it is wrong by construction.

Regression found right after (same evening): wrong/black patches on the world map, title and levels. Cause: mirroring `GX_DIRTY_VIEWPORT` along with the viewport floats made the guest's `__GXSetViewport` re-emit the XF viewport on every flush with the SDK's +342 centre offset, while aurora encodes/decodes its own viewport with +340, so `NSMBW_LOG_VIEWPORT` showed `ox=660` and `ox=662` alternating frame to frame (a 2 px shift on and off). The HLE call had already applied the viewport, so the override now stores the floats (for `GXGetViewportv`) without the dirty bit; the alternation is gone and the developer confirmed the scenes look right again. Lesson: mirror the *stores* another guest reader needs, not the side effects the host already performed.

What's next:
- Decide whether aurora's viewport offset should become 342 to match hardware (it is self-consistent at 340 today, so only guest-written XF viewports - display lists, `GXSetViewportJitter` - are 2 px off).
- Play further into 1-1 / other levels and log crashes; check pipes, power-ups, the goal pole and the level-clear save (`NSMBW_LOG_NAND`).
- Clouds looked grey and hills washed out in the broken frame — re-check colours now that the viewport is right; fog is logged per draw if needed.
- Commit hygiene: the NAND trace and the draw-log options stay env-gated; prune `*_diag.cpp` files whose purpose is finished.
- Audio output, B/+/−/HOME bindings, strap auto-advance.

## 2026-09-21 — Phase 7 (save import, second level, texture cache)
What I did:
1. **Imported a real Wii save into file 2.** `data.bin` is the Wii's SD-card save export: AES-128-CBC with the public SD key, a 0xF0C0-byte header under a fixed IV, then a `Bk` block and per-file entries each carrying its own IV. Inside was `wiimj2d.sav` from an NTSC-U console (`SMNE`). The save layout, checked against `dMj2dHeader_c`/`dMj2dGame_c` in the decomp and the translated `getSaveGame` (0x800E0470: `this+0x6C0 + i*0x980`) / `getTempGame` (0x800E04A0: `this+0x2340 + i*0x980`): 0x6A0-byte header, three 0x980-byte save files, then their three quick-save copies, each block ending in a standard CRC32 over its own bytes (the header's skips the 4-byte magic). Region only appears in the header magic, so the completed (`mGameCompletion = 0x7E`) NTSC slot went into slot 1 of the PAL file with its own CRC intact and the header untouched. Scripts stayed in the session scratchpad; a backup sits next to the save.
2. **Second level: wrong tileset.** Playing 1-2 from the imported file: stone tiles drawn as 1-1's grass. The pattern (geometry right, shared Pa0 tiles right, sprites/HUD right, level-specific Pa1 tiles showing the *previous* level's atlas) says "stale host texture for a reused guest buffer", not a decoder or texcoord bug. aurora's static texture cache only re-validates a source after `invalidate_static_texture_cache()` bumps its revision, and the only caller was the private sub-command emitted by the *host* `GXInvalidateTexAll`. NSMBW's `GXInvalidateTexAll` is 0x801C7800 (found by searching the translated functions for `lis 0x6600`; the body is `__GXFlushTextureState`, BP `0x66001000`, BP `0x66001100`, flush again — the SDK source), MKW's binding is at 0x80171110, so NSMBW's ran as guest code and its two BP 0x66 writes fell through `handle_bp` unhandled. Fixed in `aurora-main/lib/gx/command_processor.cpp`: BP 0x66 → `invalidate_static_texture_cache()`, before the value dedup. Developer confirmed 1-2 now shows the correct tileset; the 1-1 self-test run still reaches the level with no memory errors.

What I learned:
- Bug class 1 again (SDK function bound at MKW's address, dead for NSMBW), but this time the guest-side effect is a *hardware register write*, so it could be caught at the command-processor level instead of adding a per-title binding — the same principle as the locked-cache DMA (`PPC_WriteSpr`) and the AI DMA registers. Prefer that level when the effect has a register.
- A texture cache keyed on address+shape is only correct with an invalidation contract; the SDK's contract is BP 0x66, and any HLE that replaces `GXInvalidateTexAll` has to honour both routes into it.
- Read what is in the screenshot before theorising: "which things are right" (Pa0, sprites, HUD) narrowed this to one texture in one run.

What's next:
- Play on through World 1 (1-3, tower, castle) and log what breaks; the tower/castle boss fights exercise the d_en_bossNP REL.
- The `NSMBW_LOG_TEXFMT` diagnostic and the write-generation hooks would show whether other cache tiers (TLUT, copy textures) have the same gap — check the first time a palette texture looks stale.

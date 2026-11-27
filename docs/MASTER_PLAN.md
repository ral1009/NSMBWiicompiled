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

- Settings UI (reuse WiiCompiled's pattern) — the F10 overlay already runs in NSMBW; making it NSMBW's own is Phase 12 item 1
- Installer/wrapper if distributing
- Confirm no bundled Nintendo assets/code
- Final license check before any public release

---

### Phase 12 — Enhancements (post-playable; added 2026-09-26)

Each item was checked against the code on 2026-09-26. None of it starts until the open regressions in the latest progress-log entry are fixed. In recommended order:

1. **F10 settings overlay → NSMBW's own.** This is the MKW ImGui bar (`runtime/src/settings_overlay.cpp`). It already runs here, and resolution / remap / interpolation all work, but none of them work well.
   - Settings don't persist: `nsmbw_product.cpp` resets Config.toml every launch unless `NSMBW_KEEP_STATE` is set.
   - The remap page is GC-pad based. It should be laid out as a sideways Wii Remote.
   - MKW-only items should be hidden, and the UI styling cleaned up.
   - 1a — "4x doesn't look like Dolphin": **done 2026-09-26**. Causes were 4:3 mode via an unbound `SCGetAspectRatio`, the render scale being reset, and an active copy filter (see the progress log). The render scale now persists across launches; the rest of the reset still applies.
   - 1b — interpolation never reaches 180 fps: run the profiler with it on to see whether the limit is CPU or GPU, and check whether objects are really interpolated.
2. **Tilt on A/D + right stick.** Read the decomp's tilt consumer to learn which acc axes it reads. Then feed a gravity vector through `KPADStatus.acc` in `nsmbw_kpad_overrides.cpp`, the same pattern as the shake.
3. **Texture dump/replace.** aurora already has Dolphin-style replacement (`aurora-main/lib/gfx/texture_replacement.cpp`); NSMBW's `AuroraConfig` just never enables it. Check that the hashes match Dolphin's so existing HD packs work. HD fonts go through the same path.
4. **Early permanent save.** Find the real Save-vs-Quick-Save predicate in the decomp and override it. The address `0x8077AA7C` from an outside spec is unverified. The NAND HLE already persists saves.
5. **Then:**
   - Inspect nymo's HD HUD Tweaks: file overlays already work; `.LZ` files and `<memory>` patches don't.
   - Controller-specific button glyphs: per-controller texture-replacement roots, switched on `SDL_GetGamepadType`.
   - 16:9 via NSMBW's own `SCGetAspectRatio`: done 2026-09-26. Ultrawide is much harder because of camera-bound culling and 16:9 HUD anchoring.
   - Steam Deck: try the exe under Proton first; a native Linux build is its own project.
6. **Parked:**
   - Newer SMBW. Its Kamek code arrives via `<memory>` patches and a runtime loader. Supporting it would need a separate build that patches the DOL/RELs *before* translation and translates Newer's code as an extra module.
   - Online play. Prerequisites: channels 1-3 and verified co-op, a cross-machine determinism check (hash guest RAM per frame), and for rollback, save/restore of guest RAM, thread contexts and HLE state.
7. **Future ideas (2026-11-07)** now live in `docs/futureplans.md`: aspect ratios (promised for v0.3.0), no-AVX2 build, other game versions, save carry-over, Linux, mods and more.

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

## To-do (deferred)
Work that has been decided on but parked. Remove an item when it is done and note it in the progress log.

- **Confirm the seed shader list on the Iris Xe laptop** (2026-10-16). Put `initial_pipeline_cache.db` next to the exe, move `nsmbw_data\Cache` aside, then run `run_with_logging.bat`. Expect "Seeded pipeline cache ... 398 rows merged", "Pipeline prewarm finished", and no multi-second freeze on the first world-map visit.
- **Native AX audio routines** (2026-10-16, low priority). 96% of audio time is NSMBW's own AX library running as translated code (guest AI callback: ~31 ms/s, about 93 us per 3 ms block; host join+push ~1 ms/s). Rewriting the hot routines natively saves at most ~3% of a core on a game thread that is 74-86% idle, so it is only worth it if a slow CPU ever becomes the limit. Needs a bit-exact PCM comparison before and after.
- **Grow the seed shader list** (2026-10-16; partly done 2026-11-02 for v0.2.2). The developer's play cache was merged in: 398 -> 1,532 recipes (1,529 GX + 3 clear), from levels in several worlds the developer played without stutter. Levels nobody has played on the dev build are still missing. Not yet measured: how much longer the background prewarm takes on a first launch, especially on the Iris Xe laptop, now that it compiles four times as many recipes.
- **Animations freeze after a resolution change in a level** (developer report, 2026-10-31). Fixed in the source 2026-11-06, see that progress log entry; not in a release yet, so the README still lists it. The first guess here (stale copy targets) was wrong: the copy targets resized correctly, and the tile atlas they paste into did not.
- **Shader cache** (developer, 2026-10-31; for a future release). The scope is not written down yet; related open items are "Grow the seed shader list" above and confirming the seed on the laptop.

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
2. **Second level: wrong tileset.** Playing 1-2 from the imported file: stone tiles drawn as 1-1's grass. The pattern (geometry right, shared Pa0 tiles right, sprites/HUD right, level-specific Pa1 tiles showing the *previous* level's atlas) says "stale host texture for a reused guest buffer", not a decoder or texcoord bug. aurora's static texture cache only re-validates a source after `invalidate_static_texture_cache()` bumps its revision, and the only caller was the private sub-command emitted by the *host* `GXInvalidateTexAll`. NSMBW's `GXInvalidateTexAll` is 0x801C7800 (found by searching the translated functions for `lis 0x6600`; the body is `__GXFlushTextureState`, BP `0x66001000`, BP `0x66001100`, flush again — the SDK source), MKW's binding is at 0x80171110, so NSMBW's ran as guest code and its two BP 0x66 writes fell through `handle_bp` unhandled. Fixed in `aurora-main/lib/gx/command_processor.cpp`: BP 0x66 → `invalidate_static_texture_cache()`, before the value dedup. Developer confirmed 1-2 now shows the correct tileset; the 1-1 self-test run still reaches the level with no memory errors. Later the same day the developer played 1-1, 1-Tower and 1-Castle from the imported file with no visible defects — the first boss fights (Larry, `d_en_bossNP`) and the tower/castle tilesets on this port.

What I learned:
- Bug class 1 again (SDK function bound at MKW's address, dead for NSMBW), but this time the guest-side effect is a *hardware register write*, so it could be caught at the command-processor level instead of adding a per-title binding — the same principle as the locked-cache DMA (`PPC_WriteSpr`) and the AI DMA registers. Prefer that level when the effect has a register.
- A texture cache keyed on address+shape is only correct with an invalidation contract; the SDK's contract is BP 0x66, and any HLE that replaces `GXInvalidateTexAll` has to honour both routes into it.
- Read what is in the screenshot before theorising: "which things are right" (Pa0, sprites, HUD) narrowed this to one texture in one run.

What's next:
- Play on through World 1 (1-3, tower, castle) and log what breaks; the tower/castle boss fights exercise the d_en_bossNP REL.
- The `NSMBW_LOG_TEXFMT` diagnostic and the write-generation hooks would show whether other cache tiers (TLUT, copy textures) have the same gap — check the first time a palette texture looks stale.

## 2026-09-23 — Phase 7/8 (input additions, profiler, performance, tile animations)
What I did:
1. **Input.** `=` -> +, `-` -> -, `C` -> shake (also ZL/ZR on a pad), left stick -> d-pad, B -> WPAD B. Shake is not a button in NSMBW: `dGameKeyCore_c`'s helper 0x800B62A0 sets `mIsShaking` when |acc.y - previous| >= 0.28 (SDA2 float 0x8042C8A4) for 4 frames in a row, then the player adds its own cooldowns. A C press therefore feeds an 8-tick synthetic acc.y burst (1.0, 0.5, 0.0, -0.5, repeating - any two samples up to 3 ticks apart differ by >= 0.5, so it still fires if the game only reads every 2nd or 3rd frame) through `KPADStatus.acc`, and the game's own detector does the rest. The remap menu names the PAD slots after their Wii meaning for NSMBW. Developer reports the Switch Pro Controller works; +/-/C/stick not yet confirmed.
2. **Sampling profiler** (`NSMBW_PROFILE_SAMPLER=1`, `runtime/src/product/nsmbw_product.cpp`): a host thread suspends every thread ~500 times a second, unwinds its stack with the x64 unwind tables (`RtlLookupFunctionEntry`/`RtlVirtualUnwind` - the same data C++ exceptions use) and appends a 10 s histogram of whole call stacks to `build_nsmbw/nsmbw_profile.txt`, with presented frames/s and scene per window. `projects/nsmbw/tools/resolve_profile.py` names frames (llvm-nm for the exe, where translated code is `func_<guest addr>`; export tables for Windows DLLs) and prints per-thread busy/wait, self, inclusive and "who called into this DLL".
3. **What it found on the world map (first benchmark only; every item is scene-independent):** main thread 96 % busy (22910 of 23785 samples); 53.6 % inclusive inside the CRT's `getenv` (per-command diagnostic switches), 23.7 % in `WriteFile` (per-frame log lines). See `docs/issues.md` 2026-09-23 for both fixes. Overlay afterwards: 60.0 fps on the map and in 1-1 at 1x (was 24.7 on the map).
4. **Frozen tile animations (placed coins).** See the issues entry: sub-rectangle EFB copies into the tileset atlas never reached guest RAM. Fixed in aurora's `GXCopyTex` + a strided readback in `efb_ram_copy.cpp`. Developer confirmed coins spin.

What broke / what I didn't expect:
- Twice, text written through the shell's heredoc had `\n` turned into a real newline inside C++ string literals; patch scripts now go through files.
- A GQR theory cost time: my first `YrotM` test returned identity, but it had run on another guest thread's registers. The fiber scheduler swaps each thread's full `CpuContext`; the main thread's GQRs were fine throughout. Measure on the thread that does the work.
- The self-test's single right-tap on the map was sometimes swallowed and the run parked on the start node; it now re-taps every 2 s, and after the map holds right + pulses 2 in the level (Mario still dies to the first Goomba - good enough to reach 1-1, not to reach coins).

What I learned:
- An env-var check is not free: on Windows `getenv` is a locked linear scan. A diagnostic "off" switch on a per-draw path has to be cached.
- `GXSetTexCopyDst`'s width is a row pitch. Copies can write *into* an existing texture; a renderer that keys EFB copies by destination address alone will never see them.
- One concept used above: a *stack unwind* reconstructs the chain of callers from a paused thread's registers and stack, using per-function tables the compiler emits; it is what turns "the CPU is in strchr" into "fifo::process called getenv".

5. **Liquids (water / lava / poison) invisible.** Not the tile-animation path after all: liquids are drawn by raw GX code in main.dol, reached from the liquid actors' `m3d::proc_c` draw slots. Static tracing (actor profile 596 -> class-init 0x807B40A0 -> vtable 0x80950B48 in the relocated `d_basesNP` image -> `draw()` 0x807B52F0 -> 0x800EAC30 -> proc entry -> `proc_c_drawProc` -> 0x8000AFA0) found every link intact, so a runtime capture settled it: `NSMBW_LOG_LIQUID` arms aurora's per-draw log from inside the renderer via a GX debug marker (stream-ordered, because aurora processes the FIFO later than the HLE call). The liquid draws had colour writes off - `GXSetDither` was unbound and re-sent the guest's stale copy of BP 0x41. Bound it; water now draws. See `docs/issues.md`.
   The first liquid capture looked like "renderer never called" because the texture-bind trigger's 60-entry cap was used up by the tile animator (0x8000A3D0, just below the renderer) - the range and cap were wrong, not the game. Per-call-site caps since.

What broke / what I didn't expect (liquids):
- Two wrong leads before the capture: a `m3d::pushBack` override (matches the original instruction for instruction) and a "draw gate" on `dScStage_c` bytes 0x120C-0x120E (compares against 7/23/3 - a one-level special case, read at first as `!= 0`). Reading the compare operands, not just the branch shape, would have saved a round.
- The fix exposed a new difference: the sky over water is darker than hardware. Open item; A/B switch ready.

Confirmed later the same day by the developer: 8-Castle lava draws, the Switch Pro Controller stick and buttons work, keyboard `=`/`-`/`C` work, and the one-time stall entering a water level did not recur (most likely my test instance overlapping the developer's, or first-time pipeline builds). New report: item boxes look wrong since these changes.

What's next:
- Regressions from `2688f2c`: item boxes untextured everywhere (1-1 included) and the World 2 map has no ground (objects over black; screenshot from the developer). First check the strided-copy rule against the W2 map's `NSMBW_TEXCOPY` lines (a padded destination width would be misread as a sub-rectangle copy), then `NSMBW_DITHER_LEGACY=1`.
- A/B the dark sky over water with `NSMBW_DITHER_LEGACY=1` (capture_on_log.ps1 on the title demo), then bind/emulate whichever caller differs.
- Missing "Quit?" question text in the exit-level dialog (Yes/No render).
- Post-fix profiler runs across map / 1-1 / 1-2 / tower / transitions to find the next shared bottleneck; check 2x-4x now that tile copies are no longer 1024x1024 x scale each.

## 2026-09-26 — Phase 7 (World 2 map investigation, test tooling)
What I did:
1. **Isolated test runs.** Runtime portable mode (a `portable.txt` next to the exe moves config + NAND into `UserData\`) lets a copy of the exe run with its own save: `projects/nsmbw/tools/portable_shot.ps1` boots a portable copy with the self-test, waits for a scene and screenshots it. The test save puts the developer's completed file into file 1 with `mCurrentWorld = 1` (World 2) and a recomputed CRC; the real save is never touched. `burst.ps1` now uses `PrintWindow`, so captures work when Windows will not bring the game to the front.
2. **Self-test:** alternates A and 2 outside the map (it stalled on file select with A alone).
3. **EFB copy addressing fix** (issues.md 2026-09-26): copies were keyed by the cached address, textures by the physical one; canonicalised.
4. **World 2 ground - not fixed.** Traced the ground's shading end to end with new capture switches: `NSMBW_ARM_ON_TEX=<w>x<h>:<fmt>` (arms the per-draw log when a texture of that shape is bound; the armed dump now includes position/TEX0/CLR0/NRM formats, colour/normal array heads, dualTex and all three rows of the texture and post matrices), `NSMBW_DEBUG_READBACK_SMALL_COPIES` + `NSMBW_DUMP_LIGHTTEX=<dir>` (land small GPU copies in RAM and dump the map's light textures), `NSMBW_LOG_COPYMATCH`, `NSMBW_DEBUG_NO_FOG`, `NSMBW_DEBUG_NO_LIGHTING`. Every input checked is correct; the open theory is a later full-screen alpha composite (details and next step in issues.md).

What broke / what I didn't expect:
- About an hour on World 2 without a fix. Each hypothesis needed a 2-4 minute boot-to-map run; the self-test's file-select stalls cost several. Cheaper next time: arm on the *output* side first (what covers the pixels) before verifying every input of the draw.

What I learned:
- One concept used above: NSMBW's world-map lighting is "light textures" - EGG renders each light set into small ramp textures every frame and models look them up by surface normal; a black ground can come from any link in that chain, which is why it took so many captures.
- Guest memory views are separate host mappings - any cache keyed by host pointer must canonicalise guest addresses first.

What's next:
- W2 ground: test the full-screen A8 composite theory.
- The developer's PC-port feature list (not started): drop quick saves / allow normal saves before completion; fullscreen without borders and sharper output at 4K; performance for iGPUs and Steam Deck; Newer Super Mario Bros. Wii as a launch option; a custom settings/input menu replacing the MKW-derived overlay; online multiplayer. Each needs a viability assessment before any work.
- Still open: dark sky over water (`NSMBW_DITHER_LEGACY` A/B), missing "Quit?" dialog text.

## 2026-09-26 (later) — Phase 12 item 1a (output matches Dolphin)
What I did:
- Compared the developer's port and Dolphin screenshots of the title screen. The port had bars on all four sides, less of the scene left/right, and softer edges, even at 4x.
- Found three separate causes. Each is in issues.md (2026-09-26, "Output quality vs Dolphin"):
  1. `SCGetAspectRatio` was bound only at MKW's address, so the game ran in 4:3 mode and letterboxed itself. It is now bound at 0x801DD310, and the presenter is locked to 16:9.
  2. The F10 render scale was reset to 1x every launch and never applied at startup.
  3. The display-copy deflicker filter was running because NSMBW's init never set `disableCopyFilter`.
- New switch: `NSMBW_RES_SCALE=<n>` sets the render scale at launch for reproducible runs.

What broke / what I didn't expect:
- My first read ("it's rendering at 1x") was wrong for the developer's run. The render really was 4x (2560x1824 per `NSMBW_LOG_COPYDISP_GEOM`), and the blur came from the copy filter.
- Screenshot tooling: `CopyFromScreen` grabbed whatever window was in front (the game was not). `PrintWindow` captures the game window itself.
- Two of my before/after captures landed on different scenes (title vs attract demo), so they were not comparable. The final confirmation is the developer's own view, not a controlled capture.

What I learned:
- Concepts used above:
  - *Anamorphic* 16:9: the Wii renders widescreen squeezed into the same 640-wide frame, and the TV (here, the presenter) stretches it back out.
  - *Deflicker / copy filter*: a vertical blur applied when the frame is copied out for display, meant for CRT TVs.
- Two of the three bugs are one class: NSMBW's own aurora init (`InitializeAuroraWindow`) skipped settings that MKW's `main.cpp` applies (copy filter, render scale). Worth auditing the rest of `main.cpp`'s aurora config against it.

What's next:
- Audit `main.cpp`'s aurora setup against `InitializeAuroraWindow` for other skipped settings.
- Blocky texels on the blurred title-screen background at 4x (not investigated).

## 2026-09-26 (evening) — Phase 12 item 5 (HD HUD via Dolphin texture packs)
What I did:
- Downloaded nymo's HD HUD Tweaks (GameBanana 480785, with the developer's permission).
- It turned out to be a Dolphin *texture pack* (`Load/Textures/SMN/...`), not a Riivolution mod. So the plan's `.LZ` / `<memory>` concerns don't apply to it.
- NSMBW's aurora init never enabled texture replacement. It now does, from Config.toml's `texture_replacements` (issues.md 2026-09-26).
- The pack's `SMN/` folder works unchanged under `build_nsmbw/nsmbw_data/texture_replacements/`. 11 of its 36 files loaded in a boot-to-level run; the developer confirmed the sharper HUD and fonts.

What broke / what I didn't expect:
- Nothing broke. The MotionHint textures went untested because the game only shows that hint to players who haven't used the spin yet.

What I learned:
- *Texture pack* = Dolphin's mechanism: every texture the game uploads is hashed, and a file named with that hash replaces it. It needs no disc or code changes, only a matching hash algorithm. aurora already implements Dolphin's.
- Third setting skipped by NSMBW's own init (after copy filter and render scale). The audit of `main.cpp` vs `InitializeAuroraWindow` is now clearly worth doing.

- The item-box regression came back once the render scale persisted: ? blocks, bricks and coins broke at 4x only.
  - First suspect was texture replacement; an A/B with it on and off ruled that out.
  - The developer then confirmed 1x fine / 4x broken.
  - `NSMBW_LOG_TILECOPY` showed the tile copy geometry was correct at 4x.
  - Cause: the downscale blit that runs only for scaled copies clamped every sample to row 0 (issues.md). Fixed; 4x capture verified.

What's next:
- Scaled tile cache: animated tiles are written back to RAM at native size, so at 4x they look 1x next to everything else. Dolphin keeps them scaled on the GPU and stitches them into an upscaled tileset; aurora needs the same.
- Controller-specific button glyphs: find which textures carry the Wii Remote button art, then choose a replacement root per controller type.

## 2026-09-27 — Phase 12 item 5 (full HD pack, live toggle, controller button glyphs)
What I did:
- Swapped nymo's HUD pack for the full "NSMBW HD v2.81 (DDS)" Dolphin pack (its `SMN/` folder; the optional `~PSX Buttons` variant was left out). nymo's pack is kept in `build_nsmbw/nsmbw_data/texture_packs_inactive/`, not deleted. A boot-to-file-select run indexed 306 of the 312 files and loaded 69, with no load or format failures. I haven't confirmed why 312 files give 306 keys; my guess is duplicate keys across the pack's folders, since the index keeps only the first.
- **Live toggle**: "HD texture pack" checkbox in the F10 Graphics menu, saved to Config.toml `texture_replacements`. NSMBW now always builds the pack index (`allowTextureReplacements = true`), and the config value only picks the starting state. Turning it on or off bumps a revision counter, and `gx.cpp` then drops every cached static texture (`drop_all_static_textures`). A plain cache-revision bump is not enough: the source cache re-validates entries by their guest bytes, which don't change, so it would keep serving the old texture.
- **Button glyphs that follow the bindings**. The in-text button icons ("Press (2) to Start") are characters of `mj2d00_PictureFont_32_RGBA8.brfnt`. I parsed its TGLP/CMAP blocks and decoded the sheets. The layout is two 256×256 RGB5A3 sheets of 7×7 cells, each 32×32 at a 33-texel pitch; glyph index = char − 0x20; the Wii icons are on sheet 0 at `!"#$%&'(`. Its pack name is `tex1_256x256_7b53229a3b86fede_5`.
  - aurora: `aurora_set_texture_patches(hash, w, h, fmt, rects)`. Each rectangle, in native texels, is cleared and filled with a PNG, scaled to the base texture's size. The base is the pack file when the pack is on, otherwise the decoded original, so glyphs work without any pack. `png_io` gained in-memory decoding.
  - runtime: `runtime/src/product/nsmbw_button_glyphs.cpp`. It tracks the last-used device from SDL events (key press → keyboard, gamepad button/axis → `SDL_GetGamepadType`). For each icon it resolves Wii button ← GC PAD bit (fixed by `TranslatePadToWpad`) ← SDL button (live `PADGetButtonMappings`), then picks `<set>/<control>.png`. It re-patches only when that result changes.
  - Art: white icon sets for Xbox, PlayStation and Switch, cut from sheets the developer supplied by `projects/nsmbw/tools/cut_button_glyphs.py` into `projects/nsmbw/assets/button_glyphs/`, and compiled into the exe with `#embed`. `gen_button_glyphs.py` regenerates the glyph table. Keyboard and GameCube pads keep the Wii icons, since there is no art for them yet.
  - Verified: with `NSMBW_GLYPH_TEST=xbox` the title shows "Press (A) to Start" over the HD pack at 4× (screenshot). The log shows `xbox:dpad:north:-:start:back:west:south (6 cells)`, i.e. Wii 2 → south (A), because the config has `y = "south,east"`. The developer confirmed the PlayStation and Switch sets on screen.

What broke / what I didn't expect:
- The developer tested all three sets and reported two problems:
  - PlayStation Erase/Copy (Wii − / +, bound to back/start) showed bare pills. On that sheet, Share and Options are a pill with a faint grey label, and background removal drops the label. Replaced them with drawn white-disc icons (≡ for Options, arrow-out-of-tray for Share) in `cut_button_glyphs.py`.
  - The (2) on "Hold the Wii Remote sideways" stayed a Wii icon, because it is a separate layout texture, not a font character. The HD pack's `SMN/Buttons` holds exactly two such textures, `tex1_40x40_332ba0ec293b70b9_5` (the "2") and `tex1_48x48_6de12cfa944308b1_5` (a d-pad); its `~PSX Buttons` variant replaces the same two. Both are now patched whole. Other screens may use further icon textures that no pack covers; a texture dump would find them. The fix is built but not yet run: the link failed because the game was open.
- I claimed from reading the code that F10 couldn't work in NSMBW (events pumped, never polled). A before/after screenshot of pressing F10 showed the menu opening. The poller is `UpdateAuroraAndProcessEvents()` in `runtime/include/aurora_events.h`, called from `vi.cpp`; my grep only covered `.cpp` files. No change was made.

What I learned:
- *Texture patching vs. replacement*: replacement swaps a whole texture for a file; patching edits rectangles of whichever texture would otherwise be used, so it composes with any pack.
- `#embed` (a C23 feature clang also accepts in C++): the compiler pastes a file's bytes into an array initializer, so assets ship inside the exe with no generator step.
- SDL3 names gamepad buttons by *position* (south/east/west/north), not label. That's why one binding table works for all three pads: Xbox A, PlayStation ✕ and Switch B are all "south".

What's next:
- Licenses for the Xbox and PlayStation icon sheets must be confirmed before the art is committed. The Switch sheet is Zacksly's, CC-BY.
- Keyboard glyph art (keycaps); a "Glyphs: Auto / Wii / controller" override in the F10 menu.
- The 80×76 "shake" prompt textures and the Remote icons (`,` `-`) are still Wii art. Shake is bound to L/R, which the overlay only offers as analog triggers.
- The live pack toggle's own on/off switching has not been exercised in a recorded run yet.

## 2026-09-27 (later) — Phase 7/12 (boot skip, per-context controls, Kenney glyphs)
Supersedes two points of the entry above: the glyph art now comes from Kenney's CC0 "Input Prompts", replacing the sheets cut from store previews (the Xbox pack forbids redistribution and the PlayStation one is a paid Fab listing). So the license to-do is closed.

What I did:
- **Boot skip** (`projects/nsmbw/native/nsmbw_skip_boot_screens.cpp`). The strap warning and "Hold the Wii Remote sideways" screens no longer show, since this port has no Wii Remote support. The game's own `GAME_FLAG_AUTO_SKIP` (0x8042A260 bit 19) alone was not enough. A run with only the flag still faded the strap in, held it for WiiStrapKeyWait's 60-frame minimum, then showed the controller screen, because the flag only ends the *DispEndWait* states (d_s_boot.cpp:589/683/820). Jumping past the states would skip real work (warning end, HOME-menu enable, the sound-load wait). So every state still runs, and each one is made instant and invisible: flag set, `dScBoot_c::mMinWaitTimer` (+268) = 0, and both layouts' `mVisible` (+521) = false. Offsets were read from the translated func_8015CE80 / func_8015D010 / func_8015D0B0. This is applied after every state change (the existing changeStateMethod override) and once per frame. Result: black → title by 9 s (screenshots at 3/6/9 s), boot scene done at call #143 instead of #1369. `NSMBW_SHOW_BOOT_SCREENS=1` keeps the screens.
- **Per-context action controls** (`runtime/src/product/nsmbw_controls.cpp`). Physical controls are bound to actions separately for course / world map / menus, and each action produces its Wii button. Reason: the game gives one Wii button different meanings (2 = Jump / Confirm / enter course; 1 = Run / Back / Items), so one fixed mapping couldn't separate them. Gamepads (port-0 pad, else all pads) and the keyboard are read straight from SDL; the KPAD override calls `NsmbwControlsReadWpad()` instead of `TranslatePadToWpad` (`NSMBW_LEGACY_PAD_MAPPING=1` restores it).
  - Context comes from the scene profile, `dInfo_c::m_startGameInfo.mGameMode` (0x80315E98; title = 2/3), and the game-stop word at 0x8042A228. That address was derived from `isGameStop` (`[r13-22360]`, SDA base 0x8042F980) and confirmed: a 200 ms Escape hold in 1-1 logged `game-stop word 0x0 -> 0x1`, the context went to menu, and it went back to gameplay when the game resumed.
  - The F10 Controller settings menu now shows "NSMBW controls" (tabs per context, click a slot then press, right-click clears, reset) in place of the GameCube presets and mapping table. Saved to `nsmbw_controls.ini`. Verified by clicking: rebinding Jump's key to K wrote `gameplay.jump.key = k,space`, and "Reset to defaults" restored `z,space`.
- **Glyphs follow the context**. Each icon shows the first control bound to the action that produces its Wii button *in the current context*. Logged: 1 → east in menus, west on the map and in a course; A → east in a course, north on the map. A keyboard set (66 keys) was added. The F10 "Button icons" choice (Auto / Wii / Xbox / PlayStation / Switch / Keyboard) is saved as `[controller] button_icons`; its dropdown was not click-tested.
- **Art**: `import_kenney_glyphs.py` converts Kenney's 128 px icons. Kenney cuts the symbol out as transparency, so enclosed transparent areas are filled black (developer: "make the letters black") so they stay readable on light panels. Kenney's PlayStation Create/Options are bare pills, so those two are drawn as white discs (≡ and share-arrow).

What broke / what I didn't expect:
- Unattended runs stalled at file select on *both* input paths (A/B with the legacy mapping), because the self-test's 1200-tick press window ran out there. The default is now 4000; a default run reached 1-1 in ~30 s.
- The first rebind attempt failed: the per-context table was a third popup level that opened over its parent menu, and the parent kept the mouse where they overlapped. Now tabs in one popup; the formerly dead top row rebinds.
- `SendKeys` taps are shorter than a frame and never reach the game, which samples key *state* once per VI frame. Scripts must hold keys (~200 ms) with `keybd_event`.

What I learned:
- *Small-data area (SDA)*: PowerPC games keep small globals near a base register (r13), so a read like `[r13 - 22360]` names a fixed address once the base is known (0x8042F980 here).
- The game-state flags a port needs (pause, title mode) are often a few bytes the decomp already names; reading them beats inferring state from screens.

What's next:
- The GameCube pad glyph set (Kenney has one; SDL's GameCube positions need checking first).
- Glyphs for the 80×76 "shake" prompt textures, which are still Wii art.
- A file-select FPS dip (one capture showed 15.6 FPS there; not reproduced and not investigated).

## 2026-09-27 (evening) — Phase 8 (performance pass 1)
Developer report: "chugging a bit in some levels". Measured first with the sampling profiler (`NSMBW_PROFILE_SAMPLER=1`) on a self-test run into 1-1 at the developer's settings (4x, 120 FPS interpolation, HD pack on). The game thread was busy 62 % of wall time in 1-1; 1-1 held ~59 FPS but with little headroom, and file select ran at 47.5 FPS. `resolve_profile.py` gained `--children FUNC` (a function's immediate callees) for this.

What I did (each measured, same scene):
- **Tileset atlas re-decoded every frame.** NSMBW's 1024x1024 RGB5A3 tileset atlas receives six animated-tile copies per frame. Their RAM write-back changes the atlas bytes, so gx re-decoded it (`DecodeTiled<RGB5A3>` 10.7 % self), hashed it and uploaded 4 MB every frame, only for the scaled tile cache to swap in its GPU composite. New `scaled_tile_cache::lookup_unchanged`: while the RAM *outside* the tile slots is unchanged (masked hash, once per frame), it returns the composite without decoding. The cache now also runs at 1x (it used to bail at `scale <= 1`), so weak machines get the saving.
- **Texture-pack key hashing.** The pack index is now always built, so every new texture got a full XXH64 before looking up the pack. A (width, height, format) prefilter skips the hash unless the pack or a glyph patch has that shape (dumping disables the filter).
- **Sleep timers.** `ProcessSleepTimers` runs ~1000x a second from the scheduler's idle loop and took the mutex, scanned and heap-allocated each time (9.4 % inclusive). An atomic earliest-deadline now skips all of that when nothing is due.
- **Translated-function registry never published for NSMBW.** Only MKW's `main.cpp` called `TranslatedFunctionRegistry::Finalize()`, so every indirect call's lookup took the registry mutex, and the raw-dispatch fast path was off. Calling it from NSMBW's `main` failed validation: *"Generated indirect dispatch profile 'nsmbw' omits translated winner 0x8012e630"* - `PPCMfhid2_HLE_8012e630`, an MKW-address HLE registered by the shared runtime into every product (the known bug class). Omitted winners now go to the dynamic raw-dispatch list with a one-line warning instead of failing; NSMBW logs "omits 1". MKW's table is complete, so it is unaffected.
- **Unbuffered stdio.** `main` set stdout/stderr unbuffered, so every log line cost several `WriteFile` calls (4.3 % in 1-1, and hitches at scene changes that log). Now 64 KB buffers, flushed every 250 ms by a background thread, on SIGABRT and in the access-violation handler; `NSMBW_UNBUFFERED_LOG=1` restores the old mode.

Results: 1-1 game thread 62 % -> 50 % busy (real work excluding the timer-arm syscall ~58 % -> 44 %); file select 47.5 -> 54.6 FPS; the world map was already at 60. Whole process during play: 0.64 cores on average, 927 MB working set (1.2 GB private). Correctness: the ? blocks, bricks and coins animate at 4x and at 1x (screenshots; `NSMBW_RES_SCALE=1`).

What broke / what I didn't expect:
- I briefly thought the fast path had broken the ? blocks (a 4x capture showed them blank). A per-copy log showed all tile copies arrive at 4x, and more captures showed the "?" at every rotation angle. The blank frame was the spinning "?" edge-on, not a bug.
- Every audio block runs the SDK's AX library as translated code: `Audio_HLE_Tick` is now ~36 % of the game thread's busy samples. The float-heavy part (func_801A39C0 and neighbours, 0x801A0000-0x801A4000) is most likely AXFX reverb/aux processing, which runs on the CPU on real hardware too.

What I learned:
- Profile "busy" includes waiting-shaped work (arming a wait timer, the idle loop's per-millisecond service pass); compare real work, not just busy.
- A data structure that "changes every frame" may only change in known places: hash around those places and the per-frame cost disappears.

What's next:
- Native (SIMD) replacements for the hot AX routines, after identifying them precisely (AXFX reverb is the prime suspect).
- Memory: 927 MB working set, mostly GPU-side textures (4096^2 atlas at 4x, 2048^2 pack textures) and driver mappings - measure and trim.
- GPU side is unmeasured: at 4x with 120 FPS interpolation a weak GPU is the likelier bottleneck in heavy levels; add GPU timing.
- The build targets x86-64-v3 (AVX2), so CPUs without AVX2 cannot run it at all. "Runs on anything" needs a v2 build or runtime dispatch - a decision for the developer.

## 2026-09-27 (night) — Phase 8 (frame interpolation: 120 locked, 180 improved)
Developer report: 180 FPS interpolation only reached ~130 (~100 in menus) on an RTX 4060 with a 180 Hz monitor, and the saved 180 only took effect after re-selecting it in F10.

What I did:
- **Saved F10 settings were never applied at startup.** `settings_overlay::InitializeRuntimeSettings()` (interpolation target, volume/mute, display mode, skip-unready-pipelines) is called by MKW's `main.cpp` only. A `NSMBW_LOG_INTERP` run logged `target=0` with Config.toml at 180. NSMBW now calls it after the window's first frames. This is the fourth "NSMBW init skips what main.cpp does" item.
- **New diagnostics** (all env-gated): `NSMBW_LOG_INTERP=1` prints, every 2 s, presented and real game FPS, retraces per game frame (0/1/2/3+), interpolation counters (late / low-match / replay-unsafe), production vs end_frame time for kept-up and late frames, and the producer's FIFO-drain wait for the render worker. `AURORA_LOG_WORKER_TIMING=1` prints the worker cycle's phases (renderer lock, seal and its parts, prepare, overlapped encode), interpolation matching time, and the presenter's per-present surface lock / acquire / submit / schedule wait / Present.
- **What the measurements showed**, in order:
  - Interpolation made the *game* drop to ~51 real frames/s in 1-1 (60 with it off). Each late frame also loses its interpolated slots.
  - `end_frame` never blocked. The game waited in `fifo::drain` for the worker to prepare the next frame: 500-900 ms per 2 s, up to 75 ms at once.
  - The worker's seal phase spiked to 20-380 ms. Interpolation matching was < 0.7 ms, so matching was not the cause.
  - GPU load was 40-60 % at full clocks, and halving the resolution (2x) still stalled, so not GPU horsepower.
  - The presenter's surface acquire (`GetCurrentTexture`) averaged 6.6-8.6 ms per present, with peaks of 59 ms, against a 5.5 ms budget at 180 Hz. It blocks until a swapchain buffer is free, inside Dawn's device lock, so every Dawn call on the worker waited behind it.
- **Fixes:**
  - The presenter now waits for the previous present's GPU work (`Queue::OnSubmittedWorkDone` + `Instance::WaitAny`, 50 ms cap) *before* acquiring, outside Dawn's lock.
  - Tile copies the scaled tile cache owns no longer read back to RAM (a GPU->CPU sync per copy per frame that nothing read; `note_tile_copy` now reports ownership).
  - The static texture source caches sweep amortized: a fixed 512-entry threshold re-swept the whole map on every insert when nothing was freeable (17.7 % of the game thread on the world map).
- Tested and reverted: 4 staging-buffer slots instead of 3 (no improvement).

Results (self-test run, 4x, HD pack): **120 FPS mode holds 120** in 1-1, the world map and file select (dips only at scene loads). 180 mode: 1-1 135-160 (was 120-147), world map 119-155 (was 81-87), real game rate 47-53 (was 37-39).

What I learned:
- Lock coupling across threads can make an idle CPU and a half-busy GPU look like "too slow". The per-phase timers found it where the sampling profiler could not, because waits look like idle time there.

What's next:
- 180 still misses frames. The remaining worker stalls are EFB-probe downloads (`ensure_native_texture` creating resources per frame) and `gfx::end_frame`, both still contending for Dawn's device lock with the presenter. Fixing that means presenting from a path that does not share the lock, or caching those downscale textures.
- The Vulkan A/B did not take effect (the run logged present mode Immediate), so it is untested.

## 2026-09-27 (night, later) — Phase 7 (World 2 map ground fixed)
What I did:
- Reproduced the black World 2 sand with the portable test copy (W2 save). Checked the output side first, as the 2026-09-26 entry advised: a new switch, `NSMBW_DEBUG_TEXWHITE=<w>x<h>:<fmt>[,...]`, makes aurora sample solid white for textures of those shapes.
  - Light textures white: the ground stayed black, so they were not the cause.
  - The 256x256 CMPR "ground" texture white: only the fortress changed. The previous day's traced draw was the fortress, not the ground.
  - A group of large textures white: the dunes appeared. Bisected to two CI8 (palette) textures.
- `NSMBW_LOG_TLUT` showed `GXLoadTlut` calls but no `GXInitTlutObj`, and `GXInitTlutObj`/`GXLoadTlut` were bound only at MKW's addresses. Bound them at NSMBW's (0x801C7660 / 0x801C7690, identified from the translated bodies), and made the load HLE decode guest-built TLUT objects. Details in issues.md.
- Result: golden sand dunes (screenshot); title, W1 map and 1-1 unchanged.

What broke / what I didn't expect:
- The first decode used a 21-bit address mask and gave grey dunes: these palettes are in MEM2, and the SDK stores 24 bits. Grey = the right geometry sampling the wrong memory.

What I learned:
- Forcing an input to a known value (white) answers "does this input matter?" in one run. It is the output-side test the previous day's advice asked for, and it found in 20 minutes what input-by-input verification missed in an hour.
- *CI / palette texture*: pixels store indices into a separate colour table (TLUT) loaded into texture memory; if the table never arrives, every index maps to nothing.

## 2026-09-27 (night, latest) — Phase 11 (first run on another PC; release package)
What I did:
- A friend ran the build on their own PC: `NSMBWCompiled.exe` plus the 8 DLLs it imports that aren't part of Windows (listed with `llvm-objdump -p`), and their own PAL disc extracted with Dolphin. Getting it to boot took five launches, each stopping on a different problem:
  1. "No DVD root is configured": `dvd_root` was still commented out in Config.toml.
  2. The same error again: the path was written as `"C:\Users\..."`. In TOML's double-quoted strings a backslash starts an escape, so the whole file failed to parse, and the runtime fell back to defaults without surfacing that to the player. Fixed with single quotes.
  3. "Unable to initialize managed NAND": no `wii_bootstrap` folder next to the exe.
  4. "Configured NAND root is not an existing directory: D:\WiiNand": the template's example `nand_root` line had been uncommented along with `dvd_root`.
  5. Crash after `AIInit`: missing `dsp_coef.bin`.
  After that it ran.
- Problems 3 and 5 were real gaps in the build (issues.md, same date). Both files are looked up next to the exe, then in the source tree, and only MKW's CMake setup copied them next to the exe. `NsmbwProduct.cmake` now copies them too.
- Added `projects/nsmbw/tools/package_nsmbw.ps1`. It builds `dist\NSMBWCompiled-<commit>[-dirty].zip` (70 MB, 23 files) from an explicit required-file list and fails loudly if a file is missing. It includes `projects/nsmbw/tools/PLAYING.txt` (requirements, Dolphin extraction, the Config.toml pitfalls above, controls).
- Verified the package from a folder outside the repo: 40 s through boot → title → opening movie, with no crash. Control: the same folder without `dsp_coef.bin` gave the friend's exact exception after 7 s.

What broke / what I didn't expect:
- Every run on the development machine starts inside the checkout, and the runtime's source-tree fallback silently supplied the missing files there. A clean build folder had never been tested, and couldn't be from inside the repo.

What I learned:
- A package has to be tested from outside the source tree. Otherwise the same fallback that makes development convenient hides whatever the package is missing.
- *Import table*: the list of DLLs an exe needs Windows to load before `main` runs. `llvm-objdump -p <exe>` prints it, and any entry Windows doesn't ship has to travel with the exe.

What's next:
- Fix the four setup problems now listed under "Open / unconfirmed items" in issues.md: MKW wording in the DVD error and config template, TOML parse errors shown as "no DVD root", the `nand_root` example trap, and the window/video reset on every launch. They're documented in `PLAYING.txt` for now.
- Before any public release: the license check in Phase 11. The exe contains translated game code, so how a release is distributed is still an open decision.

## 2026-09-28 — Phase 8 (180 FPS interpolation at 4x)
All measurements from portable copies (own UserData; the developer's Config.toml and save are no longer edited for tests) at 4x, 180 FPS target, RTX 4060, 180 Hz monitor.

What I did (each step measured):
1. **Proved the cause.** `AURORA_DEBUG_SKIP_SLOT_PRESENT=1` drops interpolated slots before any presenter Dawn call. With it the game held 60.0-60.5 real frames/s with 0 late frames, worst seal 1.4 ms, worst FIFO-drain wait 2 ms. Without it: 44-46 frames/s, 16-30 late per 2 s. So the presenter's Dawn calls alone caused the stalls. aurora requests Dawn's `ImplicitDeviceSynchronization` (one mutex for every Dawn call), so any presenter call that blocks inside Dawn stalls the worker and then the game.
2. **Found which call.** Timing `GetCurrentTexture` separately showed it at 0.02 ms once preceded by a wait. The yesterday's `Instance::WaitAny` wait itself (4 ms average, 14 ms max) also blocks inside the device lock.
3. **Poll instead of blocking.** The presenter registers `OnSubmittedWorkDone` in `AllowProcessEvents` mode, then polls `ProcessEvents()` (holds the lock for microseconds) and sleeps 200 us between polls. Result: game 60.5 frames/s, 0 late, drain wait under 1.6 ms. Presented only 120-147, because it waited for the previous present's GPU work.
4. **Wait for present N-2, not N-1** (a ring of two completion flags): 127-137. At 2x this already locked 180.0.
5. **Replay slots skip resolve-only passes.** A render-to-texture / EFB-copy source pass whose next pass clears colour and depth is not re-drawn on interpolated slots; its texture came from the native render and its resolve is not re-run. Result: 145-159 at 4x. GPU load 63-68 %, so not GPU-bound.
6. **Mailbox present mode on D3D12.** Three presents in flight made `GetCurrentTexture` block in-lock again (3 ms average), so D3D12's Immediate swapchain has only 2 buffers. `AURORA_PRESENT_MODE=mailbox` (new override) gave 177-180. Mailbox is now the default on D3D12 as on Vulkan: it does not tear, at a cost of up to one refresh of latency.

Results on a copy of the developer's save, 4x / 180, no overrides: 1-1 holds 180.0 in steady play (dips at level start and death); World 1 map mostly 162-180 with 1-7 late frames per 2 s (was 15-30); game 60 real frames/s.

What I learned:
- With a single device-wide lock, "wait for the GPU" must never happen inside the API. Poll with a non-blocking check and sleep outside it.
- A double-buffered swapchain puts a hard floor on presentation latency: each present waits for the one two back. That is invisible at 60 Hz and decisive at 180.

What's next:
- The residual map lates (1-7 per 2 s) come from the game side (EFB-probe downloads in the seal).
- Watch for Mailbox side effects on other setups (window capture, VRR displays); `AURORA_PRESENT_MODE=immediate` restores the old mode.

## 2026-09-28 — Phase 7/12 (Wii Remote tilt; save and character investigations)
What I did:
- **Tilt.** Added "Tilt remote left/right" to the in-course bindings (F10 → Controller settings → NSMBW controls; defaults A / D and right stick left / right, sticks proportional). Tracing how the game reads tilt:
  - The tilt actors call `getAccVerticalAngleX` (0x800B5CA0), which returns an angle the controller update (`func_800B61F0`) writes each frame.
  - With no extension that angle is `0x4000 × KPADStatus.acc_vertical.y`, a field KPAD derives from the accelerometer (0x801EB170).
  - Our KPAD override replaced the whole read and left the field zero, which is bug class 3 again. It now writes `acc_vertical` as a tilted real remote would.
  - Log evidence in 1-1 (portable copy, `NSMBW_LOG_TILT`): D → game angle −0x4000, A → +0x4000, back to 0 on release.
- **Where tilt is used:** mapped the game's sprite table (0x8030A340, 484 × 0x28 bytes) from sprite ID to actor profile and scanned every `Stage/*.arc` for the tilt actors. They appear in 01-05 and 01-20 (World 1), 03-20/21 (boss seesaw), 05-22, 06-03, 06-24, 07-03, 07-22, 07-24, 08-22 and 09-05.
- **Investigation: permanent save before beating the game.** The world-map menu decides in exactly two places, both reading the permanent save slot's `mGameCompletion & FINAL_BOSS_BEATEN` (bit 0x02):
  - 0x8077AA10 picks the button text: message 7 "Quick Save" unless beaten, then 4 "Save".
  - 0x8092FC30 is the menu's item-selected handler. Unbeaten, it enters `dCourseSelectManager_c::StateID_InterruptSave...` (Nintendo's name for quick save: suspend and quit to title); beaten, it enters `StateID_Save...`.
  - Other readers of that bit are the file-select stars (0x80796080 / 0x80796250 / 0x8077D9A0) and the routine that sets it after W8 (0x801028D0). None of them gates saving.
- **Investigation: player 1 character.** Each player's character is `daPyMng_c::mPlayerType[4]` (0x80355160, u32 per player: 0 Mario, 1 Luigi, 2 Yellow Toad, 3 Blue Toad - *corrected 2026-11-15: 2 is Blue Toad and 3 Yellow Toad; NSMBW-Decomp's enum names swap them*); the Add/Drop Players screen is the `dCharacterChangeSelect*` classes in d_basesNP. Experiment: a new env switch `NSMBW_DEBUG_P1_CHARACTER=<0..3>` writes `mPlayerType[0]` while on the world map only. With `=1`, 1-1 loaded with Luigi as player 1: model, HUD head icon and his own life count (screenshot `p1luigi_lvl4_0.png`).

What broke / what I didn't expect:
- I first read `func_800B61F0`'s branch backwards and fed acc.z. That path is the extension (nunchuk) one; the log showed acc.z moving and the angle not.
- Screenshots: `shot.ps1` copies the screen, so a run while another window was in front captured that window. `burst.ps1` (`PrintWindow`) captures the game window itself.
- Scripted key holds only land in a narrow window: the self-test finishes the level about 10 s after it loads, and on the map tilt is 0 by design. Two of four runs pressed at the wrong time.
- (Later the same day.) Tilt direction was reversed on a real platform (developer: A tilted right, D left). The sign had been derived from Wii Remote documentation for the raw Y axis, the one step not taken from this game's code. Flipped in `nsmbw_kpad_overrides.cpp`; the log now shows D -> +0x4000, A -> -0x4000, and the on-screen check is back with the developer. Lesson: a sign derived partly from outside documentation is a guess until tested against the actual thing it moves.

What I learned:
- "The game reads the right field" and "our override fills the right field" are separate checks. The log that prints both input and the game's derived value found the gap in one run.
- *Sprite vs profile*: a level stores sprite numbers; a table in the game turns each into an actor type (profile). Mapping the table lets you search all levels for an actor without playing them.

What's next:
- Confirm tilt direction on screen in a level with a tilt platform (01-05 or 01-20), and that a shake still registers while tilted.
- Character: check the parts that may assume player 1 = Mario: the course-intro card (showed x05 while the level HUD showed Luigi x04; too dark to see whose head), the world map model, cutscenes and the ending, and whether file load or the Add/Drop screen resets `mPlayerType[0]`. Then decide on UI (reuse the Add/Drop screen vs an F10 option).

## 2026-09-28 (later) — Phase 12 (full save from the world map before the game is beaten)
What I did:
- Replaced the two functions that choose Quick Save with native overrides (`projects/nsmbw/native/nsmbw_wm_full_save.cpp`):
  - 0x8077AA10 sets the button text.
  - 0x8092FC30 runs the selected item.
- Each is a structural copy of its translated body (the approach `nsmbw_exi_imm_wait.cpp` already used), with one line changed: the `mGameCompletion & FINAL_BOSS_BEATEN` test reads as "beaten". So the menu takes the branch the game itself takes after the final boss, and every other store the functions make is kept.
- The saved flag is untouched. It still means "final boss beaten" for the file-select stars and anything else that reads it.
- These are the first overrides at REL addresses. Regenerating the shard manifest dropped the unique-function count from 41,043 to 41,041, i.e. the REL translation excluded exactly these two.
- Evidence (portable copy of a World 1 save; `NSMBW_LOG_STATE_CHANGES=all`):

  | | before (`dist\` build from `70304b2`) | after |
  |---|---|---|
  | Button | "Quick Save" | "Save" |
  | States | `InterruptSaveButtonSelect → InterruptNowSave → InterruptSaveEndAnimeWait → TitleSceneChangeWait` (saves, quits to title) | `SaveWindowOpen → SaveButtonSelect → NowSave → SaveEndAnimeWait → KeyWait` (saves, stays on the map) |

  `wiimj2d.sav` was rewritten during the save, and relaunching that copy resumed on the World 1 map.

What broke / what I didn't expect:
- In chat I first described 0x8092FC30's branch the wrong way round ("bit clear -> +608"). Re-reading it: bit clear jumps to +992. Forcing "beaten" instead of picking a state ID by offset made the question irrelevant, and the state log confirmed the result.
- The scripted run: after the save returned to the map, the self-test resumed presses once the scene changed and entered 1-1, and the capture then showed a 2560x1440 level. That was the test harness, not the save. Only the scratchpad copy was running (checked with `Get-Process`).

What I learned:
- To change one decision inside game code, a copy of the translated body with a single marked edit is safer than a hand-written reimplementation: it keeps every store (bug class 3) and is easy to review, because the diff against `build/.../func_<addr>.cpp` is one line.
- *Quick save vs save* in NSMBW: a quick save ("interrupt save") writes the slot and quits to the title; a full save writes it and stays on the map.

What's next:
- Not checked: whether anything else behaves differently on an unbeaten file after a full save (e.g. whether a previous quick save left in the slot interacts with it). 0x8092F940 also enters the quick-save chain from a place not yet identified; it is not overridden.

## 2026-09-28 (night) — Phase 12 (Exit from uncleared courses) and a REL memory overlap
What I did:
- **Exit from any course.** The pause menu's Exit is gated in three places, all by `dGameCom::isNowCourseClear` (0x800B4E30), which has no other caller in the DOL or any REL:
  - `Pausewindow_c::create` greys the button (`P_shadowBlack`).
  - The cursor move (0x800D0CF0) refuses button 1.
  - The confirm (0x800D0DA0) rejects the press.

  The body is a pure query (reads only), so `nsmbw_pause_exit.cpp` overrides it to return true. Enemy courses (mLevel1 32..34) stay locked by their own check. The developer confirmed Exit works from an uncleared 1-1; a portable run showed the Exit button no longer greyed.
- **Blank Exit confirmation, traced.** The developer saw no text in the "return to map?" window. Steps:
  1. `NSMBW_LOG_MSG=1` (new, `nsmbw_msg_lookup_diag.cpp`, a logged copy of `EGG::MsgRes::getMsg`) showed the main message lookup failing: group 0, index `0x46520000`, no entry. The Yes/No button labels resolved fine.
  2. The window's type is correct (`mType` = 12, RETURN_TO_MAP, set by 0x800D1010). But the MainMsgIDs table it indexes (`d_profileNP`, guest 0x8076A5F8) reads `... 61 62 | 0x46520000 0x004FCFA0 0 0 | 67 ...` in memory against `... 61 62 | 63 64 65 66 | 67 ...` in the REL file.
  3. `NSMBW_WATCH_WRITE=8076A628` (new, a per-tick poll in the tick pump) found the change at tick 4 in the boot scene, right after NW4R sound and AX mixer calls. So it isn't caused by the Exit change: the plain Exit from a cleared course reads the same entry.
- New test switch `NSMBW_AUTO_PRESS_STOP_IN_LEVEL=1`: the self-test stops pressing once in a level, so a script can use the pause menu. Its in-level walk had killed Mario and returned to the map before the pause.

What broke / what I didn't expect:
- Two scripted runs didn't test anything: the developer closed one window (the old build, correctly showing Exit locked), and in another the walk killed Mario first. Both are test-harness issues, not game ones.
- I first labelled getMsg's arguments wrong (it is `getMsg(group, index)` in r4/r5, not one packed id in r4).

What I learned:
- A value that is neither a valid index nor in the file is a strong hint of memory corruption rather than a logic error. Comparing the table in memory with the table in the REL file located it in one step.
- *Arena / heap*: the arena is the free RAM the game carves its heaps out of at boot, and every later allocation comes from those heaps. Data placed in the arena without a heap allocation is invisible to the allocator, which will hand the same bytes to someone else.

What's next:
- Fix the REL overlap properly: find which heap block owns 0x8076A628 at tick 4, then either reserve the four REL ranges from the arena or restore the images at the point the game would load them. Until then any REL data may be silently overwritten. Tracked under "Open / unconfirmed items" in issues.md.

## 2026-09-28 (late night) — Phase 6/12 (REL memory overlap fixed)
What I did:
- Measured where the game's dylink heap lives (`NSMBW_LOG_REL_HEAP=1`, new): [0x80767660, 0x80C675D8), with all four pre-placed REL images inside it. The garbage in `d_profileNP` turned out to be an ExpHeap free-block header (`'FR'`, size = the rest of the heap).
- First fix: allocate each REL's block when the game would have loaded it (at `load_async`/`link`). `d_profileNP` landed exactly on its image, but the header still appeared, in the same tick. A dump of the heap's block lists showed the heap itself was consistent: the header was a **leftover** from a temporary ~8 KB allocation that the game made and freed before the link. The other three RELs didn't land on their addresses either.
- Final fix: reserve all four ranges inside `createDylinkHeap` (0x8016EC60), while the heap is empty, using padding blocks to place each one exactly (`nsmbw_dylink_heap_reserve.cpp`). Result: 4 of 4 reserved at their addresses; the table word stays intact; no failed text lookups.

What broke / what I didn't expect:
- Scripted menu navigation kept failing. Fixed-delay presses landed mid-animation, and arrow keys sent with `keybd_event` without the extended-key flag arrive as numpad keys, which nothing is bound to. That also explains the earlier "down, down" presses that never moved a cursor. Even with the extended flag the Down press did not register, so the on-screen check is with the developer.

What I learned:
- Freeing a heap block does not erase its header bytes. A stale header in memory the heap no longer uses is harmless on a console, because the next owner overwrites it. It only did damage here because the REL's contents had been placed there before the heap's transient use, which is the opposite of the console's order.
- A heap dump before and after an allocation separates "the heap's bookkeeping is wrong" from "someone wrote stale bytes"; the two look identical from the corrupted memory alone.

What's next:
- Developer: confirm the Exit confirmation now shows its text.
- First release.

## 2026-10-11 — Release v0.1.0 (no-setup first launch)
What I did:
- Separate release build: `build_release\`, configured with `-DNSMBW_RELEASE=ON`. Only that build compiles the first-launch disc setup (`runtime/src/product/nsmbw_disc_setup.cpp`); `build_nsmbw` is unchanged.
- First launch with no valid `dvd_root`: a dialog asks for the disc image (.rvz/.iso/.wbfs) or an already-extracted folder. An image is extracted by the bundled `DolphinTool.exe` (from the official Dolphin 2609a x64 release, GPLv2+; licenses in `licenses\`) into `disc\` next to the exe. The disc is checked for SMNP01 rev 1, and the path is saved to Config.toml.
- The player-visible "Mario Kart Wii" text (Config template, DVD error) now says NSMBW in this game's builds. `PLAYING.txt` setup is down to "extract, run, pick your disc". The package is `dist\NSMBWCompiled-v0.1.0-<commit>.zip`.

What broke / what I didn't expect:
- Extraction took about 2 s, not minutes: the game's data partition is only 342 MB.
- The release configure failed until it was given ninja's and clang's full paths; neither is on the default PATH.

What I learned:
- A *GPL-compatible* tool (Dolphin is GPLv2-or-later, this repo GPLv3) can be shipped next to our exe as a separate program, so its disc-format and decryption code does not have to be ported. Shipping it only requires including its license.

What's next:
- Developer: click through the real dialogs once. The automated test used the `NSMBW_SETUP_IMAGE` hook, which skips them.


## 2026-10-11 (later): v0.1.0 follow-up (no console, F10 hint)
What I did:
- The developer confirmed the release works end to end, including the real dialogs.
- Release builds now link with `-mwindows`, the *GUI subsystem*: Windows doesn't open a console for the exe. `llvm-objdump -p` reports `Subsystem 2 (Windows GUI)`. stdout/stderr go to `nsmbw_data\NSMBWCompiled.log` instead, rewritten on each launch; `PLAYING.txt` now asks for that file in bug reports.
- The first-launch dialog and a new "disc is set up" box tell the player that F10 opens the settings menu.

Verified: the packaged zip, run outside the repo with `NSMBW_SETUP_IMAGE`, had no conhost child process, its window was titled NSMBWCompiled, and the log (183 KB) shows the scene sequence BOOT -> 6 -> 7 -> 5 -> 10 -> 8.


## 2026-10-12 — v0.1.0 follow-ups (multiplayer input, HD pack setup, glyphs)
What I did:
- **Multiplayer input** (`7dc53c3`): the input HLE only ever presented Wii Remote channel 0, so a second pad moved Mario instead of joining. Player N is now the Nth connected gamepad (keyboard = player 1); `KPADRead` keeps per-channel state and the WPAD control blocks mark channels below the player count connected each frame. Details in `docs/issues.md`.
- **HD pack in first-launch setup** (`07e3863`): after the disc step, the release asks for an optional Dolphin texture pack folder, finds `SMN` in it, copies it to `nsmbw_data\texture_replacements\SMN` and sets `texture_replacements = true`. Test hook `NSMBW_SETUP_TEXTURES=<dir>`.
- **Glyphs** (`812ad7d`): symbol keycaps for Backspace/Enter/Shift/Space/Tab, and smoother glyph downscaling when no pack is installed (bilinear base upscale + n×n averaging; previously one sample per pixel, *point sampling*, which drops thin lines).

What broke / what I didn't expect:
- `mPad`'s 0x8042A758, which the old code set to 1 as if it were a player count, is a polling interval in `beginPad` (0x8016F360).

What I learned:
- Faking per-channel SDK state for channel 0 only is invisible in single player and only shows up when a second device appears.

Verified: single player still boots to a level (`connected remotes: 0 -> 1`); the packaged zip installed a full pack (312/312 files, "Indexed 306 texture replacements"). Not verified: two controllers, glyph appearance on screen.

What's next:
- Test with two controllers; look at the keyboard glyphs without a pack.


## 2026-10-16 — Texture and shader loading: cost measured, cache plan
What I did:
- Read how shaders and textures reach the GPU, then measured them with a cold-vs-warm A/B: two 100 s self-test runs of the same exe and save (portable copy, HD pack on, D3D12 on an RTX 4060, `NSMBW_PROFILE_SAMPLER=1` + `NSMBW_LOG_INTERP=1`). The only difference was whether `nsmbw_data/Cache/` was empty.
- **Shaders already have a full cache in aurora**, in three layers:
  1. in memory, a map from pipeline-config hash to compiled pipeline;
  2. `pipeline_cache.db`, a list of every pipeline config ever used (a *recipe*, not compiled code), which is recompiled on 22 background threads at boot ("prewarm");
  3. `dawn_cache.db`, Dawn's compiled-shader blobs. With this, the prewarm is a lookup rather than a compile.
  When the cache is warm it works: "Pipeline prewarm finished: 233 pipelines in 0.0 s (Dawn blob cache: 923/923 hits)".
- **What a cold shader cache costs.** The cold run compiled the same 233 pipelines on first use. Its worst frames in the first 40 s were 186, 174, 154, 452, 509 and 591 ms, and two 2-second windows fell to 28.5 and 41.5 game FPS. The warm run's worst frame in the whole run was 30 ms, and it never dropped below 57 FPS. Compilation itself runs off the game thread (about 8 compile threads were busy in the cold boot window and none in the warm one). The game thread still stalls, though, presumably where a draw has to wait for its pipeline (the persistent-pass wait in `pipeline_cache.cpp`). That last part is not proven.
- **The cache is thrown away on almost every launch (bug, not fixed yet).** `ResetPersistentStateForCleanRun` (`nsmbw_product.cpp`) wipes both cache DBs whenever `.last_run_unclean` survives, and only an `atexit` hook removes that marker. Closing the window goes through `ExitForAuroraWindowClose` (`runtime/include/aurora_events.h`), which calls `ExitProcess(0)` on purpose and so skips every `atexit` handler. Reproduced: CloseMainWindow → the process exited → the marker was still there (timestamped at that run's start) → the next launch logged "Reset: previous run did not exit cleanly - wiped 6 pipeline/shader cache file(s)". So a player gets the cold-cache hitches on every launch. Test runs do too, because `run_nsmbw.ps1` force-kills the game: all 40 of the most recent `build_nsmbw/*.err.log` start with "Dawn cache schema changed; recreating", which is what a fresh DB logs.
- **Textures already have a thorough in-memory cache** (`gx.cpp`). It has several tiers: per texture object, per source address+shape (validated by guest write generations and an XXH3 digest), palette, and EFB copies. It has no disk tier, but the profile says one wouldn't help:
  - Decoding GX formats (`convert_texture`) is 4-8 samples per 10 s window, about 0.2 % of the game thread.
  - The cost on a miss is GPU resource creation: `new_dynamic_texture_2d` (D3D12 CreateTexture/CreateView) took 61-79 samples (about 140-180 ms of game-thread time per 10 s) in scene-load windows.
  - HD-pack textures are read from disk synchronously on the game thread. `read_binary_file` took 123 samples (about 290 ms) in the cold-OS-file-cache boot window and 37 samples in the warm one. Parsing the DDS is negligible (8 samples).
  - None of these showed up as a hitch above 30 ms in the warm run.

What broke / what I didn't expect:
- `Test-Path` in PowerShell reported the marker missing while `Get-ChildItem -Force` listed it. The relaunch log settled it.
- With async compilation and "skip unready pipelines" on, I expected a cold cache to show up as pop-in, not as 0.5 s stalls. The stalls are there.

What I learned:
- An exit path that skips `atexit` also skips anything else registered there. Here that includes aurora's shutdown, so the pipeline-cache writer's queued rows and the final `serialize_pipeline_caches()` are skipped on window close too.
- *Pipeline* (concept): on a modern GPU API, every combination of shader + blend + depth + vertex-layout state is compiled into one immutable object before it can draw. GX lets the game change any of those per draw, so the port meets new combinations as the game runs and compiles them the first time. That first compile is what a pipeline cache avoids.
- Earlier performance numbers in this log (2026-09-27/28) came from cold-cache runs, so their first ~40 s included compile stalls. The steady-state numbers are unaffected.

What's next (cache plan, in order):
1. **Window close counts as a clean exit.** Before `ExitProcess`, remove the marker (a hook the product registers) and drain aurora's pipeline-cache write queue, plus Dawn's pipeline-cache serialize. Verify: close the window, relaunch, see "Pipeline prewarm finished … hits" and no "wiped".
2. **Narrow the crash marker to what it protects against.** The 2026-09-19 failure was an abort *inside the boot prewarm*. Arm the marker before the cache load and clear it when the prewarm queue drains, so a later kill or crash no longer costs the cache. Verify: kill mid-level, relaunch warm.
3. **Ship an NSMBW seed `initial_pipeline_cache.db`.** It holds recipes only (about 0.9 MB after this run), so it is GPU- and driver-independent, and aurora already merges a bundled seed at boot. A first launch on a new PC then compiles every known pipeline at boot instead of mid-level. Needs: a collection run through all worlds, a regeneration step whenever `GXPipelineConfigVersion` changes, and the boot compile time measured on a cold Dawn cache.
4. **Textures: no disk cache.** Instead: (a) prefetch the HD pack's file bytes on a background thread at boot (as Dolphin's "prefetch custom textures" does; 174 MB for the full pack), with the current synchronous read as the fallback; (b) count `new_dynamic_texture_2d` calls per scene load, and pool GPU textures by (w, h, format, mips) only if the count justifies it.


## 2026-10-16 (later) — Window-close cache fix; texture memory leak found and fixed
What I did:
- **Plan item 1 done**: closing the window no longer throws away the shader cache. Details in `docs/issues.md`. Verified: close → relaunch logged "Pipeline prewarm finished: 226 pipelines in 0.0 s (Dawn blob cache: 886/886 hits)" and no wipe.
- The developer reported an Iris Xe laptop at 1x with interpolation off: about 30 fps, falling toward 3 fps over time. A steady decline points to something that grows, so I sampled memory every 10 s over 6-minute self-test runs (map → level → map, repeated).
  - **Memory grew the whole time**: private 700 → 2,240 MB, GPU dedicated 290 → 838 MB in 52 MB steps. Two runs gave almost the same curve, so it is deterministic.
  - New diagnostic `AURORA_LOG_TEXTURE_STATS=1` (live GPU textures plus the size of every texture cache tier, every 2 s) pointed at one tier: the static source cache, 819,000 entries after 5 minutes. Its insert breakdown showed the same few addresses inserted every frame. Root cause and fix in `docs/issues.md` ("Texture source cache grew without bound"). After the fix, memory stays flat at 560 MB private / 218 MB GPU for the whole 6 minutes.
- Checked audio as the other runaway candidate, with a new diagnostic `NSMBW_LOG_AUDIO_BACKLOG=1`. The AI accumulator (`audio.cpp`) has no ceiling, so a host too slow to mix in real time would build an endless backlog. On this machine the backlog stayed at about 0.002 s and the block loop cost 30-37 ms per second. Limiting the game to 2 logical cores didn't make it fall behind, so whether audio contributes on a weak CPU is untested.

What broke / what I didn't expect:
- The first long run was contaminated: the developer switched that window to 8x/180 FPS mid-run. That run is why the leak looked like it started there; the clean runs show it from the start.
- `burst.ps1` failed with a GDI+ error when saving into the scratchpad path; `portable_shot.ps1` into `build_nsmbw/shots` worked.

What I learned:
- A cache whose key includes a "this may have changed" counter has to retire the old key when the counter moves. Otherwise every bump is a new entry, and the cache becomes a leak that only hurts machines with little memory.
- On an integrated GPU, "GPU memory" is system RAM, so a GPU-side leak competes with everything else and ends in paging. The same leak was invisible on an 8 GB discrete card.

What's next:
- Developer: test the fixed build on the Iris Xe laptop, ideally with `NSMBW_LOG_INTERP=1` and `AURORA_LOG_TEXTURE_STATS=1`. If it still starts at 30 fps, profile there (`NSMBW_PROFILE_SAMPLER=1`); this machine can't reproduce a slow baseline.
- Cache plan items 2-4 from the entry above are unchanged.


## 2026-10-16 (night) — Laptop confirmed; seed pipeline cache; crash marker narrowed
What I did:
- **Laptop result** (developer's Iris Xe / i7-1355U log, build with the texture fix): a steady 59-60 fps at 1x for the whole session, with no decline. Texture counters stayed bounded (live ~400, static source cache ~230). 180 FPS interpolation held 165-180 presented fps, with occasional "Presenter fell behind" drops.
- What was still slow there, read from the log:
  - First launch: 17-26 fps for ~30 s and a 3.1 s freeze ("fifo drain wait ... longest 3108 ms") on the first world-map visit. That is first-use pipeline compiles with an empty cache.
  - About 10 s at 10 fps after the texture area jumped from 60 to 182 Mtexel. That looks like a render-scale change in F10, which was GPU-bound on the Iris Xe; it is not confirmed which setting was used.
- **Seed pipeline cache** (cache plan item 3): `projects/nsmbw/tools/merge_pipeline_cache.py` merged every local `pipeline_cache.db` into `projects/nsmbw/assets/pipeline/initial_pipeline_cache.db` (398 recipes, 1.6 MB). The build copies it next to the exe and the package script ships it.
- **Crash marker narrowed** (cache plan item 2): it is now also cleared once the boot prewarm queue drains, so only a crash during that window wipes the cache.
- Desktop measurements (new switches, both off by default):
  - The game thread's spare time is now printed in `NSMBW_LOG_INTERP` ("game thread idle (spare time)"). It was 74-86% at 1x on this desktop, i.e. about 4 ms of work per 16.7 ms frame.
  - `NSMBW_LOG_AUDIO_BACKLOG` now times every scheduler poll: ~900 polls/s and ~31 ms/s in total, about 3% of a core. The sampling profiler had attributed 5-7x more than that to `Audio_HLE_Poll` and the audio lock, so the sampler's numbers for small, frequently called functions are not reliable.
  - 3D utilisation at 1x on the RTX 4060 was 6-23%.

What broke / what I didn't expect:
- The game thread was never the laptop's bottleneck: the CPU side has 3x headroom even on a slower core. The remaining laptop limits are GPU-side, and only show above 1x.

What I learned:
- A pipeline recipe cache is portable between machines because it stores state, not compiled code. Only the compile has to happen on each machine, and a background prewarm at boot hides it: 398 pipelines took 2.1 s here, against 150-590 ms freezes spread through the first minute without it.
- Measure a function's cost directly (a timer around the call) before optimising what a sampling profiler blames. Here the two disagreed by 5-7x.

What's next:
- Grow the seed: merge caches from play sessions through later worlds (W2-W9, towers, castles, airships).
- GPU cost on integrated GPUs above 1x: per-pass GPU timing (Dawn timestamp queries) to find which pass dominates. Candidates are EFB copies, the scaled tile cache and the presenter.
- Texture plan item 4 (HD-pack prefetch, texture pooling) is low priority now: neither showed up as a hitch on the laptop.


## 2026-10-17 — Optimisation follow-ups (copy-target churn, palette cache, HD prefetch, idle loop, audio)
What I did:
- **EFB copy-target churn fixed** (`docs/issues.md`). In levels, ~6 GPU textures per frame were created and thrown away for the animated-tile copies, because evicting a copy also emptied its reuse pool. Creations fell from ~725 to 1-6 per 2 s, and memory stayed flat.
- **Palette cache**: applied the same retire-old-versions fix as the 2026-10-16 leak, preventively. NSMBW barely uses palette textures.
- **HD-pack prefetch**: a background-priority thread reads every pack file once at boot ("Prefetched 306 texture pack files (171.0 MB) into the OS file cache in 2.3 s"), so on-demand loads hit the Windows file cache instead of the disk. It holds no memory of its own. First run of it: NSMBW sets the pack on before aurora builds the index, so the prefetch never started until it was also triggered after index build. Not measured: the benefit with a cold file cache, which only exists after a reboot. With a warm cache, on-demand pack loads cost 0.4-37 ms per 2 s, only during scene loads (`AURORA_LOG_TEXTURE_STATS` now prints this).
- **Idle-loop power: tried and dropped.** Sleeping until the next timer/alarm/audio deadline instead of polling every 1 ms changed nothing: process CPU on the world map was 34.1% vs 36.4%/34.1% of a core, wakeups ~500/s vs ~520/s. The wakeups are real work (an audio block every 3 ms wakes the sound thread), not idle polling. Only the wakeup counter was kept, in the `NSMBW_LOG_INTERP` spare-time line.
- **Audio: measured, not changed.** The block loop splits into mix-worker join 0.4 ms, SDL push 1.5 ms and the guest callback 62 ms per 2 s. All the cost is the game's AX library running as translated code; parked in the to-do list.

What broke / what I didn't expect:
- My premise for the idle-loop change ("1000 wakeups/s for nothing") was wrong: they were ~500/s and almost all real.
- Enlarging the copy pool did nothing. The `sameKey=0` debug print showed the pool was being emptied, not exhausted.

What I learned:
- Measure the premise before building the fix: the wakeup counter would have saved writing the deadline code.
- "Content is stale" and "storage is reusable" are different invalidations. Tying the second to the first turned every cache flush into a GPU allocation.

What's next:
- The to-do list: confirm the seed on the laptop, GPU per-pass timing, grow the seed list.


## 2026-10-21 — Select World on the laptop, colored locked worlds (not a bug), map FPS at 4x/180
What I did:
- **Select World slow on the Iris Xe** (developer report: models take long to appear, FPS halves; not visible on the desktop). On the desktop, opening the menu compiled no pipelines (its recipes are already in the seed) and created 130 GPU textures in one 2 s window (the world models). Not yet confirmed on the laptop. Two suspects: the texture burst, and the HD pack, where 277 of 312 textures are 256 px or larger with no mipmaps. A *mipmap* is a chain of pre-shrunk copies the GPU samples when a texture is drawn small; without one, small-on-screen models read scattered texels from the full-size image, which is expensive on an integrated GPU.
- **"All worlds colored on a new save" was my test artifact.** Locked worlds get their dark look from fog. Three runs ran back to back in one PowerShell process, and the `NSMBW_DEBUG_NO_FOG=1` from the second run leaked into the "baseline" third run. Bisecting the copy-target fix (7c39bcf) showed dark silhouettes both with and without it. Test scripts now clear the variables they set.
- **World map ~55 fps vs 180 in levels at 4x/180** (developer, desktop). With the game window in front, my runs gave 173-180 on the map and in levels at 640x480, and 180 in a level at the developer's settings (borderless 2560x1417). With the window not in front, the map dropped to 110-140 fps and the game's GPU load rose from ~53-60% to 75-86%. That fits Windows compositing a window that cannot present directly. Not reproduced with the window in front; waiting for the developer's `NSMBW_LOG_INTERP` log.
- New diagnostics (all off by default):
  - `AURORA_GPU_TIMING=1`: GPU time per render pass from timestamp queries, summed every 2 s by pass size, native frame vs interpolated slot, and render-to-texture or not. It does not yet cover resolve/copy passes or the presenter blit.
  - `NSMBW_LOG_INTERP` now also prints pipelines compiled and queued, paced presents, anchor bumps and the presentation anchor's lead.
  - `NSMBW_AUTO_PRESS_MAP_MINUS_TICK=<n>`: the self-test presses - on the map, which opens Select World without keyboard focus.
  - `NSMBW_KEEP_WINDOW=1`: keeps the configured display mode in a self-test run.

What broke / what I didn't expect:
- Several measurements were wrong before they were right: Windows "GPU utilisation" at low load swung 11-46% between identical runs; keyboard input from a script reaches the game only if its window is in front (an Alt tap to force focus opened the map's free-look); and a covered game window cut the map from 180 to 40-140 fps.
- The GPU timer showed the map re-runs about 2.5 full-screen render-to-texture passes per interpolated slot (195 ms/s at 4x/180). Levels do similar work (130-156 ms/s), so it does not explain the map/level difference.

What I learned:
- Whether the game window is in front changes GPU cost and presentation substantially. Check focus before trusting any FPS measurement.
- Environment variables set with `[Environment]::SetEnvironmentVariable(..., 'Process')` persist for the rest of that PowerShell process, so back-to-back test runs inherit each other's switches.

What's next:
- The developer's map log at their settings; laptop Select World with and without the HD pack (a mipmap fix if the pack is the cause).


## 2026-10-25 — Select World at 4x/180: 55 -> 180 fps
What I did:
- The developer's log of "the map at 55 fps" was really the **Select World menu**: they had been pressing - in my test windows. With their settings (borderless, 2560x1440, 4x, 180) I reproduced it: 180 on the map, 53-58 in Select World, GPU ~100%, the game thread waiting ~1,500 ms per 2 s for the render worker.
- The per-pass GPU timer only found ~280 ms/s, so I extended it to the resolve, palette-conversion, presentation-snapshot and ImGui passes and counted the copies it cannot timestamp. That pointed at the **resolve-source snapshot**: 3,950 full-frame copies/s on the map (21 Gtexel/s) and 9,500/s in the menu (52 Gtexel/s), more memory bandwidth than the card has. Cause and fix are in `docs/issues.md`: sampled copies snapshot only their source rectangle plus a 2-texel border instead of the whole 2560x2112 frame. Select World now holds 180 fps; snapshot traffic fell 50x; the screenshot A/B shows no difference.
- Fixed a deadlock I had introduced in the GPU timer (issues.md). It made several test runs freeze, which first looked like the scripted - press failing.

What broke / what I didn't expect:
- Three false leads before the real one: a scheduling theory (the anchors were fine), "window focus" (real, but not the developer's problem), and GPU power states. The developer's correction that they were pressing - is what made the measurements line up.
- PowerShell 5.1's `2>` writes UTF-16, so grep found nothing in the developer's first log until it was converted.

What I learned:
- A *memory-bandwidth* bottleneck looks like "GPU 100% busy" while every timed shader pass is cheap: the work is plain copying. Counting bytes moved is the way to find it.
- Lock-order rule for callbacks: never call into a library while holding a lock that the library's callbacks also take.

What's next:
- Confirm on the Iris Xe laptop that Select World no longer halves the frame rate (its memory bandwidth is far lower, so the old copies would have hit it harder).
- The map still makes ~66 full-frame snapshots per submission for copies that are not shader-sampled (exact copies are already cropped, so these are likely large copies); worth a look if the map ever limits a slower GPU.


## 2026-10-26 — Stability audit: lock order around Dawn callbacks
What I did:
- After the GPU-timer deadlock (2026-10-25) I checked every mutex that a Dawn map callback also takes. Two more modules, EFB RAM readback and depth peek, called Dawn while holding such a mutex. Both now claim a slot under the lock and do the Dawn work outside it (details in `docs/issues.md`). The EFB readback one is on the normal play path, not just a diagnostic.

What broke / what I didn't expect:
- My first A/B loop silently ran the same exe four times: `Copy-Item` failed because the just-killed process still held the file. The rerun retried the copy until it succeeded and printed each exe's hash to prove the swap.
- The same binary measured 130-145 fps in some runs and 180 in others, with GPU time per second doubling for identical work. Single runs on this desktop are not enough to call a few-percent regression either way.

What I learned:
- The lock-order rule from the timer fix generalises into a checklist: for any lock a library callback takes, nothing that calls into the library may run while holding it. Claim under the lock, work outside, publish under the lock.

What's next:
- Remaining audit candidates are listed in chat (WebGPU validation errors are fatal; re-key hashing cost; HD-pack textures without mipmaps on the laptop).


## 2026-10-26 (later) — Audit follow-ups: one kept, one fixed, one rejected, one deferred
What I did:
- **Re-key hashing:** texture re-keys no longer hash the same bytes twice; ~42% less hashing on the map and in Select World (issues.md). A small CPU saving, kept because it was finished and measured.
- **GPU timer:** fixed resolves at offsets that are not multiples of 256 bytes (issues.md).
- **"Keep running after a GPU error" (tried and reverted):** the idea was to log WebGPU errors in release builds instead of exiting. Two findings ended it. (1) Release-mode builds (`NDEBUG`) run Dawn with `skip_validation`, so invalid commands are never reported there anyway; only out-of-memory and internal errors would reach the handler, and neither has appeared in any log. (2) A test that injected errors (`InjectError`) showed what continuing looks like when an error repeats: every frame's encoder was invalid and the screen flashed rapidly between two images, which the developer saw. That is worse than an exit, and a risk for photosensitive players. Errors stay fatal; nothing from this attempt was committed.
- **HD-pack mipmaps (deferred):** 277 of 312 pack textures have no mipmaps (smaller pre-shrunk copies used for distant or scaled-down textures). This was a suspect for the laptop's slowdown, which the texture-leak fix has since solved, so it waits until a laptop run shows a reason.

What broke / what I didn't expect:
- The error-injection test flashed on the developer's screen without warning. Rule from now on: no test that discards or corrupts frames runs in the foreground without asking first.
- A deliberately invalid command (a 3-byte buffer copy) produced no error at all, which is how the `skip_validation` toggle was found (`aurora-main/lib/webgpu/gpu.cpp`).

What I learned:
- `skip_validation` means release-mode builds rely on the driver for anything WebGPU would normally reject; a debug-mode build is the place to look for validation errors.
- After the big fixes, the remaining audit items were worth a percent of a core or less. Measure the payoff before starting the next "oversight".

What's next:
- Nothing pending from the audit. HD-pack mipmaps only if the laptop shows frame drops in levels or shimmering distant textures with the pack on.


## 2026-10-29 — Options menu in the game's own style; Options buttons in the pause and map menus; category volumes
What I did:
- **F10 opens an options menu drawn with the game's own art** (`runtime/src/product/nsmbw_options_menu.cpp`, `nsmbw_menu_draw.cpp`, `nsmbw_menu_art.cpp`). The pause window frame, the button frames, the yellow corner cursor and the fonts are read from the player's extracted disc (`Layout/pauseMenu/pauseMenu.arc`, `Layout/select_cursor`, `EU/<lang>/Font`). The colours, sizes and animation curves come from the same files' `.brlyt` / `.brlan`. A *Hermite key* is a (frame, value, slope) point; the curve between two keys is a cubic that passes through both with those slopes, which is how nw4r layouts animate. So the window scales in and out over 8 frames like the pause window, buttons grow on focus and squash on a press, and the cursor pulses on the game's 65-frame loop. Shift+F10 still opens the old ImGui bar.
  - Tabs: Graphics (display mode, resolution, frame interpolation, HD pack, bloom), Audio (Master, Music, Sound effects, Menu sounds), Controls (per-player controller picker, then rebinding), Advanced (show FPS, skip draws while compiling, copy filter, audio mixing thread).
  - The menu plays the game's own sounds (SE_SYS_CURSOR / DECIDE / BACK / INVALID) by calling `SndAudioMgr::startSystemSe` (0x801954C0) from the input poll on channel 0 (`nsmbw_kpad_overrides.cpp`).
  - Select / Back prompts use the menu bindings' glyphs. The yellow note line only appears when something matters, such as a restart being needed or no controller. The two-line descriptions were removed at the developer's request.
- **A real third button, "Options", in the in-course pause menu and the world-map menu.**
  - Layout side (`runtime/src/product/nsmbw_layout_patch.cpp`): at startup the pause and map archives are read from the disc, the Exit (or Save) button's pane subtree is cloned under new names, a group and renamed animation tracks are added, the window is grown, and the patched archive is written to `<AppData>/generated/`. A new DVD hook (`runtime/include/dvd_generated_files.h`) serves it in place of the disc file.
  - Game side (`projects/nsmbw/native/nsmbw_pause_options.cpp`, `nsmbw_map_options.cpp`): the functions that pick a button's animation or cursor pane by arithmetic on a two- or four-button layout are replaced, so they know the extra button. Most are structural copies with `EDIT` lines. The cursor steps in screen order.
  - Pressing Options plays the button's hit animation, then the menu's out animation (so it can't be seen behind the options menu), then the options menu's in animation. Closing reverses it through the game's own open state.
  - Pause: Continue / Options / Exit; the window grows 70 units and is recentred. Map: Star Coins / Add/Drop / Save / Options / Title Screen; the window sits between the screen top and the game's Back hint.
- **Music / Sound effects / Menu sounds sliders now work** (`projects/nsmbw/native/nsmbw_sound_volumes.cpp`; issues.md). There is no Voices slider: NSMBW plays voices on the effect players, so they can't be separated.
- **First launch opens the options menu before the title screen** (developer request). It opens half a second after the window appears. A marker file, `options_menu_shown`, next to `nsmbw_controls.ini` makes it happen once per install. Verified in three portable runs: the menu opened while the boot scene was still on its first state; the boot states (`dScBoot_c` #1-#11) only ran after the developer closed it with X, and the title followed. The game is held by the menu's normal stop, and nothing extra was needed to hold the scene change. Self-test runs never get it; `NSMBW_FIRST_LAUNCH_OPTIONS=1` forces it.
- **Controller picker per player** (`nsmbw_controls.cpp`): "Auto" or a specific pad, saved as `controller =` / `p2.controller =` in `nsmbw_controls.ini`.
- **Menu keys made consistent:** the menu context's keyboard defaults are now Z / Enter (confirm) and X / Backspace (back), so the first key, the one the prompts show, matches the X / Z the game's own screens show. Saved lists still at the old default order are migrated once.
- **6x / 8x with frame interpolation:** the inherited WiiCompiled rule that blocks them was removed, then restored the same day after the developer measured 8x at 180 fps running badly. It now carries that evidence in a comment.

What broke / what I didn't expect:
- The layout patch never applied on its first run. It was registered after `DVDInit_8015EA1C()`, which publishes the file table, so the hook was called too late.
- A patched archive larger than the disc file kept the disc file's offset, so absolute reads past its old end would land in the next file (issues.md, general).
- The first sound-volume attempt assumed the decomp headers' SoundPlayer size and found nothing. NSMBW's nw4r version uses 0x64-byte players, which a search validated against the brsar's playable-sound counts found it.
- Positioning the taller map window took four rounds of screenshots. Moving the Back hint put it against the screen bottom, scaling the window to 94% was rejected, and moving the window up by the whole extra height touched the top. The final split is the window 22 units below the top and 5 units above the hint.

What I learned:
- A layout file can be extended without touching the game's loader: the loader reads whatever panes, groups and animation tracks are in the archive, so cloning and renaming is enough on the data side. The code side only has to stop assuming the button count.
- When the guest picks an animation by `base + index`, adding a button means registering its animations *after* all the originals and redirecting only the functions that do the arithmetic.

What's next:
- Developer check on screen: the map window's final position, the lighter outlines, the prompts inside the window, and the migrated X / Z keys (all built, not yet seen).
- Iris Xe laptop on `fde1606` (developer report, 2026-10-31), steady frame rates by render scale: 4x 60 fps, 2x 120 fps, 1.5x and 1x 180 fps. The interpolation target used at each scale wasn't stated. For comparison, 2026-10-16: about 30 fps at 1x with interpolation off, falling over time, before the texture-leak fix. The first-launch options menu was not reported on.
- File-select gear button: investigated only. The layout has free space top right, but no gear art exists on the disc, and the file-select input would need its own button or cursor changes.

## 2026-10-31 — Release v0.2.0
- Version 0.2.0 in the exe's version resource (`runtime/cmake/NsmbwProduct.cmake`) and the package name (`package_nsmbw.ps1`). Contents since v0.1.0 include the options menu, the Options buttons, category volumes, the first-launch menu and the performance work from 2026-10-16 to 10-26. `PLAYING.txt` now describes the options menu and the new menu keys.
- Two-controller play has been tested by the developer (reported 2026-10-31), closing the open item from 2026-10-11.

## 2026-10-31 — Release v0.2.1
What I did:
- Fixed the options menu's Select / Back ignoring the menu bindings. Its handlers had a fixed layout while its prompts showed the bindings, so swapping confirm and back made them disagree (issues.md). Bound controls now win, unbound ones keep the fixed layout, and Esc always goes back.
- Version 0.2.1 in the exe's version resource and the package name.

What's next:
- The README rewrite (player-focused, with technical content moved to `docs/BUILDING.md`) is written but uncommitted, waiting for the developer's screenshots in `docs/images/`.
- A controller shortcut for the options menu (Back + Start was proposed) is not started.

## 2026-10-31 (later) — Combination bindings, rebindable options keys, shared keyboard
What I did:
- **Combination bindings** (`runtime/src/product/nsmbw_controls.cpp`). Any binding slot can hold up to three controls joined with `+`, for example `left_shoulder+right_shoulder` or `back+start`, and is down while all of them are. In the options menu's rebind screen you hold the controls together and the binding is stored when they are all released, in one fixed order so `start+back` and `back+start` are the same. Combinations show as their glyphs side by side.
  - A control that is part of any combination waits 3 frames (50 ms) before it counts on its own. Without this, pressing - a frame before + sends - alone to the game, which opens Select World on the map. A *grace period* like this is the usual way games stop a chord's first button from firing by itself.
  - After a combination, its controls stay silent until released, so letting go of one button first doesn't fire the other's own action.
  - Combinations of the options menu's own actions also count, so Back + Start never reaches the game as - and +.
- **Rebindable options keys.** A fourth bindings context, Options, holds "Open / close options" (F10; Back + Start on a gamepad), "Previous tab" (Q; LB) and "Next tab" (E; RB). Plain F10 is no longer handled by `settings_overlay.cpp`; it is only the default of that binding. Shift+F10 still opens the old bar. Page Up / Page Down / Tab remain fixed extras for the tabs.
- **Players 2-4 can use the keyboard.** Every player's rebind screen has keyboard slots. Players 2-4 have no default keys, and player 1 on Automatic keeps the keyboard when another player picks it, so players share it on keys of their own. The copies of player 1's keys that players 2-4 carried (never read before) are cleared on load.

What broke / what I didn't expect:
- Back + Start sometimes opened and closed the menu at once (issues.md): two press events in one batch both saw both buttons held.
- Players 2-4's rebind screen looked empty on the right because it hid the keyboard column; giving them the keyboard removed the reason for hiding it.

What I learned:
- To decide which event completed a chord, track held controls through the events themselves. The live state answers "is it held now", not "was it held before this event".

What's next:
- Developer confirmed: combinations, the options keys, the + / - fix and two players sharing the keyboard.

## 2026-11-02 — Seed shader list grown from a play session (for v0.2.2)
What I did:
- The developer reported no stutter while playing, so their cache (`build_nsmbw/nsmbw_data/Cache/pipeline_cache.db`) had recipes for every scene they visited. It held 1,529 GX recipes against the seed's 395, with the seed a subset of it and every row at the current `GXPipelineConfigVersion` 19. `merge_pipeline_cache.py` merged it into `projects/nsmbw/assets/pipeline/initial_pipeline_cache.db`: 398 -> 1,532 rows, 1.6 MB -> 6.3 MB. A *recipe* is the GX state a shader is built from, not compiled GPU code, so it works on any GPU; the compiled shaders (`dawn_cache.db`) are per-machine and can't be shipped.

What's next:
- Measure the first-launch prewarm time with the bigger seed (a portable copy with an empty `nsmbw_data/Cache`), on this PC and ideally the laptop, before releasing v0.2.2. The version number itself is not bumped yet.

## 2026-11-05 — 8-1 smoke visible: translated code older than the GXSetDither binding
What I did:
- Identified the smoke by parsing `Stage/08-01.arc`'s sprite list and mapping sprite IDs through the table at 0x8030A340: one `EN_CLOUD` (sprite 234) at x=336, settings `0x52`. Its constructor registers an `m3d::proc_c` (a draw callback object) whose draw slots call raw GX in `func_80008C50`.
- Logged its draws with `NSMBW_ARM_ON_TEX=64x64:3`. The quads were in the right place but had `blend type=0 colorUpdate=0 alphaUpdate=0`, so nothing was written to the screen.
- Replaced 11 stale translated functions (list in `docs/issues.md`), regenerated the shards and rebuilt. Same log line now reads `blend type=1 src=4 dst=5 colorUpdate=1`.
- Developer confirmed the smoke on screen in 8-1. A regression run (title, file select, a level, map, a bonus room) looks normal in stills.

What broke:
- Yesterday's theory was wrong: I took 12 other 64x64 IA8 textures, drawn along the whole level, for the smoke and suspected its position maths. Those draws belong to something else. What exposed it: `func_80008C50` binds three textures to maps 0-2, and only one set in the log did that, with `.9` 0x10200 past `.11` as in the file.
- During reproduction one run was started with `run_nsmbw.ps1`, which uses the real save. It could not be stopped early and ran its 75 s; the save's timestamp did not change. Test runs now use a portable copy.

What I learned:
- "Inlining" means the translator pastes a small function's body into its caller. The inliner does skip overridden functions, but only when a function is translated. An override added later does not reach callers that were translated earlier, and regenerating the shard manifest does not change them.
- Comparing `inline leaf 0x<addr>` in the generated shards against the override list finds these directly. After this fix three remain (0x8016EAB0, 0x80173DE0, 0x801A0870), in overrides from shared runtime files. Whether those three matter for NSMBW was not checked.
- A full re-translation is not automatically safe: it would drop 359 non-overridden functions from the output, 25 of them in the current build.

What's next:
- Re-check the darker-sky item: two of its suspects no longer inline the guest `GXSetDither`.
- Decide on a full re-translation. `build_release` needs a rebuild before v0.2.2 carries this fix.

## 2026-11-06 — Full re-translation of `build/`, after seeding what it would have dropped

What I did:
- Re-translated all five modules into a scratch folder and compared them with `build/`. This reproduced the earlier numbers: 412 function files lost, 53 of them overridden, 359 not, and 25 of those 359 in the current build (41,025 functions down to 41,000).
- Checked the 359 three ways: calls in the fresh output plus `function_map.txt` and `extract-data-function-pointers`; a separate scan of the original DOL and the REL relocation tables for any reference (a relocation table is the REL's list of addresses to fill in at load time, so it names every reference to another module); and a search of all hand-written source for each address.
- Added three seeds to `projects/nsmbw/function_map.txt` ("seeding" means listing an address so the translator translates it even though it found no caller), re-translated in place, regenerated the shard manifest and rebuilt `build_nsmbw`.

What broke:
- Nothing in the game yet: this was caught before any run. A plain re-translation would have dropped `0x800D0F80`, which the pause-menu override calls at `nsmbw_pause_options.cpp:1216` and `:1315`. The result would compile and link, then crash with `missing_target` on that path.
- The translator rewrites `<output.root>/RuntimeConfig.h` whenever it loads the image, even with `--outdir` elsewhere, so scratch runs need yml copies with `output.root` changed.

What I learned:
- A structural-copy override hides its callees. The translator cannot read native C++, so a function called only from such a copy looks dead. The other 24 lost functions in the build are different: only hand-written overrides that do not call them (GX display list, `GXSetTexCopyDst`, KPAD) or other lost functions refer to them.
- Of the other 334: 309 have no reference anywhere in the binaries, 17 are branch labels inside surviving functions, 3 are labels inside lost functions and 5 are called only from overrides. None were in the old build, because the shard step reads the metadata, not the folder; the leftover files were never compiled.
- The same search found two older gaps of the same kind: the EXI overrides call `0x801B93A0` and `0x801BA5F0`, which were never in the build. No log shows them being reached.
- Of the 41,000 functions in both builds, 153 changed, and nearly all of that is callers saving more registers (2,454 lines added, 2 removed; ABI masks were never narrower). The 3 `inline leaf` addresses from the last entry are fine: those overrides are Mario Kart only and are compiled out for NSMBW.

Result: 41,006 functions, the same set as the seeded scratch run. 0 compile errors. A portable smoke run on 2026-11-07 reached the map and 1-1 and went through pause -> Exit -> yes/no -> map with no `missing_target` or crash. It is weak evidence:
- A second game instance was running at the same time, and the Exit and OK presses did not come from the test script.
- The Enter-on-Continue press the test was meant to send never reached the pause menu, because the first Escape opened the Options overlay instead.
- A retry stopped at boot with "No DVD root is configured". The test's own `Set-Content -Encoding utf8` had written the portable `Config.toml` with a byte-order mark (BOM: three invisible bytes some editors put at the start of a UTF-8 file), and after that run the file held a second `[video]` table. That duplicate makes the TOML invalid, so the runtime fell back to defaults. Writing test configs without a BOM avoids it; whether the runtime should cope with a BOM is not investigated.

What's next:
- (Done 2026-11-07) The developer played the new build, including pause -> Continue: fine.
- Rebuild `build_release`. Add a check that fails the shard step when native code calls an address that is neither in the build nor overridden.

## 2026-11-06 — Animated tiles no longer freeze when the resolution changes

What I did:
- Traced why coins and item blocks freeze after a resolution change, fixed it in `aurora-main/lib/gx/scaled_tile_cache.cpp`, and added `NSMBW_RES_SCALE_SWITCH=<scale>@<seconds>[,...]` (`nsmbw_product.cpp`). The switch changes the render scale live, timed from the first level entered after the world map. It saves nothing and needs no keyboard focus.
- Tested the same source with the fix stubbed out and with it on, from portable copies, so the real save and Config.toml were not touched.

What broke:
- The animated tiles. NSMBW draws each tile frame and copies it into its slot in the tileset atlas (explained in the "Placed coins" entry of `docs/issues.md`). The scaled tile cache keeps that atlas as a GPU texture at one scale, and each new tile frame is pasted onto it.
- After a scale change, a tile copy at the new scale is refused by `slot_belongs`, because the scales differ. Only `lookup()` can resize an atlas, and `gx.cpp` calls it only if `lookup_unchanged` fails. That fast path never checked scale, and its hash skips the tile slots, so it kept succeeding and returned the old texture. The tiles stayed on their last pasted frame.
- The first test run was invalid. With no input, Mario was dead by about 14 s into 1-1 (lives ×05 -> ×04, an enemy most likely, not observed), so switches at 25 s and later landed on the map. The second run fit the switches into the first 13 s, but its burst after the return to 4x read 0 with the fix on too, because he was already dying, so a third run covered that return. The third run's self-test also stopped pressing before it re-entered a level (`NSMBW_AUTO_PRESS_TICKS`, default 4000), so a fourth pair of runs raised that limit for the re-entry case.

What I learned:
- The earlier guess in the open items (stale copy targets) was wrong: the log shows the copies resized correctly (`gpu=128x128` -> `64x64`). The atlas was the stale part.
- There were two more cases than the report. Non-whole scales (1.5x, possibly Auto): `note_tile_copy` returned before storing the slot, so the old atlas stayed in use. Changing scale on the world map and then entering a level also froze tiles, because the atlas outlives the level when its address and contents are unchanged.
- Without the fix, going back to the original scale does appear to resume pastes: the `?` block jumped to a different frame in that run. That fits the cause, though Mario's death freeze hid any further movement.

Result (changed pixels inside two `?` blocks across a 5-frame burst, fix off vs. fix on):

| Case | Fix off | Fix on |
|---|---|---|
| 4x, before any change | 101-161 | 92-192 |
| 4x -> 2x in the level | 0 | 162-204 |
| 2x -> 1.5x in the level | 0 | 128-213 |
| 4x -> 1.5x in the level | not run | 131-250 |
| 1.5x -> 4x in the level | not run | 130-209 |
| Scale changed on the map, level re-entered | 0 | 89-178 |

With the fix, the log shows `drop atlas ... x4: tile copies are now x2`, then `build atlas ... x2 -> 2048x2048, 6 tiles pasted`. Without it, no `build atlas` line follows the change. No `missing_target` in any of these runs.

Not tested: Auto (window size), coins by eye (only `?` blocks were measured; all six tile copies log the same way), frame interpolation on, or a developer playing it.

What's next:
- Developer confirms on screen with a normal play session, including coins.
- Remove the line from the README's Known issues in the release that ships this; the current release still has the bug.

## 2026-11-07 — Title screen stutter: HD textures preloaded while the screen is black
What I did:
- Measured the stutter with a new switch, `NSMBW_LOG_LONG_FRAMES=<ms>` (`runtime/src/hle/vi.cpp`): one line per frame whose production took at least that long, timestamped, so a single hitch can be lined up with the log lines around it. With the HD pack, the frame just after the title's logo and text appeared took 64-86 ms in every run. The lines right before it were 33 `texture_replacement: loaded ...` and one `patched tex1_256x256_7b53229a3b86fede_5.dds`.
- Broke that frame down with temporary timers: patching the button-icon font 24-31 ms (the HD base is 2048x2048), reading 33 pack files about 40 ms, uploading them about 20 ms (44 MB in total, because every pack file is uncompressed BGRA). With the pack off, the frame was 25 ms, mostly the font patch on a 1024x1024 upscaled base.
- A screenshot burst showed what is on screen: the title opens with an iris wipe on the level background, and the logo and prompt appear after it. So the loads happened in a visible scene; a hitch while the screen is black would not show, because the game's fades count frames, not time.
- Fix 1, per-scene preload (your suggestion of a longer black screen): NSMBW names each scene in `createNextScene`, which runs after the previous scene has faded out. aurora records every pack file that scene then loads on demand, and the next time the scene starts it loads those files first: reads on 8 threads, uploads on the game thread, about 25-30 ms for the title. A seed list ships with the exe, so a first launch on another PC already has the title, file select, 1-1, the W1 map and the course-in screen.
- Fix 2, the icon font is built on a worker thread when its patch list is set, so the title's first draw only uploads it (3-4 ms). This first failed to match: the patch lists are set one texture at a time and each call bumped one shared counter, so the font's prebuilt copy was always one or two generations old. Each texture now has its own generation.
- Details and evidence in `docs/issues.md` ("The title stuttered as its logo and text appeared").

What broke / what I didn't expect:
- The worst frames were not where I first looked. A 6-second watchdog dump lands in the same 2-second window, but `NSMBW_LOG_LONG_FRAMES` put the hitch before it.
- The context-dependent icons were costing ~25 ms on every map <-> level switch too, not only at the title; the prebuild fixed those as well.
- Taking screenshots during a run causes long frames of its own (PrintWindow), so the burst was only used for what is on screen, not for timing.

What I learned:
- *Preloading* (concept): doing the expensive part of a load before anything needs it, at a moment when a pause is invisible (here, the black between scenes). It needs to know in advance what will be needed; the recorded list per scene is that knowledge.
- A global "something changed" counter is fine for invalidating a cache but wrong for matching one result to one input: other inputs move it too.
- Reading pack files in parallel took the title's 40 ms of reads down to 11-14 ms; the GPU upload (13-17 ms) can't move off the game thread without making Dawn calls from other threads.

What's next:
- Developer: check the title (and file select, map entry) by eye, with your HD pack. Your own play will grow `nsmbw_data/Cache/texture_preload.txt`; merge it into the seed before a release with `python projects/nsmbw/tools/merge_texture_preload.py projects/nsmbw/assets/textures/initial_texture_preload.txt build_nsmbw/nsmbw_data/Cache/texture_preload.txt`.
- Not measured on the Iris Xe laptop or with a cold OS file cache (after a reboot), where the preload's black would be longer.
- The 36 ms frame on world-map entry is something else; not investigated.

## 2026-11-07 — F10 pauses everything; - skips the opening and hint movies
What I did:
- Found why F10 desynced hint movies. The game steps every player's recorded input in its system loop (`dScStage_c::play`, 0x80102250), which runs every frame outside the object updates F10 stops. That step (0x800DDD40) holds the replay only for the pause, Super Guide/hint-movie pause and warning bits of the game-stop word, not the HOME-menu bit F10 set. So Mario froze but his recorded input kept running. F10 now also sets the hint-movie pause bit (OTASUKE_PAUSE, 0x4).
- Found why sound kept playing under F10. The game's sound pause (0x8006A830) is only called from the code that recomputes the stop word each frame, inside an update F10 stops. The KPAD override now calls it on the game thread when the menu opens and closes. This reverses the 2026-10-28 choice to leave sound playing under the menu, at your request.
- Added a skip (`runtime/src/product/nsmbw_movie_skip.cpp`). The button is - rather than +: you chose to keep + for the game's own pause menu in a hint movie. The first press shows "Press (-) to skip" in the bottom-right corner, with the glyph of player 1's - binding (`nsmbw_button_glyphs`). A second press within 4 s skips. No game code changed. The host drives exits the game already has:
  - Opening: the movie scene's own code (0x80922910) already has a + skip for later movies but never for the opening. Its two writes (`mMovieEnded = 1`, `mState = 10`) send the opening into its normal end: it marks the opening seen, saves, and loads the world map.
  - Hint movie: the call the pause window's exit makes, `0x80102370(3, 0, 2, 5)`. In hint-movie mode it returns to the hint movie list.
- New test switches: `NSMBW_LOG_MOVIE_SKIP`, `NSMBW_AUTO_SKIP_TEST=<frames>` (scripted - presses), `NSMBW_LOG_REPLAY` (replay object words every 30 frames), `NSMBW_OPTIONS_MENU_TEST=<open>,<close>` (+ `_SCENE`; opens and closes F10 without keyboard focus), `NSMBW_F10_NO_REPLAY_HOLD=1` (old F10 behaviour, for A/B).

Evidence:
- Opening: a fresh save in the portable copy reached the movie scene (profile 8). The log shows `[skip] prompt`, then `[skip] skip`, then `createRoot(profile=0x3)`. Screenshots `build_nsmbw/shots/skipopen2_prompt_0.png` (prompt in the corner over the opening) and `skipopen1_end_0.png` (World 1 map).
- Replay: on the title screen's idle demo (same replay objects as hint movies), F10 opened for 300 frames. Old behaviour: the replay's frame index (+0x74) ran 0xDA → 0x224 while the menu was open. New: the replay entered its hold state and the index stayed at 0x111, continuing from there after closing (`nsmbw_f10b_old.err.log` / `nsmbw_f10b_new.err.log`).
- You reported "it works" from your own play. I couldn't test a real hint movie: no test save reaches one. So the hint-movie skip and the sound pause rest on your check.

What broke / what I didn't expect:
- The first prompt drew only the glyph: the menu font is loaded on the options menu's first open, so the prompt now loads it itself.
- The opening leaves its "playing" state between its parts. Resetting the prompt whenever the scene stopped being skippable hid it, so it is now reset only on a scene change.
- The plain title screen has no replay object; its idle demo (after about 40 s, via the attract-mode movie) does.

What I learned:
- *Game-stop word*: one 32-bit value the game ORs pause reasons into (pause menu, Super Guide pause, warning, HOME Menu). Each system checks the reasons it cares about, so setting one bit doesn't pause everything.
- Freezing a game by stopping its object updates misses whatever its system loop runs outside them: replay input, the fader, sound.

- Hint movie list (follow-up, at your request): it opens from the hint block in a small course at the far left of World 1, and it doesn't pause the game, so the course's Gameplay bindings applied. That's why B (bubble, Wii A) confirmed and Y (run, Wii 1) went back. Two wrong attempts first: I assumed the list was on the world map, then guessed its buttons from code. You then pointed me at the course, and I drove a fresh save there with scripted input (`NSMBW_TEST_INPUT`). A dump of the list's object (`NSMBW_DUMP_HINT_LIST`) showed its state field (+0x88) leaving the idle value 0x809930B8 exactly while the list was on screen. Now, only while the list is on screen, the controls use the Menu bindings with 2 and A swapped (confirm → Wii A, back stays Wii 1). The log of the final run shows the course using Gameplay (Mario jumped and hit the block), then Menu with confirm sent as Wii A, and back closed the list.
  - *Execute list* (concept): the framework keeps every live game object in a linked list that it walks each frame to update them. Reading it tells the host which objects exist, without calling game code. An object existing isn't the same as it being on screen, which is why the state field was needed.

What's next:
- You tested all of it in your own play (2026-11-07): F10 pause, both skips and the hint list.
- Check the hint-movie skip's fade and return to the list by eye.

## 2026-11-14 — A second exe for CPUs without AVX2
What I did:
- Traced the AVX2 requirement. It is one compiler flag, `-march=x86-64-v3`, inherited from upstream WiiCompiled's first commit and copied onto NSMBW's targets on 2026-08-31. Nothing in the game needs it. It lets the compiler use AVX2/FMA/BMI2 everywhere, and 7 FMA intrinsics in `ppc_isa_float.h` needed it to compile. aurora, SDL and Crypto++ were already built without it.
- The real constraint was accuracy, not instructions. Gekko's multiply-add instructions (`fmadd`, `ps_madd`, about 1,550 call sites in the translated code) are *fused*: they round once, at the end. The runtime got that from hardware FMA. The earlier futureplans idea ("becomes a multiply plus an add") would round twice and be off by one ULP on some inputs, enough to desync a recorded-input replay.
  - *ULP* (unit in the last place): the gap between a float and the next float. "One ULP off" = the smallest possible error, but not zero.
- Wrote an exact SSE2 replacement for the float lanes (`runtime/include/isa/ppc_isa_fma_compat.h`):
  1. widen to double, where a float times a float is exact (24+24 bits fit in 53);
  2. add in double (one rounding) and get that rounding's error exactly with *TwoSum* (six adds/subtracts that recover what the rounding threw away);
  3. *round to odd*: if the error isn't zero, nudge the result to the neighbouring double whose last bit is 1, on the side of the true value. A value rounded this way, in at least 2 more bits than the target, rounds to float exactly as the true value would;
  4. round once to float.
  Double-precision `fmadd` in that build calls the C library's exact software `fma`.
- Added `-DMKW_HOST_ISA=v2|v3` (default v3) in place of the three hard-coded flags. The CPU check (`host_cpu_baseline.cpp`) now takes its feature list from the same level. When the v3 exe finds the CPU lacking, it runs `NSMBWCompiled-compat.exe` from its own folder with the same arguments, so a player never picks an exe. `package_nsmbw.ps1` ships the compat exe when one is built; BUILDING.md has the commands.

What broke / what I didn't expect:
- My first `msub` helper flipped the sign of a NaN addend; the real instruction returns it as it was. The test caught it, and it's fixed.
- The perf script failed twice: the exe it copied over was still locked after the game had exited (antivirus scanning the new 223 MB file is my guess). Each build now runs from its own exe name. Runs 5-7 then exited before the 80 s were up, with no crash event in Windows' log. That looks like the windows being closed; the blocks before each exit are kept.

What I learned:
- Verified, not assumed:
  - `projects/nsmbw/tools/test_fma_compat.cpp` compares against the FMA instruction under all four FTZ/DAZ settings: 320M results, zero non-NaN differences.
  - The same test shows the wrong methods failing: multiply-then-add on 75% of near-tie inputs, double-without-round-to-odd on 47k of 20M. So the round-to-odd step is load-bearing.
  - One NaN case differs by design: when both multiplied inputs are NaN, which NaN comes out depends on operand order. The compiler picks that per call site in the v3 exe too.
- `llvm-objdump` scan: the compat exe has no AVX/FMA/BMI/MOVBE/LZCNT in our own code (the v3 exe has 2.1 million). The rest is in zstd and SDL paths that check the CPU first. About 700 instructions sit in stripped code in the Dawn and DirectX-compiler DLLs; they are probably CPU-checked too, but that is not proven.
  - *`tzcnt` is safe on old CPUs, `lzcnt` is not*: tzcnt is encoded as `rep bsf`, which an old CPU runs as `bsf` with the same answer for nonzero inputs; lzcnt runs as `bsr`, which answers differently.
- Speed, 7 alternating 80 s self-test runs from a portable copy: game-thread spare time on the map was v3 72.9% vs v2 73.3%, in the level 77.6% vs 78.7%. No measurable cost on this PC. But the game thread only works about a quarter of each frame, so this rules out a big slowdown, not a small one.

What's next:
- Ship it in the next release: build `build_release_compat\`, package, and only then change the CPU line in README and PLAYING.txt.
- Run it on a real pre-AVX2 CPU or under Intel SDE (`-snb`). That is the only way to prove the DLLs are safe and to see the speed there.

## 2026-11-14 — The game is a DLL; NSMBWCompiled.exe is a launcher

What I did:
- First step of multi-version support (docs/futureplans.md section 3, decided today: one launcher plus one game DLL per version and CPU level). PAL only for now; nothing about addresses changed.
  - *DLL* (dynamic-link library): a file of compiled code like an exe, but with no starting point of its own. Another program loads it with `LoadLibrary` and calls the functions it *exports*.
- `runtime/cmake/NsmbwProduct.cmake`: the game (translated code, overrides, runtime) is now the `NSMBWGame` target, built as `NSMBWCompiled-P1.dll` (`-compat` is added for an `MKW_HOST_ISA=v2` build). `NSMBWCompiled.exe` is the new `nsmbw_launcher.cpp`: it checks the CPU, picks the fast or compat DLL, loads it and calls its one export, `NsmbwGameMain()` (the old `main()` in `nsmbw_product.cpp`). The build command is unchanged; building the launcher builds the DLL first.
- What had to move or change, and why:
  - The 16 MB stack flag (`-Wl,--stack`) moved to the launcher. Windows reads a process's default thread stack size from the exe's header only; on a DLL the flag does nothing, and the old deep-call-chain crash would come back. Checked: `llvm-readobj` shows `SizeOfStackReserve: 16777216` on the exe.
  - The CPU check runs in the launcher (`MkwHostCpuMeetsLevel`, a non-exiting version of `host_cpu_baseline.cpp`'s check), because a v3 DLL runs AVX2 code in its own startup code during `LoadLibrary`. The DLL keeps its own check as a safety net. This replaces the old "start `NSMBWCompiled-compat.exe`" hand-off: the compat build is now `NSMBWCompiled-P1-compat.dll`, and `package_nsmbw.ps1` ships that.
  - The icon resource stays on the exe, where SDL looks for it. The 45 s screenshot shows it in the title bar.
  - Crash and stall logs printed offsets from `GetModuleHandleW(nullptr)`, which is now the launcher. They use `__ImageBase` (the linker's name for the base address of the module the code is in) instead: `memory.cpp`, `nsmbw_product.cpp` (2 places).
  - Window close (`ExitForAuroraWindowClose`, `aurora_events.h`) used `ExitProcess`. Inside a DLL, that sends the DLL `DLL_PROCESS_DETACH` (Windows' "you are being unloaded" call), which runs the DLL's `atexit` handlers and global destructors: exactly the C++ shutdown that function exists to skip, because it can fault while a guest fiber is active. It now flushes the logs, prints `[runtime] window closed - exiting` and calls `TerminateProcess`.
  - Only `NsmbwGameMain` is exported (`llvm-objdump -p`: 1 export). Marking one function `dllexport` also stops the linker from exporting every function, which with ~30,000 translated functions would come close to the 65,535 export limit.

What broke / what I didn't expect:
- `resolve_profile.py` named 58% of samples `?`: it subtracted the exe's default base address (0x140000000) from `llvm-nm`'s addresses, and a DLL's is 0x180000000. It now reads `ImageBase` from the file. Its default `--exe` is the DLL.
- The profile file is opened in append mode, so `NSMBWCompiled.exe+0x411342b` lines from older runs looked like a sampler bug at first. The last window (this run) names `NSMBWCompiled-P1.dll` 17,907 times and the exe never.

What I learned:
- Verified with self-test runs (`run_nsmbw.ps1`, the real save):
  - Run 1: boot, title, game setup, world map, a level, back to the map; then the developer closed the window at 40 s. No crash event in Windows' Application log.
  - Run 2 (`NSMBW_UNBUFFERED_LOG=1`): the same plus a second level entry, then `[runtime] window closed - exiting`, exit code 0 at 53 s. Screenshot at 45 s: 1-1, 179.9 FPS.
  - Profiler run: after the resolver fix, the busiest thread resolves to `func_801A39C0`, `Audio_HLE_Poll`, `MusicAttenuation::TickGuest`, etc.
- Not verified: the compat DLL (no compat build made), the "CPU too old" message boxes, a release (`-DNSMBW_RELEASE`) build of the launcher (GUI subsystem, log file) and the package script.

What's next:
- Move the first-launch setup out of the DLL into the launcher: game files (disc image or extracted folder; the disc ID will choose the DLL), HD texture packs, and a Riivolution mods slot to fill in later. The launcher must know the disc before it can choose a DLL, so `nsmbw_disc_setup.cpp` belongs there.
- Then the address layer and the USA (E1) DLL.

## 2026-11-14 — Setup screen in the launcher; tested with the USA and PAL discs

What I did:
- Second step of multi-version support. The launcher (`NSMBWCompiled.exe`) now shows a setup window before it loads any game DLL (`runtime/src/product/nsmbw_launcher_setup.cpp`). It has three parts:
  - *Game files*: "Disc image..." extracts an `.rvz`/`.iso`/`.wbfs` into `disc\` next to the exe with DolphinTool.exe; "Extracted folder..." takes a folder Dolphin already extracted.
  - *HD texture pack*: "Install pack..." copies the pack's `SMN` folder into `nsmbw_data\texture_replacements\` and turns the setting on, and a checkbox turns it on and off.
  - *Riivolution mods*: a greyed-out placeholder ("Add mod..." disabled). The shared runtime's loader (`hle/storage/riivolution.cpp`) is not wired into NSMBW yet.
- The disc ID in `sys\boot.bin` (bytes 0-5 game ID, byte 7 revision) chooses the DLL from a table: today only `SMNP01` rev 1 → `NSMBWCompiled-P1.dll`. Any other NSMBW disc is named (game ID, revision, region) and refused, and nothing is saved.
- When the window opens: the configured `dvd_root` is missing or not a supported disc, or Shift is held while the game starts (`NSMBW_SETUP_SHOW=1` does the same). Otherwise the game starts directly, as before. Self-test runs never open it.
- Settings are written to the same Config.toml through the same header the game uses (`runtime_config.h`), before the DLL loads. The game's own first-launch dialogs (`nsmbw_disc_setup.cpp`, release builds only) are deleted; the setup now runs in every build.
- Script hooks: `NSMBW_SETUP_IMAGE`, `NSMBW_SETUP_FOLDER`, `NSMBW_SETUP_TEXTURES`.
- The window is per-monitor DPI-aware (sharp text) and has a common-controls v6 manifest (themed buttons).
  - *Manifest*: an XML resource inside the exe that tells Windows which system library versions to use. Ours only asks for the newer button/control styles; it adds no DPI or OS-version entries that could change how Windows treats the game.

What broke / what I didn't expect:
- The first step's build had also compiled `nsmbw_launcher.cpp` into the DLL: the runtime's source list is a glob of `runtime/src/**/*.cpp`, and the launcher lives there. Harmless (an unused `main` in the DLL), but now excluded with the other product files.
- `LoadCursorW(nullptr, IDC_ARROW)` failed to compile: this project doesn't define `UNICODE`, so `IDC_ARROW` is the ANSI-string form. Used the numeric ID instead.
- The first screenshot showed the disc's long path cut off at the window edge; it is now its own label, shortened in the middle (`SS_PATHELLIPSIS`).

What I learned:
- Verified, all from a portable copy (its own empty Config.toml, never the real one):
  - Empty config: the window opens with "Not set yet" and Play disabled; Quit exits without loading the game (screenshot).
  - USA disc (the developer's dump in `C:\Emulation\roms\wii\`), extracted `DATA\` folder and the `.rvz`: both logged `This disc (SMNE01 revision 1, USA) is not supported yet`. No `dvd_root` written, no extracted files left behind.
  - PAL `.rvz`: extracted to `disc\`, `dvd_root` saved, `[launcher] disc: Europe (PAL), revision 1`, and the game ran boot → title → game setup.
  - Reopened with a disc set: Play enabled, PAL name and shortened path shown (screenshot).
  - HD pack install: 312 files / 174 MB copied, `texture_replacements = true` written, and the game started.
  - The developer's own setup (valid disc, no Shift): no window; the self-test reached a level.
  - The game process is per-monitor DPI-aware (`GetProcessDpiAwareness` = 2) without the launcher setting it, so setting it for the window should not change the game. That API reports 2 for both versions of per-monitor awareness, so this is likely, not proven.
- The USA dump's `main.dol` SHA-256 is `166eb6b7...`: the retail USA rev 1 pressing that calls `DVDDiskCheck` from `main` (the team branch's `variants.toml`). The USA DLL will need the BCA answer described in `MULTI_REGION_GUIDE.md` section 4.
  - *BCA* (burst cutting area): a barcode ring on real Wii discs that the game can read to check it is a pressed disc; with no drive, the runtime has to give the answer the game expects.

What's next:
- The address layer (`nsmbw_va.h` and the tools), then the USA (E1) translation and DLL. Adding a row to the launcher's version table is the last step.
- Not yet run: the compat DLL, a release (`-DNSMBW_RELEASE`) build of the launcher, `package_nsmbw.ps1`, and the window at a DPI other than this monitor's.

## 2026-11-14 — USA (E1) build: address layer, translation, DLL; boots until EGG::AsyncDisplay

What I did:
- *Address layer* (`runtime/include/nsmbw_va.h`; the approach is MULTI_REGION_GUIDE.md's, with PAL as the base): every guest address in our C++ stays written as a PAL rev 1 ("P1") address. For PAL every macro is the identity. A USA build (`-DNSMBW_GAME_VERSION=E1`) maps them at compile time with tables generated from NSMBW-Maps/address-map.txt. The macros:
  - `NSMBW_VA(0x...)` maps an address; one the map has no counterpart for fails the build.
  - `NSMBW_FUNC(tok)` names a translated function by the version's address.
  - `NSMBW_VA_PAIR(hi, lo)` handles an address copied code builds in two halves (`r3 = 0x80430000u; ... r3 + -23776`). Mapping the top half alone is wrong near a range boundary, so it maps the full address and subtracts the low half again.
  - `NSMBW_SDA13(off)` / `NSMBW_SDA2(off)`: for `r13 - off`. `r13` moved 0x300 but the small globals moved 0x2E0/0x2F8 (and other amounts in other parts), so each offset is mapped (USA offsets differ from PAL by +32, -16, +8 ... bytes).
  - `NSMBW_BY_VERSION(pal, usa)` for facts that are not addresses (a REL's size, the game code "SMNP"/"SMNE", the console region EU=2/US=1).
- The hook macros (`PPC_NATIVE_OVERRIDE`, `REGISTER_*`, `MKW_KNOWN_*`) map their own address argument, so the specialisations they define are keyed on the version's address, which is what translated code looks up.
- Tools, all in projects/nsmbw/tools:
  - `unpack_game_code.py` copies `main.dol` and LZ77-decompresses the four RELs from an extracted disc. It reproduces NSMBW-Decomp/original byte for byte from the PAL disc.
  - `nsmbw_address_map.py` reads the address map.
  - `map_function_map.py --prove` converts our function map and compares every word of every function between the versions.
  - `va_codemod.py` wraps addresses in our NSMBW code (idempotent, `--check`).
  - `gen_version_tables.py E1` writes `generated_nsmbw-E1/`: the compiler tables, the USA translator projects, and a converted copy of projects/nsmbw/native for the translator, which finds our overrides by reading those sources.
- USA code from the developer's disc (`main.dol` SHA-256 `166eb6b7...`, the pressing that calls `DVDDiskCheck`) translated into `build/nsmbw-E1*`. `build_nsmbw-E1\` builds `NSMBWCompiled-E1.dll`. The launcher's version table has the USA row.
- Region items: the game code seeded at 0x80000000 (which also picks the NAND title, so the USA save goes to `534d4e45`), the console region, and the options-menu font folder (`US/EngUS` on the USA disc).

What broke / what I didn't expect:
- My REL reader first read `d_basesNP`'s relocation tables where `d_enemiesNP` is loaded (a REL's relocation data comes after its sections and is freed after linking), which looked like 100 changed functions.
- The codemod also wrapped the stub names `nsmbw_runtime_shims.cpp` defines for Mario Kart addresses the shared runtime calls, so the USA DLL failed to link (6 undefined symbols). Such lines now carry `// nsmbw-va: not an address`.
- Shared runtime code outside NSMBW `#if` blocks has NSMBW addresses too: `os_thread.cpp` (OSInitContext, the first USA run's `missing_target 0x801AD0F0`), `gx_fifo.cpp` (`__gx`), and `vi.cpp`.
- `vi.cpp`'s `WriteGuestStateLocked` writes Mario Kart addresses into NSMBW memory on every retrace (an initialized flag, TV format, render sizes, frame-buffer pointers), and gates the post-retrace callback on Mario Kart's `sSystem` address. In PAL NSMBW those land on unrelated variables; the game has worked regardless. They are now mapped like everything else, so USA touches the corresponding words and behaves as PAL does. The bug itself is open (docs/issues.md).
- The first USA runs: a pointer read of the text "Expi" inside our EXIInit copy (`r13 + -N` offsets, fixed with `NSMBW_SDA13`), then the hang below.

What I learned:
- Verified:
  - `map_function_map.py --prove`: of 29,799 function-map entries, 24,602 are identical in USA, 5,159 differ only in ways the map explains (branch targets, r13/r2 and lis-built addresses, pointers), 36 differ, 2 have no counterpart. The 36 are the known region differences (language functions, 50/60 Hz `Configuration::initialize`, the strap screen, the disc check, a DVD-error check) plus a few the checker cannot judge (functions with no known end).
  - The USA translation has exactly PAL's function counts per module (main.dol 16,187; RELs 16,158 / 30,366 / 24,117 / 18,512). The PAL `build/` folders hold stale files from older translations; the metadata lists match.
  - `gen_version_tables.py`: 693 hook tokens, all mapped; no two-step address in the copied code needed review after `NSMBW_VA_PAIR`.
  - PAL after all of it (address layer in every file): title, map and three level entries in 60 s at 180 FPS.
  - USA boots through the launcher (`[launcher] disc: USA, revision 1`): threads, NAND (its own portable save), all four RELs reserved at the USA load addresses (d_enemiesNP 0x809A2CC0, d_en_bossNP 0x80B1C940).
- Not working yet: the USA main thread sleeps in `EGG::AsyncDisplay` (USA 0x802BB6A4, PAL 0x802BB904) on a thread queue at 0x807602F4 and is never woken. PAL sleeps in the same place and is woken. The pre-retrace callback (USA 0x802BAC30) and the waker (USA 0x802BB840) do run on USA, and the translated code of that whole area is equivalent to PAL's, so the waker's own condition must differ: something it reads (a counter, the TV mode) has a different value.
  - `NSMBW_LOG_RETRACE_CB=1` (new) logs the post-retrace callback and its guard. It never fires on either version: NSMBW registers only the pre-retrace callback.

What's next:
- Find which value the waker (PAL 0x802BBAA0) reads differs between the two runs, and from there what in the runtime sets it.
- Then title, map, level, text in English/French/Spanish, the USA save, and the disc check.

## 2026-11-15 — The USA version plays: title, map and levels

What I did:
- Routed the shared runtime's addresses through the address layer too (`va_codemod.py --shared`, 27 files). Every game-memory literal there is mapped, including Mario Kart Wii leftovers: for PAL the macro is the identity, and for USA each one moves to the matching location, so USA does whatever PAL does. `func_` names are left raw in shared files (the Mario Kart stubs in `nsmbw_runtime_shims.cpp` are called by those names). Three literals that are not addresses (a range bound, a stack pointer, an arena top) are marked `// nsmbw-va: not an address`.
- The codemod now also rewrites the hand-written `r13 - 20352u` form (`r13 - NSMBW_SDA13(20352u)`), not only the copied `r13 + -N` form. Five more sites: `nsmbw_os_init_stm.cpp` (3), `nsmbw_exi_probe.cpp`, and Mario Kart STM/interrupt code in `os_init.cpp` / `os_interrupt.cpp`.
- New diagnostics: `NSMBW_LOG_SCHED_DISABLE=1` (the scheduler-disable count: changes around alarm callbacks, SelectThread returning early because of it, and its value after each boot step) and `NSMBW_WATCH_WRITE_CALLS=<hex addr>` (checks a guest word on every dispatched call and reports the call after which it changed, the eight calls before with their r3/r4, and the 64 bytes around it; `NSMBW_WATCH_WRITE` only checks once per frame, too late for boot).

What broke / what I didn't expect:
- After mapping the shared runtime, USA got worse: it exited at boot (the game's global destructors ran) instead of hanging. `NSMBW_LOG_SCHED_DISABLE` showed `SelectThread` returning early because the scheduler-disable count at USA 0x8042A700 was 5,460,993 (0x00535401). The count is in .bss in both DOLs and still 0 after every native boot step, so the junk came from inside the game's start-up. `NSMBW_WATCH_WRITE_CALLS` put the write in the thread start, and the value was the giveaway: 0x00535401 is "ST" 01, the `/dev/stm/immediate` marker that our `__OSInitSTM` override writes to `r13 - 20352`. On USA that PAL offset is the scheduler-disable count (USA `r13 - 0x4F80`). With the count non-zero, the boot thread's suspend returned at once, `main` was never switched to, and the program ran off its end.
  - *.bss*: the zero-filled part of a program's memory, which is not stored in the file, so a non-zero value there was written at run time.
- Before that fix, mapping the scheduler globals (`kThreadQueueArrayAddr`, the pending mask, the reschedule counter, the disable count, the switch-thread callback; NSMBW addresses in a shared header) is what turned the old `EGG::AsyncDisplay` hang into this exit: the old hang came from the runtime's scheduler reading PAL's run-queue location while the USA game put its threads on its own.

What I learned:
- Verified (self-test runs, 60 s each, both rebuilt from the same tree):
  - PAL: boot, strap, course-in, title, game setup, map, level, map, level, map, level, map; no errors; 177.9 FPS average.
  - USA (portable copy, the developer's USA disc): the identical scene sequence, no errors. Screenshots: the title screen, the World 1 map in English with our key prompts, and World 1-1. 60 FPS: the portable copy's default settings (no frame interpolation).
  - The USA save is written to `UserData\NAND\title\00010004\534d4e45` ("SMNE"), separate from PAL's.
  - A fresh USA save plays the opening movie (scene 8) before the map.
- Not checked on USA yet: French and Spanish text, the options menu (F10) and its fonts from `US/EngUS`, the extra pause and map buttons (patched `Layout/*.arc`), quick-save-to-full-save, the HD pack, the disc check's BCA path, any world past 1, and 180 FPS interpolation.

What's next:
- Check the unchecked features above on USA.
- Package both DLLs together (`package_nsmbw.ps1` knows only P1 so far) and let the launcher's setup screen accept either disc.

## 2026-11-15 — One package for PAL and USA, with compat builds; map-cursor fix

What I did:
- Map-menu cursor (reported by the developer): choosing Options in the world-map menu left the map menu's selection brackets drawn behind the options menu. `nsmbw_map_options.cpp` hid the cursor with `dSelectCursor_c::Cancel(0)`, but the map menu's cursor is slot 4 (its `SelectCursorSetup(..., 4, false)`; the game's own close, `func_8077B190`, calls `Cancel(4)`). Now `Cancel(4)`. The pause window's slot 0 was already right.
- Release package: `package_nsmbw.ps1` ships four game DLLs next to one launcher: PAL and USA, each fast (AVX2) and compat (x86-64-v2). New build folders `build_release-E1\`, `build_release_compat\`, `build_release_compat-E1\` (configure lines in docs/BUILDING.md). The script warns about any that are missing.
- Player docs (README.md, PLAYING.txt): USA rev 1 discs are supported; the minimum CPU is SSE4.2 (the compat build), marked as untested on an older CPU; the setup steps describe the launcher's setup window (and Shift to reopen it); the USA save folder. Known issues say the USA version is tested only through World 1.
- The launcher clears `MKW_FORCE_UNSUPPORTED_CPU` after reading it (`nsmbw_launcher.cpp`).

What broke / what I didn't expect:
- The first compat test from the zip loaded the compat DLL but never wrote a log: the test switch `MKW_FORCE_UNSUPPORTED_CPU=1`, which makes the launcher pick the compat DLL, was inherited by the DLL, whose own load-time CPU check (`host_cpu_baseline.cpp`) honours it too and showed "unsupported processor" while loading. Only a test-switch problem (a real old CPU passes the DLL's level-2 check), but it made the compat path untestable on this PC. The old compat-exe hand-off cleared the switch for its child; the launcher now does the same.

What I learned:
- Verified from the zip itself (each a fresh portable copy, the setup hook pointed at the disc, 30-45 s self-test): PAL fast, PAL compat, USA fast, USA compat each loaded the expected DLL (read from the process's module list) and ran boot, strap, course-in, title, game setup and the opening movie with no errors; saves went to `534d4e50` (PAL) and `534d4e45` (USA).
- Not verified: the map-cursor fix on screen (it needs the map menu driven by hand); either compat DLL on a real pre-AVX2 CPU; the USA dev build (`build_nsmbw-E1`) has not been relinked with the cursor fix because a game started from that folder still has the DLL open.

What's next:
- The developer checks the map-menu Options button on screen.
- Check the remaining features on USA (F10 fonts, pause/map buttons, full save, French/Spanish, HD pack, later worlds, 180 FPS).
- Run a compat DLL on a real older CPU or under Intel SDE.

## 2026-11-15 — 180 FPS interpolation stuck at 40 fps: cause found and fixed

What I did:
- Developer report: with 180 FPS interpolation at 528p, the game drops to 40 fps and the log repeats `Presenter fell behind; dropped 140/147 stale presentation jobs` every second. The queue holds 9 jobs (`enqueue_presentations`, `aurora.cpp`), 3 per game frame at 180, and an overflow clears the 7 waiting, so 140-147 dropped of 180 made = about 40 shown.
- Ruled out with 90 s portable runs (each held 178-180 fps): window size (640x480 and borderless 2560x1440), render scale (4K behaved like 528p), the window being covered by a topmost window, the HD pack. The game thread had 66-96 % spare time throughout.
- Reproduced in a 200 s borderless run with the HD pack: about 140 s in, on the world map, it fell to exactly 40.0 fps and stayed there. In that state `AURORA_LOG_WORKER_TIMING` shows acquire 0.02 ms and Present 0.27 ms but **schedule wait 24.6 ms average, 44 ms max**: the presenter is not slow, it is sleeping until deadlines that are too far in the future.
- Added `AURORA_LOG_PRESENT_ANCHOR=1` (`encode_sealed_frame`): logs a group's lead over its own anchor whenever it changes.

What broke / what I didn't expect:
- Before the collapse, every run shows a slow ramp on the map: the presenter's wait for the GPU (acquire) climbs from 0.02 ms to about 5 ms over ~20-30 s, then sits at the 5.5 ms budget. Timed GPU work is flat (~75 ms per second, `AURORA_GPU_TIMING`), and Immediate present mode ramps the same way. The display here runs at 179.953 Hz (`DwmGetCompositionTimingInfo`: 10000000/55570), the game paces 180.0 presents/s: 0.047 extra frames/s, one 5.56 ms frame every ~21 s, which matches the ramp rate (~0.2 ms/s measured, 0.26 predicted). Once saturated, the frame worker is held up and a group occasionally finishes encoding after its own anchor.

What I learned:
- Root cause (the 40 fps lock): `encode_sealed_frame` slides a late group's anchor forward by whole periods, and the cursor `s_lastGroupAnchor` then forces every later group to `last + interval`. The cursor only advances, so a slide is never undone: the whole schedule stays that many periods ahead for the rest of the session. Evidence: the anchor log showed lead 0, then one slide to 16.67 ms (1 period) after a late group (`encode done +4.25 ms vs own anchor`), and the lead stayed at 1.0 period for the remaining ~150 s with no further line.
- Why 2 periods locks it: a group's first slot is due ~14 ms after it is enqueued plus the lead. With 1 period of lead, 5 jobs are still waiting when the next group of 3 arrives (8 <= 9, fine). With 2 periods, 8 are waiting (11 > 9), so every arrival clears the queue and the presenter sleeps toward deadlines it is never allowed to reach. Arithmetic, not yet logged at the moment of a 2-period slide.
- Two other game instances (a USA build and another session's package test) were running during some of these runs, so the GPU timings are not clean. The pacing logic itself does not depend on that.

- Fix (same day): the cursor now lets a lead decay. A group whose own anchor is earlier may start up to 1/24 period (~0.7 ms) before `last + interval`, so one period of lead is gone after ~24 groups (0.4 s); groups still never share an anchor. A 240 s run with the fix: 11 slides to 1.0 period, each logged back down to 0.0 within ~0.4 s, never above 1.0, every 2 s window 179.9-180 fps, one drop of 7 jobs.
- Trade-off seen in that run: slides happen more often than before (11 vs 1 in a similar run), because a lead no longer stays around to absorb the next late group. Each slide holds one frame for a period; not checked on screen whether that is visible.

- Frame interpolation "Match display" (same day, developer's request): Config.toml `frame_interpolation_fps = 1`, F10 and ImGui pickers. aurora caches the window's display refresh (`aurora_get_display_refresh_hz`, `window.cpp`): SDL's display mode, replaced on Windows by DWM's composition rate when they agree within 1 Hz, because Windows modes report whole hertz. On each paced frame `vi.cpp` (`MatchDisplayRateTick`) picks slots = round(refresh / 60) and, within 0.5 % of that multiple, stretches the 60 Hz game frame to slots / refresh (16671 us here), so presents equal the refresh exactly; otherwise it uses the multiple below the refresh with the clock unchanged (144 Hz -> 120). The VI interval is now kept in nanoseconds. 240 FPS is offered again (the config reader used to rewrite 240 to 180).
- Match display test (240 s, borderless 2560x1440, 528p, HD pack): log `179.953 Hz (DWM 179.953, display mode 179.959) -> 180 fps interpolation, game frame 16671.000 us`; 0 slides (11 with fixed 180), fps 178.9-180, one drop of 7 jobs, audio backlog 0.002 s.
- **Correction:** the creep is not the refresh mismatch. With presents matched to the display it still ramps the same way (acquire 0.02 -> ~1.8 ms, then Present 0.2 -> ~3.8 ms over ~30 s on the map, back to 0.2 after a scene change). The ~0.2 ms/s agreement above was a coincidence. Ruled out since: live textures and texture caches (`AURORA_LOG_TEXTURE_STATS`, flat or cycling), GPU work (flat), GPU clock (flat ~1150-1170 MHz across a ramp), present mode (Immediate ramps too). With the lead-decay fix it no longer costs frames, only up to ~5 ms of extra latency. Cause unknown.
- DWM's reported rate varied between runs (179.953 in most, 180.002 in two), so the matched interval follows whatever DWM reports when the window opens or changes display.
- 240 FPS test (150 s, 2560x1440 borderless, scale 4.09 in the config): presented 178-181 fps (the display's limit), 8821 jobs dropped (~60/s), 17 slides, game thread 56-76 % idle, timed GPU work 113 ms/s. The log only shows aurora's initial "scale 1" line, and the GPU-timing labels are logical sizes, so the 4K render scale is unconfirmed; the screenshot's geometry edges look smooth.

What's next:
- The developer plays with Match display and reports hitches or latency.
- Find the creep: PresentMon (present mode composed vs independent flip, display latency per present), and whether it happens at 120 or with interpolation off.

## 2026-11-15 — Render scale not kept between launches (fixed); it also explained the missing HD tiles

What I did:
- **Render scale not remembered.** `runtime_config.h` `IsSupportedResolutionMultiplier` accepts only 0, 1, 1.5, 2, 3, 4, 6, 8 when Config.toml is read (unchanged since `ec226e8`). Since `31d39f4` (2026-11-07, presets by height) the menu saves 720p = 1.3636, 1080p = 2.0455, 1440p = 2.7273, 4K = 4.0909, 5K/6K likewise, so every preset except Auto and 528p is written correctly and rejected on the next launch, which falls back to 1.0. Evidence: the same portable copy with `resolution_multiplier = 4.09091` rendered `native 640x528` passes (`AURORA_GPU_TIMING`); with `NSMBW_RES_SCALE=4.09091` the same config gave `native 2618x2160`. Consequence: the 240 FPS "4K" test above actually ran at 528p.
- **HD pack missing on ? blocks, bricks and placed coins in levels.** These are animated tiles; their pack art is the 32x32 format-5 frames in `World\Blocks\{Yellow,Blue,Orange}` and `World\Bricks`. Old builds run from scratch copies (same save, scale 4, their own pack): 10/28 (`fde1606`) shows the HD art; v0.2.2 (`32e8f44`, 11/07) and current looked like the original 32x32 art upscaled. Aurora commits in that range: `cd62742`, `e9039fb`, `65ad310`.
- **Render-scale fix:** `IsSupportedResolutionMultiplier` accepts 0 or any finite 1-8. A second cap hid behind it: `nsmbw_product.cpp` pulled any saved scale above 4.0 (the old "4x" preset) back to 4.0 at startup, so a saved 4K would have become 4.0 / "Custom"; the cap is now the 4K value (2160 / 528, +0.01 for the 6-digit rounding in Config.toml). 5K/6K are still pulled back to 4K at launch, as that comment intended. Verified on a portable copy without `NSMBW_RES_SCALE`: saved 4.09091 rendered `native 2618x2160`, saved 2.72727 (1440p) rendered `native 1745x1440`, and Config.toml kept both values.
- Added `AURORA_LOG_REPLACEMENT_LOOKUPS=1` (`texture_replacement.cpp` `find_replacement`): each pack lookup's outcome once per key (cached / loaded now / no pack entry / failed earlier) and the size handed back.

What broke / what I didn't expect:
- **Three wrong turns, all from screenshots judged by eye.** (1) "Removing the preload lists brings the HD tiles back": a clean A/B (no lists at all vs the seed list) looked the same in 1-1, and the lookup log shows the 17 tile frames resolving to their 256x256 pack textures in both (`loaded now` then `cached`, or `cached`). The preload is not the cause. (2) Building with the 10/28 `scaled_tile_cache.cpp` (before `e9039fb`) looked HD in one run, but three repeat runs of the current file looked the same as it, and `e9039fb`'s only behaviour change (dropping an atlas whose tile copies change scale) never logged a drop. The ? block's "?" animates, so crops from different frames differ in shape; judging HD vs original from that shape is unreliable.

What I learned:
- **The HD-tile report is the render-scale bug.** ? blocks, bricks and placed coins are animated tiles: each frame the game draws the tile's animation frame (HD pack art, bound as 256x256 - `NSMBW_LOG_TILE_FRAME_BIND`) into a 32x32 area of the frame and copies it into the tileset atlas with a strided `GXCopyTex` (`NSMBW_LOG_SMALL_COPIES`: six slots in 1-1, the blue coin, brick and ? block at (480/512/544, 32), coins at (960, 0/32) and (992, 0)). That copy is made at the render scale, so at 528p it is 32x32 and the HD art is squeezed back to the original size; sprites sample the HD texture directly and stay HD at any scale. Dumped slot copies (`AURORA_DUMP_TILE_ATLAS`): the ? block at 1x is 32x32 with square rivets and a stair-stepped "?", at 4x 128x128 with round rivets and clean 45-degree chamfers - the pack's art, which is itself drawn in a pixel style. Since `31d39f4` (11-07) every launch ran at 528p whatever the menu said, so the tiles looked un-HD from v0.2.2 on; with the render-scale fix they are HD again at any scale above native.
- The earlier "not caused by the render-scale bug" was wrong: I judged the pack's pixel-style "?" as original art. The atlas dump with and without `e9039fb`'s scale drop was identical, and the preload made no difference, so neither is involved.
- Coverage of this pack (not a regression): the ? block strip has 20 frames, 16 in the pack (`World\Blocks\Yellow`); 4 have no entry. The brick animation frames (RGB5A3) have no entry either - the pack's one `World\Bricks` file is format 14 - so animated bricks stay original art even at 4x. The 10/28 build loaded the same 16 Yellow frames.
- At 528p these tiles cannot show HD art in any build: the copy is native size by design.
- The pack's tile frames are 256x256 BC7 DDS (DX10 header, 65,684 bytes).

What's next:
- The developer checks ? blocks and coins at their usual render scale after a restart.


## 2026-11-15 — Player 1 character: every write found (investigation, no code yet)

What I did:
- Traced everything that writes `daPyMng_c::mPlayerType[4]` (0x80355160), the table of which character each player slot plays, as groundwork for an F10 "player 1 character" option. The plan and the full table of writers are in docs/futureplans.md section 7a.
- The DOL side was easy (NSMBW-Decomp's `syms.txt` names it). The Add/Drop Players screen lives in `d_basesNP.rel`, which has no symbols, so two small scripts did the mapping:
  - a relocation scanner: a REL stores, for every instruction that refers to a DOL address, a *relocation* entry (the loader patches the address in at link time). Listing every relocation that points at 0x80355150-0x803551A0 gives every REL function that touches the player tables, without reading 1.8 MB of code;
  - a class labeller: each class's state table (pointers to its state functions) sits right before its `Class_c::StateID_Name` strings in the REL's data, so a state function's pointer location names its class. That labelled `dCharacterChangeSelectBase_c` (the selector), `dNumberOfPeopleChange_c` (the Add/Drop screen that owns the selectors) and ~20 others.
  - Both scripts were in the session's temporary folder, not the repo.
- Read the save manager's two copy loops, the five writers and the selector's scroll and "taken" logic in the translated code (`build/nsmbw*/functions/func_<addr>.cpp`), and read the constant tables they index straight from `NSMBW-Decomp/original/d_basesNP.rel`.
- Checked the disc and the HD pack for a Mario portrait in the selector's style: there is none (the selector only ever shows Luigi and the two Toads). Made candidate images from an upscaled Mario Kart Wii mugshot (Real-ESRGAN's anime model, then reframed to match Luigi's portrait); which image to use is not decided.

What broke / what I didn't expect:
- My first pass only matched the table's exact address and decided the Add/Drop screen never writes it. It does, through a register that holds the table's base with an offset in the store (`FlatWriteRam32(r6, ...)` after `r6 = 0x80350000 + 20832`), which my "write through the base register" count missed. Reading the loop bodies found the commit (0x807A0BF0) and the slot reset (0x807714C0; *corrected later the same day: it runs when a slot's selector starts, not on cancel*).
- Two of the relevant helpers are inlined into their callers (0x800BB180 into the `dInfo_c` constructor and 0x807A0DB0; 0x8076FE60 / 0x8076FE40 into 0x807702A0 / 0x80770D80), so overriding them at their own address would not change those call sites (CLAUDE.md's second bug class).

What I learned:
- The save already keeps the table (it stores, per character, the slot that plays it), so a changed player 1 survives save and reload without new code.
- Player 1 turns back into Mario because `dNumberOfPeopleChange_c` (0x807A1E80) resets the whole table to Mario/Luigi/Yellow/Blue when a file is started from game setup (scene 10), and a slot's selector starts it on a fixed default (0x807714C0). Player 1 never gets a selector of its own: the reset marks slot 0 as already joined.
- Nobody but player 1 can ever be Mario: the selector cycles positions 1-3 only, and position 0 (Mario) has no portrait in `charaChangeSelectContents.arc`.
- Not verified by running: the meaning of the `+0x6C8 == 1` condition in 0x807A1E80 (probably "one player"), and what the selector shows if a slot is handed Mario. Everything here is PAL; the USA REL has not been compared.

What's next:
- On the developer's go-ahead: the F10 option and the overrides in futureplans 7a steps 1-3, tested with `NSMBW_WATCH_WRITE=80355160`; then decide on step 4 (Mario for players 2-4, needs a fourth portrait).

## 2026-11-15 — Player 1 can play any character (F10 > Advanced)

What I did:
- New option "Player 1 character" (F10 > Advanced; Mario, Luigi, Blue Toad, Yellow Toad), changeable on the world map. It writes `daPyMng_c::mPlayerType[0]` and swaps with the slot that had that character. The save already stores that table, so the choice is per game file. Design and open points: docs/futureplans.md 7a.
- The investigation entry above planned five code overrides. Reading every reader of the selector's constants (through the REL's relocation table) showed the Add/Drop Players code only knows characters through five constant tables in d_basesNP's read-only data, all loaded from memory at run time. So `nsmbw_p1_character.cpp` rewrites four of them every frame with Mario and player 1's character swapped (a *transposition*: a swap of two values that leaves the others alone). The game's own code then keeps player 1's character at file start, starts no other slot on it, and offers Mario in its place. No code override was needed for steps 2-3.
- Two overrides, both structural copies with marked `EDIT` lines:
  - 0x80773840, the selector card's lives cache: the card standing in for Mario shows Mario's lives.
  - 0x809027C0, the world-map player's constructor: it always built Mario's model (model type 0). It now passes player 1's character.
- The selector card's picture: `nsmbw_mario_portrait.cpp` overwrites player 1's character's portrait with a Mario picture right after the game reads `charaChangeSelectContents.arc`. Same size and format, so every pointer into the archive stays valid; the original is put back when player 1 changes.
  - To know when and where the archive was read, the shared DVD layer got a read notification, `RuntimeDvd::SetReadObserver` (`dvd.cpp`, `dvd_generated_files.h`).
  - The picture comes from `nsmbw_data/portraits/Im_plofileMario_00.tpl`, made from any PNG with the new `projects/nsmbw/tools/make_portrait_tpl.py`. Failing that, the disc's own Mario head from the score screen is used.
- `NSMBW_DEBUG_P1_CHARACTER` now calls the option's setter once on the first map; `NSMBW_LOG_P1_CHARACTER=1` logs the swaps and patches. `nsmbw_product.cpp` exposes `NsmbwDataDirPath()` for native files.

What broke / what I didn't expect:
- **The Toads' numbers are the other way round from NSMBW-Decomp's `PLAYER_TYPE_e`.** Setting 2 ("Yellow Toad" there) gave Blue Toad in the level and on the HUD. The decomp's own code agrees with the game: its map HUD table maps 2 to `P_BkinoFace`, and the selector card that shows lives of 2 makes `P_kinoB` visible (pane slot card+4; the names are fetched in the order lui, kinoB, kinoY). The menu labels and the portrait mapping were fixed, and the investigation entry above is corrected. Trust the decomp's code over its enum names.
- **The map player stayed Mario** while the HUD showed Luigi, even in a freshly built map scene. The world-map player's constructor hardcodes model type 0.
  - My first fix took the model type from the REL table 0x8093CDF0 ({0,1,3,2}), which the other map players use. Blue Toad then walked the map as Yellow Toad: that table maps a slot to its default character, it does not map a character to a model. The model type is the character number itself.
- **A build error:** `runtime_config.h` includes `toml.hpp`, which the native-override target has no include path for. That's why the data folder now comes through `NsmbwDataDirPath()`.

What I learned:
- Verified in game (portable copy, self-test with `NSMBW_AUTO_PRESS_STOP_IN_LEVEL=1`, so the player dies and the map is rebuilt):
  - Each of Luigi, Blue Toad and Yellow Toad set on the map gave that character in 1-1, with its own HUD head and lives (Luigi x04, Blue x05, Yellow x05).
  - The next map showed that character's model and head.
  - Shots: `build_nsmbw/shots/p1luigi3_map2_0.png`, `p1c2_*`, `p1c3_*`.
  - The portrait patch found the archive at file select (scene 10) and on every map, and logged the right picture (Luigi -> `Im_plofileLuiji_00`, 2 -> `Im_plofileKinoB_00`), from both the disc fallback and a made TPL.
- `make_portrait_tpl.py`'s XXH64 matches the reference test vectors. Hashing the disc's three portraits reproduces the three file names in the developer's HD pack exactly (`tex1_150x150_4b19253008e5a918_5` = Luigi, etc.), so the HD name it prints for a new portrait is the one the loader will look for.
- USA: the five tables and both overridden functions are at the same addresses with identical bytes in the USA REL. The DOL's player tables are -0x300, mapped by `NSMBW_VA`. The USA DLL compiled; it did not link because a USA game was running from `build_nsmbw-E1`.
- Not verified:
  - the Add/Drop Players selector itself (needs a second player): Mario offered in player 1's character's place, with his portrait and lives;
  - the F10 row on screen;
  - switching back to Mario;
  - starting a file whose save has a non-Mario player 1;
  - the other players' map models in multiplayer (0x808EB7D0 indexes the slot -> default character table by its actor parameter);
  - the USA build running.

What's next:
- The developer: check the F10 row, then Add/Drop Players with a second controller, and save, quit and reload with Luigi.
- Choose the Mario portrait; install it with `make_portrait_tpl.py --hd-pack`.
- Link and run the USA build once its running game is closed.
- (Later the same day) Developer report: changing the character in F10 did not change the map. The constructor fix only applies to a map player built afterwards. Now the map reloads itself after a change: `NsmbwP1CharacterTick` remembers the model type the constructor used, and when it differs from player 1 on the map, F10 has been closed for 30 frames and no scene change is pending, it calls `0x809218B0` (dScGameOver_c's "back to the map": `setNextScene(WORLD_MAP, 0x80102B50())`, the parameter built from `dInfo_c`'s current world and node). The first test showed nothing because the test switch fired on the map's first frame, before the map player existed (logged `model 1`); it now waits 300 frames. Verified: the map reloaded and showed Luigi on the same node. The USA build was linked (its game had already been closed) but not run.

## 2026-11-15 — Mario in the Add/Drop Players selector (four characters)

What I did:
- The developer asked for a character select for player 1 even after choosing 1 player, using the game's own Add/Drop screen. Stage 1, done: that screen's carousel now offers all four characters. Design: docs/futureplans.md 7a.
- Layout (`nsmbw_layout_patch.cpp`, `PatchCharaContents`), generated at boot like the pause menu:
  - adds `Im_plofileMario_00.tpl` to `charaChangeSelectContents.arc` (new: inserting a node into a U8 archive, `AddArchiveFile`);
  - adds a texture-list entry (`AddTexture`) and two materials cloned from Luigi's (`CloneMaterial`);
  - adds `P_mario_00` / `_01` cloned from `P_lui_00` / `_01`.
  Verified by parsing the generated archive: texture 5, materials 14 / 15, panes using them, a 150x150 RGB5A3 picture.
- Code, in `nsmbw_p1_character.cpp`:
  - native `0x8076FD70` (position -> card value; Mario is 7, greyed 10);
  - copies of `0x807708E0` (Mario taken check; scroll to position 0), `0x807736B0` (show Mario's pane and lives for his value), `0x8076E600` and `0x8076EA00` (arrow ends).
- Retired from the first version, because Mario now has his own position and card:
  - swapping the position tables;
  - the lives-cache override (0x80773840);
  - overwriting a portrait in memory, and the DVD read notification it needed (`dvd.cpp` and `dvd_generated_files.h` are back to their committed state).
  What stays swapped: the file-start reset, each slot's start character, and the slot -> start position table.
- `NSMBW_TEST_INPUT` entries can start with `p<N>:` to script players 2-4; a scripted player counts as connected (`nsmbw_controls.cpp`).

What broke / what I didn't expect:
- The first design (swap Mario and player 1's character in the position tables) was only half right. The selector's confirm code (0x807708E0) and its greyed-out check (0x8076FD70) map positions to characters by hand, not through the tables, so a Mario sitting in Luigi's place would have been judged "Luigi, taken" and could not be chosen. Replaced by the four-position carousel.
- Scripting player 2: the first runs confirmed player 2 at once.
  - A logged confirm (now permanent under `NSMBW_LOG_P1_CHARACTER`) moved by exactly 120 frames when the "join" tap did, so it was that tap.
  - A scripted remote is connected from the start, so its selector opens without joining, and the first `2` is the confirm.
  - Also: `$Input` is a reserved PowerShell variable, which emptied the script in an earlier run.
- The scroll directions are the reverse of what I assumed: screen-right goes down a position. Luigi is position 3, so Mario is three presses right.
- Arrows: both showed on Mario's card. The arrow code (0x8076E600, and 0x8076EA00 when arriving at an end) hard-codes position 1 as the first end.

What I learned:
- Verified (portable copy, scripted players 1 and 2; shots `build_nsmbw/shots/sel10_*`, `sel12_*`, `sel13_*`):
  - player 2 scrolls Luigi -> Yellow -> Blue -> Mario, with the portraits and x05 lives on each card;
  - with player 1 as Mario, Mario's card is greyed and confirming it is refused (logged);
  - with player 1 as Luigi, player 2 opens on Mario with only the left arrow, scrolls to Blue (both arrows) and back (left arrow only), confirms Mario, jumps in next to Luigi, and the map HUD shows Luigi x05 and Mario x05.
- The card picture value is the selector's only notion of a portrait (0 Blue, 1 Yellow, 2 Luigi, +3 greyed, 6 none); the card code has a function per character for the picture and lives.
- Not verified: player 2's map model as Mario; the custom-TPL portrait in the selector (only the disc fallback was on screen this time); USA with these overrides.

What's next:
- Stage 2: player 1's own selector after "1 player".
  - Log the 2-player game-setup path (script "2 players" with a `p2:` player).
  - Then make 1 player take it with slot 0 not pre-joined (`dInfo+0x384`).
- USA: `gen_version_tables.py E1`, compare the new functions' bytes, build and run.

## 2026-11-15 — A character select for 1 player

What I did:
- Stage 2 of the developer's request: choosing "1 player" on the Number of Players screen now opens the game's own Add/Drop screen ("1 Players") with a selector for player 1, offering all four characters. Code: `nsmbw_p1_character.cpp`. Design: docs/futureplans.md 7a.
- How game setup works, from state logs of both paths (scripted input):
  - "1" went StartMember -> LowBatteryCheck -> map.
  - "2" went StartMember -> ConnectionCheck, with the Add/Drop screen (`dNumberOfPeopleChange_c`) running one selector for the extra player.
  - StartMember's execute (0x80918B00) decides: it writes the join states `dInfo+0x384 = {3,0,0,0}`, and for an index > 0 sets the screen's count (`+1732`) and changes to ConnectionCheck.
  - A 2-player run with player 2 choosing Yellow Toad showed the choice survives into the game (map: Mario and Yellow Toad).
- Overrides (structural copies, `EDIT` lines, all only when `OnePlayerSelect`: scene 10, count 1, Mario in the selector):
  - 0x80918B00: "1 player" takes the multiplayer branch (count 1); player 1 starts not joined.
  - 0x807A0DB0: the slot -> remote table's entry for player 1 stays -1 instead of remote 0.
  - 0x807A11A0, 0x807A14D0, 0x807A1510: the loops that wire, start (`+665`), wait for (`+666`) and finish selectors run over `count` selectors instead of `count - 1`.
  Selector 1 is the card in the middle; it claims the first free connected remote at the start of its select state (0x8076FAE0), which is now player 1's.

What broke / what I didn't expect:
- First attempt (only StartMember): the screen opened with no selector, because it only ever starts selectors for *extra* players. I removed that override until the rest existed rather than leave a worse 1-player start.
- Then I started selector 0 (slot 0). It worked logically: logged card values showed position 0 -> Mario, then Blue after a scroll. But the card on screen stayed blank and didn't follow the scroll: selector 0's card is never placed, and the middle card belongs to selector 1. Switching the loops to selector 1 fixed it.
- Two flags I misread before checking: `+662` is "animating", not "chosen" (`+666` is chosen); 0x807A0D20 is "player 1 pressed back" in game setup (that branch is the cancel path that resets the table), not "done".
- Two more `
`-in-a-heredoc build breaks while adding log lines (C++ strings written through a shell heredoc lose their escapes); fixed with the editor.

What I learned:
- Verified (portable copy, scripted player 1; shots `build_nsmbw/shots/one8_*`, `one9_*`):
  - "1 Players" with player 1's card on Mario (x05, left arrow only);
  - left x3 -> Blue, Yellow (both arrows), Luigi (one arrow);
  - confirm (logged at position 3); Luigi jumps in; Start Game; the map HUD shows Luigi x05.
- USA: all 13 new overridden functions and tables are byte-identical at the same addresses in the USA REL; the USA DLL builds and links. Not run.
- Not verified: Back from the "1 Players" screen; a real controller; the title says "1 Players" (the game's own plural text, never meant for 1).

What's next:
- The developer tries it with a controller (F10 option, Add/Drop with two players, "1 player" select).
- The "1 Players" title; the Mario portrait image; committing.

## 2026-11-15 — Character select: fixes from the developer's play test

**What I did**
- Carousel order: Mario is now the leftmost card (positions 3, 2, 1, 0 = Mario, Luigi, Yellow, Blue; the position <-> character tables are rewritten with Mario on, and 0x8076FD70 / 0x807708E0 map through them instead of hard-coding position 0).
- Player 1 chooses with any player count in game setup, each card in its own column (0x807A0170 copy places selector 0 too).
- Voices: no Mario "here we go" on choosing "1" (0x807AC7A0); Mario's own join voice on his card (0x80771090); Start Game plays player 1's character's decide voice and VoiceEndWait waits on it (0x807A1B60, 0x80919130).
- Back on Start Game / Back re-opens the character select (0x807A1B60 flag + 0x80918D10 copy).
- Map reload after a character change waits on the game-stop word (0x8042A228) instead of the options menu's own state, and the request is cleared when a map player is built.
- Default portrait: made from the developer's original 64x64 Mario Kart Wii image, resampled to 150x150 without the AI upscale (developer request); the HD file is renamed to the new hash (`tex1_150x150_809770f85bf8b175_5.png`).

**What broke**
- Scripted 2-player runs played as 1 player: with the stop scene set to the map, the self-test's own presses picked "1" first. Scripts that need game setup must stop the self-test at scene 10.
- The Number of Players screen has "1" on top and 2/3/4 below, so "2" is a down press, not right.

**What I learned**
- A *structural copy* is our own C++ copy of a translated game function with one changed line (marked EDIT), registered at the function's address so the game calls ours.
- Card placement per player count is a 4x4 table (0x80935330: count 2 -> spots 7/8, 3 -> 4/5/6, 4 -> 0..3) into ten null panes at screen+1500.
- Sound names come from the disc's BRSAR: Mario JOIN 797/798, DECIDE 803/804; Luigi 869 / 875; Blue Toad (KO) 940 / 946; Yellow Toad (KO2) 1010 / 1016.

**What's next**
- Developer play test with sound; Free Mode / Coin Battle number-of-players screens with the "1" voice skip; map model after an F10 change from the real menu (checked here through the debug setter, which uses the same setter).

## 2026-11-15 — Player 1's character moves from F10 to Add/Drop Players

**What I did** (developer request: remove the F10 option, let the map's Add/Drop Players change player 1)
- Removed the "Player 1 character" row from F10 > Advanced (`nsmbw_options_menu.cpp`). The setter stays for `NSMBW_DEBUG_P1_CHARACTER` and the map pick.
- World map Add/Drop Players: player 1 now has its own card (left spot) running the same selector as game setup. Inside the screen player 1 starts not joined (0x807A0DB0 copy writes state 0 into the screen's copy only), selector 0 claims remote 0, and OK (0x807A1510 copy, 0x807A0D20) is taken only once player 1's card is confirmed.
- After OK the map reloads with the new model once the screen has closed.

**What broke**
- The first version reloaded the map as soon as player 1's card was confirmed, under the open screen: the selector writes `mPlayerType[0]` on confirm and the map's Add/Drop screen does not set the game-stop word. Fixed by also waiting until the screen's per-frame card placement (0x807A0170) has stopped for 30 frames (docs/issues.md).
- Test scripting: after a fresh game setup the map's opening swallowed early menu presses; the 2-player test used the second map visit (`3.2@...`).

**What I learned**
- On the map the recruit state ends only on player 1's OK; it never waits for choices (that wait is game-setup only), so the OK had to be gated on player 1's card.
- The map HUD lists players in character order, not slot order (Luigi above Blue Toad with player 1 as Blue Toad).

**Verified** (portable copy, scripted): 1 player picks Blue Toad on the map -> map reloads with Blue Toad's model and HUD; same with player 2 as Luigi in the game. **Not checked:** the 2-player Add/Drop screen itself on screen (only its result), dropping/adding other players in the same visit, a real controller, USA.

**What's next**
- Developer play test of the map pick.

## 2026-11-16 — Map reload after the Add/Drop pick: the fade starts on OK

**What I did** (developer: "it waits way too long", then "make the black fade start immediately" and "shouldn't even see the old model")
- Measured with VI ticks in the log: OK at tick 2282, reload requested at 2333 (51 frames), because the reload waited 30 frames after the game-stop word cleared and 30 after the Add/Drop screen stopped drawing. Cut both to 2 frames: 23 frames, the screen's exit animation.
- Screenshots 0.2 s apart then showed the old map (Mario's model, HUD sliding in) for ~0.6 s after the screen closed, then the wipe. My earlier "about one frame" was wrong: those screenshots were 1 s apart.
- Now the OK itself (0x807A1510 copy) requests the reload on that frame and starts the fade-out with `dFader_c::startFadeOut` (0x800B0A90). First with 20 frames; the developer then asked to try it without the fade, and a 1-frame fade-out (a cut to black on the OK frame; `nofade_*.png`: the screen mid-exit, then black, then the wipe in) is what they kept. `NSMBW_P1_OK_FADE_FRAMES=<n>` sets another length for A/B. Result (`build_nsmbw/shots/reloadt9_*.png`, 0.2 s apart): the screen's exit animation, at most one shot of the map, then black, then the wipe in with Blue Toad. The developer watched it and said it was good.

**What broke**
- `startFadeOut` returned 1 but nothing changed on screen. Logging the fader (`mFader_c::mFader`, 0x8042A720; status at +4) every 3 frames: it stayed in FADE_OUT (3) for 63 frames, then OPAQUE (0), then the new scene's FADE_IN (2). The game does not run the fader's calc while the map's menu is up.

**What I learned**
- The fix runs `dFader_c::calc` (0x800B0B00) from our per-frame tick on any frame the game did not (the fader's current frame, u16 at +12, unchanged since the last tick), only while our OK fade is running. Once the fader is OPAQUE, the scene change proceeds with it as it is (its own fade-out is not repeated).
- A *fader* is the game's full-screen transition object (fade, circle wipe, Mario wipe); one is current at a time, and a scene change fades it out, swaps scenes, and fades it back in.

**What's next**
- Developer play test.

## 2026-11-07 and 2026-11-14 — Aspect ratios, resolution presets, save backups (recorded 2026-11-18)

These were committed without a progress-log entry; the details are in the commit messages, docs/futureplans.md 1a/1b and docs/issues.md (2026-11-07 entries). Summary for the record:

**What I did**
- `31d39f4`: resolution presets named by render height (528p native, 720p ... 6K; the base frame is 640x528, so a preset of H rows is scale H/528); `graphics_api` honoured for NSMBW, with `auto` meaning Vulkan under Wine/Proton; a save backup per launch (`SaveBackups/`, 10 kept); a 16:9 / 4:3 row in F10, applied after a restart; the button-prompt style row.
- `98b406c`, `9dac290`, `30d53ab`, `48590fe`, `fddf2d4`: `[video] aspect_ratio` and an experimental 21:9 that widens the game's own view (the level view width at 0x8042D058 drives both the projection and the culling), with the HUD, fades and the world map's camera corrected.
- `8a7d19b`, `73e5935` (2026-11-14): 21:9 menu backgrounds widened to the screen edges (9 layouts), and the tile drawer draws enough columns that the ground no longer pops in at the right edge.

**What broke**
- The first 21:9 attempt only stretched the picture (same projection, wider viewport); measured from captures and withdrawn the same day (`30d53ab`) before the version that widens the view.
- 4:3 turned out to be the game's own letterboxed mode (3D viewport top=52 height=352), so the F10 row says so.

**What I learned**
- Not verified: the save-backup rotation past 10 files, Proton (no Linux test here), fractional render scales, 21:9 in later worlds, bosses and cutscenes.

**What's next**
- Covered by the v0.3.0 checklist (next entry).

## 2026-11-18 — Upstream #420/#421 reviewed, power-up freeze toggle investigated, version 0.3.0

**What I did**
- Version number set to 0.3.0: `runtime/cmake/NsmbwProduct.cmake` (exe version resource, `0,3,0,0` / `0.3.0`) and `package_nsmbw.ps1` (zip name). Nothing built from it yet.
- docs/futureplans.md: the triage table now says what was built since 2026-11-07 (items 1-4, 8, 9, 11), 0.2.3 is recorded as never released (its items ship in 0.3.0), and two new sections: 7b (freeze toggle) and 7c (upstream review).
- Read upstream WiiCompiled #420 and #421 (on `origin`, 172 commits past our merge base) for fixes that matter to NSMBW.
- Investigated the "item pickup lag" toggle promised in the v0.2.2 notes.

**What broke**
- Nothing; no code other than the two version strings changed. The translator was patched temporarily for the test below and the patch reverted (`git diff -- translator` is empty).

**What I learned**
- Upstream: `crorc` is computed wrong in our `ppc_isa_cr.h` too, but NSMBW's translated code never calls it. Translating the PAL DOL with upstream's GQR-loop and `nor.` fixes applied gave the same 16,187 files with no differences (both runs to the scratch folder; RELs not tried). The rest is runtime robustness (crash-safe save writes, fiber lifetimes) and a GX performance rework that would conflict with our GX changes. None of it is needed for 0.3.0; details and the order to port it in are in futureplans 7c.
- The freeze: `dAcPy_c::executeChangeInit` (0x80141080) calls `daPlBase_c::stopOther` (0x8004E050), which sets the actor execute-stop request, but only while bit 1 of `dInfo_c::mGameFlag` (0x8042A260) is set; with the bit clear the game already has a no-freeze path. An *execute-stop request* is a bit mask the game copies once per frame into `dActor_c::mExecStop`; every actor whose own mask overlaps it skips its update that frame. A structural-copy override of 0x80141080 can take the no-freeze path behind an option. It can't be done at `isPlayerGameStop` (0x8004E040) because the translator inlined that leaf into 2 shards, and that check also drives the pipe, door and goal-pole freezes. Estimate: about half a day with testing. Not built.

**What's next**
- Decide whether the freeze toggle goes into 0.3.0.
- The release checklist: rebuild the four DLLs from one commit, test PAL and USA (USA past World 1), the compat build on an older CPU or Intel SDE, the package from outside the repo, then the release notes and the `v0.3.0` tag.

## 2026-11-18 — Option: no power-up freeze (F10 > Advanced)

**What I did**
- `projects/nsmbw/native/nsmbw_power_up_freeze.cpp`: structural copies of `dAcPy_c::executeChangeInit` (0x80141080) and `executeChangeNormal` (0x80141460), generated by a script from `build/nsmbw/functions/func_<addr>.cpp`. With the option off, the first skips `stopOther` (0x8004E050) and the inlined `dAudio::pauseMove`, and both return 0 instead of `isPlayerGameStop()`. With it on (the default) they do what the originals do. Each changed line is marked `EDIT`.
- Setting `[gameplay] power_up_freeze` (`runtime_config.h`), `settings_overlay::settings::PowerUpFreeze()` / `SetPowerUpFreeze()`, and a "Power-up freeze" row at the top of F10's Advanced tab with the note "Off: the level keeps moving while Mario changes size". It applies from the next power change.
- Test hook `NSMBW_DEBUG_P1_POWERUP=<power-up>@<ticks>` (`nsmbw_tick_read_pump.cpp`): that many VI ticks into the first level after a world map, it writes player 1's pending power-up (`mPowerupCopy2`, +5344), as a pickup does, then logs `dActor_c::mExecStopReq` (0x80429FD8) and `mPowerup` (+4240) for 90 ticks.
- Shard manifests (PAL and USA) and `gen_version_tables.py E1` re-run; `va_codemod.py --check` and `--shared --check` report 0 files. Version 0.3.0 docs: README features, its Future Plans (USA and the AVX2 requirement were still listed as plans), futureplans item 16 / 7b.

**What broke**
- The first test run changed nothing: the save's Mario was already Super Mario, so a mushroom (1 -> 1) is not a change. Fire flower (2) after that.
- Planned as one override; reading `updateChange` showed `executeChangeNormal` also returns `isPlayerGameStop()` on every frame of the flashing, and a true return makes `executeMain` stop the player himself. So both functions are overridden.

**What I learned**
- Results (1-1, ~2 s in, self-test presses stop in the level; PAL from a portable copy of the dev build with a copy of the save, USA from `build_nsmbw-E1`'s portable folder, its Config.toml restored afterwards):

| Version | power_up_freeze | Frames with `mExecStopReq` = 0x0F | `mPowerup` after |
|---|---|---|---|
| PAL | true | 58 (+0 to ~+57) | 2 |
| PAL | false | 0 | 2 |
| USA | true | 58 | 2 |
| USA | false | 0 | 2 |

  So with the option on the game's own freeze runs unchanged (0x0F = every actor kind is told to skip its update, about 1 s), and with it off nothing is stopped and the change still completes. The word around the byte read `0x0F000F00` while frozen, i.e. the request and, two bytes later, the applied `mExecStop` copy.
- Screenshot of the F10 Advanced tab: the row, its On/Off buttons and the note are drawn, and five rows fit (the tab's limit; there is no room for a sixth).
- Not verified: by playing (a real ? block pickup, damage, mini mushroom, propeller/penguin, Yoshi), that enemies visibly keep moving (only the stop request is measured), and multiplayer (which already takes this path).

**What's next**
- Developer play test with the option off.

## 2026-11-18 — Options menu audit: Bloom removed, what the Advanced switches do

**What I did**
- Traced every options-menu row to the code that consumes its value. All are live for NSMBW except Bloom.
- Removed Bloom (issues.md, 2026-11-18): the row and setters, the Shift+F10 "Disable bloom" box, `[video] disabled_post_processing_paths`, `game_graphics_options.h`, and `ApplyRuntimeCallOptions`, which compared every call's target with Mario Kart's `ScnRenderer::createPath` (0x8023BD38, not a function in NSMBW). The shard emitter no longer emits that call in `MKW_STATIC_TRANSLATED_CALL`; shards re-emitted for PAL and USA, emitter tests pass (4/4), both dev builds rebuilt.
- Measured the three Advanced switches the developer saw no difference from (USA portable build, 4K, 180 fps; its Config.toml restored afterwards).

**What broke**
- Nothing. The Bloom row had already been invisible since 2026-11-07: "Aspect ratio" made Graphics 6 rows, and the menu draws 5.

**What I learned**
- *Copy filter* (on = the Wii's display-copy deflicker, a vertical blur meant for CRTs; off by default): two captures of the same paused frame (F10 open over the title, which freezes the scene) differ in 32% of pixels, but only 1.1% by more than 16 levels; the "on" frame is slightly softer vertically (visible on the flowers when zoomed). Real, and small at 4K, which matches not noticing it.
- *Audio mixing thread* (moves the AX voice mixing off the game thread): two alternating runs each (70 s self-test, title to map to level) gave the game thread 70.6% / 72.7% spare with it on and 74.0% / 69.7% with it off. The audio work left on the game thread drops from ~74 to ~60 ms per 2 s with it on, about 0.7% of the game thread, smaller than the run-to-run spread. No sound difference by design. On this PC it has no measurable effect.
- *Skip draws while compiling*: only acts while a shader is compiling for the first time (aurora `pipeline_cache.cpp`: a draw whose pipeline isn't built yet is skipped instead of waiting). With the seeded pipeline cache prewarmed at boot there is usually nothing compiling, so nothing changes. The 2026-10-16 cold-cache runs still showed 0.5 s stalls with it on, so even its intended case is unproven. Not measured today.

**What's next**
- Developer decision on the three switches (keep, relabel, or hide from the menu with their defaults kept).

## 2026-11-18 — Two Advanced switches hidden

**What I did**
- Removed "Skip draws while compiling" and "Audio mixing thread" from F10's Advanced tab (developer's decision after the audit above). Their settings are unchanged: both default on, both still read from Config.toml (`[video] skip_unready_pipelines`, `[audio] mix_worker`) and both stay in the Shift+F10 developer bar, so a player with an old config keeps whatever they set. Advanced now has Power-up freeze, Show FPS and Copy filter.
- README's feature line for Advanced updated.

**What broke**
- Nothing.

**What I learned**
- Nothing new; the reasons are the measurements in the previous entry.

**What's next**
- Commit with the rest of today's work after the developer's play test of the power-up freeze.

## 2026-11-19 — Developer report: death freeze, Add/Drop sends you to World 1, soft coins at 4K

**What I did**
- **Death freeze:** "Power-up freeze" off now also skips the freeze when a player dies while others are still alive. `nsmbw_death_freeze.cpp` is a structural copy of `dAcPy_c::stopOtherDownDemo` (0x8013DA30) that skips its `daPlBase_c::stopOther` call (0x8004E050) when the option is off. The last player's death (`m_7c` set by `initPlayerDownCommon` when `daPyMng_c::mNum` reaches 0) sets the stop request itself and ends the level; left alone. Built, **not run**: it needs a 2+ player death, which is hard to script; the developer will test.
- **Add/Drop -> World 1:** reproduced with a script (Select World to World 5 through `NSMBW_AUTO_PRESS_MAP_MINUS_TICK` + `NSMBW_TEST_INPUT`, then `NSMBW_DEBUG_P1_CHARACTER=1@2400`, which now takes a delay in map frames). The reload's parameter builder 0x80102B50 packs world = `dInfo_c`+0x3C, sub-map = +0x40, mode 5; those were 0/0 while `dScWMap_c::m_WorldNo` was 4, so it reloaded World 1. They hold the world of the last file load / course entry, not where the map is. Fix: copy `m_WorldNo`/`m_SceneNo` (0x8042A52D/E) into them just before the reload. After: the map reloads in World 5 with Luigi on Mario's node.
- **Soft coins at 4K:** the 2160p preset is 4.09x, so tile copies are 131x131, and since e9039fb a non-whole copy drops the tile atlas (0 `build atlas` lines in the log), leaving coins and ? blocks drawn from the 32x32 RAM write-back. Now such a copy is resampled on the GPU to the nearest whole scale (128) and the atlas uses it. Tile-slot dumps show 128x128 coins.
- The menu showing "Power-up freeze: On" after reopening: the developer said it was fixed before I found a cause; nothing changed for it.

**What broke**
- My first look at the coins blamed nothing (the screenshot looked fine at a glance); a 3x crop at native pixels showed the stair-steps, and the copy log's `gpu=131x131` gave the cause.

**What I learned**
- A *structural copy* override is a C++ copy of a function's translated body with one line changed, so every other store the original makes still happens (see the CLAUDE.md debugging notes).
- Non-whole render scales reach code that assumed whole ones: anything that divides a copy's size by its source size has to handle the remainder.

**What's next**
- Developer: test the death freeze with 2 players. Not checked: Add/Drop reload from a node other than a world's start, and from sub-maps; coins at full window size on screen.
- Still open from this report: ultrawide stretch on Select World, the character select and items screen models; geometry cut off on the World 3 map and the 3-2 pipe area; the World 4 Big Bertha camera jumping (likely the camera clamp at 0x8007A860..0x8007A8D4 when the zone is narrower than the 21:9 view: left limit > right limit).

## 2026-11-19 — 21:9: menu models, narrow-zone camera and edges; test warps

**What I did**
- **Menu 3D models at 21:9** (Select World islands, character select, items): they use a 16:9 orthographic camera (853x456) that the world-map perspective fix didn't reach. `AuroraSetModelOrthoXScale` narrows each model around its own origin while that camera is current. Verified on Select World.
- **Narrow zones** (World 3-3's pipe room, 576 units wide; the developer first called it 3-2): the camera sat at the room's right limit and the room's wall stopped with bare background on one side. Now `dBg_c::getLeftLimit` / `getRightLimit` are wrapped so the limits centre any zone narrower than the view (every camera mode then centres), and aurora scissors the level's tile draws to the zone so both sides show the level's own background. Verified in that room with 2 players; 1-1 unchanged.
- **Big Bertha camera jump** (World 4): `nsmbw_wide_camera.cpp` centres when 0x8007A6C0's clamp range is empty. The limits wrapper above should cover it too. Not played.
- **Test tools to reach a room by script:** `NSMBW_DEBUG_WARP`, `NSMBW_DEBUG_GOTO` (scene change the way a pipe does), `NSMBW_DEBUG_SWAP_AREAS`; 2-player setup through `NSMBW_TEST_INPUT` (CLAUDE.md).
- Also today (earlier entries in issues.md): death freeze follows "Power-up freeze", Add/Drop reload keeps the world, coins sharp at the 2160p scale.

**What broke**
- Several dead ends on the narrow-zone look, each tried and shown to the developer: repeat the edge column (stripes), mirror (copied the room's pipe), fill from the wall's own tiles (seams), black bars (wanted the background), a fade into the background (worked, but the clean edge was preferred; kept off in aurora).
- My first look at "3-2" was the wrong level (a World 3 map ambush room); the room came from the developer's correction to 3-3.
- Two aurora traps: the runtime's raw FIFO draws skip `handle_draw`, and the pipeline config is cached across draws (a texture change alone doesn't rebuild it), so per-draw decisions have to be made before the pipeline lookup.

**What I learned**
- A *scissor* is a rectangle the GPU won't draw outside; changing it per draw lets one class of draws (the tiles) be cut to the zone while others (background, HUD) draw full width.
- The level tile drawer draws from the zone's own tile buffer and wraps past its ends; the level files have almost nothing past a zone's edges.

**What's next**
- Developer: play the Bertha arena at 21:9, a 2-player death with "Power-up freeze" off, and a few other rooms narrower than 21:9.

## 2026-11-21 — World 2 sandfall black (palette objects); World 4 clouds investigated

**What I did**
- Reproduced both reports in the portable test copy (`build_nsmbw/portable_p1`) with the developer's completed save copied in (the old test save stopped at a "Save?" prompt after `NSMBW_DEBUG_MAP_WORLD`). World 2 scrolls vertically: walking up (`NSMBW_TEST_INPUT` screen-up pulses) then free-look reached the developer's view.
- **World 2 sandfall:** found the material in the disc files first (`mtSandTide`, CI8 `W2SandFall` mixed with `W2SandTide` by vertex alpha), then bisected with `NSMBW_DEBUG_TEXWHITE=256x128:9` (bands gone, so the palette texture is the black input). Logs showed the texture converted with the ground's 32-entry palette every frame, and the game never loading the sandfall's own palette. The translated NW4R palette loader (0x8024BAD0) explains it: it skips `GXLoadTlut` when a material's `GXTlutObj` bytes equal the ones last loaded, and our native `GXInitTlutObj` left them zero. It now writes them the way the game's 0x801C7660 does. Developer confirmed fixed.
- **World 4 clouds:** not a runtime bug. The opaque backdrop (`mtCloudB`, one continuous sheet) shows the colour of its texture even where alpha is 0, and the HD pack's replacement (`SMN/World/Clouds/tex1_256x80_5040f01a9e7ecc48_6.dds`) has blocky colour there. With texture replacement off the same view matches the developer's reference. Ruled out first: the indirect ripple, the blended layer (`mtCloudA`), vertex colours, the texture decode (`NSMBW_DEBUG_SKIP_DRAWS`, `NSMBW_DEBUG_TEXWHITE`).

**What broke**
- My first theory for World 2 (a copied palette object with zero bytes) was wrong in its route but right in its cause: the object was never copied; NW4R *compared* its bytes.
- I spent several W4 runs on the runtime before checking the HD pack: every test copy links the same pack folder. Check `texture_replacements = false` first when a texture looks wrong.
- Several logging runs missed the cloud draws: a frame on the W4 map has thousands of draws, and NW4R skips reloading an identical texture object, so the bind-armed log never saw `mtCloudA`. `NSMBW_LOG_DRAW_TEXGEN_TEX=256x80` (new) filters by texture size instead.

**What I learned**
- A palette (TLUT, "texture look-up table") texture stores an index per pixel; the palette turns indices into colours. A palette with too few entries makes every larger index transparent black in aurora.
- Bug class 3 can also bite through a *cache*: game code that compares an object's bytes to decide whether to reload it treats all unwritten objects as the same object.
- After any change that stops an inlined SDK copy (like 2026-11-05), the native version must mirror every store the guest version made, not only those another SDK function reads.

**What's next**
- World 4 clouds: done. The developer chose to drop that pack file for everyone: aurora's replacement index now skips it by name (`kIgnoredReplacements`), verified with the pack on. A repaired HD version could replace it later.
- Other `GXInitTlutObj` callers re-translated on 2026-11-05 (0x801826E0, 0x80245C80, 0x8029C960, 0x802B4D80) also get real bytes now; nothing else seen to change, not checked beyond the W2 map.

## 2026-11-22 — 21:9: camera zoom for zones narrower than the view (prototype)

**What I did**
- Surveyed the narrow zones again (2026-11-21, docs/futureplans.md 1 item 9): 152 zones, 114 distinct tile layouts. Widening them by hand is about 50-100 h; the developer chose to zoom the camera in for now and widen rooms later.
- Built `projects/nsmbw/native/nsmbw_narrow_zone_zoom.cpp`, an override of 0x80078EB0. That function returns the camera's zoom factor Z (from the zone's zoom type and ID); every user of it sets the visible size to base size × Z. After the original body, if base width × Z is wider than the current zone, Z becomes zone width / base width, so the view exactly fills the zone. It has 7 callers and is not a leaf, so the translator never inlines it and this one override sees every use. It applies only when the base width is above 16:9's 408.7.
- The zone width comes from the course data: `dScStage_c::mCurrFile` / `mCurrAreaNo` (+0x120E / +0x120F) → `dCd_c` file → `sAreaData` with that ID. dBg_c's zone limits (+0x90030 / +0x90034) can't be used, because they still read 0.0 when the zone-entry code (0x801549E0) asks for the zoom.
- Settings: `NSMBW_NARROW_ZONE_ZOOM=0` turns it off, `=<factor>` caps the zoom-in (default: no cap). `NSMBW_LOG_NARROW_ZONE_ZOOM=1` logs each change with both widths and the caller.
- Tested in a new portable copy (`build_nsmbw/portable_zoom`, `NSMBW_ASPECT=21:9`, `NSMBW_DEBUG_WARP` + `NSMBW_DEBUG_GOTO`):
  - **1-1 bonus room** (area 2 zone 1, 512 × 272): zoom 1.2143 → 0.9545 (×1.272). Visible area x 992-1504, exactly the room. The room fills the screen with its walls at both sides. Lost: the rock ceiling above the brick rows (visible y 266-480 of the room's 208-480, 58 units ≈ 3.6 tiles). The floor, Mario, all coins and the bricks stay visible.
  - **3-3 pipe room** (area 3 zone 2, 576 × 304): zoom 1.3571 → 1.0738 (×1.264). Fills the width. Lost: the row of icicles along the ceiling, and the top coin rows now sit at the frame edge under the HUD score.
  - **16:9**, same bonus room: no zoom log lines. View 496.3 × 272, unchanged (the room's full height).
  - Screenshots: `build_nsmbw/shots/zoom11_*.png`, `zoom11off_0.png`, `zoom33_1.png`, `zoom33off_0.png`, `zoom169_0.png`.

**What broke**
- The camera scrolls up in a fixed room when Mario climbs: confirmed by the developer playing it (2026-11-22), so the lost ceiling can still be seen by jumping. My scripted jumps had not shown it: the visible y stayed at 266 for 286 samples with Mario on the floor in every screenshot.
- My first two jump scripts never ran: `NSMBW_TEST_INPUT` counts a new visit only when the scene *profile* changes. A pipe-style `NSMBW_DEBUG_GOTO` is stage → stage, so the room after it is still the same visit, with frames counted from the course start.

**What I learned**
- The 16:9 view at zoom setting 4 is 496.3 units wide (Z = 1.2143), not the survey's estimate of 480. The narrow-zone survey's per-zoom widths are therefore a few percent low, and the zoom these rooms need is about 1.27×, not 1.23×.
- A "zoom factor" here multiplies the visible size: bigger Z shows more. Zooming in means making Z smaller.

**What's next**
- Developer: judge the look in a few rooms (1-1 bonus room, 3-3 pipe room, a tall shaft such as 3-5), and decide between full zoom and a capped zoom with background at the sides (`NSMBW_NARROW_ZONE_ZOOM=1.1`).
- Still untested: walking between two zones without a pipe (3-5's two shafts), multiplayer, and the Big Bertha arena.

## 2026-11-22 — 0.3.0 release candidate: four DLLs rebuilt, shader seed expanded, package tested

**What I did**
- Merged the pipeline caches from the developer's play sessions (`build_nsmbw\nsmbw_data` and its portable copies, `build_nsmbw-E1`, `build_compat`) into `projects/nsmbw/assets/pipeline/initial_pipeline_cache.db`: 1532 -> 1957 recipes (all config version 19, none dropped). `build_nsmbw\nsmbw_data`'s cache already held every row of the others.
- Checked VA wrapping (`va_codemod.py --check`, `--shared --check`: 0 files), re-ran `gen_version_tables.py E1`, regenerated both shard manifests, and rebuilt `build_release`, `build_release-E1`, `build_release_compat`, `build_release_compat-E1` (all within 4 minutes of each other).
- `package_nsmbw.ps1`: 32 files, 255 MB, no warnings.
- Tested the zip extracted to `C:\Users\ryanl\nsmbw_reltest\` (outside the repo, so the `runtime/assets` fallback could not hide a missing file), each copy portable, with the setup scripted from the developer's `.rvz` images (`NSMBW_SETUP_IMAGE`):
  - PAL: DolphinTool extraction, `[launcher] disc: Europe (PAL), revision 1`, seed merged (1957 rows, 0 skipped, prewarm 20.3 s), fresh save -> opening movie -> W1 map -> 1-1 on screen.
  - USA: same, to 1-1 (screenshot).
  - PAL and USA with `MKW_FORCE_UNSUPPORTED_CPU=1`: the process's loaded modules show `NSMBWCompiled-P1-compat.dll` / `NSMBWCompiled-E1-compat.dll`; both reached 1-1.

**What broke**
- The USA run sat on the World 1 map of its fresh save for about 5 minutes until the developer pressed confirm by hand. Not a game fault: the self-test presses 2 only twice on the map (ticks 3780 and 3900) and then stops. The compat runs added `NSMBW_TEST_INPUT=3.1@200+3000=100/60:8` and needed no help.

**What I learned**
- In `NSMBW_TEST_INPUT` the bits are Wii Remote buttons as held sideways: 2 (jump / confirm) is 0x100, and 0x1 is d-pad left, not a confirm.
- The launcher does not log which game DLL it loaded; the process's module list is the evidence.

**What's next**
- Developer playtest of the package (same day), all confirmed: the 21:9 zoom (full zoom kept; 3-5 between zones, multiplayer, Big Bertha, USA), power-up freeze (pickups, damage, mini mushroom, death), character select (title and map Add/Drop, real controllers), 180 FPS and Match display, save-backup rotation (10 kept), USA past World 1, shader stutter in later worlds, the World 2 sandfall / World 4 clouds / W1 map edge fixes, and the setup window by hand. Not confirmed: tilt direction on a tilting platform, and the sky above water against hardware (both stay in docs/issues.md's open list).
- README: Known issues gained the narrow-room zoom line; the USA line no longer says "tested through World 1". Release notes drafted in `dist/RELEASE_NOTES-v0.3.0.md` (not in the repo).
- Still open for the release: the `PLAYING.txt` Config.toml items, then the `v0.3.0` tag.

## 2026-11-22 — Native Linux launcher and AppImage

**What I did**

Contributed by RivierGrullon in PR ral1009/NSMBWiicompiled#7; the Linux testing below is theirs (Bazzite on a desktop and a Lenovo Legion Go). The developer has not run Linux. On Windows the PR was built separately (2026-11-23, portable copy) and still reached title, map and 1-1.

Built USA rev 1 with Clang 18 on Ubuntu 24.04. Imported a WBFS through the
packaged file picker and ran the AppImage on Bazzite with Vulkan. The title,
World 1 map and level 1-1 all rendered. Holding Shift reopened setup.
ISO and RVZ copies of the same disc produced the same main.dol.

**What broke**

A clean translation stopped at character selection with a missing call to
`0x80773220`. The card setter and the world map camera call at `0x809079B0`
both come from handwritten C++ and needed entries in the function map.

**What I learned**

Raising the main thread's stack limit only fixed part of the problem. libco
allocates each guest fiber separately. A 129-frame test reproduced the crash
with the old 64 KiB stack and passed with 16 MiB. NSMBW also needs to install
the page-fault handler itself because it has a separate entry point from MKW.

**What's next**

Test the PAL and x86-64-v2 game libraries on Linux and try the AppImage on
Steam Deck.

## 2026-11-23 — GitHub issue #10: setup deleted a disc image kept inside `disc\`

**What I did**
- Read the report (v0.3.0): the tester put `DISC.rvz` in `<game folder>\disc\`, chose "Disc image...", saw "Could not move the extracted disc into the game folder", and the image was gone.
- Read `ExtractDiscImage` in `nsmbw_launcher_setup.cpp`. After extracting into `disc_extracting\`, it ran `fs::remove_all(disc)` to clear any earlier extraction, then renamed the new one into place. The code assumed `disc\` holds only the launcher's own extraction.
- Reproduced in a portable copy (PAL `.rvz` at `disc
smbw.rvz`, `NSMBW_SETUP_IMAGE` pointing at it): the old build deleted the image; the new build kept it, extracted `sys` and `files` beside it, and the launcher accepted the disc and started the game.
- Fix: stop wiping or replacing `disc\`. Its entries are removed one at a time, skipping the picked image (compared with `fs::equivalent`/canonical path), a failed removal aborts with a message saying the image was untouched, and the extracted entries are then moved in.

**What broke**
- The reporter's "Could not move" error did not reproduce (the rename succeeded here), so that half of the report is unexplained. Likely, not proven: an Explorer window or antivirus holding the emptied directory so it could not be removed or replaced. The new code never removes the directory, so it avoids that case, but that was not tested.

**What I learned**
- A function that "owns" a folder must not delete it wholesale when the user can point it at a file inside that folder. `remove_all` also fails quietly unless its `error_code` is checked.
- Why it was not seen before: the developer's images are outside the game folder and the private package skips the picker.

**What's next**
- Reply on issue #10 (image is not recoverable; re-copy it and keep it outside `disc\`), then close it once released. README Known issues and `PLAYING.txt` (whose "extracted into a disc folder" wording is the likely reason the tester put the image there) now warn about v0.3.0 and earlier. Entry in docs/issues.md.

## 2026-11-25 — PR 11: choose the game language (tested and merged)

**What I did**
- Built and tested ElAmine01's draft PR ral1009/NSMBWiicompiled#11, written without a toolchain. It binds `SCGetLanguage` (0x801DD460), which always returned its English fallback because the runtime has no SYSCONF, to a `[game] language` setting limited to the language folders on the extracted disc, with an F10 > Advanced row.
- Checked its claims against the translated body (`func_801DD460.cpp`: IPL.LNG item 11 lookup, English fallback, not a leaf so no inlining) and the USA mapping (0x801DD320, same body).
- Ran the PR's checklist in portable copies: PAL map in French/German/Italian, Spanish title, French pause menu, Spanish course clear (via `NSMBW_DEBUG_GOTO=1:5` near the 1-1 goal), French and Spanish game over (new `NSMBW_DEBUG_P1_LIVES=1`), unknown and missing-on-disc languages, the F10 row through a restart, and the USA build in French and Spanish. File opens were confirmed with NTFS last-access times, since the DVD layer does not log them.
- Fixed two bugs in it: `auto` used the regional format instead of the display language; and the F10 row showed the `NSMBW_LANGUAGE` override, so presses were saved but the row did not move (the developer's report).

**What broke**
- A test disc made of directory junctions hung at the strap screen in every language; a real copy (342 MB) worked. Not investigated: a test-setup problem, not a game one.
- Synthetic Right presses with scan code 0 were ignored by the options menu: it reads SDL scancodes, which come from the scan code field. Pass the real one (0x4D for Right).

**What I learned**
- The language-specific files are only the fonts, `Message.arc`, `staffroll.bin` and the `corseClear` / `gameOver` / `wiiStrap` layouts. The French, German and Italian game-over layouts are the English one renamed, so "GAME OVER!" in English is correct there.
- A setting with a test override needs two readers: what the run uses (override first) and what the menu edits (saved value only).

**What's next**
- Unchecked: `auto` on a non-English Windows, the staff roll, and the USA pause / course-clear / game-over screens. Thank the contributor and close the PR as merged by hand (their fork is gone, so the branch could not be updated).

## 2026-11-26 — 0.3.1 scope; a broken Config.toml now says so

**What I did**
- Listed what changed since v0.3.0 (the tag is `c59d260`; the release zip was built from `8e0dab2`, only docs between): the issue #10 setup fix, the language option (PR 11), and the native Linux launcher (PR 7, Rivier Grullon). Decided 0.3.1 ships Windows only; the AppImage waits until there is a Linux build machine (WSL2 was considered: about 10-12 GB, and its Vulkan path is uncertain), and Linux players use Proton in the meantime.
- Fixed the first item on the 0.3.1 list: a Config.toml with a TOML syntax error was ignored with no message. The launcher now reads the file before anything else does and, if it does not parse, shows the parser's error with Yes (keep it as `Config.toml.bad`, start with defaults) or No (quit). Entry in docs/issues.md.
- Tested in a portable copy with `dvd_root = "C:\Games\NSMBW"`: No, Yes and a valid file all behaved as intended (details in the issues entry).

**What broke**
- My first test sent its "y" keypress and screenshot to whatever window was in front (the developer's Claude window), not the game's dialog. Answering a dialog by key press needs the dialog in front, which a test from another app can't guarantee. Fix: find the process's dialog window (`#32770`), read its text and image from that window (`GetDlgItemText`, `PrintWindow`) and post the button's command to it, which never touches the foreground.
- PowerShell variable names are case-insensitive: `$p` (the process) overwrote `$P` (the folder path) in the same script.

**What I learned**
- The open item about the template's `nand_root` example was a Mario Kart leftover: NSMBW's template only has `dvd_root`, in single quotes, where a backslash is literal. (TOML: in a "double-quoted" string `\` starts an escape such as `\n`; in a 'single-quoted' one it is just a character.)
- The runtime's config is a static inside a header, so the launcher exe and the game DLL each keep their own copy; anything that repairs the file has to happen before either reads it.

**What's next**
- Rest of 0.3.1: port the `crorc` fix from upstream WiiCompiled #420, the developer's test pass (language `auto`, USA screens, issue #10, the release package, a PAL regression run), release notes.
- The Linux launcher has no equivalent check.

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

Keep entries here as work happens. Written primarily by the developer, per CLAUDE.md's documentation policy — Claude should ask questions to help produce these, not draft them wholesale.

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

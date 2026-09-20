# NSMBWiicompiled

A native PC port of *New Super Mario Bros. Wii* (NSMBW) via **static recompilation** — the same technique [WiiCompiled](https://github.com/patchzyy/Wiicompiled) uses for Mario Kart Wii, applied to a different game. No emulator, no interpreter, no JIT: the game's PowerPC code is translated once, ahead of time, into real, compilable C++.

This is a student side project, built with AI-assistance, and doubles as a from-scratch education in C, PowerPC, and the recompilation toolchain. See [`CLAUDE.md`](CLAUDE.md) for the working norms and [`docs/MASTER_PLAN.md`](docs/MASTER_PLAN.md) for the full phase plan, learning curriculum, and dated progress log — that file is the single source of truth on scope and status; this README just summarizes it.

## Status: Phase 6 (runtime bring-up) — first correct 3D frame

As of 2026-09-19, the recompiled build renders its first correct 3D output: the title-screen backdrop (hills, bushes, clouds, fog) through the real GX → aurora pipeline. The intro cutscene also plays if idle on the title screen. Known-broken right now:

- Sky, ground tiles, Mario, the logo, and the "PRESS 2" prompt are missing/wrong on that same frame
- A 2D layout element (a blue rectangle) draws over the 3D scene instead of compositing with it
- Crashes in the middle of the intro cutscene when a large amount of penguin suits and propeller suits are spawned. 
- All the textures are wrong, with characters having just silhouettes. 

Earlier milestones, in order: guest game thread running (2026-09-01) → GX render-state + startup-overlay fixes (2026-09-04) → input (WPAD), scene creation, and the first 3D frame (2026-09-19). Full phase list and dated entries (what was tried, what broke, what was learned) are in `docs/MASTER_PLAN.md` §6.

**Not yet started:** real audio (Phase 8), a correctness-validation harness (Phase 9), and any compatibility pass across the 8 worlds (Phase 10).

## How this differs from decompilation

- **Recompilation is the deliverable.** WiiCompiled's translator pipeline (PowerPC decode → IR/SSA lift → C++ emit → native compile) produces the actual playable build.
- **[NSMBW-Decomp](https://github.com/NSMBW-Community/NSMBW-Decomp) is reference material, not the goal.** Its symbol maps and verified source are used as ground truth to make the recompilation correct; this project isn't trying to extend or finish that decomp itself.

## Repository layout

| Path | What it is |
| --- | --- |
| `translator/` | The game-agnostic PowerPC → C++ translator (decoder, IR/SSA lifter, emitter), from WiiCompiled |
| `runtime/` | Shared runtime: HLE implementations of Wii SDK calls (GX, OS, audio, input, DVD, etc.) plus NSMBW-specific product code (`runtime/src/product/nsmbw_product.cpp`) |
| `aurora-main/` | Vendored [aurora](https://github.com/encounter/aurora) — GX-to-modern-GPU (D3D12/Vulkan/OpenGL via Dawn/WebGPU) compatibility layer |
| `projects/nsmbw/` | NSMBW's manifest (`nsmbw.yml`), per-REL manifests, `function_map.txt` (real NSMBW addresses), and native overrides (`native/*.cpp`) where guest behavior needed a hand-written host implementation |
| `projects/mkwii/` | The original Mario Kart Wii project this was forked alongside, kept for reference |
| `docs/MASTER_PLAN.md` | Project scope, phase breakdown, learning curriculum, and the dated progress log |
| `Launcher/` | Build-automation scripts inherited from WiiCompiled (Windows/Linux); largely MKWii/Retro-Rewind-oriented, not yet adapted for a one-command NSMBW build |

## Building

There's no packaged one-command build for NSMBW yet. The working path, per `CLAUDE.md`:

```bash
cmake --build build_nsmbw --target NSMBWCompiled
```

Any time a `PPC_NATIVE_OVERRIDE` address is added under `projects/nsmbw/native/` (or an override file is removed), the build shard manifest must be regenerated first, or the build produces a `missing_target` crash at that address:

```bash
dotnet run --project translator/src/Translator.Cli -- emit-nsmbw-build-shards \
  --project projects/nsmbw/nsmbw.yml \
  --modules-file projects/nsmbw/modules.txt \
  --native-source-dir projects/nsmbw/native
```

Prerequisites (from `translator/README.md`): .NET 8 SDK, CMake ≥ 3.16, Ninja, Clang/LLVM (LLVM-MinGW targeting `x86-64-v3` is the tested path; MSVC is not).

**You need your own legally dumped PAL NSMBW disc.** Nothing here works without it, and no Nintendo assets or code are bundled — translation runs locally against your own disc image.

## Debugging notes worth knowing before touching this code

- The dominant bug class so far: an SDK function is natively overridden at *Mario Kart Wii's* address in the shared runtime, so it's silently dead for NSMBW — symptoms look like rendering/logic bugs, but the actual cause is that the function ran as translated code with no host-side effect. Check `projects/nsmbw/function_map.txt` for NSMBW's real addresses before suspecting the host implementation.
- A second class: the translator inlines small leaf functions into their callers, which bypasses a native override bound at that leaf's address. If a bound function's HLE never logs a call, check the generated shards for `inline leaf 0x<addr>`.
- Most diagnostics in this tree are env-var gated (`NSMBW_LOG_*`, `NSMBW_DUMP_*`, `NSMBW_GPU_PEEK*`, `NSMBW_TEST_TRIANGLE`, `NSMBW_AUTO_PRESS_SELFTEST`/`_STOP_SCENE`) and cost nothing when unset.

## License

Licensed under [GPL-3.0](LICENSE), inherited from WiiCompiled. Third-party components (aurora, and anything else bundled or linked) are listed with their own licenses in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). No Nintendo assets, code, or data of any kind are shipped here.

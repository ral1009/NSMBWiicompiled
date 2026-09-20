# CLAUDE.md — NSMBW Static Recompilation Project

## What this project is

Attempting to apply static recompilation (in the style of WiiCompiled, which does this for Mario Kart Wii) to New Super Mario Bros. Wii, using NSMBW-Decomp/NSMBW-Maps as reference material for symbols and verified source. See `docs/MASTER_PLAN.md` for the full project overview, phase breakdown, learning curriculum, and progress log — read it before making assumptions about scope or approach. That file is the single source of truth; don't create separate plan/curriculum docs elsewhere.

I (the developer) am learning C, PowerPC, and this whole toolchain largely *through* this project. I have limited programming background going in. This file exists to keep AI assistance from quietly outpacing my actual understanding.

## How to work with me on code

- Explain *why*, not just *what*. If you fix a bug or write a function, tell me the reasoning well enough that I could reproduce it myself next time, not just enough to make the immediate error go away.
- If something you're about to do requires a concept I probably haven't learned yet (see the curriculum doc), flag that briefly rather than silently handling it for me.
- Prefer smaller, explainable changes over large ones I can't reasonably review. If a change is big, break it down and walk through it in pieces.
- When something breaks, help me understand the failure (compiler error vs linker error vs logic error, which layer it's in) before jumping to the fix.

## Documentation policy

**Claude writes the documentation.** (Changed 2026-09-19 — the earlier policy had me writing every devlog entry and commit message myself; I've decided the project moves faster and the record stays more accurate if Claude does it, since Claude has the full evidence trail from each debugging session in front of it.)

What that means in practice:

- After meaningful work — a fix that changed observable behaviour, a root cause found, a milestone reached — update `docs/MASTER_PLAN.md`'s Progress Log with an entry in the existing template (What I did / What broke / What I learned / What's next). Write it so that I, or someone who wasn't in the session, can follow the reasoning: what was observed, what was ruled out and how, what the actual cause was, and what the fix does. Evidence over narrative.
- Write commit messages the same way: a short subject, then a body that explains *why*, names the root cause, and lists what was verified. Don't pad; don't claim more than was actually confirmed.
- Code comments explaining a non-obvious design decision (why an override exists, why a value is what it is, what fails without it) are welcome and should cite the evidence (an address, a log line, a disassembly detail) rather than restate the code.
- Keep `CLAUDE.md` itself current when the way we work changes.
- Still keep me in the loop: when a doc update goes beyond recording what happened — e.g. changing the phase plan, scope, or a stated decision — say so in chat rather than silently rewriting it.
- I still want to *understand* what's being written. If an entry relies on a concept I probably haven't met yet, add a one-line explanation of it in the entry rather than assuming it.

## General working style

- Terse, evidence-based feedback over encouragement or padding.
- If you're not sure whether something is correct, say so — don't present a guess as settled.
- Never declare something fixed or working without direct evidence (a screenshot, a log line, a before/after comparison). "It should work now" is not a result.
- When a screenshot or log is ambiguous, say what is actually visible in it rather than what you expected to see. I will check.
- I own a legally dumped copy of the relevant discs. Don't ask about this repeatedly; it's established.

## Repo layout and git

- This directory (`Wiicompiled/`) is the project repo: docs, `CLAUDE.md`, scaffolding. `MKWiicompiled/`, `NSMBW-Decomp/`, `NSMBW-Files/`, `NSMBW-Maps/` are separate clones / data and are gitignored here.
- `MKWiicompiled/` is its own git repo (the WiiCompiled fork with the NSMBW product). Its push target is the `ral1009` remote, never `origin` (that's upstream WiiCompiled). Its `README.md` is the public-facing status page (keep the status table current when a stage changes); `docs/MASTER_PLAN.md` is kept as an identical copy in both repos — edit the one here and copy it over, since only the fork is pushed.
- Build: `cmake --build MKWiicompiled/build_nsmbw --target NSMBWCompiled`. Any new `PPC_NATIVE_OVERRIDE` address under `MKWiicompiled/projects/nsmbw/native/` needs the shard manifest regenerated first (`dotnet run --project translator/src/Translator.Cli -- emit-nsmbw-build-shards --project projects/nsmbw/nsmbw.yml --modules-file projects/nsmbw/modules.txt --native-source-dir projects/nsmbw/native`, run from `MKWiicompiled/`) — forgetting it, including when an override file is *removed*, produces a `missing_target` crash at that address.
- A guest function that is only reached through a function pointer the translator can't see (e.g. the AX DSP-task callbacks) has to be added to `projects/nsmbw/function_map.txt` and the base translation re-run: `dotnet run --project translator/src/Translator.Cli -- translate-recursive 0x80004050 --project projects/nsmbw/nsmbw.yml --outdir build/nsmbw/functions --output-metadata build/nsmbw/base_translation_output.json --threads 8` (from `MKWiicompiled/`, ~20 s; the start address is the DOL entry point — seeding from 0x80004000 drops it), then regenerate shards and build. Symptom when missing: `missing_target - guest address 0x... was called but is not translated`.
- Run + screenshots: `projects/nsmbw/tools/run_nsmbw.ps1 -Tag <name> -Seconds 120 -ShotEvery 10 -EnvVars @('NSMBW_AUTO_PRESS_SELFTEST=1')` writes `build_nsmbw/nsmbw_<name>.err.log` and PNGs to `build_nsmbw/shots/`. Scene transitions appear as `createRoot(profile=0x..)` lines (profile IDs: 0 BOOT, 5 STAGE/title, 6 RESTART_CRSIN, 7 CRSIN, 8 MOVIE, 10 GAME_SETUP). If a build fails with "Permission denied" on the exe, a previous instance is still running (`Get-Process NSMBWCompiled | Stop-Process`).

## Debugging approach that has worked

- Bisect at boundaries, don't fix things blind. A hand-written test draw (`NSMBW_TEST_TRIANGLE`) through the real pipeline, plus the GPU peek captures at pass end / after resolve / after blit, located the first broken stage in one run after weeks of guessing at individual GX setters.
- The recurring bug class in this port: an SDK function is natively overridden at **Mario Kart Wii's** address in the shared runtime and therefore silently dead for NSMBW. Symptoms look like rendering/logic bugs; the cause is "this function ran as translated code and its side effect never reached the host". Check `projects/nsmbw/function_map.txt` (real NSMBW addresses) before suspecting the host implementation.
- A second recurring class: the translator inlines small leaf functions into their callers, so a native override at that leaf's address is bypassed at those call sites. If a bound function's HLE never logs a call, check the generated shards for `inline leaf 0x<addr>`. When that happens, handle the effect at the level the translator can't inline away (an SPR write, an MMIO register) instead — see `PPC_WriteSpr` for the locked-cache DMA and `dsp.cpp` for the AI DMA registers.
- A third class: a native override replaces an SDK function *including its guest-side bookkeeping*. If the guest's other (still translated) SDK code later reads a `__gx`/`__OSData` field that the replaced body would have written, it sees stale data. Found twice on 2026-09-20: `GXBeginDisplayList` (the host never knew recording started) and `GXLoadTexObj` (the guest's `__GXSetSUTexRegs` read texture sizes the override never cached). When binding an SDK function, read its translated body and mirror every store to guest globals that another guest function consumes.
- Most diagnostics in this tree are env-var gated (`NSMBW_LOG_*`, `NSMBW_DUMP_*`, `NSMBW_GPU_PEEK*`, `NSMBW_TEST_TRIANGLE`, `NSMBW_AUTO_PRESS_SELFTEST` / `_STOP_SCENE`). They cost nothing when unset and are worth keeping until the area they cover is stable. The most useful ones for "where is it stuck / what is it drawing": `NSMBW_LOG_STATE_CHANGES=1` (every scene state transition by name), `NSMBW_LOG_IDLE_THREADS=1` (thread states + sleeper call chain when the scheduler idles), `NSMBW_LOG_DRAW_TEXGEN=<profile>` (per-draw texture/texgen/TEV/blend/projection/SU-scale), `NSMBW_LOG_TEXFMT=<profile>` (GXLoadTexObj calls interleaved with raw draws), `NSMBW_DUMP_TEXTURES=<dir>` + `NSMBW_DUMP_TEXTURES_SCENE=<profile>` (decoded textures as PPM/PGM; `projects/nsmbw/tools/ppm2png.ps1 -Dir <dir>` converts them).

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
- `MKWiicompiled/` is its own git repo (the WiiCompiled fork with the NSMBW product). Its push target is the `ral1009` remote, never `origin` (that's upstream WiiCompiled).
- Build: `cmake --build MKWiicompiled/build_nsmbw --target NSMBWCompiled`. Any new `PPC_NATIVE_OVERRIDE` address under `MKWiicompiled/projects/nsmbw/native/` needs the shard manifest regenerated first (`dotnet run --project translator/src/Translator.Cli -- emit-nsmbw-build-shards --project projects/nsmbw/nsmbw.yml --modules-file projects/nsmbw/modules.txt --native-source-dir projects/nsmbw/native`, run from `MKWiicompiled/`) — forgetting it, including when an override file is *removed*, produces a `missing_target` crash at that address.

## Debugging approach that has worked

- Bisect at boundaries, don't fix things blind. A hand-written test draw (`NSMBW_TEST_TRIANGLE`) through the real pipeline, plus the GPU peek captures at pass end / after resolve / after blit, located the first broken stage in one run after weeks of guessing at individual GX setters.
- The recurring bug class in this port: an SDK function is natively overridden at **Mario Kart Wii's** address in the shared runtime and therefore silently dead for NSMBW. Symptoms look like rendering/logic bugs; the cause is "this function ran as translated code and its side effect never reached the host". Check `projects/nsmbw/function_map.txt` (real NSMBW addresses) before suspecting the host implementation.
- A second recurring class: the translator inlines small leaf functions into their callers, so a native override at that leaf's address is bypassed at those call sites. If a bound function's HLE never logs a call, check the generated shards for `inline leaf 0x<addr>`.
- Most diagnostics in this tree are env-var gated (`NSMBW_LOG_*`, `NSMBW_DUMP_*`, `NSMBW_GPU_PEEK*`, `NSMBW_TEST_TRIANGLE`, `NSMBW_AUTO_PRESS_SELFTEST` / `_STOP_SCENE`). They cost nothing when unset and are worth keeping until the area they cover is stable.

# CLAUDE.md — NSMBW Static Recompilation Project

## What this project is

Attempting to apply static recompilation (in the style of WiiCompiled, which does this for Mario Kart Wii) to New Super Mario Bros. Wii, using NSMBW-Decomp/NSMBW-Maps as reference material for symbols and verified source. See `docs/MASTER_PLAN.md` for the full project overview, phase breakdown, learning curriculum, and progress log — read it before making assumptions about scope or approach. That file is the single source of truth; don't create separate plan/curriculum docs elsewhere.

I (the developer) am learning C, PowerPC, and this whole toolchain largely *through* this project. I have limited programming background going in. This file exists to keep AI assistance from quietly outpacing my actual understanding.

## How to work with me on code

- Explain *why*, not just *what*. If you fix a bug or write a function, tell me the reasoning well enough that I could reproduce it myself next time, not just enough to make the immediate error go away.
- If something you're about to do requires a concept I probably haven't learned yet (see the curriculum doc), flag that briefly rather than silently handling it for me.
- Prefer smaller, explainable changes over large ones I can't reasonably review. If a change is big, break it down and walk through it in pieces.
- When something breaks, help me understand the failure (compiler error vs linker error vs logic error, which layer it's in) before jumping to the fix.

## Documentation policy — read this carefully

**Any documentation that gets committed or pushed (updates to `docs/MASTER_PLAN.md`, especially its Progress Log section, README updates, commit messages, code comments explaining design decisions) must be written primarily by me, not generated wholesale by you.**

This isn't a style preference — it's the actual point of the project. The devlog and documentation are meant to be evidence that I understand what happened, not a record of what an AI produced on my behalf. If you write it for me, it stops being true.

Concretely, when it's time to write something that will be committed:

- **Ask me first**, rather than drafting it and showing me the draft. Good questions: "What was the actual problem here?" / "Walk me through what you tried before this worked." / "How would you explain this to someone who hasn't seen the code?"
- Help me **structure and tighten** what I say — fixing grammar, suggesting organization, pointing out where I've skipped a step someone else would need — but the substance and explanation should come from me answering your questions, not from you inferring it from the diff and writing it up.
- If I'm clearly struggling to articulate something because I don't actually understand it yet, say so directly rather than papering over the gap with polished prose. That's a signal I need to go back and learn the thing, not a documentation problem to solve.
- It's fine for you to write a rough scaffold/outline of *what sections a doc needs* — it's not fine for you to fill in the technical explanations inside those sections on my behalf.
- Exception: purely mechanical stuff (a table of install commands, a checksum list, boilerplate license headers) doesn't need this treatment — the policy is about explanatory/technical writing, not everything with words in it.

## General working style

- Terse, evidence-based feedback over encouragement or padding.
- If you're not sure whether something is correct, say so — don't present a guess as settled.
- I own a legally dumped copy of the relevant discs. Don't ask about this repeatedly; it's established.

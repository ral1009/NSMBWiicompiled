# Contributing

Thanks for helping out. This is a static recompilation of New Super Mario Bros. Wii
for PC. Please read this before opening an issue or pull request.

## Never share game files

Do not attach, link to or paste disc images, ROMs, extracted game files (`.arc`,
`.brres`, `.rel`, `main.dol`, etc.), save files, or anything copied from the game.
Issues or PRs containing them will be deleted. You must provide your own legally
dumped copy of the game.

## Reporting a bug

Search the existing issues and the Known issues section of the README first.

Include:

- **Version**: the release number or the zip name (`NSMBWCompiled-<commit>.zip`).
- **What happened** and **what you expected**.
- **Steps to reproduce**: which world/level, what you pressed, and whether it
  happens every time.
- **Log file**: `nsmbw_data\NSMBWCompiled.log` next to the exe. Attach the whole
  file; don't paste a snippet.
- **Screenshot or short video** for anything visual.
- **System**: Windows version, GPU and driver version.
- **Settings** you changed from the defaults (resolution scale, frame rate,
  HD textures on/off), if any.

For a crash, say what you were doing just before it happened.

## Suggesting a feature

Open an issue describing what you want and why. Don't start on a large change
before it has been discussed, since it may not fit the project's direction.

## Pull requests

- Keep each PR to one change, and explain *why* in the description, not just what.
- Building: see `docs/BUILDING.md`.
- Test it in-game before submitting, and say what you tested (which levels,
  which settings).
- If you add or remove a native override under `projects/nsmbw/native/`,
  regenerate the shard manifest (see `docs/BUILDING.md`).
- If you fix a bug, add an entry to `docs/issues.md`
  (Symptom / Root cause / Fix / Scope).
- Do not include generated output (`build/`, `build_nsmbw/`, `generated_nsmbw/`)
  or any game data.

## AI-assisted code

Code written with AI tools (Copilot, ChatGPT, Claude, etc.) is accepted, as long
as you understand it. In the PR description you must be able to explain:

- **why** the change is needed: what was broken or missing, and how you know;
- **what** the change does, in your own words;
- **how you tested it** in-game.

If you can't answer questions about your own PR during review, it will be closed.
Don't submit unreviewed AI output or changes you haven't run.

## Conduct

Be respectful. Harassment, insults or personal attacks get removed and may get
you blocked from the project.

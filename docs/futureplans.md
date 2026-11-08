# Future plans

What we intend to build next and how. Nothing here is done yet. `docs/MASTER_PLAN.md` stays the record of phases and progress; this file is the to-do list for after the current release.

Written 2026-11-07. Items are in rough priority order. "Check first" lists what must be found out before the work can be sized.

## 1. More aspect ratios (target: v0.3.0)

**Goal:** 16:10 (Steam Deck), 4:3 and 21:9 ultrawide, on top of today's 16:9. This was the most-requested feature, and v0.3.0 has been promised.

**How:**
1. Add an aspect-ratio setting to the F10 menu. The game already asks `SCGetAspectRatio` whether it is in 4:3 or 16:9 mode, and we bind that function, so 4:3 may mostly be a matter of returning the other value. *Check first:* that the game's 4:3 mode renders correctly at higher render scales.
2. For wider-than-16:9 screens, make the camera show more of the level. The game culls objects it thinks are off screen, so a wider view will show things popping in at the edges until the culling width is widened too. *Check first:* where the camera and culling read the view width.
3. Fix the HUD and pause/map layouts so they anchor to the screen edges instead of a fixed 16:9 frame. `runtime/src/product/nsmbw_layout_patch.cpp` already edits the game's layouts and is the tool for this.
4. In the F10 menu, show the pixel height next to each resolution multiplier. The base is 480p, so 1.5x is 720p, 2x is 960p and 3x is 1440p. Today the numbers are confusing.

**Testing without an ultrawide monitor:** make a custom resolution with CRU (for example 1920x824 on a 1080p screen) and set GPU scaling to "preserve aspect ratio". A Reddit user with a 3440x1440 monitor also offered to test; the developer said they would DM them.

## 2. Lower CPU requirements (no AVX2)

**Why it fails today:** `runtime/cmake/NsmbwProduct.cmake` (line ~155) builds the game with `-march=x86-64-v3`, which means AVX2, FMA and BMI2. The comment there says it is needed because `runtime/include/isa/ppc_isa_float.h` and `ppc_isa_quantized.h` use intrinsics (`_mm_fmadd_ps`, `_mm_shuffle_epi8`) that need those features to compile.

**How:**
1. Rewrite those helpers so they have a plain-SSE2 version (fused multiply-add becomes a multiply plus an add).
2. Build with `-march=x86-64` and compare behaviour against the current build. FMA rounds once and the replacement rounds twice, so results can differ in the last bit; check for visible or physics differences.
3. Decide between one slower baseline build for everyone, or two builds (or a run-time switch) so fast CPUs keep the fast path. Measure the speed cost first.
4. Add a start-up check that says "this CPU is not supported" instead of crashing with an illegal-instruction error.

*Check first:* whether Dawn or aurora (the graphics layers) have their own CPU requirement.

**Not planned:** a DX8 or OpenGL fallback for pre-Vulkan GPUs. Very old hardware was suggested as a minimum-spec test bench; the missing Vulkan makes that not worth the work.

## 3. Support for the other game versions

**Goal:** stop requiring the PAL revision 1 disc (SMNP01). Others: PAL revision 0, NTSC-U (SMNE), NTSC-J (SMNJ), Korean (SMNK). Several people said a PAL rev 1 dump is hard to find.

**Why it is big:** every version has different code. Every entry in `projects/nsmbw/function_map.txt`, every `PPC_NATIVE_OVERRIDE` address, every guest-memory constant and every shader or texture-name hash that depends on an address is specific to one version.

**How:**
1. *Check first:* does NSMBW-Decomp / NSMBW-Maps hold symbol maps for the other versions? Measure how many of our addresses actually differ.
2. Replace hard-coded addresses in the override files with a table per version, picked from the disc ID at start-up. Do this refactor once, not once per version.
3. Translate each version's DOL and RELs with the existing pipeline. Decide whether one exe carries several translated versions or each version gets its own exe.
4. Test each version with the self-test (`NSMBW_AUTO_PRESS_SELFTEST`) from boot to a level.

## 4. Save carry-over

Two separate things:

- **Between game versions.** The save is `NAND\title\00010004\534d4e50\wiimj2d.sav` (`534d4e50` is the text `SMNP`). Other regions use `SMNE`, `SMNJ`, `SMNK` in that folder name. *Check first:* whether the save layout and checksum are the same across regions (the decomp's save code should say). If so, add an "import save from another version" button that copies the file into the right folder.
- **Between releases of this port.** The game's own save format should not change, but our files (`Config.toml`, `nsmbw_controls.ini`, the options-menu settings) can. Give each a version number and migrate old files instead of resetting them. Keep a few rotating backups of `wiimj2d.sav` so a crash mid-save cannot lose a playthrough.

## 5. Linux and Steam Deck

A user ran the Windows exe under Proton and reported two problems (not yet reproduced; the developer has not tried Linux):

- The renderer setting `auto` chooses DX12, which draws nothing under Proton. Setting it to Vulkan fixed it. **Fix:** default to Vulkan when running under Wine/Proton.
- `nand_root` had to be set by hand in the config. **Fix:** give it an automatic default, like the disc path.

After those, a native Linux build is a separate project: check how much of the runtime and the PowerShell scripts is Windows-only. Android is in scope later and depends on the same work.

## 6. Mods

- **Riivolution support.** Riivolution patch sets overlay replacement files onto the disc and apply memory patches. Needed for most mods, including Newer Super Mario Bros. Wii. Dolphin HD texture packs are the only mod type supported today.
- **Replacement files from a folder** (`.arc` archives, textures). The HD-pack loader and the generated-files hook (`dvd_generated_files.h`) already do most of this.
- **Newer SMBW** is harder: its code arrives through memory patches and a loader, so it needs a separate build that patches the DOL and RELs *before* translation (see MASTER_PLAN Phase 12 item 6).

## 7. Smaller features

- **Swappable player 1 character.** `NSMBW_DEBUG_P1_CHARACTER=<0..3>` already forces it on the world map, so the game accepts the change. Make it an F10 option and check that it survives level loads, saves and the results screen. Decide whether it is saved per game file or is just a setting.
- **Confirm/cancel swap preset.** Rebinding already covers it; a one-click preset is only convenience.
- **Gamepad extras.** Gamepads work and are rebindable. Rumble and gyro-as-tilt are not done.
- **Regression tests.** Script the self-test and compare state-change logs and screenshots per build, to catch the "SDK function overridden at Mario Kart Wii's address" bug class early.
- **Installer, auto-update, crash reporter** that saves the log.

## 8. Performance and known issues to investigate

- **180 fps interpolation falls to about 40 fps on a fast PC** (RTX 5080, Ryzen 7 7800X3D). One user report, not reproduced. That is much worse than the Iris Xe laptop, so it is probably not simply "the PC is too slow". When their details arrive, run with `NSMBW_LOG_INTERP=1` and `AURORA_LOG_WORKER_TIMING=1` at the same settings and see where the time goes.
- **Claims about performance need numbers.** The recorded Iris Xe results (2026-10-31) are 1x and 1.5x at 180 fps, 2x at 120 fps, 4x at 60 fps. There is no recorded Dolphin comparison. Measure Dolphin and this port on the same machine and settings before stating one is faster, then put the table in the README.
- **8x with 180 fps interpolation** is deliberately blocked because it ran badly (MASTER_PLAN, 2026-10-31).

## 9. Parked

- **Online multiplayer** (a self-hosted server was suggested). Needs channels 1-3 and verified co-op first, plus a determinism check across machines (hash guest RAM each frame). Rollback netcode would also need save and restore of guest RAM, thread state and emulated-hardware state. See MASTER_PLAN Phase 12 item 6.
- **PS5 and other current consoles.** Not pursued.

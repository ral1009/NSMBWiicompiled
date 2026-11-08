# Future plans

What we intend to build next and how. Nothing here is done yet. `docs/MASTER_PLAN.md` stays the record of phases and progress; this file is the to-do list for after the current release.

Written 2026-11-07. Items are in rough priority order. "Check first" lists what must be found out before the work can be sized.

## Triage: how likely each item is to work, and which release

Ranked by plausibility (how sure we are it can be done with the code we have), then sorted by size of work. Based on reading the repo on 2026-11-07; nothing was built or run, so "verified" below means read in source, not tested.

| # | Item | Plausibility | Work | Release |
|---|---|---|---|---|
| 1 | Resolution labels show pixel height | Very high | Tiny: edit the label list | **0.2.3** |
| 2 | Linux/Proton: default to Vulkan, automatic `nand_root` | High (cause of bug 1 not yet proven) | Small | **0.2.3** |
| 3 | Rotating backups of the save file | Very high | Small | **0.2.3** |
| 4 | 4:3 mode as an F10 option | High: the game and our code already support it | Small to medium | **0.3.0** |
| 5 | Swappable player 1 character | High: switch already works on the map | Medium (saves, results screen) | **0.3.0** |
| 6 | Riivolution file mods | Medium-high: loader already in the shared runtime | Medium (wire into NSMBW, test with a real pack) | **0.3.0** |
| 7 | No-AVX2 build | Medium: only 7 call sites need rewriting, but cannot be tested on a real old CPU here | Medium | **0.3.0** (as a separate build) |
| 8 | 16:10 and ultrawide (wider view) | Medium: the MKW code in this repo is a worked example; NSMBW's HUD and culling are the unknowns | Large | **0.3.0** for 16:10; ultrawide in 0.3.0 only if the spike below works, otherwise 0.4.0 |
| 9 | 180 fps drop to ~40 fps on a fast PC | Unknown: needs the reporter's details | Unknown | **0.2.3** if the cause turns out small; otherwise 0.3.0 |
| 10 | Rumble and gyro tilt | Medium-low: rumble goes through the Wii Remote motor calls, which are not bound | Medium | later (0.4.0) |
| 11 | Other game versions (NTSC-U first) | Medium: an address map exists, but each version needs its own disc to test | Very large | later (0.4.0 or after) |
| 12 | Save carry-over between regions | Unverified: save format not yet read | Small once item 11 exists | later, with item 11 |
| 13 | Newer SMBW (code mods) | Low-medium | Very large | later |
| 14 | Android, online play, native Linux build | Low | Very large | later |
| 15 | Regression tests, installer, auto-update | Medium | Medium each | later |

### What the repo investigation found

- **CPU check already exists.** `runtime/src/host_cpu_baseline.cpp` runs before anything else and shows a message naming the missing instruction sets. The plan's "add a start-up check" step is already done; if we lower the requirement, update its list and message.
- **AVX2 is a smaller problem than it looked.** The two instruction-set headers use only a handful of non-baseline instructions: `_mm_fmadd_ps` / `_mm_fmsub_ps` (FMA, 6 uses) and `_mm_shuffle_epi8` (SSSE3, 1 use) in `ppc_isa_float.h` and `ppc_isa_quantized.h`. Everything else in them is plain SSE2. The audio mixer (`runtime/src/hle/audio/ax_mix_kernels.h`) already has a non-AVX2 path behind `MKW_AX_MIX_AVX2`. The rest of the requirement is the `-march=x86-64-v3` flag itself, which lets the compiler use AVX2/BMI for all translated code. The cost of dropping it (slower translated code) can only be measured, not predicted.
- **4:3 already exists in the game.** NSMBW has native 4:3 and 16:9 modes (`EGG::Screen::TV_MODE_16_9`, `dGameCom::GetAspectRatio`, per the decomp). Our override of `SCGetAspectRatio` (`projects/nsmbw/native/nsmbw_sc_aspect_ratio.cpp`) already answers from the `widescreen` setting in Config.toml, and `nsmbw_product.cpp` locks the window to 16:9 when it is on. What is missing is an F10 option for it. Layouts and the wipe fader read the ratio when they are created, so a switch probably only takes effect on the next scene; not yet tested.
- **The MKW project solved wider-than-16:9 already, in code we can reuse as a pattern.** `runtime/src/dynamic_aspect.cpp` and `runtime/src/hle/gx/gx_dynamic_aspect.h` rewrite the game's screen-size records so the view gets wider (Hor+) or taller (Vert+). NSMBW has the same kind of table (`EGG::Screen::sTVModeInfo` at 0x80350820 in `function_map.txt`). It is not a copy-paste: the HUD code in the decomp scales by fixed 4:3 and 16:9 widths (`d_2d.cpp`, `m_2d.cpp`, `m_wipe_fader.cpp`), and the game's off-screen culling (for example `chkOffScreen`) assumes the 16:9 width.
- **The shared runtime already contains a Riivolution loader** (`runtime/src/hle/storage/riivolution.cpp`, hooked into `dvd.cpp` and `nand_fs.cpp`), written for Mario Kart Wii. It handles disc file overlays and redirecting the savegame. It does not apply Riivolution memory patches, which Newer needs. I did not check whether NSMBW's disc-file path goes through the same hooks.
- **Other versions have an address map.** `NSMBW-Maps/address-map.txt` maps PAL v1 addresses to NTSC-U v1/v2 (E1, E2), Japan v1/v2 (J1, J2), PAL v2 (P2), Korea (K), Taiwan (W) and China (C). For NTSC-U v1 the shift is a constant that changes only a few times through the code, so remapping our `function_map.txt` and override addresses looks mechanical. The hard parts are the roughly 70 native override files (some are copies of translated code that differs per version), and that each version has to be tested against a real disc dump of it. The decomp and symbol files cover PAL v1 only.
- **Rumble is not wired for the Wii Remote.** `pad.cpp` sends rumble to GameCube pads, but nothing in `projects/nsmbw/` binds the Wii Remote motor calls.
- **Not checked:** how the save file is laid out (so region compatibility is unknown), why `nand_root` was not picked automatically under Proton (`DiscoverNandRootPath` exists in the runtime), and anything about the 180 fps report.

### Suggested order of work

1. **0.2.3 (a day or two):** items 1-3, plus item 9 if the details arrive and the cause is simple. All are small and easy to check on this PC, apart from Linux, which needs the reporter to retest.
2. **0.3.0:** start with a time-boxed spike for item 8: apply the wider screen record, drive the window to 21:9 with the CRU trick, and record what breaks (HUD position, pop-in at the edges, fades). That result decides whether ultrawide ships in 0.3.0. Do items 4-7 alongside; the 4:3 option (item 4) also proves out the "aspect read at scene creation" question that item 8 depends on.
3. **After 0.3.0:** item 11 first for NTSC-U, because it also unlocks item 12 and the largest audience. Needs a US disc to test; ask the community for a tester, since the repo cannot be verified without one.

## 1a. 4:3 progress (2026-11-07, uncommitted)

- **Built:** an "Aspect ratio" row (16:9 / 4:3) in the F10 Display tab, a `SetWidescreen` config setter, and `SCGetAspectRatio_HLE` (`runtime/src/hle/sc.cpp`) now fixes its answer at first call, because the game asks for the ratio at boot and again as it builds objects (`m2d::Simple_c`, the faders), so a mid-session change would mix the two. The row says "Applies after a restart" when the saved choice differs from the running one. A 4:3 `VILockAspectRatio` was added in `nsmbw_product.cpp` to match the 16:9 one.
- **Seen (portable copy, `widescreen = false`, title screen):** the game switches to its 4:3 layout (the logo and "Press Z to Start" are laid out differently), so the game side works.
- **Still wrong:** the picture stays in the same 16:9-shaped box (about 640x370 inside a 640x480 window, black above and below), so 4:3 content is stretched wide. `NSMBW_LOG_PRESENT_SEQ=1` shows the final present covers the whole 640x480 window, so the shape is decided before the presenter, probably in which region of the 640x528 render buffer is copied out (the game's 608x456 picture vs. the 832-wide anamorphic 16:9 one). Cause not found yet. Measured later the same day: the game's canvas table (`EGG::Screen::sTVModeInfo`, 0x80350820 in the PAL DOL) is 608x456 for 4:3 and 832x456 for 16:9, and both modes render into the same 640x456 EFB region and display-copy it identically (`NSMBW_LOG_COPYDISP_GEOM`: src 640x456, dst 640x480). The 16:9 lock gives a present viewport of 640x360 and a 360-row picture on screen (rows 91-450 of the capture). In 4:3 the present viewport is the full 640x480, yet the capture still shows a 371-row picture (rows 85-455) with black above and below. Those two facts do not agree, so either the bars are drawn into the frame before the present (a game-drawn letterbox, or a region of the render buffer that is not picture) or the capture is not showing the viewport. Next step: dump the XFB / presentation image (`NSMBW_GPU_PEEK*`) in 4:3 and see whether the bars are already in it. **Cause found (later the same day):** the bars are the game's own letterbox, not a presentation bug. `NSMBW_LOG_VIEWPORT=1` shows that in 16:9 mode the game sets its 3D viewport to the full 640x456 (top 0), and in 4:3 mode to `top=52 height=352` (a 640x352 strip, 77% of the height) while 2D layers still use 640x456. So NSMBW's own 4:3 mode is a 16:9-ish view letterboxed with 52 rows of black above and below, which is why a CRT user would call the game 16:9-only. Our output matches it (the 371-row picture is the 352-row strip plus the scaling). The developer's 2000x1125 screenshot (picture 1500x868, black on all four sides) is the same thing: a 4:3 box with the game's letterbox inside it. So the 4:3 option as built is faithful but not what a CRT user wants. Real full-screen 4:3 means changing the game's 4:3 view: use the full 640x456 viewport and give the 3D camera a taller projection (Vert+: more vertical level visible), the same idea as MKW's `dynamic_aspect.cpp`. Next step: find where the game computes the 352 (`d_SmallScoreManager.cpp` has `c_SCISSOR_SIZE_Y = 352`, so score popups are clipped to the same strip and would need the same change) and what draws the bars. Do not ship the 4:3 row as 'full screen 4:3' until that is done; shipping it as 'original 4:3 (letterboxed)' would be accurate.

## 1b. Ultrawide: first attempt stretched the picture (2026-11-07)

**Correction of an earlier version of this section:** it said 21:9 worked and showed "more sky and ground on both sides". That was wrong. The goal is the original game's look in a wider frame, with more of the level visible and nothing stretched. What the first attempt did was move the HUD to the wider edges and stretch the 3D world sideways.

- **What was tried:** the game's 16:9 canvas width (`EGG::Screen::sTVModeInfo`, 0x80350820 + 12) was rewritten (832 -> 1092 for 21:9) and the presenter locked to 21:9.
- **How the stretch was found:** a 16:9 and a 21:9 capture of the start of 1-1. The castle takes the same share of the picture height (about 43%) in both but 32% more of the width at 21:9 (27% -> 36%), which is almost exactly 21:9 / 16:9. So no extra world is visible.
- **Why (measured, `NSMBW_LOG_DRAW_TEXGEN`):** the level is drawn with an orthographic projection, `proj[0][0]` = 0.0040 (a view 500 units wide) and `proj[1][1]` = 0.0074, identical at 16:9 and at 21:9. Only the viewport changed: 640x456 became 840x456 (= 640 x 1092 / 832), wider than the 640-wide render buffer. The HUD draws at 640x456 in both. So the canvas table moves the 2D layouts but does not change how much world the game's camera shows.
- **Status:** 21:9, 16:10 and any other ratio are disabled in the F10 menu and in Config.toml (they fall back to 16:9) unless `NSMBW_EXPERIMENTAL_ASPECT=1`. Only 16:9 and the game's own letterboxed 4:3 are offered. The `aspect_ratio` setting, `RunningAspect`, `ApplyAspectSetting` and the present lock stay, since they are what a working version will build on.
- **What a correct Hor+ needs:**
  1. The world camera's horizontal extent must grow by the ratio (500 -> about 656 units at 21:9) with the vertical extent unchanged. Where the game computes the 500 is not found yet; it did not follow the canvas table.
  2. The game's culling must see the same wider extent, or objects in the new side bands will be missing. A GX-level shortcut (scale the projection's x term and clamp the viewport to 640) would widen only what is drawn, not what is culled, so it is not enough on its own.
  3. Fades and wipes (`m_wipe_fader.cpp`, `m_color_fader.cpp`) use fixed 16:9 widths and need the same treatment.
- **Same idea for full-screen 4:3 (later):** the 4:3 letterbox is the game's camera viewport (`top=52 height=352`), set by its scene code (`ScnRoot` -> camera viewport -> `GXSetViewport`). Full-screen 4:3 means more vertical view (Vert+) rather than stretching, with the same culling question.
- **Exact camera numbers (2026-11-07, `%.7g` projection log):** the level view is exactly 272 units tall (`proj[1][1]` = 2/272) and 272 x 832/456 = 496.28 wide (`proj[0][0]` = 0.004029955), so the width is the height times the 16:9 canvas aspect.
- **Where it is not:** widening the canvas table, the DOL's three small-data 832.0 floats (0x8042C294, 0x8042C440, 0x8042CF94) and d_basesNP's 832.0 (next to 456 / 0.3 / 2750) all left the level projection unchanged. A RAM scan (MEM1 + MEM2) during the level found no stored edges (+-248.14 / +-136) and no stored copy of the matrix, so the game appears to build the projection in a temporary each frame. Still unknown where its 832/456 comes from.
- **A working shortcut, with one problem (`AURORA_WIDE_HORPLUS=1`, experiment):** in `aurora-main/lib/gx/command_processor.cpp`, a viewport wider than 640 (840 at 21:9) is squeezed back to 640 and every projection's x row is scaled by the same factor. Result at 21:9 (captures `hp_*`):
  - World map: more of the map on both sides, nothing stretched (round mushrooms stay round).
  - Level 1-1: more of the level on both sides (the next ? blocks, the blue pipe, the slope), castle back to its 16:9 proportions.
  - **But the ground is missing in the new band at the bottom-left**: the game only draws the tiles it thinks are on screen, using the 16:9 width. That is the culling problem predicted above, now seen. Fixing it means widening the range the tile renderer (and actor culling) treat as visible.
- **Next:** find the tile renderer's visible-range calculation (dBg) and widen it by the same factor; then check actors and effects popping in at the edges while scrolling; then fades and wipes.

## 1. More aspect ratios (target: v0.3.0)

**Goal:** 16:10 (Steam Deck), 4:3 and 21:9 ultrawide, on top of today's 16:9. This was the most-requested feature, and v0.3.0 has been promised.

**How:**
1. Add an aspect-ratio setting to the F10 menu. The `widescreen` setting in Config.toml already switches `SCGetAspectRatio` between 4:3 and 16:9, so this is mostly a menu entry. *Check first:* that the game's 4:3 mode renders correctly at higher render scales, and whether it needs a scene change or a restart to take effect.
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
4. The start-up check that says "this CPU is not supported" already exists (`runtime/src/host_cpu_baseline.cpp`). Update its list of required features and its message to match whatever the new baseline is.

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

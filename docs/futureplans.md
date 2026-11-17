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
| 5 | Swappable player 1 character | Built 2026-11-15: Mario in the Add/Drop selector (game setup and the world map; the F10 option was removed the same day), character select after "1 player" (section 7a) | Small now: test with real controllers, the "1 Players" title, pick the portrait | **0.3.0** |
| 6 | Riivolution file mods | Medium-high: loader already in the shared runtime | Medium (wire into NSMBW, test with a real pack) | **0.3.0** |
| 7 | No-AVX2 build | Built 2026-11-14; packaged 2026-11-15 as compat DLLs for PAL and USA, chosen by the launcher; not yet run on a pre-AVX2 CPU | Small now: test | **0.3.0** |
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
- **AVX2 is a smaller problem than it looked** (done 2026-11-14, see section 2). The two instruction-set headers use only a handful of non-baseline instructions: `_mm_fmadd_ps` / `_mm_fmsub_ps` (FMA, 6 uses) and `_mm_shuffle_epi8` (SSSE3, 1 use) in `ppc_isa_float.h` and `ppc_isa_quantized.h`. Everything else in them is plain SSE2. The audio mixer (`runtime/src/hle/audio/ax_mix_kernels.h`) already has a non-AVX2 path behind `MKW_AX_MIX_AVX2`. The rest of the requirement is the `-march=x86-64-v3` flag itself, which lets the compiler use AVX2/BMI for all translated code. The cost of dropping it (slower translated code) can only be measured, not predicted.
- **4:3 already exists in the game.** NSMBW has native 4:3 and 16:9 modes (`EGG::Screen::TV_MODE_16_9`, `dGameCom::GetAspectRatio`, per the decomp). Our override of `SCGetAspectRatio` (`projects/nsmbw/native/nsmbw_sc_aspect_ratio.cpp`) already answers from the `widescreen` setting in Config.toml, and `nsmbw_product.cpp` locks the window to 16:9 when it is on. What is missing is an F10 option for it. Layouts and the wipe fader read the ratio when they are created, so a switch probably only takes effect on the next scene; not yet tested.
- **The MKW project solved wider-than-16:9 already, in code we can reuse as a pattern.** `runtime/src/dynamic_aspect.cpp` and `runtime/src/hle/gx/gx_dynamic_aspect.h` rewrite the game's screen-size records so the view gets wider (Hor+) or taller (Vert+). NSMBW has the same kind of table (`EGG::Screen::sTVModeInfo` at 0x80350820 in `function_map.txt`). It is not a copy-paste: the HUD code in the decomp scales by fixed 4:3 and 16:9 widths (`d_2d.cpp`, `m_2d.cpp`, `m_wipe_fader.cpp`), and the game's off-screen culling (for example `chkOffScreen`) assumes the 16:9 width.
- **The shared runtime already contains a Riivolution loader** (`runtime/src/hle/storage/riivolution.cpp`, hooked into `dvd.cpp` and `nand_fs.cpp`), written for Mario Kart Wii. It handles disc file overlays and redirecting the savegame. It does not apply Riivolution memory patches, which Newer needs. I did not check whether NSMBW's disc-file path goes through the same hooks.
- **Other versions have an address map.** `NSMBW-Maps/address-map.txt` maps PAL v1 addresses to NTSC-U v1/v2 (E1, E2), Japan v1/v2 (J1, J2), PAL v2 (P2), Korea (K), Taiwan (W) and China (C). For NTSC-U v1 the shift is a constant that changes only a few times through the code, so remapping our `function_map.txt` and override addresses looks mechanical. The hard parts are the roughly 70 native override files (some are copies of translated code that differs per version), and that each version has to be tested against a real disc dump of it. The decomp and symbol files cover PAL v1 only.
- **Rumble is not wired for the Wii Remote.** `pad.cpp` sends rumble to GameCube pads, but nothing in `projects/nsmbw/` binds the Wii Remote motor calls.
- **Not checked:** how the save file is laid out (so region compatibility is unknown), why `nand_root` was not picked automatically under Proton (`DiscoverNandRootPath` exists in the runtime), and anything about the 180 fps report.

### Suggested order of work

1. **0.2.3 (a day or two):** items 1-3, plus item 9 if the details arrive and the cause is simple. All are small and easy to check on this PC, apart from Linux, which needs the reporter to retest.
2. **0.3.0:** start with a time-boxed spike for item 8: apply the wider screen record, drive the window to 21:9 with the CRU trick, and record what breaks (HUD position, pop-in at the edges, fades). That result decides whether ultrawide ships in 0.3.0. Do items 4-7 alongside; the 4:3 option (item 4) also proves out the "aspect read at scene creation" question that item 8 depends on.
3. **After 0.3.0:** item 11 first for NTSC-U, because it also unlocks item 12 and the largest audience. Needs a US disc to test (the developer has one since 2026-11-14). Packaging is decided: a launcher plus one DLL per version (section 3).

## 1a. 4:3 progress (2026-11-07, uncommitted)

- **Built:** an "Aspect ratio" row (16:9 / 4:3) in the F10 Display tab, a `SetWidescreen` config setter, and `SCGetAspectRatio_HLE` (`runtime/src/hle/sc.cpp`) now fixes its answer at first call, because the game asks for the ratio at boot and again as it builds objects (`m2d::Simple_c`, the faders), so a mid-session change would mix the two. The row says "Applies after a restart" when the saved choice differs from the running one. A 4:3 `VILockAspectRatio` was added in `nsmbw_product.cpp` to match the 16:9 one.
- **Seen (portable copy, `widescreen = false`, title screen):** the game switches to its 4:3 layout (the logo and "Press Z to Start" are laid out differently), so the game side works.
- **Still wrong:** the picture stays in the same 16:9-shaped box (about 640x370 inside a 640x480 window, black above and below), so 4:3 content is stretched wide. `NSMBW_LOG_PRESENT_SEQ=1` shows the final present covers the whole 640x480 window, so the shape is decided before the presenter, probably in which region of the 640x528 render buffer is copied out (the game's 608x456 picture vs. the 832-wide anamorphic 16:9 one). Cause not found yet. Measured later the same day: the game's canvas table (`EGG::Screen::sTVModeInfo`, 0x80350820 in the PAL DOL) is 608x456 for 4:3 and 832x456 for 16:9, and both modes render into the same 640x456 EFB region and display-copy it identically (`NSMBW_LOG_COPYDISP_GEOM`: src 640x456, dst 640x480). The 16:9 lock gives a present viewport of 640x360 and a 360-row picture on screen (rows 91-450 of the capture). In 4:3 the present viewport is the full 640x480, yet the capture still shows a 371-row picture (rows 85-455) with black above and below. Those two facts do not agree, so either the bars are drawn into the frame before the present (a game-drawn letterbox, or a region of the render buffer that is not picture) or the capture is not showing the viewport. Next step: dump the XFB / presentation image (`NSMBW_GPU_PEEK*`) in 4:3 and see whether the bars are already in it. **Cause found (later the same day):** the bars are the game's own letterbox, not a presentation bug. `NSMBW_LOG_VIEWPORT=1` shows that in 16:9 mode the game sets its 3D viewport to the full 640x456 (top 0), and in 4:3 mode to `top=52 height=352` (a 640x352 strip, 77% of the height) while 2D layers still use 640x456. So NSMBW's own 4:3 mode is a 16:9-ish view letterboxed with 52 rows of black above and below, which is why a CRT user would call the game 16:9-only. Our output matches it (the 371-row picture is the 352-row strip plus the scaling). The developer's 2000x1125 screenshot (picture 1500x868, black on all four sides) is the same thing: a 4:3 box with the game's letterbox inside it. So the 4:3 option as built is faithful but not what a CRT user wants. Real full-screen 4:3 means changing the game's 4:3 view: use the full 640x456 viewport and give the 3D camera a taller projection (Vert+: more vertical level visible), the same idea as MKW's `dynamic_aspect.cpp`. Next step: find where the game computes the 352 (`d_SmallScoreManager.cpp` has `c_SCISSOR_SIZE_Y = 352`, so score popups are clipped to the same strip and would need the same change) and what draws the bars. Do not ship the 4:3 row as 'full screen 4:3' until that is done; shipping it as 'original 4:3 (letterboxed)' would be accurate.

## 1b. Ultrawide (21:9) - experimental, open problems listed below (2026-11-07)

Goal: the original look in a wider frame, more of the level visible, nothing stretched, HUD at the screen edges. The first attempt (rewriting only the canvas table) stretched the world and the HUD; the working version changes six things, all in `ApplyAspectSetting` (`runtime/src/product/nsmbw_product.cpp`) plus one aurora setting:

| What | Where | 16:9 value | Why |
|---|---|---|---|
| Level view width | 0x8042D058, copied by `mVideo::create` (0x800E8BC0) | 408.70 (= 224 x 832/456) | `dBg_c` (0x80077B00) divides it by the zoom to get the visible rectangle (`dBgParameter_c` size, 496.28 x 272 in 1-1). That rectangle drives the level's projection AND the game's tile and actor culling, so both widen together. |
| 16:9 canvas width | `EGG::Screen::sTVModeInfo` 0x80350820 + 12 | 832 | the map camera and the layout code read it |
| Layout adjust factor | 0x8042B364 (`d2d::Multi_c` ctor), 0x8042DF28 (`m2d::Simple_c` ctor) | 19/26 = 608/832 | layouts are drawn in 608-wide 4:3 space and stretched to the screen, so positions reach the edges by themselves; HUD panes are shrunk by this factor to keep their shape |
| Colour fader half width | 0x8042DFD0 (`mColorFader::draw`) | 416 | fades cover the full width |
| Wipe width factor | 0x8042E040 (`mWipeFader` ctor) | 1.368 = 832/608 | the Bowser-face wipe stopped 32 px short of each side without it |
| Viewport squeeze | `AuroraSetWideViewportLimit(640)` (`command_processor.cpp`) | off | the widened canvas makes the game ask for a 3D viewport wider than its 640-unit frame (840 at 21:9); squeezing it back lets the wider projection fill the frame |

Each constant is checked against its 16:9 value before it is written, and a log line says so if it is not found.

- **Plus the world map** (perspective camera, `AuroraSetPerspectiveXScale` in `command_processor.cpp`): the map camera keeps a 16:9 aspect (`proj[1][1]/proj[0][0]` = 832/456 at 21:9 too, viewport 640), so it was stretched (reported by the developer). Its x row is scaled by (16:9)/(21:9); the map now shows more on both sides with round mushroom houses. Where the map gets 832/456 is not found (not the canvas table, not the DOL/REL 832 constants).
- **Checked at 21:9:** title, world map, 1-1 start and running right, pause menu, the HUD (? block and HUD icons measured square/round), 16:9 unchanged.
- **Open problems (developer's 21:9 screenshots, 2026-11-07, and my captures):**
  1. **Tile pop-in at the sides - fixed 2026-11-14.** The BG tile drawer (0x80082740; found by logging GXLoadTexObj callers: it loads the 1024x1024 tile texture through 0x8010AAC0) draws ceil(28 / zoom + 0.999) + 2 columns of 16-unit tiles from the view's left edge, with 28.0 read from 0x8042C1A4 (only that function reads it). 28 x 16 = 448 units covers the 16:9 base view (408.7), not 21:9's. ApplyAspectSetting scales it with the view (+1 column). Verified: running right through 1-1 at 21:9, the ground reaches the right edge in every frame (before: a hard edge ~40 px short).
  2. **Menu backgrounds narrower than the screen - fixed 2026-11-14.** `nsmbw_layout_patch.cpp` (WidenBackgrounds) serves widened copies of 9 layouts (fileSelectBase, fileSelectPlayer, sequenceBG, preGame, continue, controllerInformation, stockItem, timeUp, wipeDokan): every picture or window pane at least 790 units wide inside a location-adjust subtree is widened to the canvas width + 64 (1156 at 21:9), centre kept. 12 more layouts were surveyed but their wide panes are outside the adjusted subtree, so the game already stretches them full width. Checked at 21:9: file select and player count fill the width (my captures and the developer's). Scenery strips (the course intro's world bars) are spread over the wider pane, a mild horizontal stretch.
  3. **The Bowser-face wipe** leaves the sides uncovered late in the animation (early frames covered the full width after the fader patch).
  4. **Title screen:** the ground ends before the right edge (fixed camera near the end of the title scene's level).
  5. **"The world looks flat":** measurement says the foreground is not stretched (? block 49x49 px, Mario 0.55 width/height as at 16:9), but the developer still sees it; needs a side-by-side at the same spot.
- **Not checked:** later worlds, bosses, autoscroll, cutscenes, multiplayer zoom, 32:9 (only behind `NSMBW_EXPERIMENTAL_ASPECT=1`).
- **16:10 and full-screen 4:3** need the opposite (a taller view) and are not done.
- Diagnostics added: `NSMBW_LOG_BGPARAM=1` (the visible rectangle once a second), `NSMBW_ASPECT=<w>:<h>` (override for a run), full-precision projection in `NSMBW_LOG_DRAW_TEXGEN`.

## 1. More aspect ratios (target: v0.3.0)

**Goal:** 16:10 (Steam Deck), 4:3 and 21:9 ultrawide, on top of today's 16:9. This was the most-requested feature, and v0.3.0 has been promised.

**How:**
1. Add an aspect-ratio setting to the F10 menu. The `widescreen` setting in Config.toml already switches `SCGetAspectRatio` between 4:3 and 16:9, so this is mostly a menu entry. *Check first:* that the game's 4:3 mode renders correctly at higher render scales, and whether it needs a scene change or a restart to take effect.
2. For wider-than-16:9 screens, make the camera show more of the level. The game culls objects it thinks are off screen, so a wider view will show things popping in at the edges until the culling width is widened too. *Check first:* where the camera and culling read the view width.
3. Fix the HUD and pause/map layouts so they anchor to the screen edges instead of a fixed 16:9 frame. `runtime/src/product/nsmbw_layout_patch.cpp` already edits the game's layouts and is the tool for this.
4. In the F10 menu, show the pixel height next to each resolution multiplier. The base is 480p, so 1.5x is 720p, 2x is 960p and 3x is 1440p. Today the numbers are confusing.

**Testing without an ultrawide monitor:** make a custom resolution with CRU (for example 1920x824 on a 1080p screen) and set GPU scaling to "preserve aspect ratio". A Reddit user with a 3440x1440 monitor also offered to test; the developer said they would DM them.

## 2. Lower CPU requirements (no AVX2)

**Status (2026-11-14): built, not yet shipped.** A second exe, `NSMBWCompiled-compat.exe`, is built for `x86-64-v2` (SSE4.2: Intel Nehalem 2008+, AMD Bulldozer 2011+) with `-DMKW_HOST_ISA=v2`. The main exe starts it by itself on a CPU without AVX2. How it works and how to build it: `docs/BUILDING.md`, *CPUs without AVX2*. The full story is in the MASTER_PLAN progress log, 2026-11-14.

What the earlier plan got wrong: it said fused multiply-add "becomes a multiply plus an add". That rounds twice where the Wii rounds once, and in a test it was wrong on 75% of near-tie inputs, which could desync recorded-input replays. The compat exe emulates the single rounding exactly with SSE2 instead. It was checked bit-for-bit against the FMA instruction on 320M inputs.

Left to do:
1. **Ship it.** *Done 2026-11-15:* `build_release_compat\` and `build_release_compat-E1\` are packaged, and README / PLAYING.txt now list SSE4.2 as the minimum (marked untested). Originally: build `build_release_compat\` (configure line in BUILDING.md) next to `build_release\` and package. The README and PLAYING.txt CPU requirement should change in that release, not before.
2. **Run it on a real pre-AVX2 CPU, or under Intel SDE** (`sde -snb -- NSMBWCompiled-compat.exe`, Sandy Bridge emulation; a download). A static scan found no AVX/FMA/BMI in our own code, and only CPU-checked paths in zstd and SDL. About 700 such instructions are in stripped code inside `webgpu_dawn.dll`, `dxcompiler.dll` and `dxil.dll`. Those are very likely chosen at run time, but only running it proves that.
3. **Speed on an old CPU** is unknown. On this PC the compat exe showed no measurable difference (game-thread spare time, 7 alternating runs), but that test only rules out a large slowdown. Double-precision multiply-adds call the C library's software `fma` there; if a profile on old hardware shows that as hot, it is the first thing to speed up.

**Not planned:** a DX8 or OpenGL fallback for pre-Vulkan GPUs. Very old hardware was suggested as a minimum-spec test bench; the missing Vulkan makes that not worth the work.

## 3. Support for the other game versions

**Goal:** stop requiring the PAL revision 1 disc (SMNP01). Others: PAL revision 0, NTSC-U (SMNE), NTSC-J (SMNJ), Korean (SMNK). Several people said a PAL rev 1 dump is hard to find.

**Why it is big:** every version has different code. Every entry in `projects/nsmbw/function_map.txt`, every `PPC_NATIVE_OVERRIDE` address, every guest-memory constant and every shader or texture-name hash that depends on an address is specific to one version.

**How:**
1. *Check first:* does NSMBW-Decomp / NSMBW-Maps hold symbol maps for the other versions? Measure how many of our addresses actually differ.
2. Replace hard-coded addresses in the override files with a table per version, picked from the disc ID at start-up. Do this refactor once, not once per version.
3. Translate each version's DOL and RELs with the existing pipeline. Packaging is decided: one launcher plus one DLL per version (see "Decision" below).
4. Test each version with the self-test (`NSMBW_AUTO_PRESS_SELFTEST`) from boot to a level.

**Reference:** `C:\Users\ryanl\NSMBWRecompCollab\MULTI_REGION_GUIDE.md` (outside this repo, written 2026-11-14) explains how the team branch `merge/phase1-regions` runs USA and PAL from one source tree. Every guest address stays written as one version's address. The macros in `nsmbw_va.h` map it to the other version at compile time, and `verify_variant.py` checks word by word that every hooked function is identical or equivalent on both versions. For us PAL (P1) stays the base address space; the guide measured that 134 of 135 of our hook addresses and all 243 constants map to USA.

**USA disc:** the developer obtained a USA dump on 2026-11-14. Hash its `main.dol` first: there are two USA pressings, and one has no `DVDDiskCheck` call.

**Decision (2026-11-14): one launcher exe plus one game DLL per version.** The team branch builds one exe per version and has the USA exe start the PAL one. Instead, `NSMBWCompiled.exe` becomes a small launcher. It reads the disc ID from `sys/boot.bin`, checks the CPU, loads one game DLL with `LoadLibrary` and calls one exported entry function.
- *Why:* the compat build (section 2) already makes a second exe, which `host_cpu_baseline.cpp` starts on a CPU without AVX2. Two versions × two CPU levels would be four exes starting each other; the launcher turns it into one choice of which DLL to load, and the compat relaunch goes away.
- *Why not one exe holding both versions:* the address layer is compile-time (`constexpr`, template arguments such as `InvokeDirectCpu<0x...>`, `case` labels), and both versions' translated code uses the same `func_XXXXXXXX` names. Merging them would mean run-time address lookups, prefixed names, an exe of about 160 MB, and no build-time check that every address maps.
- *Download size:* not smaller than separate exes. Each DLL carries its own translated code (about 80 MB) and runtime; only aurora, SDL3, Dawn and libc++, which are already DLLs, are shared.

What the split needs (read in source, not tried):
1. `runtime/cmake/NsmbwProduct.cmake`: `add_executable(NSMBWCompiled ...)` becomes a `SHARED` library per version (and per CPU level), plus a new launcher target.
2. `-Wl,--stack,16777216` must move to the launcher. Windows takes the main thread's stack size from the exe only, so on the DLL it does nothing and the deep-call-chain stack overflow it fixed comes back. The same applies to `-mwindows` in release builds.
3. `main()` in `nsmbw_product.cpp` becomes the exported entry function.
4. `GetModuleHandleW(nullptr)` in `memory.cpp:392` and `nsmbw_product.cpp:597` / `:812` returns the launcher, so the `rvaFromBase` in crash and stall logs would be wrong. Use the DLL's own module handle. `resolve_profile.py` and the symbol lookup must read the DLL rather than the exe.
5. `GetModuleFileNameW(nullptr)` (finding `nsmbw_data` next to the exe) stays correct as long as the DLLs sit next to the launcher.
6. `package_nsmbw.ps1` and the POST_BUILD copy steps list the DLLs instead of the exes.

**The launcher is also the setup screen** (developer's request, 2026-11-14). Before loading a DLL it asks for:
- *Game files:* a disc image (extracted with DolphinTool) or an already-extracted folder. The disc ID in `sys/boot.bin` picks the DLL. This moves `nsmbw_disc_setup.cpp`'s first-launch picker out of the game and into the launcher: the launcher must know the disc before it can choose a DLL.
- *Texture packs:* the HD-pack install that `OfferTexturePack` does today (copy `SMN` into `nsmbw_data/texture_replacements/`).
- *Riivolution mods:* a placeholder for now. The shared runtime has a Riivolution loader (`runtime/src/hle/storage/riivolution.cpp`, section 6) that is not wired into NSMBW yet.

*Order:* do the split first with PAL only, and check that it plays the same as before (self-test to a level, crash log addresses resolve) before starting on the address layer. A break then points at the split, not the region work.

*Status (2026-11-14):* the split is built and runs (steps 1-6 above; see the MASTER_PLAN entry of that date). Self-test reached a level twice; window close exits cleanly; the profiler resolves against the DLL. Not yet run: the compat DLL, the release launcher, `package_nsmbw.ps1`. The setup screen is built too (game files, HD pack, Riivolution placeholder) and was tested with the developer's USA and PAL discs: USA is recognised and refused until its DLL exists, PAL extracts and plays. Next: the address layer.

*Status (later the same day):* the address layer is in and PAL plays unchanged; the USA code is translated and `NSMBWCompiled-E1.dll` builds and boots through the launcher, but hangs before the first scene in EGG::AsyncDisplay (MASTER_PLAN entry "USA (E1) build").

*Status (2026-11-15):* the USA version plays: title, map and levels, its own save folder (MASTER_PLAN entry of that date). Left: check the remaining features on USA, and package both DLLs together. Building it is in docs/BUILDING.md.

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

- **Swappable player 1 character, Mario in the Add/Drop selector, and a character select for 1 player.** Built 2026-11-15 (game setup, and Add/Drop Players on the world map; MASTER_PLAN log, same date). Section 7a.

### 7a. Player 1 character and a four-character selector: how it works, and what is left

All addresses are PAL rev 1; the USA REL has the same selector tables and functions at the same addresses with identical bytes (checked 2026-11-15 for the first set; re-check the later overrides), and the DOL's player tables move by -0x300 through the address layer. Code: `projects/nsmbw/native/nsmbw_p1_character.cpp`, layout: `runtime/src/product/nsmbw_layout_patch.cpp` (`PatchCharaContents`).

**Character numbers.** `daPyMng_c::mPlayerType[4]` (0x80355160, u32 per player slot) holds 0 Mario, 1 Luigi, **2 Blue Toad, 3 Yellow Toad**. NSMBW-Decomp's `PLAYER_TYPE_e` names the two Toads the other way round; the game draws Blue Toad for 2, its own map HUD table maps 2 to `P_BkinoFace`, and the selector card that shows lives of 2 makes `P_kinoB` visible. Lives (`mRest`, 0x80355190), power-up and stored items are indexed by character. The save keeps the table (0x800E1780 / 0x800E1930), so the choice is per game file.

**Changing it on the world map** (2026-11-15; replaced an F10 > Advanced option at the developer's request): Add/Drop Players runs the same player-1 selector as game setup. The setup copy (0x807A0DB0) gives player 1 join state 0 inside the screen only (dInfo_c keeps 3), so selector 0 runs for it at the left spot; the recruit copy (0x807A1510) takes player 1's OK only after its card is confirmed (+666). The selector writes `mPlayerType[0]` itself on confirm; the pick is also applied through `NsmbwP1CharacterSet` once the screen has closed, as a fallback. The map player's model is built once, in its constructor (0x809027C0, which hardcoded Mario's model type 0 - overridden to use player 1's character), so after a change the map reloads itself (`0x809218B0`, the game-over screen's "back to the map", same world and node) on the OK frame itself, with a cut to black there (`dFader_c::startFadeOut(1)`; `NSMBW_P1_OK_FADE_FRAMES` for A/B) and kept running by our tick, because the game doesn't run the fader while the map's menu is up (2026-11-16; before, the reload waited for the screen to close and the old map showed for ~0.6 s). Other changes (`NSMBW_DEBUG_P1_CHARACTER`) reload once nothing is paused (stop word 0x8042A228) and the Add/Drop screen has not drawn (0x807A0170) for 2 frames: that screen does not set the stop word, and the first version reloaded the map under it. `NSMBW_DEBUG_P1_CHARACTER=<0..3>` sets the character 300 frames into the first map through `NsmbwP1CharacterSet`; `NSMBW_LOG_P1_CHARACTER=1` logs.

**The Add/Drop selector offers all four characters** (built and tested 2026-11-15). The selector (`dCharacterChangeSelectBase_c`) is a carousel of positions; the original's tables (0x80933D00 / 0x80933D10) read 1 Blue, 2 Yellow, 3 Luigi and it only visited 1-3. With Mario offered, both tables are rewritten so positions 3, 2, 1, 0 are Mario, Luigi, Yellow, Blue: Mario is the leftmost card (developer request, 2026-11-15), and the code below maps positions to characters through the tables, never by hand.
- Layout: `Im_plofileMario_00.tpl`, two materials cloned from Luigi's, and `P_mario_00` / `_01` cloned from `P_lui_00` / `_01` (no animation names a portrait pane). The picture: `nsmbw_data/portraits/Im_plofileMario_00.tpl` if present (`projects/nsmbw/tools/make_portrait_tpl.py <png> <nsmbw_data>`; `--hd-pack <dir>` also installs the PNG under its HD-pack name), else the disc's score-screen Mario head (`pointResult.arc` `im_mario_00`) centred on the card.
- `0x8076FD70` (native): position -> card picture value, 0 Blue, 1 Yellow, 2 Luigi, +3 greyed (taken), 6 none; Mario 7 / 10. Goes position -> character -> value.
- `0x807708E0` (copy): confirming checks the character at the position is not taken (the original mapped 3/2/1 to Luigi/Yellow/Blue by hand and let anything else through); scrolling stops at 0 instead of 1.
- Voices: `0x80771090` (copy) plays Mario's own join voice (797/798; it gave him Yellow Toad's); `0x807A1B60` (copy, Start Game) plays player 1's character's decide voice (Mario 803/804, Luigi 875, Blue 946, Yellow 1016; it was always Mario's) and `0x80919130` (game setup VoiceEndWait) waits on that one; `0x807AC7A0` (copy) drops Mario's "here we go" when "1" is chosen on Number of Players. Screen-right (`0x4`) goes *down* a position, screen-left (`0x8`) up.
- `0x807736B0` (copy): for Mario's values the card is set as Luigi's, then Luigi's pane is swapped for `P_mario_0<card>` and the lives redrawn from `+676` (the card's lives cache already holds Mario's there).
- `0x8076E600` and `0x8076EA00` (copies): the arrows' first end is position 0, not 1.
- The three "which character does a slot start on" tables (file-start reset 0x809352A0, slot start 0x80933CC0, slot -> start position 0x80933D20) are rewritten each frame with Mario and player 1's character swapped, so player 1 keeps its character at file start and no other slot starts on it.
Everything selector-side only applies when the layout really got its Mario panes (`NsmbwSelectorHasMario`).

Tested (portable copy, scripted with `NSMBW_TEST_INPUT`, which now takes `p<N>:` entries for players 2-4 and counts them as connected): player 2 scrolls Luigi -> Yellow -> Blue -> Mario; with player 1 as Mario, Mario's card is greyed and confirming is refused; with player 1 as Luigi, player 2 starts on Mario, can scroll away and back, confirms Mario, and the map HUD then shows Luigi and Mario. A scripted remote is connected from the start, so its selector opens without a join press; its first `2` press is the confirm.

**Still open:**
- **Player 1 chooses in game setup, for any player count**: built 2026-11-15. Game setup's StartMember (0x80918B00) sends "1 player" to the Add/Drop screen too, and player 1 starts not joined for every count; the screen (0x807A0DB0 setup, 0x807A11A0 wiring, 0x807A14D0 / 0x807A1510 recruiting) leaves remote 0 free and runs `count` selectors, from selector 1 (the middle card) for one player and selector 0 otherwise. Cards go to the spots of the per-count table 0x80935330 (count 2 -> 7/8, 3 -> 4/5/6, 4 -> 0..3; null panes at screen+1500) through 0x807A0170, now also for selector 0. Back on Start Game / Back re-opens the selector with the same count (0x807A1B60 sets a flag, game setup's ConnectionCheck 0x80918D10 re-opens instead of going to StartMember); back on the selectors still leaves to Number of Players. Tested with scripted input: 1 player (Mario leftmost; Back, Luigi, Start Game -> map as Luigi), 2/3/4 players (cards side by side; 2 players reach 1-1 as Mario and Luigi). Not checked: the title text ("1 Players"), a real controller, the voices by ear.
- Seen nowhere yet: a file saved with a non-Mario player 1 started from the title; the other players' map models (0x808EB7D0 picks them from 0x8093CDF0 by its actor parameter; in the test, player 2 as Mario showed on the HUD, the map model was not checked).
- Free Mode / Coin Battle still reset to Mario at their start (0x800BB180 inlined into 0x807A0DB0 and `dInfo_c`'s constructor).
- Mario portrait (2026-11-15, developer's local data only - neither file is in the repo or a package): default = the developer's Mario Kart Wii mugshot (its 64x64 original, framed and resampled to 150x150 without upscaling; changed 2026-11-15 at the developer's request), as `build_nsmbw/nsmbw_data/portraits/Im_plofileMario_00.tpl`; HD = a head-and-shoulders crop of the official NSMBW Mario render (MarioWiki, `NSMBW_Mario_Solo_Artwork.png`) as `texture_replacements/SMN/UI/Menu/tex1_150x150_809770f85bf8b175_5.png`, named from that default's pixels. Seen in game with the HD pack on. A shipped build still falls back to the disc's score-screen head. The level-entry wipe stays Mario-shaped (`wipeMario`).
- USA: every overridden function and table is byte-identical at the same address in the USA REL; the USA DLL builds and links with all of it (2026-11-15). Not run.

## 8. Performance and known issues to investigate

- **180 fps interpolation falls to about 40 fps on a fast PC** (RTX 5080, Ryzen 7 7800X3D). One user report, not reproduced. That is much worse than the Iris Xe laptop, so it is probably not simply "the PC is too slow". When their details arrive, run with `NSMBW_LOG_INTERP=1` and `AURORA_LOG_WORKER_TIMING=1` at the same settings and see where the time goes.
- **Claims about performance need numbers.** The recorded Iris Xe results (2026-10-31) are 1x and 1.5x at 180 fps, 2x at 120 fps, 4x at 60 fps. There is no recorded Dolphin comparison. Measure Dolphin and this port on the same machine and settings before stating one is faster, then put the table in the README.
- **8x with 180 fps interpolation** is deliberately blocked because it ran badly (MASTER_PLAN, 2026-10-31).

## 9. Parked

- **Online multiplayer** (a self-hosted server was suggested). Needs channels 1-3 and verified co-op first, plus a determinism check across machines (hash guest RAM each frame). Rollback netcode would also need save and restore of guest RAM, thread state and emulated-hardware state. See MASTER_PLAN Phase 12 item 6.
- **PS5 and other current consoles.** Not pursued.

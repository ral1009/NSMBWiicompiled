# Future plans

What we intend to build next and how. Written before any of it was built; the triage table's Plausibility column now says what has been built since (status as of 2026-11-18). `docs/MASTER_PLAN.md` stays the record of phases and progress; this file is the to-do list for after the current release.

Written 2026-11-07. Items are in rough priority order. "Check first" lists what must be found out before the work can be sized.

## Triage: how likely each item is to work, and which release

Ranked by plausibility (how sure we are it can be done with the code we have), then sorted by size of work. Based on reading the repo on 2026-11-07; nothing was built or run, so "verified" below means read in source, not tested.

| # | Item | Plausibility | Work | Release |
|---|---|---|---|---|
| 1 | Resolution labels show pixel height | Done 2026-11-07 (`31d39f4`): presets named by render height (528p native ... 6K) | - | **0.3.0** |
| 2 | Linux/Proton: default to Vulkan, automatic `nand_root` | Half done 2026-11-07: `graphics_api` is honoured and `auto` means Vulkan under Wine/Proton (not tested under Proton); `nand_root` only got clearer error messages, its cause is unconfirmed | Small: needs the reporter to retest | **0.3.0** |
| 3 | Rotating backups of the save file | Done 2026-11-07: one copy per launch to `SaveBackups/`, 10 kept (rotation confirmed by the developer 2026-11-22) | - | **0.3.0** |
| 4 | 4:3 mode as an F10 option | Built 2026-11-07: the game's own letterboxed 4:3, applied after a restart (section 1a); full-screen 4:3 not done | Full-screen 4:3: medium (Vert+) | **0.3.0** (letterboxed) |
| 5 | Swappable player 1 character | Built 2026-11-15: Mario in the Add/Drop selector (game setup and the world map; the F10 option was removed the same day), character select after "1 player" (section 7a) | Played by the developer 2026-11-22 (title and map Add/Drop) | **0.3.0** |
| 6 | Riivolution file mods | Medium-high: loader already in the shared runtime | Medium (wire into NSMBW, test with a real pack) | **0.4.0** (moved from 0.3.0 at its release, 2026-11-22) |
| 7 | No-AVX2 build | Built 2026-11-14; packaged 2026-11-15 as compat DLLs for PAL and USA, chosen by the launcher; not yet run on a pre-AVX2 CPU | Small now: test | **0.3.0** |
| 8 | 16:10 and ultrawide (wider view) | 21:9 built as experimental 2026-11-07, edge tiles and menu backgrounds fixed 2026-11-14; wipe and title ground confirmed fixed by the developer 2026-11-22 (section 1b). 16:10 not done (falls back to 16:9) | 16:10: medium (needs a taller view) | **0.3.0** for 21:9 (experimental); 16:10 later |
| 9 | 180 fps drop to ~40 fps on a fast PC | Fixed 2026-11-15 (`a49a5e6`, progress log): a 40 fps lock found and fixed on this PC; whether it is the reporter's case is unconfirmed; 180 FPS and Match display played 2026-11-22 | - | **0.3.0** |
| 10 | Rumble and gyro tilt | Medium-low: rumble goes through the Wii Remote motor calls, which are not bound | Medium | later (0.4.0) |
| 11 | Other game versions (NTSC-U first) | USA rev 1 built 2026-11-14/15 (a DLL per version behind the launcher); played past World 1 by the developer 2026-11-22. Other versions not started | - | **0.3.0** for USA rev 1; others later |
| 12 | Save carry-over between regions | Unverified: save format not yet read | Small once item 11 exists | later, with item 11 |
| 13 | Newer SMBW (code mods) | Low-medium | Very large | later |
| 14 | Android, online play | Low | Very large | later |
| 15 | Regression tests, installer, auto-update | Medium | Medium each | later |
| 16 | Toggle for the power-up / damage freeze ("item pickup lag"; promised in the v0.2.2 release notes) | Built 2026-11-18: F10 > Advanced > Power-up freeze (section 7b); played by the developer 2026-11-22 (pickups, damage, mini mushroom, death) | - | **0.3.0** |
| 17 | Assist options, infinite lives first (section 7d) | High for lives: one static function, `daPyMng_c::decRest`; not checked whether it is inlined. Invincibility not checked | Small | later (0.4.0) |
| 18 | Player-vs-player collision toggle for co-op (section 7d) | Medium-high: the game has `setCcPlayerRev`, `ccCheckStamp` and a per-pair `setNoHitPlayer` | Small to medium | later (0.4.0) |
| 19 | Game speed and hold-to-fast-forward (section 7d) | Medium: we already control the VI retrace interval (Match display); audio must mute away from 100%, not tried | Small to medium | later (0.4.0) |
| 20 | Practice tools: instant restart, game-frame level timer, death counter, input display (section 7d) | Medium: restart can reuse the map-reload trick and a cloned pause button | Medium | later (0.4.0) |
| 21 | Camera zoom-out, experimental (section 7d) | Medium: the ultrawide view-width and tile-column constants exist; exposes level edges and pop-in | Medium | after 21:9 ships |

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

1. **0.2.3:** never released. Items 1-3 and 9 were built after v0.2.2 and ship in 0.3.0 instead (2026-11-18).
2. **0.3.0:** start with a time-boxed spike for item 8: apply the wider screen record, drive the window to 21:9 with the CRU trick, and record what breaks (HUD position, pop-in at the edges, fades). That result decides whether ultrawide ships in 0.3.0. Do items 4-7 alongside; the 4:3 option (item 4) also proves out the "aspect read at scene creation" question that item 8 depends on.
3. **After 0.3.0:** item 11 first for NTSC-U, because it also unlocks item 12 and the largest audience. Needs a US disc to test (the developer has one since 2026-11-14). Packaging is decided: a launcher plus one DLL per version (section 3).

## 1a. 4:3 progress (2026-11-07; committed in `31d39f4` and `98b406c`)

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
  3. **The Bowser-face wipe - fixed (developer, 2026-11-22 playtest).** Left the sides uncovered late in the animation. Which change fixed it was not identified.
  4. **Title screen ground edge - fixed (developer, 2026-11-22 playtest).** The ground ended before the right edge. Which change fixed it was not identified.
  5. **"The world looks flat":** measurement says the foreground is not stretched (? block 49x49 px, Mario 0.55 width/height as at 16:9), but the developer still sees it; needs a side-by-side at the same spot.
  6. **Menu 3D models stretched - fixed 2026-11-19** (Select World verified; items screen and 2-player character select not yet seen). They use a 16:9 orthographic camera (853x456); `AuroraSetModelOrthoXScale` narrows each model around its own origin.
  7. **Camera jumping in zones narrower than the view - fix built 2026-11-19, untested in the arena** (World 4 Big Bertha): `nsmbw_wide_camera.cpp` centres the camera when the clamp range is empty.
  10. **World map edge visible at 21:9 (developer report 2026-11-20; investigated, not built).** The map camera (`dWCamera_c`, m_instance 0x8042A564, not decompiled; `NSMBW_DUMP_WMCAM=1` logs its changing fields) has no limits: `0x8092CD30` copies the player's position into its target (+0x4BC x / +0x4C0 / +0x4C4) every frame, `0x8092CC20` derives the eye (+0x4B0..) from it, `0x8092C5E0` copies both into the view (+0x1E8 eye x, +0x1F4 target x). The 16:9 game never shows the edge only because the maps extend past the path. A clamp therefore needs per-map limits: clamp the target x to [leftmost path x + d, rightmost path x - d], d = (21:9 half-width - 16:9 half-width) at the target's distance, so the extreme nodes show exactly what 16:9 shows. Limits: read from the map's route data (not decompiled; unknown effort) or measured once per map (9 worlds + sub-maps, scripted walks to each end; estimate 2-3 h) and stored in a table.
  9. **How many zones are narrower than the 21:9 view (survey 2026-11-20, `projects/nsmbw/tools/survey_narrow_zones.py`, PAL disc):** 375 zones in all; an estimated 152 (116 stage files) are narrower than the 21:9 view at their zoom (estimate: the 16:9 view at a zoom taken as the narrowest fixed-camera room at that zoom, since rooms are built to fit it). 45 of them are in the numbered courses 01-19 (33 stages, mostly bonus/pipe rooms 512 units wide at zoom 4 and a few tall shafts like 3-5, 8-4, 8-6, 9-5). These all show the background at their sides today (centred, clean edge).
     - **Correction and grouping by layout (2026-11-21):** the survey called every stage numbered 20+ a Toad House, minigame or Coin Battle; the tilesets show 20-25 are towers, ghost houses and castles (`toride`, `obake`, `shiro`) and 38 the airships (`hikousen`), so most of those ~107 are main game. Grouping the 152 zones by identical tile layout (every bgdat object on layers 0-2 that intersects the zone, relative to its origin, plus the zone size; sprites not compared; the grouping script was a one-off and is not in the repo) gives 114 distinct layouts: 01-19 courses 45 layouts / 45 zones (8 tall shafts); 20-25 40 / 44 (14 tall); Toad Houses 26-29 **2 / 32**; 33-35 (probably the map enemy battles, not confirmed) 20 / 24; airships 5 / 5; ending/credits 41-42 2 / 2. The only real duplication is the Toad Houses (two rooms) and one 544-wide castle boss room shared by 02/04/05/06/07-24. About 25 layouts are made of 10 tile objects or fewer (Toad Houses, tower boss rooms `toride_boss1..8`, most of 06-33..35 and 07-33..35). 08-38 area 1 zone 4 (airship) has no tile objects at all, so its edge is models/actors and adding tiles cannot fix it.
     - **Zooming the camera in instead (assessed 2026-11-21, not built):** mechanically easy: 0x80078EB0 is the zoom scale (from the zone's zoom type/ID at dBg_c +0x900FE/+0x900FF via 0x80078D00, player-count switch in 0x80078860); 0x801549E0 stores it at zone entry (+0x9000C and the view size), 0x8007A6C0 uses it every frame, and the tile drawer and actor spawner already handle zoom changes. But the fixed-camera rooms are exactly one 16:9 screen in both directions (512x272 at zoom 4 = a 480x270 view), so hiding the sides needs 1.23-1.31x in 39 of the 45 01-19 zones and hides 3-5 tiles of the room's height (floor or ceiling off screen). Making those rooms scroll vertically leaves only ~3 tiles of travel. Clean only for 1-6 area 3 and 6-2 area 1 (1.04-1.06x), tolerable for ~4 more (<=1.2x). Tall shafts keep working but lose ~20% vertical view. Developer's call, 2026-11-21: zoom first (adding tiles, 50-100 h, is too much effort for now); tiles can come in a later update and would let the zoom be removed zone by zone.
     - **Zoom prototype built 2026-11-22** (`nsmbw_narrow_zone_zoom.cpp`, override of 0x80078EB0; MASTER_PLAN 2026-11-22). The real 16:9 view at zoom setting 4 is 496.3 wide, not 480, so these rooms need ~1.27x. 1-1's bonus room fills the width and loses its rock ceiling (~3.6 tiles); 3-3's pipe room loses its icicle ceiling, and its top coins sit under the HUD. 16:9 unchanged. The camera follows Mario up into the lost ceiling (developer, playing). Developer playtest 2026-11-22: full zoom kept as the default; walking between zones (3-5), multiplayer, Big Bertha and USA all fine.
     - **Adding real tiles by hand, estimate (not tried):** the level files have almost nothing past a zone's edge, and the tile drawer only reads the zone's own buffer (zone + 1 column), so each patched zone also has to be widened in the level file, with the extra wall placed in a level editor (Reggie! Next) and the patched `.arc` served only at 21:9 (the 16:9 game would otherwise scroll into the new space). Roughly 15-30 min per ordinary room, 1-2 h per tall shaft, plus about a day to build the serving path and test that a widened zone changes nothing else (camera, enemies, the end-of-room pipes). Courses 01-19 only (45 layouts): about 2-3 days. Everything (114 layouts: 92 rooms ~23-46 h, 22 tall shafts ~22-44 h, the ~25 layouts of 10 objects or fewer at the low end), plus the serving path: about 50-100 h, revised 2026-11-21 from "about 1 week", which assumed the 20+ stages were mostly duplicates. Rough; no zone has been done yet. Suggested first room to prove the serving path: 1-1's bonus room (area 2 zone 1).
     - **Also covers (developer, 2026-11-20):** World 4's Big Bertha room, where the water ends at the zone edge and Bertha swims on past it in the air at 21:9. The level file has no water there, so this is the same fix (widen the zone and its water by hand).
  8. **Edges of the world shown (developer report 2026-11-19):** the World 3 map's left side and the 3-2 pipe area show geometry ending inside the wider view. That art does not exist past what 16:9 shows; options are a narrower view in those places (pillarbox) or accepting it. Not decided.
     - **Tried 2026-11-19: repeat the edge tiles at the tile lookup.** World 3-2's underground zone (area 2, tiles 19-60) has wall tiles only 1-2 tiles past its edges (read from `Stage/03-02.arc`), so drawing further shows nothing. An override of the tile-address lookup 0x80077520 (all tile reads go through 0x80083B40, the only user of the block-index leaf 0x80083F90) returned the zone-edge column's tile past the zone's limits (dBg_c floats at `m_bg_p`+0x90030 / +0x90034). It ran (a log showed the substitutions) but the picture didn't change: with a debug inset of 200 units in 1-1, the ? block inside the inset still drew, so the level drawer (0x80082740 -> 0x80082BE0, which reads a tile ring buffer at r21 with a row width at +48) does not fetch through that lookup. Removed (it would only have changed collision).
     - **Built 2026-11-19: clamp the drawer's column.** The drawer reads the zone's own tile buffer and wraps past its ends; `nsmbw_zone_edge_tiles.cpp` clamps the column instead, so the outermost column (3-2's walls) repeats to the screen edge. Verified with a test inset in 1-1; not yet seen in 3-2.
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

The experimental x86-64 AppImage reached level 1-1 on Bazzite with the USA
rev 1 disc and Vulkan. The next checks are PAL, the compatibility build and
Steam Deck. See [Building on Linux](BUILDING.md#native-linux--appimage-experimental).

### Proton

A user ran the Windows exe under Proton and reported two problems (not yet reproduced; the developer has not tried Linux):

- The renderer setting `auto` chooses DX12, which draws nothing under Proton. Setting it to Vulkan fixed it. **Fix:** default to Vulkan when running under Wine/Proton.
- `nand_root` had to be set by hand in the config. **Fix:** give it an automatic default, like the disc path.

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

### 7b. Toggle for the power-up / damage freeze (2026-11-18; built the same day, `nsmbw_power_up_freeze.cpp`)

In single player the game freezes every other actor for about a second while Mario changes size (any power-up, and damage that shrinks him). The v0.2.2 notes promised a toggle for it in 0.3.0.

- **What freezes it:** `dAcPy_c::executeChangeInit` (0x80141080, `d_a_player.cpp`) runs on the first frame of a power change. It calls `daPlBase_c::stopOther` (0x8004E050), which sets `dActor_c::mExecStopReq |= 0xF` (an *execute-stop request*: actors whose kind is in that mask skip their update; the changing player and Yoshi are taken out of it), and `dAudio::pauseMove` (0x8006A7D0) for the player's sound. `initChangeInit` undoes both when the change ends.
- **The game's own switch:** `stopOther` does nothing unless `daPlBase_c::isPlayerGameStop` (0x8004E040) is true, i.e. bit 1 of `dInfo_c::mGameFlag` (0x8042A260). `executeChangeInit` then returns that same bit, which picks whether the player is frozen-and-redrawn (`updateChange` sets visibility and collision) or keeps moving. So the no-freeze path already exists in the game. That multiplayer is the case where bit 1 is clear is my reading of the code, not checked in a run: the only r13-relative write to `mGameFlag` is in `func_800BB450`, and who sets bit 1 was not found.
- **Proposed change:** a structural copy of `func_80141080` with `EDIT` lines that, when the option is on, skip the `stopOther` call and the inlined `pauseMove`, and return 0 (the path taken when bit 1 is clear). `executeChangeInit` is called only through a member-function table (`l_changeActionProc`), so no caller can inline it and the override always runs.
- **Why not the simpler places:** overriding `isPlayerGameStop` does not work, because the translator inlined it into 2 shards (the second recurring bug class in CLAUDE.md), and it would also remove the pipe, door, goal-pole and water-tank freezes, which use the same check. Clearing bit 1 of `mGameFlag` has the same problem, and its other readers (36 functions read the word) were not audited.
- **Left alone:** pipe/door/goal/death freezes, and the flashing after damage (that is invincibility, not a freeze).
- **Built (2026-11-18):** `projects/nsmbw/native/nsmbw_power_up_freeze.cpp` overrides both 0x80141080 and 0x80141460 (`executeChangeNormal` returns the same bit every frame of the flashing, and a true return is what keeps the player himself still, via `updateChange` -> `executeMain`). `[gameplay] power_up_freeze` (default true), F10 > Advanced > "Power-up freeze". Test hook `NSMBW_DEBUG_P1_POWERUP=<power-up>@<ticks>`. Results in the progress log.
- **Work (as estimated):** one override file, a Config.toml key and an F10 row, then the usual steps (shard manifest, `va_codemod.py`, `gen_version_tables.py E1`). Roughly half a day including a play test of mushroom, fire flower, mini mushroom (`executeChangeInit` also caps the jump speed and drops a carried actor for it) and damage, on PAL and USA. Risks: enemies and moving platforms keep moving during the change; untested.

### 7c. Upstream WiiCompiled #420 and #421 (reviewed 2026-11-18, not applied)

`origin` has 172 commits since our merge base (`806f412`). The two newest, #420 "fix runtime and translator correctness" and #421 "improve runtime and translation performance" (17-18 Nov 2026), were read for this release:

- **#420, translator/ISA:** `crorc` was computed wrong in `ppc_isa_cr.h` (case 6: `~a | b` instead of `a | ~b`); our copy has the same line, but NSMBW's translated code makes no `crorc` calls through it (grep of `build/nsmbw*/functions`). `stwcx.` was decoded from the wrong opcode: no `stwcx.` in NSMBW's output. The GQR dataflow fix (loops that change a GQR) and `nor.` lifting: our translator with those two patches applied re-translated the PAL DOL to the same 16,187 files with 0 differences; RELs not checked. `lfs` and paired loads now widen a float's bits without the host flush mode: this changes results only for single-precision denormals while the guest has FPSCR[NI] set (which this runtime mirrors as host FTZ/DAZ); NSMBW's code has ~220k such loads, impact unknown and probably nil.
- **#420, runtime:** crash-safe save and config writes (`host_file.h`, used by NAND), fiber lifetime fixes, pad trigger state, audio ownership, networking, and 23 new test files.
- **#421:** a GX display-list cache, texture-binding and worker changes, ARM audio kernels, and a lower-memory translator mode. It touches GX files we have changed heavily, so it would conflict; benefit for NSMBW unmeasured.
- **Decision for 0.3.0:** not taken (each needs a re-translation or rebuild and a full re-test). After 0.3.0, port single pieces with a play test each: the `crorc` line, the `lfs` widening, and the save-write safety.

### 7d. Options from the 2026-11-18 idea review (nothing built)

The developer put an AI-generated list of "things a PC port could fix" through the repo on 2026-11-18. Effort sizes are estimates from reading code (decomp and this repo); nothing was built or run. Suggested order: 18 and 17 first, then 19, 20, 21.

**Added (triage rows 17-21):**
- **17. Assist options.** Infinite lives first: losing a life goes through `daPyMng_c::decRest` (a static function; lives are `daPyMng_c::mRest[4]`). *Check first:* the generated shards for an inlined copy (CLAUDE.md, second recurring class). Invincibility: not checked, damage probably has several entry points. The Super Guide stays as it is.
- **18. Player-vs-player collision toggle.** Hooks in the game: `daPlBase_c::setCcPlayerRev` (push), `ccCheckStamp` (stomp), and `setNoHitPlayer` / `updateNoHitPlayer` / `mpNoHitPlayer` (an existing per-pair "cannot collide" slot). *Decide:* whether lifting and throwing another player stays on. Test with two real controllers (the developer has tested two before).
- **19. Game speed and hold-to-fast-forward.** `vi.cpp` now owns the retrace interval (`g_hostRetraceOverrideNanos`, added for Match display), so 50%-400% is a change to it. Audio does not follow the game clock, so mute away from 100%. Game logic stays frame-exact, so replays (title demo, Super Guide, hint movies) stay valid. Also skips world-map animations and the flagpole tally. *Check first:* how interpolation and audio behave at 4x.
- **20. Practice tools.** Instant restart (a pause-menu button cloned the way Options was, and a reload path like the player-1 map reload, 0x809218B0, or the `d_s_restart_crsin` scene), a level timer, a death counter and an input display. The timer counts game frames, not wall time, so it is right at any frame rate or interpolation setting.
- **21. Camera zoom-out (experimental), after 21:9 ships.** Reuses the level view width (0x8042D058) and the tile-column constant (0x8042C1A4, section 1b). Risks: shows the level's edges, culling pop-in, levels designed around the fixed view. Ship as an opt-in with the same checks as ultrawide.

**Parked (with the reason):**
- Per-character physics (Luigi jumps higher, Toads run faster): the physics tables (`d_a_player_data.hpp`) are per power-up, not per character, and those traits come from SMB2. It would be a new mechanic and would desync the title demo, Super Guide and hint movies unless limited to live play.
- Challenge modifiers (low gravity, mirror mode, coin rush): fun, not a fix. Same replay caveat. After item 17.
- Off-screen player indicators for co-op: the game's answer is zooming out plus bubbles; changing it is a camera redesign.
- A full-game speedrun split timer: needs level-complete hooks; wait until item 20's level timer works.
- A toggle for the "bah" vocals: unknown whether they are a separate stream track. A stream dump would say; only then decide.

**Rejected (with the reason):**
- Uncompressed or spatial audio: only the disc's compressed audio exists, so there is no better source.
- Dynamic shadows, ambient occlusion, new lighting: large renderer work that changes the look. The game's own bloom is already an F10 option.
- Using a stored power-up mid-level: the game's inventory exists only on the world map (`dMj2dGame_c::mStockItemCount`), so this is a new mechanic needing HUD and input work.
- Swapping characters mid-level: the world-map Add/Drop screen already does it (section 7a); mid-level it conflicts with per-slot lives and power-ups.
- Vector UI: HD packs already replace the UI textures and fonts.

**Already built, no entry needed:** controller and keyboard input, shake and tilt remapping, button prompts, 21:9 (experimental), high render scales, 120/180/240/Match display interpolation, full saves on the world map, the `-` movie skip. The power-up freeze toggle is item 16 (section 7b). Riivolution and the level loader are item 6, Newer SMBW is item 13, online play stays parked (section 9).

## 8. Performance and known issues to investigate

- **180 fps interpolation falls to about 40 fps on a fast PC** (RTX 5080, Ryzen 7 7800X3D). One user report, not reproduced. That is much worse than the Iris Xe laptop, so it is probably not simply "the PC is too slow". When their details arrive, run with `NSMBW_LOG_INTERP=1` and `AURORA_LOG_WORKER_TIMING=1` at the same settings and see where the time goes.
- **Claims about performance need numbers.** The recorded Iris Xe results (2026-10-31) are 1x and 1.5x at 180 fps, 2x at 120 fps, 4x at 60 fps. There is no recorded Dolphin comparison. Measure Dolphin and this port on the same machine and settings before stating one is faster, then put the table in the README.
- **8x with 180 fps interpolation** is deliberately blocked because it ran badly (MASTER_PLAN, 2026-10-31).

## 9. Parked

- **Online multiplayer** (a self-hosted server was suggested). Needs channels 1-3 and verified co-op first, plus a determinism check across machines (hash guest RAM each frame). Rollback netcode would also need save and restore of guest RAM, thread state and emulated-hardware state. See MASTER_PLAN Phase 12 item 6.
- **PS5 and other current consoles.** Not pursued.

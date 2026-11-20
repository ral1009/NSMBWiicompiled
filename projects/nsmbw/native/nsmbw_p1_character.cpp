// Player 1 can play any character (F10 > Advanced > "Player 1 character"; docs/futureplans.md 7a),
// and the Add/Drop Players selector offers all four characters, Mario included.
//
// Which character each player slot plays is daPyMng_c::mPlayerType[4] (0x80355160, u32 per slot:
// 0 Mario, 1 Luigi, 2 Blue Toad, 3 Yellow Toad - not NSMBW-Decomp's PLAYER_TYPE_e names, which swap
// the Toads: the game draws Blue Toad for 2, its map HUD table maps 2 to P_BkinoFace, and the
// selector card showing lives of 2 makes P_kinoB visible). The save keeps it (0x800E1780 stores it,
// 0x800E1930 restores it), and almost everything else looks things up *by character* (lives
// mRest 0x80355190, power-up, HUD heads), so writing slot 0 on the world map is enough to play as
// another character.
//
// The selector (dCharacterChangeSelectBase_c, d_basesNP) is a carousel of *positions*: 0 Mario,
// 1 Blue, 2 Yellow, 3 Luigi (position <-> character tables 0x80933D00 / 0x80933D10, left as they
// are). The original only ever visits 1-3 and has pictures for those three, so nobody but slot 0
// could be Mario, and slot 0 never gets a selector. Making it offer Mario:
//   - the layout gets P_mario_00 / _01 (runtime/src/product/nsmbw_layout_patch.cpp);
//   - 0x8076FD70 (position -> card picture value, greyed when taken) knows position 0;
//   - 0x807708E0 (the selector's input state) checks Mario when confirming position 0 and scrolls
//     left down to it;
//   - 0x807736B0 (the card's "show this value") shows Mario's pane and lives for his value.
// All three only change behaviour when the layout really has the Mario panes (NsmbwSelectorHasMario).
//
// Three constant tables decide which character a slot starts on; each assumes slot 0 is Mario, so
// they are rewritten in guest memory with Mario and player 1's character c swapped (sigma: 0 <-> c):
//   0x809352A0  file-start reset, char per slot {0,1,3,2}   read by 0x807A1E80, 0x807A0440
//   0x80933CC0  a slot's starting char {0,1,3,2}             read by 0x807714C0
//   0x80933D20  slot -> starting position {0,3,2,1}          read by 0x8076FEE0
// So the file-start reset keeps player 1's character, and no other slot starts on it. (Every reader
// found through the REL's relocation table; all load the tables from memory.) Re-checked every frame
// from mPlayerType[0] (NsmbwP1CharacterTick), so a character loaded from a save takes effect before
// the next reset reads them.
#include "nsmbw_va.h"  // NSMBW_VA / NSMBW_FUNC: guest addresses are PAL rev 1, mapped per version
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" uint32_t g_nsmbwCurrentSceneProfile;
extern "C" uint32_t g_nsmbwCurrentViTick;
extern "C" bool NsmbwSelectorHasMario(); // runtime/src/product/nsmbw_layout_patch.cpp

namespace {
constexpr uint32_t kPlayerTypeAddr = NSMBW_VA(0x80355160u);  // daPyMng_c::mPlayerType[4]
constexpr uint32_t kPlayerEntryAddr = NSMBW_VA(0x80355150u); // daPyMng_c::mPlayerEntry[4]: slot is in the game
constexpr uint32_t kResetTable = NSMBW_VA(0x809352A0u);
constexpr uint32_t kStartCharTable = NSMBW_VA(0x80933CC0u);
constexpr uint32_t kStartPosTable = NSMBW_VA(0x80933D20u);
constexpr uint32_t kPosToCharTable = NSMBW_VA(0x80933D00u); // carousel position -> character
constexpr uint32_t kCharToPosTable = NSMBW_VA(0x80933D10u); // character -> carousel position
constexpr std::array<uint32_t, 4> kOrigChars = {0, 1, 3, 2};   // both character tables, per slot
constexpr std::array<uint32_t, 4> kOrigPosToChar = {0, 2, 3, 1};
constexpr std::array<uint32_t, 4> kOrigCharToPos = {0, 3, 1, 2};
// With Mario offered: the leftmost card is the highest position (screen-right moves down a position),
// so positions 3, 2, 1, 0 read Mario, Luigi, Yellow Toad, Blue Toad from the left - Mario first, the
// original three in their original order (developer request 2026-11-15; Mario had been position 0,
// the right end). The code that maps positions to characters by hand (0x8076FD70, 0x807708E0's
// confirm) reads these tables in the overrides below.
constexpr std::array<uint32_t, 4> kMarioPosToChar = {2, 3, 1, 0};
constexpr std::array<uint32_t, 4> kMarioCharToPos = {3, 2, 0, 1};

uint32_t Sigma(uint32_t ch, uint32_t c) {
    if (ch == 0) return c;
    if (ch == c) return 0;
    return ch;
}

// The five tables' wanted contents for player 1 = c, with or without Mario in the carousel.
struct Wanted {
    std::array<uint32_t, 4> chars, startPos, posToChar, charToPos;
};
Wanted WantedFor(uint32_t c, bool mario) {
    Wanted w{};
    w.posToChar = mario ? kMarioPosToChar : kOrigPosToChar;
    w.charToPos = mario ? kMarioCharToPos : kOrigCharToPos;
    for (uint32_t s = 0; s < 4; ++s) {
        w.chars[s] = Sigma(kOrigChars[s], c);
        w.startPos[s] = w.charToPos[w.chars[s]];
    }
    return w;
}

bool LogEnabled() {
    static const bool on = std::getenv("NSMBW_LOG_P1_CHARACTER") != nullptr;
    return on;
}

// -1 until the tables were found to hold either the original values or contents this file wrote;
// then the character they are swapped for (0 = original). If memory ever holds something else
// (another game version's layout, a mod), nothing is written and it is logged once.
int g_applied = -1;
bool g_appliedMario = false;
bool g_gaveUp = false;

bool Holds(uint32_t addr, const std::array<uint32_t, 4>& w) {
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t v = 0;
        if (!Memory::TryRead32(addr + 4 * i, v) || v != w[i]) return false;
    }
    return true;
}
bool TablesMatch(uint32_t c, bool mario) {
    const Wanted w = WantedFor(c, mario);
    return Holds(kResetTable, w.chars) && Holds(kStartCharTable, w.chars) && Holds(kStartPosTable, w.startPos) &&
           Holds(kPosToCharTable, w.posToChar) && Holds(kCharToPosTable, w.charToPos);
}
void Write(uint32_t addr, const std::array<uint32_t, 4>& w) {
    for (uint32_t i = 0; i < 4; ++i) Memory::Write32(addr + 4 * i, w[i]);
}

void ApplyTables(uint32_t c) {
    if (g_gaveUp) return;
    if (g_applied < 0) {
        for (uint32_t k = 0; k < 4 && g_applied < 0; ++k) {
            for (bool m : {false, true}) {
                if (g_applied < 0 && TablesMatch(k, m)) {
                    g_applied = int(k);
                    g_appliedMario = m;
                }
            }
        }
        if (g_applied < 0) {
            g_gaveUp = true;
            std::fprintf(stderr, "[nsmbw][p1chara] selector tables hold unexpected values; player 1 stays as loaded\n");
            return;
        }
    }
    // Without Mario in the selector, a slot started on Mario would sit on a position it can't show.
    const bool mario = NsmbwSelectorHasMario();
    if (!mario) c = 0;
    if (uint32_t(g_applied) == c && g_appliedMario == mario && TablesMatch(c, mario)) return;
    const Wanted w = WantedFor(c, mario);
    Write(kResetTable, w.chars);
    Write(kStartCharTable, w.chars);
    Write(kStartPosTable, w.startPos);
    Write(kPosToCharTable, w.posToChar);
    Write(kCharToPosTable, w.charToPos);
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] tables set for character %u, Mario %s (scene %u)\n", c, mario ? "offered" : "not offered", g_nsmbwCurrentSceneProfile);
    g_applied = int(c);
    g_appliedMario = mario;
}

// The character at a carousel position, from the (possibly rewritten) table.
uint32_t CharAtPos(uint32_t pos) {
    uint32_t ch = 0;
    Memory::TryRead32(kPosToCharTable + 4 * (pos & 3), ch);
    return ch;
}

uint32_t ReadType(uint32_t slot) {
    uint32_t v = 0;
    Memory::TryRead32(kPlayerTypeAddr + 4 * slot, v);
    return v;
}
} // namespace

// The model type the world-map player was built with (NsmbwWmPlayerCtor_809027C0), -1 = none yet.
int g_mapModelType = -1;
uint32_t g_mapSceneSeen = 0; // g_nsmbwCurrentSceneProfile last frame, to notice a new scene
int g_menuClosedFrames = 0;
bool g_reloadRequested = false;
// Player 1's character picked on the world map's Add/Drop screen (0x807A1510 copy), applied once
// nothing is paused any more (NsmbwP1CharacterTick); -1 = none.
int g_mapPick = -1;
// VI tick of the last frame the Add/Drop screen placed its cards (0x807A0170 runs every frame while
// it is open). The world map's Add/Drop screen does not set the game-stop word, and the selector
// writes player 1's character as soon as its card is confirmed, so without this the map reloaded
// under the open screen (2026-11-15 test).
uint32_t g_addDropSeenTick = 0;
// Set by the map Add/Drop OK (0x807A1510 copy): reload the map on the next frame instead of waiting for
// the screen to close, so the scene change's fade starts right away and covers the screen's own exit
// animation (developer request 2026-11-16: waiting for the close felt too slow).
bool g_reloadNow = false;
extern "C" bool NsmbwP1CharacterSet(int ch);

extern "C" bool NsmbwOptionsMenuIsOpen(); // runtime/src/product/nsmbw_options_menu.cpp

namespace {
// The map player's model is built once, in its constructor, and a different character is a
// different model class (0x800D6E00 allocates one of two classes by type), so it can't be swapped in
// place. After player 1 changes on the map, the map is reloaded the game's own way: 0x809218B0
// (dScGameOver_c's "back to the world map" state, referenced only from its state table) calls
// dScene_c::setNextScene(WORLD_MAP, 0x80102B50(), false), where 0x80102B50 builds the map parameter
// from dInfo_c's current world and node (+0x3C, +0x40), so the player comes back where they were,
// with the scene change's normal fade. It ignores r3. Called the way nsmbw_kpad_overrides.cpp calls
// game functions: on the game thread, with the live context saved and put back.
constexpr uint32_t kNextSceneAddr = NSMBW_VA(0x80428A40u); // dScene_c::m_nextScene (u16), 0x2EE = none pending
constexpr uint32_t kReloadMap = NSMBW_VA(0x809218B0u);
constexpr uint32_t kStartFadeOut = NSMBW_VA(0x800B0A90u); // dFader_c::startFadeOut(u16 duration)
// Fade-out length on the map Add/Drop OK: 1 = cut to black on the OK frame (developer chose it over a
// 20-frame fade, 2026-11-16); NSMBW_P1_OK_FADE_FRAMES=<n> overrides it for A/B.
uint32_t OkFadeFrames() {
    static const uint32_t frames = [] {
        const char* v = std::getenv("NSMBW_P1_OK_FADE_FRAMES");
        const long n = v ? std::strtol(v, nullptr, 10) : 1; // nsmbw-va: not an address
        return uint32_t(n < 1 ? 1 : n);
    }();
    return frames;
}
constexpr uint32_t kFaderCalc = NSMBW_VA(0x800B0B00u); // dFader_c::calc()
constexpr uint32_t kFaderPtr = NSMBW_VA(0x8042A720u);  // mFader_c::mFader (r13 - 21088)
// Set while our OK fade-out runs. The game does not run the fader's calc while the map's menus are
// up: in a 2026-11-16 log the fade-out started on the OK frame stayed in FADE_OUT (status 3) for 63
// frames, until the map's menu was back, so nothing was seen until then. While set, the tick runs
// dFader_c::calc itself on any frame the game did not (the fader's current frame, +12, unchanged).
bool g_okFadeActive = false;
uint32_t g_okFadeLastFrame = 0xFFFFFFFFu;
void KeepOkFadeRunning() {
    if (!g_okFadeActive) return;
    uint32_t fader = 0, status = 0, word = 0;
    if (!Memory::TryRead32(kFaderPtr, fader) || fader == 0 || !Memory::TryRead32(fader + 4, status) || status != 3) {
        g_okFadeActive = false; // done (OPAQUE) or taken over by the scene change
        return;
    }
    Memory::TryRead32(fader + 12, word);
    const uint32_t frame = word >> 16; // mCurrFrame (u16 at +12)
    if (frame == g_okFadeLastFrame) {
        CpuContext* ctx = CurrentCpuContext();
        const CpuContext saved = *ctx;
        InvokeDirectCpu<kFaderCalc>(ctx);
        *ctx = saved;
        Memory::TryRead32(fader + 12, word);
    }
    g_okFadeLastFrame = word >> 16;
}
constexpr uint32_t kGameStopWord = NSMBW_VA(0x8042A228u); // dGameCom game-stop flags (nsmbw_options_menu.cpp)

// Diagnostic for the "Add/Drop returned me to World 1" report: dInfo's current course (what the
// reload reads, +0x3C world / +0x40 node / +0x44) against the live map's dScWMap_c::m_WorldNo / m_SceneNo.
void LogMapPosition(const char* tag) {
    uint32_t info = 0, a = 0, b = 0, c2 = 0, wm = 0;
    Memory::TryRead32(NSMBW_VA(0x8042A25Cu), info);
    if (info) { Memory::TryRead32(info + 0x3C, a); Memory::TryRead32(info + 0x40, b); Memory::TryRead32(info + 0x44, c2); }
    Memory::TryRead32(NSMBW_VA(0x8042A52Cu), wm); // bytes: ..., m_WorldNo (0x8042A52D), m_SceneNo (0x8042A52E)
    std::fprintf(stderr, "[nsmbw][p1chara] %s: dInfo+3C/40/44 = %u/%u/%u, map world %u scene %u\n", tag, a, b, c2, (wm >> 16) & 0xFFu, (wm >> 8) & 0xFFu);
}
void ReloadMapIfModelIsStale(uint32_t c) {
    if (g_nsmbwCurrentSceneProfile != g_mapSceneSeen) { // a new scene: its map player is new
        g_mapSceneSeen = g_nsmbwCurrentSceneProfile;
        g_reloadRequested = false;
    }
    // "Nothing is paused" from the game's own stop word (0x8042A228): F10 sets HOME_MENU while it is
    // open and clears it when closed, and the map's own menus set it too. The options menu's IsOpen
    // also counts a held close key / button, which is only polled while the menu draws, so it could
    // stay "open" after closing and the reload never came (developer report 2026-11-15: Yellow Toad
    // HUD, Mario on the map).
    uint32_t stop = 0;
    Memory::TryRead32(kGameStopWord, stop);
    g_menuClosedFrames = stop != 0 ? 0 : g_menuClosedFrames + 1;
    static uint32_t frames = 0;
    if (LogEnabled() && g_nsmbwCurrentSceneProfile == 3u && (++frames % 120) == 0) {
        uint32_t w = 0;
        Memory::TryRead32(kNextSceneAddr, w);
        std::fprintf(stderr, "[nsmbw][p1chara] map check: model %d, player 1 %u, menu closed %d frames, requested %d, next scene 0x%X\n",
                     g_mapModelType, c, g_menuClosedFrames, int(g_reloadRequested), w >> 16);
        LogMapPosition("map check");
    }
    const bool now = g_reloadNow;
    g_reloadNow = false;
    if (g_nsmbwCurrentSceneProfile != 3u || g_reloadRequested || g_mapModelType < 0 || uint32_t(g_mapModelType) == c) return;
    if (!now && (g_menuClosedFrames < 2 || g_nsmbwCurrentViTick - g_addDropSeenTick < 2u)) return;
    uint32_t word = 0; // m_nextScene is the high half of this big-endian word
    if (!Memory::TryRead32(kNextSceneAddr, word) || (word >> 16) != 0x2EEu) return; // a scene change is already on its way
    g_reloadRequested = true;
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] map player was built as %d, player 1 is %u: reloading the map (tick %u)\n", g_mapModelType, c, g_nsmbwCurrentViTick);
    // The reload's parameter comes from 0x80102B50: world = dInfo_c+0x3C, sub-map = dInfo_c+0x40
    // (mode 5). Those are only the world the file was loaded or a course entered from, not where the
    // map is now: after Select World to World 5 the reload went to World 1 (2026-11-19, logged 0/0
    // against dScWMap_c::m_WorldNo 4). So copy the live map's world and sub-map in first.
    {
        uint32_t info = 0, wm = 0;
        if (Memory::TryRead32(NSMBW_VA(0x8042A25Cu), info) && info != 0 && Memory::TryRead32(NSMBW_VA(0x8042A52Cu), wm)) {
            Memory::Write32(info + 0x3C, (wm >> 16) & 0xFFu); // dScWMap_c::m_WorldNo (0x8042A52D)
            Memory::Write32(info + 0x40, (wm >> 8) & 0xFFu);  // dScWMap_c::m_SceneNo (0x8042A52E)
        }
    }
    if (LogEnabled()) LogMapPosition("before reload");
    CpuContext* ctx = CurrentCpuContext();
    const CpuContext saved = *ctx;
    InvokeDirectCpu<kReloadMap>(ctx);
    if (now) {
        // The world map holds a requested scene change until its own menu is back (~0.6 s after the
        // Add/Drop screen closes, with the old model and the HUD sliding in; 2026-11-16 shots
        // reloadt3_5..7), so start the fade-out here, on the OK frame. dFader_c::startFadeOut(u16).
        ctx->gpr[3] = OkFadeFrames();
        InvokeDirectCpu<kStartFadeOut>(ctx);
        g_okFadeActive = ctx->gpr[3] != 0;
        g_okFadeLastFrame = 0xFFFFFFFFu;
        if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] startFadeOut(%u) -> %u\n", OkFadeFrames(), ctx->gpr[3]);
    }
    *ctx = saved;
}
} // namespace

// Every frame, from the VI pump (nsmbw_tick_read_pump.cpp): keep the tables matching player 1, and
// rebuild the map if its player model is someone else.
extern "C" void NsmbwP1CharacterTick() {
    if (g_mapPick >= 0) {
        uint32_t stop = 0;
        Memory::TryRead32(kGameStopWord, stop);
        if (g_reloadNow || (stop == 0 && g_nsmbwCurrentViTick - g_addDropSeenTick >= 2u)) { // OK pressed / screen closed
            const int pick = g_mapPick;
            g_mapPick = -1;
            if (ReadType(0) != uint32_t(pick) && !NsmbwP1CharacterSet(pick) && LogEnabled())
                std::fprintf(stderr, "[nsmbw][p1chara] map pick %d refused\n", pick);
        }
    }
    KeepOkFadeRunning();
    const uint32_t c = ReadType(0);
    if (c > 3) return;
    ApplyTables(c);
    ReloadMapIfModelIsStale(c);
}

// For the options menu (nsmbw_options_menu.cpp). Character numbers as in mPlayerType.
extern "C" int NsmbwP1CharacterGet() {
    const uint32_t c = ReadType(0);
    return c <= 3 ? int(c) : 0;
}

// Only on the world map (scene 3): in a level or a menu the player actors and the selector have
// already read the table. And not a character another player in the game is using: their actor on
// the map would keep the old model until the next scene.
extern "C" bool NsmbwP1CharacterCanSet(int ch) {
    if (ch < 0 || ch > 3 || g_nsmbwCurrentSceneProfile != 3u || g_gaveUp) return false;
    for (uint32_t s = 1; s < 4; ++s) {
        uint32_t entry = 0;
        Memory::TryRead32(kPlayerEntryAddr + 4 * s, entry);
        if (entry != 0 && ReadType(s) == uint32_t(ch)) return false;
    }
    return true;
}

// Swaps with whichever slot has `ch`, so the four slots keep four different characters (the
// selector's "taken" check and the save both assume that). Runs on the game thread: the options menu
// is drawn and driven inside the VI retrace.
extern "C" bool NsmbwP1CharacterSet(int ch) {
    if (!NsmbwP1CharacterCanSet(ch)) return false;
    const uint32_t old = ReadType(0);
    if (old == uint32_t(ch)) return true;
    for (uint32_t s = 1; s < 4; ++s) {
        if (ReadType(s) == uint32_t(ch)) Memory::Write32(kPlayerTypeAddr + 4 * s, old);
    }
    Memory::Write32(kPlayerTypeAddr, uint32_t(ch));
    ApplyTables(uint32_t(ch));
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] player 1 %u -> %d\n", old, ch);
    return true;
}


// The world-map player's constructor (daWmPlayer_c, d_basesNP 0x809027C0, called from 0x80902790).
// It builds its model manager (dPyMdlMng_c at +460, constructor 0x800D6DB0) with model type 0 =
// Mario's model, whoever player 1 is: the HUD showed Luigi's head while the map still walked Mario
// (2026-11-15 test, shots/p1luigi_level_0.png). The model class is chosen in that constructor
// (0x800D6E00), so it has to get the right type here: the model type is the character number itself.
// Evidence: with player 1 as Blue Toad (2), using the REL's {0,1,3,2} table at 0x8093CDF0 (read by
// 0x808EB7D0 for the other map players, indexed by slot) gave type 3 and drew Yellow Toad, Yellow
// being character 3 (shots/p1kinob_map2_0.png); that table is slot -> default character, not
// character -> model. Takes effect when a map scene is created.
// Structural copy of build/nsmbw-d_basesNP/functions/func_809027C0.cpp; one EDIT line.
namespace {
uint32_t MapPlayerModelType() { // 0..3; remembered so a later change can reload the map
    g_mapModelType = NsmbwP1CharacterGet();
    g_reloadRequested = false; // a new map player: a later change may reload again (a map -> map
                               // reload keeps the scene profile, so the per-scene reset missed it)
    return uint32_t(g_mapModelType);
}
} // namespace

extern "C" void NsmbwWmPlayerCtor_809027C0(CpuContext* MKW_RESTRICT ctx)
{
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];

    goto loc_809027C0;

loc_809027C0:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    ctx->lr = NSMBW_VA(0x809027DCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    InvokeDirectCpu<NSMBW_VA(0x800F60E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    r5 = NSMBW_VA_PAIR(0x80990000u, -27656);
    r0 = -1;
    r5 = (r5 + -27656);
    r31 = 0;
    r6 = 1;
    MemoryInline::FlatWrite8((r30 + 396), static_cast<uint8_t>(r31));
    r3 = (r30 + 460);
    r4 = MapPlayerModelType(); // EDIT: was r4 = 0 (always Mario's model)
    MemoryInline::FlatWrite8((r30 + 424), static_cast<uint8_t>(r6));
    MemoryInline::FlatWrite32((r30 + 456), r0);
    MemoryInline::FlatWrite32((r30 + 428), r0);
    MemoryInline::FlatWrite32((r30 + 432), r0);
    MemoryInline::FlatWrite32((r30 + 96), r5);
    ctx->lr = NSMBW_VA(0x80902814u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    InvokeDirectCpu<NSMBW_VA(0x800D6DB0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    r5 = NSMBW_VA_PAIR(0x80380000u, -32568);
    r3 = NSMBW_VA_PAIR(0x80940000u, -10428);
    r4 = (r5 + -32568);
    f3.d = MemoryInline::FlatReadFloat32((r5 + -32568));
    f1.d = MemoryInline::FlatReadFloat32((r4 + 8));
    r5 = 6;
    f2.d = MemoryInline::FlatReadFloat32((r4 + 4));
    r0 = 2;
    f0.d = MemoryInline::FlatReadFloat32((r3 + -10428));
    r4 = NSMBW_VA_PAIR(0x80430000u, -23424);
    guest_range_0 = MemoryInline::ResolveRangeHost((r30 + 496), 0, 269u, false, true);
    MemoryInline::WriteResolvedFloat32(guest_range_0, 0u, (r30 + 496), f3.d);
    r3 = r30;
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 4u, ((static_cast<uint64_t>(PpcBitCastToU32Inline(static_cast<float>(f2.d))) << 32) | PpcBitCastToU32Inline(static_cast<float>(f1.d))))) {
        MemoryInline::WriteResolvedFloat32(guest_range_0, 4u, (r30 + 500), f2.d);
        MemoryInline::WriteResolvedFloat32(guest_range_0, 8u, (r30 + 504), f1.d);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 76u, ((static_cast<uint64_t>(static_cast<uint32_t>(r5)) << 32) | static_cast<uint32_t>(r5)))) {
        MemoryInline::WriteResolved32(guest_range_0, 76u, (r30 + 572), r5);
        MemoryInline::WriteResolved32(guest_range_0, 80u, (r30 + 576), r5);
    }
    MemoryInline::WriteResolved8(guest_range_0, 106u, (r30 + 602), static_cast<uint8_t>(r31));
    MemoryInline::WriteResolved8(guest_range_0, 107u, (r30 + 603), static_cast<uint8_t>(r31));
    MemoryInline::WriteResolved32(guest_range_0, 140u, (r30 + 636), r31);
    MemoryInline::WriteResolvedFloat32(guest_range_0, 168u, (r30 + 664), f0.d);
    MemoryInline::WriteResolved32(guest_range_0, 240u, (r30 + 736), r0);
    MemoryInline::WriteResolved8(guest_range_0, 268u, (r30 + 764), static_cast<uint8_t>(r31));
    MemoryInline::FlatWriteRam32((r4 + -23424), r30);
    MemoryInline::WriteResolved32(guest_range_0, 172u, (r30 + 668), r31);
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r30 = MemoryInline::FlatRead32((r1 + 8));
    r0 = MemoryInline::FlatRead32((r1 + 20));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(809027C0, NsmbwWmPlayerCtor_809027C0, (CpuContext* ctx), (ctx));

// ---- Mario in the selector ----------------------------------------------------------------------
//
// The card (dCharacterChangeSelectContents_c) shows a "value" per card: 0 Blue, 1 Yellow, 2 Luigi,
// +3 for the greyed-out (taken) variant, 6 nothing. 0x8076FD70 turns a carousel position into that
// value (table 0x80933CF0 {6,0,1,2}) and 0x807736B0 hands it to the card setter 0x80773220, whose
// picture functions are written for those three characters (0x807730A0 Luigi: lives at +680, pane
// slot card+2; 0x80773120 Blue; 0x807731A0 Yellow). Mario gets values 7 / 10: the card setter is
// run as for Luigi (2 / 5, so the greyed-out shadow and the common panes behave the same), then
// Luigi's pane is swapped for the layout's P_mario_0<card> and the lives text is redrawn from
// +676, which the card's lives cache (0x80773840) already fills with Mario's lives.
namespace {
constexpr uint32_t kPosToCardValue = NSMBW_VA(0x80933CF0u); // {6,0,1,2}
constexpr uint32_t kIsTaken = NSMBW_VA(0x8076FC80u);        // (selector, character) -> another joined slot has it
constexpr uint32_t kCardSetter = NSMBW_VA(0x80773220u);     // (card object, value, card index)
constexpr uint32_t kDispNumber = NSMBW_VA(0x800B3B60u);     // dGameCom::LayoutDispNumber(const int&, const int&, LytTextBox_c*, bool)
constexpr uint32_t kLivesDigits = NSMBW_VA(0x8093F520u);    // the digit count 0x807730A0 passes
constexpr uint32_t kMarioValue = 7, kMarioTaken = 10;

bool SelectorOffersMario() { return NsmbwSelectorHasMario(); }

// nw4r::lyt::Pane: parent +0x0C, child list +0x10 (count, then the end node: next +0x14, prev
// +0x18), own list node +0x04 (next, prev), visibility = bit 0 of +0xBB, name +0xBC.
uint32_t FindSiblingByPrefix(uint32_t pane, const char* prefix) {
    uint32_t parent = 0;
    if (pane == 0 || !Memory::TryRead32(pane + 0x0C, parent) || parent == 0) return 0;
    const uint32_t end = parent + 0x14;
    uint32_t node = 0;
    Memory::TryRead32(end, node);
    for (int guard = 0; node != 0 && node != end && guard < 64; ++guard) {
        const uint32_t p = node - 4;
        bool match = true;
        for (size_t i = 0; prefix[i] != 0 && match; ++i) match = Memory::Read8(p + 0xBC + uint32_t(i)) == uint8_t(prefix[i]);
        if (match) return p;
        if (!Memory::TryRead32(node, node)) break;
    }
    return 0;
}
void SetVisible(uint32_t pane, bool on) {
    if (pane == 0) return;
    const uint8_t f = Memory::Read8(pane + 0xBB);
    Memory::Write8(pane + 0xBB, on ? uint8_t(f | 1) : uint8_t(f & ~1));
}

// Replaces the card setter call in 0x807736B0 (r3 card object, r4 value, r5 card index).
void ShowCardValue(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3], value = ctx->gpr[4], card = ctx->gpr[5] & 1;
    const bool mario = SelectorOffersMario() && (value == kMarioValue || value == kMarioTaken);
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] card 0x%08X[%u] shows value %u\n", self, card, value);
    if (mario) ctx->gpr[4] = value == kMarioValue ? 2 : 5; // as Luigi, then swapped below
    InvokeDirectCpu<kCardSetter>(ctx);
    if (!SelectorOffersMario()) return;
    uint32_t lui = 0;
    Memory::TryRead32(self + 596 + (card + 2) * 4, lui);
    const uint32_t marioPane = FindSiblingByPrefix(lui, "P_mario_");
    if (marioPane == 0) {
        static bool logged = false;
        if (!logged) std::fprintf(stderr, "[nsmbw][p1chara] selector card has no P_mario pane next to 0x%08X\n", lui);
        logged = true;
        return;
    }
    SetVisible(marioPane, mario);
    if (!mario) return;
    SetVisible(lui, false);
    const CpuContext saved = *ctx;
    ctx->gpr[3] = self + 676; // Mario's lives in the card's lives cache
    ctx->gpr[4] = kLivesDigits;
    ctx->gpr[5] = Memory::Read32(self + 628 + card * 4); // the card's lives text box
    ctx->gpr[6] = 0;
    InvokeDirectCpu<kDispNumber>(ctx);
    *ctx = saved;
}
} // namespace

// 0x8076FD70 (selector, card index, position): writes the card's next value to the card object
// (+656 for card 0, +664 for card 1; the object is at selector+116). Native version of the original:
// value from 0x80933CF0, then "is this character taken by another joined slot" (0x8076FC80, passed
// the character: Luigi 1 for value 2, Yellow 3 for 1, Blue 2 for 0) turns 0..2 into 3..5. Added:
// position 0 is Mario, value 7, taken 10. Called directly from six places (0x80770790, 0x80770850,
// 0x807708E0, 0x80770BF0, 0x80770D00, 0x8076FEE0), never inlined.
extern "C" void NsmbwSelectorCardValue_8076FD70(CpuContext* MKW_RESTRICT ctx) {
    const CpuContext saved = *ctx;
    const uint32_t self = ctx->gpr[3], card = ctx->gpr[4], pos = ctx->gpr[5];
    uint32_t value = 6;
    Memory::TryRead32(kPosToCardValue + 4 * pos, value);
    uint32_t ch = card; // the original leaves r4 as it was for a value it has no character for
    if (value == 2) ch = 1;
    if (value == 1) ch = 3;
    if (value == 0) ch = 2;
    if (SelectorOffersMario()) { // the character from the rewritten table, then its card value
        ch = CharAtPos(pos);
        static constexpr uint32_t kValueForChar[4] = {kMarioValue, 2, 0, 1}; // Mario, Luigi, Blue, Yellow
        value = kValueForChar[ch & 3];
    }
    ctx->gpr[3] = self;
    ctx->gpr[4] = ch;
    InvokeDirectCpu<kIsTaken>(ctx);
    if (ctx->gpr[3] != 0) {
        if (value <= 2) value += 3;
        else if (value == kMarioValue) value = kMarioTaken;
    }
    *ctx = saved;
    const uint32_t cardObject = Memory::Read32(self + 116);
    if (LogEnabled() && Memory::Read32(cardObject + (card == 0 ? 656u : 664u)) != value) {
        std::fprintf(stderr, "[nsmbw][p1chara] selector 0x%08X slot %d pos %u -> card 0x%08X[%u] value %u\n", self,
                     int(Memory::Read32(self + 728)), pos, cardObject, card, value);
    }
    Memory::Write32(cardObject + (card == 0 ? 656u : 664u), value);
}

PPC_NATIVE_OVERRIDE_VOID(8076FD70, NsmbwSelectorCardValue_8076FD70, (CpuContext* ctx), (ctx));

// The selector's input state (0x807708E0, reached through its state table). Structural copy of
// build/nsmbw-d_basesNP/functions/func_807708E0.cpp. EDITs: confirming position 0 checks whether
// Mario is taken (the original's check maps positions 3/2/1 to Luigi/Yellow/Blue by hand and let
// anything else through), and left scrolls down to position 0 instead of stopping at 1. Right still
// stops at 3, so the carousel reads Mario, Blue, Yellow, Luigi.
extern "C" void NsmbwSelectorInput_807708E0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r0_rot_6 = 0;
    uint32_t r3_addr_0 = 0;
    uint32_t r3_addr_1 = 0;
    uint32_t r3_addr_2 = 0;
    uint32_t r3_addr_3 = 0;
    uint32_t r5_addr_0 = 0;
    uint32_t r5_addr_1 = 0;
    uint32_t r5_addr_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_807708E0;

loc_807708E0:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    r31 = NSMBW_VA_PAIR(0x80990000u, 96);
    r31 = (r31 + 96);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    r30 = r3;
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    ctx->lr = NSMBW_VA(0x80770908u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x8076FAE0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8077090C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80770934;
    }
}

loc_80770910:
{
    r0 = -1;
    MemoryInline::FlatWrite32((r30 + 728), r0);
    r3 = (r30 + 552);
    r4 = (r31 + 336);
    r12 = MemoryInline::FlatRead32((r30 + 552));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80770930u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80770B24;
}

loc_80770934:
{
    r5 = MemoryInline::FlatRead32((r30 + 736));
    r3 = r30;
    r4 = 0;
    ctx->lr = NSMBW_VA(0x80770944u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8076FD70u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    r0 = MemoryInline::FlatRead32((r30 + 728));
    r3 = NSMBW_VA_PAIR(0x80370000u, 32648);
    r5 = NSMBW_VA_PAIR(0x80430000u, -24016);
    r4 = 2304;
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r3 = (r3 + 32648);
    r3_addr_1 = (r3 + r0);
    r3 = MemoryInline::FlatRead32(r3_addr_1);
    r29 = MemoryInline::FlatRead32((r5 + -24016));
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 32));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80770974u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80770978:
{
    // NSMBW_LOG_P1_CHARACTER: every confirm press the selector sees, with its slot and position
    // (found that a scripted player 2's "join" press was the confirm: a scripted remote is connected
    // from the start, so its selector opens without joining).
    if (LogEnabled() && r3 != 0) {
        uint32_t slot = 0, pos = 0;
        Memory::TryRead32(r30 + 728, slot);
        Memory::TryRead32(r30 + 736, pos);
        std::fprintf(stderr, "[nsmbw][p1chara] selector slot %u confirm pressed at position %u (tick %u)\n", slot, pos, g_nsmbwCurrentViTick);
    }
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_80770A6C;
    }
}

loc_8077097C:
{
    r0 = MemoryInline::FlatRead32((r30 + 736));
    r3 = NSMBW_VA_PAIR(0x80930000u, 15664);
    r3 = (r3 + 15664);
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & -4);
    r3_addr_2 = (r3 + r0);
    r0 = MemoryInline::FlatRead32(r3_addr_2);
    if (SelectorOffersMario()) {
        // EDIT: the checks below map 3/2/1 to Luigi/Yellow/Blue by hand (and 0 to Mario, added);
        // with the rewritten order, give them the code of the character actually at this position.
        static constexpr uint32_t kCodeForChar[4] = {0, 3, 1, 2}; // Mario, Luigi, Blue, Yellow
        r0 = kCodeForChar[CharAtPos(MemoryInline::FlatRead32((r30 + 736))) & 3];
    }
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_80770994:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807709AC;
    }
}

loc_80770998:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(2));
}

loc_8077099C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807709C4;
    }
}

loc_807709A0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(1));
}

loc_807709A4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807709DC;
    }
}

loc_807709A8:
{
    // EDIT: position 0 (Mario) is checked like the others; the original never reached it.
    if (SelectorOffersMario() && r0 == 0) {
        goto loc_mario_check;
    }
    goto loc_807709F0;
}

loc_mario_check:
{
    r3 = r30;
    r4 = 0; // EDIT: character 0, Mario
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8076FC80u)>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_mario_check_result:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807709F0;
    }
}

{
    goto loc_80770B24; // taken: refuse, as for the other characters
}


loc_807709AC:
{
    r3 = r30;
    r4 = 1;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8076FC80u)>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_807709BC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807709F0;
    }
}

loc_807709C0:
{
    goto loc_80770B24;
}

loc_807709C4:
{
    r3 = r30;
    r4 = 3;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8076FC80u)>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_807709D4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807709F0;
    }
}

loc_807709D8:
{
    goto loc_80770B24;
}

loc_807709DC:
{
    r3 = r30;
    r4 = 2;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8076FC80u)>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_807709EC:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80770B24;
    }
}

loc_807709F0:
{
    r4 = MemoryInline::FlatRead32((r30 + 648));
    r3 = MemoryInline::FlatRead32(r4);
    r0 = (r3 + 1);
    MemoryInline::FlatWrite32(r4, r0);
    r4 = MemoryInline::FlatRead32((r30 + 648));
    r3 = MemoryInline::FlatRead32((r30 + 744));
    r0 = MemoryInline::FlatRead32(r4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(r3));
}

loc_80770A10:
{
    if (((cr & NSMBW_VA(0x80000000u)) != 0)) {
        goto loc_80770A38;
    }
}

loc_80770A14:
{
    MemoryInline::FlatWrite32(r4, r3);
    r3 = NSMBW_VA_PAIR(0x80430000u, -30142);
    r0 = MemoryInline::FlatRead16((r3 + -30142));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(3));
}

loc_80770A24:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80770A38;
    }
}

loc_80770A28:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22648);
    r4 = 3;
    r3 = MemoryInline::FlatRead32((r3 + -22648));
    ctx->lr = NSMBW_VA(0x80770A38u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8019BB60u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
}

loc_80770A38:
{
    r0 = MemoryInline::FlatRead32((r30 + 736));
    r5 = NSMBW_VA_PAIR(0x80930000u, 15664);
    r5 = (r5 + 15664);
    r3 = (r30 + 552);
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & -4);
    r4 = (r31 + 720);
    r5_addr_1 = (r5 + r0);
    r0 = MemoryInline::FlatRead32(r5_addr_1);
    MemoryInline::FlatWrite32((r30 + 672), r0);
    r12 = MemoryInline::FlatRead32((r30 + 552));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80770A68u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80770B24;
}

loc_80770A6C:
{
    r4 = MemoryInline::FlatRead32((r30 + 728));
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(2));
    r0 = (r0_rot_4 & -4);
    r3 = (r29 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 4));
    r0 = MemoryInline::FlatRead32((r3 + 28));
    r0 = (r0 & 8);
}

loc_80770A84:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80770ACC;
    }
}

loc_80770A88:
{
    r3 = MemoryInline::FlatRead32((r30 + 736));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(3));
}

loc_80770A90:
{
    if (((cr & NSMBW_VA(0x80000000u)) == 0)) {
        goto loc_80770ACC;
    }
}

loc_80770A94:
{
    r4 = 5;
    MemoryInline::FlatWrite32((r30 + 672), r4);
    r0 = (r3 + 1);
    r5 = MemoryInline::FlatRead32((r30 + 120));
    MemoryInline::FlatWrite32((r30 + 736), r0);
    r0 = 1;
    r3 = (r30 + 552);
    r4 = (r31 + 656);
    MemoryInline::FlatWrite32((r5 + 600), r0);
    r12 = MemoryInline::FlatRead32((r30 + 552));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80770AC8u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80770B24;
}

loc_80770ACC:
{
    r0_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(2));
    r0 = (r0_rot_5 & -4);
    r3 = (r29 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 4));
    r0 = MemoryInline::FlatRead32((r3 + 28));
    r0 = (r0 & 4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80770AE0:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80770B24;
    }
}

loc_80770AE4:
{
    r3 = MemoryInline::FlatRead32((r30 + 736));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(SelectorOffersMario() ? 0 : 1)); // EDIT: was 1 (left stopped at position 1)
}

loc_80770AEC:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_80770B24;
    }
}

loc_80770AF0:
{
    r4 = 4;
    MemoryInline::FlatWrite32((r30 + 672), r4);
    r0 = (r3 + -1);
    r5 = MemoryInline::FlatRead32((r30 + 120));
    MemoryInline::FlatWrite32((r30 + 736), r0);
    r0 = 2;
    r3 = (r30 + 552);
    r4 = (r31 + 656);
    MemoryInline::FlatWrite32((r5 + 600), r0);
    r12 = MemoryInline::FlatRead32((r30 + 552));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80770B24u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80770B24:
{
    r0 = MemoryInline::FlatRead32((r1 + 36));
    r31 = MemoryInline::FlatRead32((r1 + 28));
    r30 = MemoryInline::FlatRead32((r1 + 24));
    r29 = MemoryInline::FlatRead32((r1 + 20));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807708E0, NsmbwSelectorInput_807708E0, (CpuContext* ctx), (ctx));

// The card's "show the new value" step (0x807736B0, called from its execute 0x80772F50). Structural
// copy of build/nsmbw-d_basesNP/functions/func_807736B0.cpp; EDIT: both card-setter calls go through
// ShowCardValue, which draws Mario for his values.
extern "C" void NsmbwSelectorCardShow_807736B0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_807736B0;

loc_807736B0:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r0 = MemoryInline::FlatRead32((r3 + 652));
    r4 = MemoryInline::FlatRead32((r3 + 656));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(r4));
}

loc_807736D0:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807736E0;
    }
}

loc_807736D4:
{
    MemoryInline::FlatWrite32((r3 + 652), r4);
    r5 = 0;
    ctx->lr = NSMBW_VA(0x807736E0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ShowCardValue(ctx); // EDIT: was InvokeDirectCpu<0x80773220u>(ctx)
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_807736E0:
{
    r0 = MemoryInline::FlatRead32((r31 + 660));
    r4 = MemoryInline::FlatRead32((r31 + 664));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(r4));
}

loc_807736EC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80773700;
    }
}

loc_807736F0:
{
    MemoryInline::FlatWrite32((r31 + 660), r4);
    r3 = r31;
    r5 = 1;
    ctx->lr = NSMBW_VA(0x80773700u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ShowCardValue(ctx); // EDIT: was InvokeDirectCpu<0x80773220u>(ctx)
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_80773700:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807736B0, NsmbwSelectorCardShow_807736B0, (CpuContext* ctx), (ctx));

// The selector arrows (dCharacterChangeSelectArrow_c, 0x8076E600, reached through its state table):
// hides one arrow at the first position and the other at position 3, from the position the
// selector copies into the arrow object (+612, written by 0x80770BF0). With Mario at position 0
// the first position is 0, not 1 (2026-11-15 test: both arrows showed on Mario's card).
// Structural copy of build/nsmbw-d_basesNP/functions/func_8076E600.cpp; one EDIT line.
extern "C" void NsmbwSelectorArrows_8076E600(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r12 = ctx->gpr[12];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_8076E600;

loc_8076E600:
{
    r0 = MemoryInline::FlatRead32((r3 + 612));
}

loc_8076E608:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(SelectorOffersMario() ? 0 : 1))) { // EDIT: was 1
        goto loc_8076E62C;
    }
}

loc_8076E60C:
{
    r4 = 1;
    r0 = 0;
    MemoryInline::FlatWrite32((r3 + 596), r4);
    r4 = MemoryInline::FlatRead32((r3 + 584));
    MemoryInline::FlatWrite32((r3 + 592), r0);
    r0 = MemoryInline::FlatRead8((r4 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r4 + 187), static_cast<uint8_t>(r0));
}

loc_8076E62C:
{
    r0 = MemoryInline::FlatRead32((r3 + 612));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_8076E634:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8076E658;
    }
}

loc_8076E638:
{
    r4 = 0;
    r0 = 1;
    MemoryInline::FlatWrite32((r3 + 596), r4);
    r4 = MemoryInline::FlatRead32((r3 + 588));
    MemoryInline::FlatWrite32((r3 + 592), r0);
    r0 = MemoryInline::FlatRead8((r4 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r4 + 187), static_cast<uint8_t>(r0));
}

loc_8076E658:
{
    r0 = MemoryInline::FlatRead32((r3 + 612));
    r4 = NSMBW_VA_PAIR(0x80990000u, -144);
    MemoryInline::FlatWrite32((r3 + 608), r0);
    r4 = (r4 + -144);
    r3 = (r3 + 520);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectJump(ctr, ctx);
    return;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(8076E600, NsmbwSelectorArrows_8076E600, (CpuContext* ctx), (ctx));

// The selector arrows' per-frame update (0x8076EA00): when the position (+612) changed, it plays
// the out animation of the arrow at an end (position 1 in the original, 3 at the other end) and
// the in animation of one that was hidden. Same first-end fix as 0x8076E600 (2026-11-15 test:
// scrolling back to Mario left both arrows up). Structural copy of
// build/nsmbw-d_basesNP/functions/func_8076EA00.cpp; one EDIT line.
extern "C" void NsmbwSelectorArrowsUpdate_8076EA00(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r3_addr_0 = 0;
    uint32_t r3_addr_1 = 0;
    uint32_t r3_addr_2 = 0;
    uint32_t r4_addr_0 = 0;
    uint32_t r4_addr_1 = 0;
    uint32_t r4_addr_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_8076EA00;

loc_8076EA00:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r4 = -1;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r3 = (r3 + 112);
    // inline leaf 0x800C9700 (9 guest instruction(s))
}

loc_inl0_0x800C9700:
{
}

loc_inl0_0x800C9704:
{
    if ((static_cast<int32_t>(r4) >= static_cast<int32_t>(0))) {
        goto loc_inl0_0x800C9718;
    }
}

loc_inl0_0x800C9708:
{
    r4 = MemoryInline::FlatRead32((r3 + 392));
    r0 = MemoryInline::FlatRead32((r3 + 404));
    r4_addr_0 = (r4 + r0);
    r3 = MemoryInline::FlatRead8(r4_addr_0);
    goto loc_inl0_cont_800C9700;
}

loc_inl0_0x800C9718:
{
    r3 = MemoryInline::FlatRead32((r3 + 392));
    r3_addr_1 = (r3 + r4);
    r3 = MemoryInline::FlatRead8(r3_addr_1);
}

loc_inl0_cont_800C9700:
{
    // end of inlined leaf 0x800C9700
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8076EA24:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8076EB50;
    }
}

loc_8076EA28:
{
    r3 = MemoryInline::FlatRead32((r31 + 612));
    r0 = MemoryInline::FlatRead32((r31 + 608));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(r3));
}

loc_8076EA34:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8076EB34;
    }
}

loc_8076EA38:
{
}

loc_8076EA3C:
{
    MemoryInline::FlatWrite32((r31 + 608), r3);
    if ((static_cast<int32_t>(r3) != static_cast<int32_t>(SelectorOffersMario() ? 0 : 1))) { // EDIT: was 1
        goto loc_8076EA84;
    }
}

loc_8076EA44:
{
    r0 = MemoryInline::FlatRead32((r31 + 592));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(2));
}

loc_8076EA4C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8076EAB8;
    }
}

loc_8076EA50:
{
    r3 = (r31 + 112);
    r4 = 2;
    ctx->lr = NSMBW_VA(0x8076EA5Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C9580u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = 3;
    MemoryInline::FlatWrite32((r31 + 592), r0);
    r4 = NSMBW_VA_PAIR(0x80990000u, -144);
    r3 = (r31 + 520);
    r12 = MemoryInline::FlatRead32((r31 + 520));
    r4 = (r4 + -144);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8076EA80u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8076EB50;
}

loc_8076EA84:
{
    r0 = MemoryInline::FlatRead32((r31 + 596));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8076EA8C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8076EAB8;
    }
}

loc_8076EA90:
{
    r0 = 1;
    MemoryInline::FlatWrite32((r31 + 596), r0);
    r4 = NSMBW_VA_PAIR(0x80990000u, -144);
    r3 = (r31 + 520);
    r12 = MemoryInline::FlatRead32((r31 + 520));
    r4 = (r4 + -144);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8076EAB4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8076EB50;
}

loc_8076EAB8:
{
}

loc_8076EABC:
{
    if ((static_cast<int32_t>(r3) != static_cast<int32_t>(3))) {
        goto loc_8076EB00;
    }
}

loc_8076EAC0:
{
    r0 = MemoryInline::FlatRead32((r31 + 596));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(2));
}

loc_8076EAC8:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8076EB34;
    }
}

loc_8076EACC:
{
    r3 = (r31 + 112);
    r4 = 3;
    ctx->lr = NSMBW_VA(0x8076EAD8u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C9580u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = 3;
    MemoryInline::FlatWrite32((r31 + 596), r0);
    r4 = NSMBW_VA_PAIR(0x80990000u, -144);
    r3 = (r31 + 520);
    r12 = MemoryInline::FlatRead32((r31 + 520));
    r4 = (r4 + -144);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8076EAFCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8076EB50;
}

loc_8076EB00:
{
    r0 = MemoryInline::FlatRead32((r31 + 592));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8076EB08:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8076EB34;
    }
}

loc_8076EB0C:
{
    r0 = 1;
    MemoryInline::FlatWrite32((r31 + 592), r0);
    r4 = NSMBW_VA_PAIR(0x80990000u, -144);
    r3 = (r31 + 520);
    r12 = MemoryInline::FlatRead32((r31 + 520));
    r4 = (r4 + -144);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8076EB30u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8076EB50;
}

loc_8076EB34:
{
    r12 = MemoryInline::FlatRead32((r31 + 520));
    r4 = NSMBW_VA_PAIR(0x80990000u, -80);
    r3 = (r31 + 520);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    r4 = (r4 + -80);
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8076EB50u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_8076EB50:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(8076EA00, NsmbwSelectorArrowsUpdate_8076EA00, (CpuContext* ctx), (ctx));


// ---- Player 1's own selector after "1 player" -----------------------------------------------------
//
// Game setup (scene 10). The "Number of Players" screen (dSelectPlayer_c) hands its choice to
// dScGameSetup_c's StartMember state (0x80918B00). For 2-4 it starts the Add/Drop screen
// (dNumberOfPeopleChange_c, "N Players") with player count +1732 = N, and that screen runs a
// selector for each *extra* player; player 1 is pre-joined (state 3 at dInfo_c+0x384, its remote 0
// reserved in the screen's slot -> remote table +1744) and just stands there. For 1 it skipped the
// screen. The screen keeps four selector objects (+116..+128); it starts and places 1..N-1 (with one,
// selector 1 is the card in the middle). With these EDITs, "1 player" opens the screen with count 1,
// player 1 not joined and remote 0 not reserved, and the loops cover N selectors from selector 1
// instead of N - 1. Selector 1 claims the first free connected remote at the start of its select
// state (0x8076FAE0, called from 0x807708E0) - player 1's - so player 1 picks from all four characters
// on the middle card. (Starting selector 0 instead worked logically, but its card is not placed on
// screen: the visible card stayed selector 1's blank one, 2026-11-15 test.) The choice is
// committed like any other player's (verified first that a 2-player game setup keeps player 2's
// choice, 2026-11-15: Yellow Toad on the map). All of it only when OnePlayerSelect().
namespace {
constexpr uint32_t kNowScene = NSMBW_VA(0x80428A42u); // dScene_c::m_nowScene (u16)
bool InGameSetup() {
    uint32_t word = 0; // m_nowScene is the low half of the big-endian word at 0x80428A40
    return Memory::TryRead32(kNowScene - 2, word) && (word & 0xFFFFu) == 10u;
}
bool OnWorldMap() {
    uint32_t word = 0;
    return Memory::TryRead32(kNowScene - 2, word) && (word & 0xFFFFu) == 3u;
}
// The Add/Drop screen at `screen` has player 1 choosing too (any player count): in game setup, and
// since 2026-11-15 on the world map ("Add/Drop Players"), which replaced the F10 option.
bool OnePlayerSelect(uint32_t screen) {
    uint32_t count = 0;
    return SelectorOffersMario() && (InGameSetup() || OnWorldMap()) && Memory::TryRead32(screen + 1732, count) &&
           count >= 1 && count <= 4;
}

// The first selector the screen runs in that mode: with one player the middle card (selector 1, the
// only one the screen places for a count of 1), otherwise selector 0, the left column where player 1
// used to stand (2026-11-15: with 2-4 players player 1 had no selector, and its standing figure was
// drawn at its character's place, behind player 2's card).
uint32_t FirstSelector(uint32_t screen) {
    uint32_t count = 0;
    Memory::TryRead32(screen + 1732, count);
    return count == 1 ? 1u : 0u;
}
// dScGameSetup_c with its "Number of Players" screen at +208 set to "1".
bool OnePlayerChosen(uint32_t gameSetup) {
    uint32_t selectPlayer = 0, index = 1;
    if (!SelectorOffersMario() || !Memory::TryRead32(gameSetup + 208, selectPlayer) || selectPlayer == 0) return false;
    return Memory::TryRead32(selectPlayer + 620, index) && index == 0;
}
} // namespace

// dScGameSetup_c StartMember execute (0x80918B00; state table 0x8098B3A4). After the "Number of
// Players" screen (+208) finishes (+619) with a choice (+624 == 1) it writes the join states
// dInfo+0x384 = {3,0,0,0}; index +620 == 0 went to the 1-player path (LowBatteryCheck), otherwise the
// Add/Drop screen (+184) gets +1662 = 1 and count +1732 = index + 1 and the scene goes to
// ConnectionCheck. Structural copy of build/nsmbw-d_basesNP/functions/func_80918B00.cpp.
extern "C" void NsmbwGameSetupStartMember_80918B00(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r12 = ctx->gpr[12];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80918B00;

loc_80918B00:
{
    r4 = MemoryInline::FlatRead32((r3 + 208));
    r6 = NSMBW_VA_PAIR(0x809A0000u, 3048);
    r6 = (r6 + 3048);
    r0 = MemoryInline::FlatRead8((r4 + 619));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80918B14:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_return;
    }
}

loc_80918B18:
{
    r0 = MemoryInline::FlatRead32((r4 + 624));
}

loc_80918B20:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(1))) {
        goto loc_80918BCC;
    }
}

loc_80918B24:
{
    r4 = NSMBW_VA_PAIR(0x80430000u, -23972);
    r0 = 3;
    r4 = MemoryInline::FlatRead32((r4 + -23972));
    r8 = 0;
    if (SelectorOffersMario()) r0 = 0; // EDIT: player 1 starts not joined, so it gets a selector
    MemoryInline::FlatWrite32((r4 + 900), r0);
    MemoryInline::FlatWrite32((r4 + 904), r8);
    MemoryInline::FlatWrite32((r4 + 908), r8);
    MemoryInline::FlatWrite32((r4 + 912), r8);
    r4 = MemoryInline::FlatRead32((r3 + 208));
    r0 = MemoryInline::FlatRead32((r4 + 620));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80918B50:
{
    // EDIT: "1 player" takes the 2-4 player branch too (count = index + 1 = 1).
    if (((cr & 0x20000000u) != 0) && !OnePlayerChosen(r3)) {
        goto loc_80918B88;
    }
}

loc_80918B54:
{
    r5 = MemoryInline::FlatRead32((r3 + 184));
    r0 = 1;
    r4 = (r6 + 336);
    MemoryInline::FlatWrite8((r5 + 1662), static_cast<uint8_t>(r0));
    r6 = MemoryInline::FlatRead32((r3 + 208));
    r5 = MemoryInline::FlatRead32((r3 + 184));
    r6 = MemoryInline::FlatRead32((r6 + 620));
    r0 = (r6 + 1);
    MemoryInline::FlatWrite32((r5 + 1732), r0);
    r3 = (r3 + 116);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectJump(ctr, ctx);
    return;
    return;
}

loc_80918B88:
{
    r4 = NSMBW_VA_PAIR(0x80430000u, -23800);
    r5 = NSMBW_VA_PAIR(0x80430000u, -23112);
    r7 = MemoryInline::FlatRead32((r4 + -23800));
    r4 = (r6 + 464);
    r0 = 1;
    r6 = MemoryInline::FlatRead32((r7 + 8));
    MemoryInline::FlatWrite8((r6 + 84), static_cast<uint8_t>(r8));
    r6 = MemoryInline::FlatRead32((r7 + 12));
    MemoryInline::FlatWrite8((r6 + 84), static_cast<uint8_t>(r8));
    r6 = MemoryInline::FlatRead32((r7 + 16));
    MemoryInline::FlatWrite8((r6 + 84), static_cast<uint8_t>(r8));
    r5 = MemoryInline::FlatRead32((r5 + -23112));
    MemoryInline::FlatWrite8((r5 + 2957), static_cast<uint8_t>(r0));
    r3 = (r3 + 116);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectJump(ctr, ctx);
    return;
    return;
}

loc_80918BCC:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(-1));
}

loc_80918BD0:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_return;
    }
}

loc_80918BD4:
{
    r0 = 0;
    MemoryInline::FlatWrite8((r3 + 264), static_cast<uint8_t>(r0));
    r4 = (r6 + 144);
    r3 = (r3 + 116);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectJump(ctr, ctx);
    return;
    return;
}

loc_return:
{
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(80918B00, NsmbwGameSetupStartMember_80918B00, (CpuContext* ctx), (ctx));

// dNumberOfPeopleChange_c InitialSetup execute (0x807A0DB0; state table 0x8094B7C4): copies the join
// states, wires the four selectors, sets the slot -> remote table +1744.. to -1 and then entry 0 to
// remote 0 (player 1). Structural copy of build/nsmbw-d_basesNP/functions/func_807A0DB0.cpp.
extern "C" void NsmbwPeopleChangeSetup_807A0DB0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r6_rot_0 = 0;
    uint32_t r6_rot_1 = 0;
    uint32_t r6_rot_2 = 0;
    uint32_t r6_rot_3 = 0;
    uint32_t r6_rot_4 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r9 = ctx->gpr[9];
    uint32_t r10 = ctx->gpr[10];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_807A0DB0;

loc_807A0DB0:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r4 = NSMBW_VA_PAIR(0x80430000u, -23968);
    r5 = NSMBW_VA_PAIR(0x80430000u, -23972);
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    r0 = MemoryInline::FlatRead32((r4 + -23968));
    r31 = MemoryInline::FlatRead32((r5 + -23972));
    r0 = (r0 & 16);
}

loc_807A0DDC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_807A0DE8;
    }
}

loc_807A0DE0:
{
    r3 = r31;
    // inline leaf 0x800BB180 (15 guest instruction(s))
    r6 = NSMBW_VA_PAIR(0x80350000u, 20832);
    r7 = 0;
    r5 = (r6 + 20832);
    r0 = 1;
    MemoryInline::FlatWriteRam32((r5 + 4), r0);
    r4 = 3;
    r0 = 2;
    MemoryInline::FlatWriteRam32((r6 + 20832), r7);
    MemoryInline::FlatWrite32((r3 + 904), r7);
    MemoryInline::FlatWriteRam32((r5 + 8), r4);
    MemoryInline::FlatWrite32((r3 + 908), r7);
    MemoryInline::FlatWriteRam32((r5 + 12), r0);
    MemoryInline::FlatWrite32((r3 + 912), r7);
    MemoryInline::FlatWrite32((r3 + 900), r4);
    // end of inlined leaf 0x800BB180
}

loc_807A0DE8:
{
    r0 = 1;
    MemoryInline::FlatWrite8((r31 + 2812), static_cast<uint8_t>(r0));
    r0 = 0;
    r4 = NSMBW_VA_PAIR(0x80350000u, 20832);
    MemoryInline::FlatWrite32((r30 + 1696), r0);
    r4 = (r4 + 20832);
    r0 = MemoryInline::FlatRead32(r4);
    MemoryInline::FlatWrite32((r30 + 1760), r0);
    MemoryInline::FlatWrite32((r30 + 1776), r0);
    r0 = MemoryInline::FlatRead32((r31 + 900));
    // EDIT: on the world map player 1 starts not joined inside the screen (dInfo_c keeps its 3), so
    // selector 0 runs for it like for a joining player and player 1 can pick another character.
    if (OnWorldMap() && OnePlayerSelect(r30)) r0 = 0;
    MemoryInline::FlatWrite32((r30 + 1680), r0);
}

loc_807A0E14:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(3))) {
        goto loc_807A0E24;
    }
}

loc_807A0E18:
{
    r3 = MemoryInline::FlatRead32((r30 + 1696));
    r0 = (r3 + 1);
    MemoryInline::FlatWrite32((r30 + 1696), r0);
}

loc_807A0E24:
{
    r0 = MemoryInline::FlatRead32((r4 + 4));
    MemoryInline::FlatWrite32((r30 + 1764), r0);
    MemoryInline::FlatWrite32((r30 + 1780), r0);
    r0 = MemoryInline::FlatRead32((r31 + 904));
    MemoryInline::FlatWrite32((r30 + 1684), r0);
}

loc_807A0E3C:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(3))) {
        goto loc_807A0E4C;
    }
}

loc_807A0E40:
{
    r3 = MemoryInline::FlatRead32((r30 + 1696));
    r0 = (r3 + 1);
    MemoryInline::FlatWrite32((r30 + 1696), r0);
}

loc_807A0E4C:
{
    r0 = MemoryInline::FlatRead32((r4 + 8));
    r5 = (r30 + 8);
    MemoryInline::FlatWrite32((r30 + 1768), r0);
    r6 = (r31 + 8);
    MemoryInline::FlatWrite32((r30 + 1784), r0);
    r0 = MemoryInline::FlatRead32((r31 + 908));
    MemoryInline::FlatWrite32((r30 + 1688), r0);
}

loc_807A0E6C:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(3))) {
        goto loc_807A0E7C;
    }
}

loc_807A0E70:
{
    r3 = MemoryInline::FlatRead32((r30 + 1696));
    r0 = (r3 + 1);
    MemoryInline::FlatWrite32((r30 + 1696), r0);
}

loc_807A0E7C:
{
    r0 = MemoryInline::FlatRead32((r4 + 12));
    MemoryInline::FlatWrite32((r5 + 1764), r0);
    MemoryInline::FlatWrite32((r5 + 1780), r0);
    r0 = MemoryInline::FlatRead32((r6 + 904));
    MemoryInline::FlatWrite32((r5 + 1684), r0);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_807A0E94:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A0EA4;
    }
}

loc_807A0E98:
{
    r3 = MemoryInline::FlatRead32((r30 + 1696));
    r0 = (r3 + 1);
    MemoryInline::FlatWrite32((r30 + 1696), r0);
}

loc_807A0EA4:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x807A0EACu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x807A09D0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    r4 = NSMBW_VA_PAIR(0x80350000u, 20832);
    r7 = NSMBW_VA_PAIR(0x80930000u, 21360);
    r3 = NSMBW_VA_PAIR(0x80930000u, 21292);
    f2.d = MemoryInline::FlatReadFloat32((r7 + 21360));
    f0.d = MemoryInline::FlatReadFloat32((r3 + 21292));
    r6 = 4;
    r5 = r30;
    r4 = (r4 + 20832);
    r0 = (r30 + 1680);
    r9 = (r30 + 1696);
    r3 = 1;
    r7 = -1;
    ctr = r6;
}

loc_807A0EE0:
{
    r6 = MemoryInline::FlatRead32((r5 + 1680));
    r8 = MemoryInline::FlatRead32(r4);
}

loc_807A0EEC:
{
    if ((static_cast<int32_t>(r6) != static_cast<int32_t>(3))) {
        goto loc_807A0F0C;
    }
}

loc_807A0EF0:
{
    r6_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r8), static_cast<uint32_t>(2));
    r6 = (r6_rot_2 & -4);
    r6 = (r30 + r6);
    r6 = MemoryInline::FlatRead32((r6 + 1612));
    MemoryInline::FlatWrite8((r6 + 609), static_cast<uint8_t>(r3));
    f1.d = MemoryInline::FlatReadFloat32((r6 + 176));
    f1.d = static_cast<double>(PpcForceSingleValueInline(f1.d + f2.d));
    MemoryInline::FlatWriteFloat32((r6 + 176), f1.d);
}

loc_807A0F0C:
{
    r6_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r8), static_cast<uint32_t>(2));
    r6 = (r6_rot_4 & -4);
    r4 = (r4 + 4);
    r6 = (r30 + r6);
    r6 = MemoryInline::FlatRead32((r6 + 1612));
    MemoryInline::FlatWrite8((r6 + 615), static_cast<uint8_t>(r3));
    guest_range_0 = MemoryInline::ResolveRangeHost((r5 + 116), 0, 1500u, true, false);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    MemoryInline::FlatWrite32((r6 + 640), r0);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::ReadResolved32(guest_range_0, 16u, (r5 + 132));
    MemoryInline::FlatWrite32((r6 + 116), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::ReadResolved32(guest_range_0, 32u, (r5 + 148));
    MemoryInline::FlatWrite32((r6 + 120), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::ReadResolved32(guest_range_0, 48u, (r5 + 164));
    MemoryInline::FlatWrite32((r6 + 124), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 16u, (r5 + 132));
    r8 = MemoryInline::ReadResolved32(guest_range_0, 48u, (r5 + 164));
    MemoryInline::FlatWrite32((r6 + 520), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::FlatRead32((r30 + 1732));
    MemoryInline::FlatWrite32((r6 + 744), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    MemoryInline::FlatWrite32((r6 + 648), r9);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::FlatRead32((r30 + 1612));
    MemoryInline::FlatWrite32((r6 + 128), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::FlatRead32((r30 + 1616));
    MemoryInline::FlatWrite32((r6 + 132), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::FlatRead32((r30 + 1620));
    MemoryInline::FlatWrite32((r6 + 136), r8);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r8 = MemoryInline::FlatRead32((r30 + 1624));
    MemoryInline::FlatWrite32((r6 + 140), r8);
    MemoryInline::FlatWrite32((r5 + 1744), r7);
    r8 = MemoryInline::ReadResolved32(guest_range_0, 48u, (r5 + 164));
    r10 = MemoryInline::FlatRead32((r8 + 540));
    r6 = MemoryInline::FlatRead8((r10 + 187));
    r6 = (r6 & 254);
    r6 = (r6 | 1);
    MemoryInline::FlatWrite8((r10 + 187), static_cast<uint8_t>(r6));
    r8 = MemoryInline::FlatRead32((r8 + 544));
    r6 = MemoryInline::FlatRead8((r8 + 187));
    r6 = (r6 & 254);
    r6 = (r6 | 1);
    MemoryInline::FlatWrite8((r8 + 187), static_cast<uint8_t>(r6));
    r6 = MemoryInline::ReadResolved32(guest_range_0, 1496u, (r5 + 1612));
    MemoryInline::FlatWriteFloat32((r6 + 604), f0.d);
    r6 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r5 + 116));
    r5 = (r5 + 4);
    MemoryInline::FlatWriteFloat32((r6 + 752), f0.d);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_807A0EE0;
    }
}

loc_807A0FE4:
{
    r0 = OnePlayerSelect(r30) ? uint32_t(-1) : 0u; // EDIT: was 0 (remote 0 reserved for player 1)
    MemoryInline::FlatWrite32((r30 + 1744), r0);
    r3 = NSMBW_VA_PAIR(0x80430000u, -30142);
    r0 = MemoryInline::FlatRead16((r3 + -30142));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(3));
}

loc_807A0FF8:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A105C;
    }
}

loc_807A0FFC:
{
    r3 = MemoryInline::FlatRead32((r30 + 1564));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1568));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1572));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1540));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1544));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    goto loc_807A10DC;
}

loc_807A105C:
{
    r3 = MemoryInline::FlatRead32((r30 + 1572));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1564));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1568));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1540));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r30 + 1544));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    // inline leaf 0x800CDD50 (4 guest instruction(s))
    r3 = NSMBW_VA_PAIR(0x80360000u, -24676);
    r3 = (r3 + -24676);
    r3 = MemoryInline::FlatRead32((r3 + 24));
    // end of inlined leaf 0x800CDD50
    r0 = MemoryInline::FlatRead32((r30 + 1732));
    r4 = r3;
    MemoryInline::FlatWrite32((r31 + 972), r0);
    r5 = 2;
    r6 = 28;
    r7 = 0;
    r3 = MemoryInline::FlatRead32((r30 + 1544));
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(2), static_cast<uint32_t>(6), static_cast<uint32_t>(6), static_cast<uint32_t>(6));
    ctx->lr = NSMBW_VA(0x807A10DCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C9B50u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r10 = ctx->gpr[10];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_807A10DC:
{
    r3 = (r30 + 1028);
    ctx->lr = NSMBW_VA(0x807A10E4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C95F0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r10 = ctx->gpr[10];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 1028);
    r4 = 2;
    r5 = 0;
    ctx->lr = NSMBW_VA(0x807A10F4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C94C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r10 = ctx->gpr[10];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 1028);
    r4 = 3;
    r5 = 0;
    ctx->lr = NSMBW_VA(0x807A1104u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C94C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r10 = ctx->gpr[10];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r6 = MemoryInline::FlatRead32((r30 + 1496));
    r4 = NSMBW_VA_PAIR(0x80990000u, 15392);
    r0 = 0;
    r3 = (r30 + 1436);
    r5 = MemoryInline::FlatRead8((r6 + 187));
    r4 = (r4 + 15392);
    r5 = (r5 & 254);
    r5 = (r5 | 1);
    MemoryInline::FlatWrite8((r6 + 187), static_cast<uint8_t>(r5));
    MemoryInline::FlatWrite8((r30 + 1664), static_cast<uint8_t>(r0));
    r12 = MemoryInline::FlatRead32((r30 + 1436));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807A113Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r10 = ctx->gpr[10];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r30 = MemoryInline::FlatRead32((r1 + 8));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807A0DB0, NsmbwPeopleChangeSetup_807A0DB0, (CpuContext* ctx), (ctx));

// dNumberOfPeopleChange_c OnStageAnimeEndWait initialize (0x807A11A0; state table 0x8094B7F4): starts
// selectors 1..count-1 (+661) and points each at the screen's slot -> remote table (+644 = +1744),
// characters (+652 = +1760) and +656 = +1776. Structural copy of
// build/nsmbw-d_basesNP/functions/func_807A11A0.cpp.
extern "C" void NsmbwPeopleChangeWire_807A11A0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r9 = ctx->gpr[9];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_807A11A0;

loc_807A11A0:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    r4 = 1;
    r5 = 0;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    r31 = r3;
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    r30 = 0;
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    r29 = 1;
    MemoryInline::FlatWrite8((r3 + 1671), static_cast<uint8_t>(r29));
    MemoryInline::FlatWrite8((r3 + 1672), static_cast<uint8_t>(r30));
    r3 = (r3 + 1028);
    ctx->lr = NSMBW_VA(0x807A11DCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C93E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 1028);
    r4 = 0;
    r5 = 0;
    ctx->lr = NSMBW_VA(0x807A11ECu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800C93E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = NSMBW_VA_PAIR(0x80930000u, 21276);
    r8 = (r31 + 4);
    f0.d = MemoryInline::FlatReadFloat32((r3 + 21276));
    r6 = (r31 + 1744);
    r5 = (r31 + 1760);
    r4 = (r31 + 1776);
    r9 = OnePlayerSelect(r31) ? 0u : 1u; // EDIT: was 1 (count selectors, not count - 1)
    if (OnePlayerSelect(r31)) r8 = r31 + 4 * FirstSelector(r31); // EDIT: from FirstSelector
    r7 = 3;
    goto loc_807A1250;
}

loc_807A1210:
{
    r3 = MemoryInline::FlatRead32((r8 + 116));
    r9 = (r9 + 1);
    MemoryInline::FlatWrite8((r3 + 661), static_cast<uint8_t>(r29));
    r3 = MemoryInline::FlatRead32((r8 + 164));
    MemoryInline::FlatWrite32((r3 + 572), r30);
    MemoryInline::FlatWrite8((r3 + 569), static_cast<uint8_t>(r29));
    MemoryInline::FlatWrite32((r3 + 584), r7);
    MemoryInline::FlatWrite32((r3 + 588), r30);
    MemoryInline::FlatWriteFloat32((r3 + 564), f0.d);
    r3 = MemoryInline::FlatRead32((r8 + 116));
    MemoryInline::FlatWrite32((r3 + 644), r6);
    r3 = MemoryInline::FlatRead32((r8 + 116));
    MemoryInline::FlatWrite32((r3 + 652), r5);
    r3 = MemoryInline::FlatRead32((r8 + 116));
    r8 = (r8 + 4);
    MemoryInline::FlatWrite32((r3 + 656), r4);
}

loc_807A1250:
{
    r0 = MemoryInline::FlatRead32((r31 + 1732));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r9), static_cast<int32_t>(r0));
}

loc_807A1258:
{
    if (((cr & NSMBW_VA(0x80000000u)) != 0)) {
        goto loc_807A1210;
    }
}

loc_807A125C:
{
    r29 = r31;
    r30 = 0;
}

loc_807A1264:
{
    r3 = MemoryInline::FlatRead32((r29 + 1612));
    r3 = MemoryInline::FlatRead32((r3 + 296));
    r3 = MemoryInline::FlatRead32((r3 + 4));
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 172));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807A1280u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r8 = ctx->gpr[8];
    r9 = ctx->gpr[9];
    r12 = ctx->gpr[12];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r30 = (r30 + 1);
    r29 = (r29 + 4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r30), static_cast<int32_t>(4));
}

loc_807A128C:
{
    if (((cr & NSMBW_VA(0x80000000u)) != 0)) {
        goto loc_807A1264;
    }
}

loc_807A1290:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -30142);
    r0 = MemoryInline::FlatRead16((r3 + -30142));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(3));
}

loc_807A129C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A12AC;
    }
}

loc_807A12A0:
{
    r3 = MemoryInline::FlatRead32((r31 + 1612));
    r0 = 1;
    MemoryInline::FlatWrite8((r3 + 618), static_cast<uint8_t>(r0));
}

loc_807A12AC:
{
    r0 = MemoryInline::FlatRead32((r1 + 36));
    r31 = MemoryInline::FlatRead32((r1 + 28));
    r30 = MemoryInline::FlatRead32((r1 + 24));
    r29 = MemoryInline::FlatRead32((r1 + 20));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807A11A0, NsmbwPeopleChangeWire_807A11A0, (CpuContext* ctx), (ctx));

// dNumberOfPeopleChange_c NowEntrantRecruit execute (0x807A1510; state table 0x8094B830): waits while
// any started selector is busy (+662), then for player 1's OK (0x807A0D20), then marks the started
// selectors done (+667). Structural copy of build/nsmbw-d_basesNP/functions/func_807A1510.cpp.
extern "C" void NsmbwPeopleChangeRecruit_807A1510(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_807A1510;

loc_807A1510:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r4 = (r3 + 4);
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r5 = MemoryInline::FlatRead32((r3 + 1732));
    r0 = (r5 + -1);
    ctr = r0;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r5), static_cast<int32_t>(1));
    if (OnePlayerSelect(r3)) { // EDIT: count selectors (from FirstSelector) instead of count - 1
        r4 = r3 + 4 * FirstSelector(r3);
        ctr = r5;
        SetCRResident(cr, xer, 0, static_cast<int32_t>(r5), static_cast<int32_t>(0));
    }
}

loc_807A1538:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_807A1558;
    }
}

loc_807A153C:
{
    // nop
}

loc_807A1540:
{
    r3 = MemoryInline::FlatRead32((r4 + 116));
    r0 = MemoryInline::FlatRead8((r3 + 662));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_807A154C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A16A0;
    }
}

loc_807A1550:
{
    r4 = (r4 + 4);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_807A1540;
    }
}

loc_807A1558:
{
    r3 = r31;
    ctx->lr = NSMBW_VA(0x807A1560u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x807A0D20u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_807A1564:
{
    if (r3 != 0 && OnWorldMap() && OnePlayerSelect(r31)) { // EDIT: map OK only after player 1 confirmed a card
        const uint32_t sel = MemoryInline::FlatRead32(r31 + 116); // selector 0 = player 1's slot
        if (MemoryInline::FlatRead8(sel + 666) == 0) {
            r3 = 0;
        } else {
            g_mapPick = int(MemoryInline::FlatRead32(sel + 724) & 3u);
            g_reloadNow = true;
            if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] map Add/Drop OK: player 1 picked %d (tick %u)\n", g_mapPick, g_nsmbwCurrentViTick);
        }
    }
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_807A1614;
    }
}

loc_807A1568:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -30142);
    r0 = MemoryInline::FlatRead16((r3 + -30142));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(3));
}

loc_807A1574:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A1590;
    }
}

loc_807A1578:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 121;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807A158Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_807A15A4;
}

loc_807A1590:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 122;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807A15A4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_807A15A4:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -30142);
    r0 = MemoryInline::FlatRead16((r3 + -30142));
}

loc_807A15B0:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(3))) {
        goto loc_807A15C0;
    }
}

loc_807A15B4:
{
    r0 = 0;
    MemoryInline::FlatWrite32((r31 + 1736), r0);
    goto loc_807A15C8;
}

loc_807A15C0:
{
    r0 = 1;
    MemoryInline::FlatWrite32((r31 + 1736), r0);
}

loc_807A15C8:
{
    r5 = (r31 + 4);
    r6 = OnePlayerSelect(r31) ? 0u : 1u; // EDIT: was 1 (count selectors, not count - 1)
    if (OnePlayerSelect(r31)) r5 = r31 + 4 * FirstSelector(r31); // EDIT: from FirstSelector
    r4 = 1;
    goto loc_807A15E8;
}

loc_807A15D8:
{
    r3 = MemoryInline::FlatRead32((r5 + 116));
    r5 = (r5 + 4);
    r6 = (r6 + 1);
    MemoryInline::FlatWrite8((r3 + 667), static_cast<uint8_t>(r4));
}

loc_807A15E8:
{
    r0 = MemoryInline::FlatRead32((r31 + 1732));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r6), static_cast<int32_t>(r0));
}

loc_807A15F0:
{
    if (((cr & NSMBW_VA(0x80000000u)) != 0)) {
        goto loc_807A15D8;
    }
}

loc_807A15F4:
{
    r12 = MemoryInline::FlatRead32((r31 + 1436));
    r4 = NSMBW_VA_PAIR(0x80990000u, 15968);
    r3 = (r31 + 1436);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    r4 = (r4 + 15968);
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807A1610u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_807A16A0;
}

loc_807A1614:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -30142);
    r0 = MemoryInline::FlatRead16((r3 + -30142));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(3));
}

loc_807A1620:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807A16A0;
    }
}

loc_807A1624:
{
    r3 = MemoryInline::FlatRead32((r31 + 1732));
    r4 = (r31 + 4);
    r0 = (r3 + -1);
    ctr = r0;
    if (OnePlayerSelect(r31)) { // EDIT: wait for count selectors' choices (+666), not count - 1
        r4 = r31 + 4 * FirstSelector(r31);
        ctr = r3;
    }
}

loc_807A1638:
{
    if ((static_cast<int32_t>(r3) <= static_cast<int32_t>(OnePlayerSelect(r31) ? 0 : 1))) { // EDIT: was 1
        goto loc_807A1658;
    }
}

loc_807A163C:
{
    // nop
}

loc_807A1640:
{
    r3 = MemoryInline::FlatRead32((r4 + 116));
    r0 = MemoryInline::FlatRead8((r3 + 666));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_807A164C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807A16A0;
    }
}

loc_807A1650:
{
    r4 = (r4 + 4);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_807A1640;
    }
}

loc_807A1658:
{
    r5 = (r31 + 4);
    r6 = OnePlayerSelect(r31) ? 0u : 1u; // EDIT: was 1 (count selectors, not count - 1)
    if (OnePlayerSelect(r31)) r5 = r31 + 4 * FirstSelector(r31); // EDIT: from FirstSelector
    r4 = 1;
    goto loc_807A1678;
}

loc_807A1668:
{
    r3 = MemoryInline::FlatRead32((r5 + 116));
    r5 = (r5 + 4);
    r6 = (r6 + 1);
    MemoryInline::FlatWrite8((r3 + 667), static_cast<uint8_t>(r4));
}

loc_807A1678:
{
    r0 = MemoryInline::FlatRead32((r31 + 1732));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r6), static_cast<int32_t>(r0));
}

loc_807A1680:
{
    if (((cr & NSMBW_VA(0x80000000u)) != 0)) {
        goto loc_807A1668;
    }
}

loc_807A1684:
{
    r12 = MemoryInline::FlatRead32((r31 + 1436));
    r4 = NSMBW_VA_PAIR(0x80990000u, 15648);
    r3 = (r31 + 1436);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    r4 = (r4 + 15648);
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807A16A0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_807A16A0:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807A1510, NsmbwPeopleChangeRecruit_807A1510, (CpuContext* ctx), (ctx));

// dNumberOfPeopleChange_c NowEntrantRecruit initialize (0x807A14D0; state table 0x8094B83C): lets
// selectors 1..count-1 leave their OnStageWait (+665; read by 0x807702A0). Without the EDIT, player
// 1's selector 0 stayed in OnStageWait (2026-11-15 test). Structural copy of
// build/nsmbw-d_basesNP/functions/func_807A14D0.cpp.
extern "C" void NsmbwPeopleChangeRecruitStart_807A14D0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_807A14D0;

loc_807A14D0:
{
    r5 = 1;
    MemoryInline::FlatWrite8((r3 + 1664), static_cast<uint8_t>(r5));
    r6 = (r3 + 4);
    r7 = OnePlayerSelect(r3) ? 0u : 1u; // EDIT: was 1 (count selectors, not count - 1)
    if (OnePlayerSelect(r3)) r6 = r3 + 4 * FirstSelector(r3); // EDIT: from FirstSelector
    goto loc_807A14F8;
}

loc_807A14E8:
{
    r4 = MemoryInline::FlatRead32((r6 + 116));
    r6 = (r6 + 4);
    r7 = (r7 + 1);
    MemoryInline::FlatWrite8((r4 + 665), static_cast<uint8_t>(r5));
}

loc_807A14F8:
{
    r0 = MemoryInline::FlatRead32((r3 + 1732));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r7), static_cast<int32_t>(r0));
}

loc_807A1500:
{
    if (((cr & NSMBW_VA(0x80000000u)) != 0)) {
        goto loc_807A14E8;
    }
}

loc_807A1504:
{
    ctx->gpr[0] = r0;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x807A0A50u)>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    cr = ctx->cr;
    xer = ctx->xer;
    ctx->gpr[0] = r0;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->cr = cr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807A14D0, NsmbwPeopleChangeRecruitStart_807A14D0, (CpuContext* ctx), (ctx));

// dNumberOfPeopleChange_c card placement (0x807A0170, called every frame from 0x8079FEA0): moves
// selectors 1-3's cards, arrows and buttons to the layout spot each was given for this player count
// (+1716, from the table at 0x80935330: count 2 -> spots 7/8, 3 -> 4/5/6, 4 -> 0..3; spots are the
// null panes at screen+1500). Selector 0 is skipped because in game setup player 1 was pre-joined
// and drawn as a standing figure at its spot (0x807A0440). With player 1 choosing (2026-11-15:
// "When you press 2 players, player 1 isnt allowed to choose"), selector 0 has a card that was
// left at the layout's default place, half under player 2's. Structural copy of
// build/nsmbw-d_basesNP/functions/func_807A0170.cpp; the EDIT loops over all four selectors.
namespace {
bool PlacesPlayer1Card(uint32_t screen) {
    uint32_t count = 0;
    return OnePlayerSelect(screen) && Memory::TryRead32(screen + 1732, count) && count >= 2;
}
} // namespace
extern "C" void NsmbwPeopleChangePlaceCards_807A0170(CpuContext* MKW_RESTRICT ctx)
{
    g_addDropSeenTick = g_nsmbwCurrentViTick;
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint8_t* guest_range_0 = nullptr;
    uint8_t* guest_range_1 = nullptr;
    uint8_t* guest_range_2 = nullptr;
    uint8_t* guest_range_3 = nullptr;
    uint8_t* guest_range_4 = nullptr;
    uint8_t* guest_range_5 = nullptr;
    uint8_t* guest_range_6 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r9 = ctx->gpr[9];
    uint32_t r10 = ctx->gpr[10];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r14 = ctx->gpr[14];
    uint32_t r15 = ctx->gpr[15];
    uint32_t r16 = ctx->gpr[16];
    uint32_t r17 = ctx->gpr[17];
    uint32_t r18 = ctx->gpr[18];
    uint32_t r19 = ctx->gpr[19];
    uint32_t r20 = ctx->gpr[20];
    uint32_t r21 = ctx->gpr[21];
    uint32_t r22 = ctx->gpr[22];
    uint32_t r23 = ctx->gpr[23];
    uint32_t r24 = ctx->gpr[24];
    uint32_t r25 = ctx->gpr[25];
    uint32_t r26 = ctx->gpr[26];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];
    PPC_FPR f4 = ctx->fpr[4];
    PPC_FPR f5 = ctx->fpr[5];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_807A0170;

loc_807A0170:
{
    MemoryInline::FlatWriteRam32((r1 + -304), r1);
    r1 = (r1 + -304);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 308), r0);
    r11 = (r1 + 304);
    // inline leaf 0x802DD030 (19 guest instruction(s))
    guest_range_0 = MemoryInline::ResolveRangeHost((r11 + -72), 0, 72u, false, true);
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 0u, ((static_cast<uint64_t>(static_cast<uint32_t>(r14)) << 32) | static_cast<uint32_t>(r15)))) {
        MemoryInline::WriteResolved32(guest_range_0, 0u, (r11 + -72), r14);
        MemoryInline::WriteResolved32(guest_range_0, 4u, (r11 + -68), r15);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 8u, ((static_cast<uint64_t>(static_cast<uint32_t>(r16)) << 32) | static_cast<uint32_t>(r17)))) {
        MemoryInline::WriteResolved32(guest_range_0, 8u, (r11 + -64), r16);
        MemoryInline::WriteResolved32(guest_range_0, 12u, (r11 + -60), r17);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 16u, ((static_cast<uint64_t>(static_cast<uint32_t>(r18)) << 32) | static_cast<uint32_t>(r19)))) {
        MemoryInline::WriteResolved32(guest_range_0, 16u, (r11 + -56), r18);
        MemoryInline::WriteResolved32(guest_range_0, 20u, (r11 + -52), r19);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 24u, ((static_cast<uint64_t>(static_cast<uint32_t>(r20)) << 32) | static_cast<uint32_t>(r21)))) {
        MemoryInline::WriteResolved32(guest_range_0, 24u, (r11 + -48), r20);
        MemoryInline::WriteResolved32(guest_range_0, 28u, (r11 + -44), r21);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 32u, ((static_cast<uint64_t>(static_cast<uint32_t>(r22)) << 32) | static_cast<uint32_t>(r23)))) {
        MemoryInline::WriteResolved32(guest_range_0, 32u, (r11 + -40), r22);
        MemoryInline::WriteResolved32(guest_range_0, 36u, (r11 + -36), r23);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 40u, ((static_cast<uint64_t>(static_cast<uint32_t>(r24)) << 32) | static_cast<uint32_t>(r25)))) {
        MemoryInline::WriteResolved32(guest_range_0, 40u, (r11 + -32), r24);
        MemoryInline::WriteResolved32(guest_range_0, 44u, (r11 + -28), r25);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 48u, ((static_cast<uint64_t>(static_cast<uint32_t>(r26)) << 32) | static_cast<uint32_t>(r27)))) {
        MemoryInline::WriteResolved32(guest_range_0, 48u, (r11 + -24), r26);
        MemoryInline::WriteResolved32(guest_range_0, 52u, (r11 + -20), r27);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 56u, ((static_cast<uint64_t>(static_cast<uint32_t>(r28)) << 32) | static_cast<uint32_t>(r29)))) {
        MemoryInline::WriteResolved32(guest_range_0, 56u, (r11 + -16), r28);
        MemoryInline::WriteResolved32(guest_range_0, 60u, (r11 + -12), r29);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 64u, ((static_cast<uint64_t>(static_cast<uint32_t>(r30)) << 32) | static_cast<uint32_t>(r31)))) {
        MemoryInline::WriteResolved32(guest_range_0, 64u, (r11 + -8), r30);
        MemoryInline::WriteResolved32(guest_range_0, 68u, (r11 + -4), r31);
    }
    // end of inlined leaf 0x802DD030
    r6 = NSMBW_VA_PAIR(0x80930000u, 21284);
    r5 = NSMBW_VA_PAIR(0x80930000u, 21276);
    r0 = 3;
    r4 = (r3 + 4);
    if (PlacesPlayer1Card(r3)) { r0 = 4; r4 = r3; } // EDIT: was selectors 1-3 (player 1 had no card)
    f5.d = MemoryInline::FlatReadFloat32((r6 + 21284));
    f4.d = MemoryInline::FlatReadFloat32((r5 + 21276));
    ctr = r0;
}

loc_807A01A0:
{
    r0 = MemoryInline::FlatRead32((r4 + 1716));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(10));
}

loc_807A01A8:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807A0418;
    }
}

loc_807A01AC:
{
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & -4);
    guest_range_2 = MemoryInline::ResolveRangeHost((r4 + 116), 0, 52u, true, false);
    r6 = MemoryInline::ReadResolved32(guest_range_2, 0u, (r4 + 116));
    r5 = (r3 + r0);
    MemoryInline::FlatWriteRamFloat32((r1 + 24), f4.d);
    r5 = MemoryInline::FlatRead32((r5 + 1500));
    r10 = MemoryInline::FlatRead32((r6 + 612));
    guest_range_3 = MemoryInline::ResolveRangeHost((r5 + 132), 0, 48u, true, false);
    r0 = MemoryInline::ReadResolved32(guest_range_3, 28u, (r5 + 160));
    MemoryInline::FlatWriteRam32((r1 + 204), r0);
    r0 = MemoryInline::ReadResolved32(guest_range_3, 12u, (r5 + 144));
    MemoryInline::FlatWriteRam32((r1 + 188), r0);
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_3, 0u);
        if (resolved_pair.valid) {
            r9 = resolved_pair.first;
            r8 = resolved_pair.second;
        } else {
            r9 = MemoryInline::ReadResolved32(guest_range_3, 0u, (r5 + 132));
            r8 = MemoryInline::ReadResolved32(guest_range_3, 4u, (r5 + 136));
        }
    }
    r7 = MemoryInline::ReadResolved32(guest_range_3, 8u, (r5 + 140));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_3, 16u);
        if (resolved_pair.valid) {
            r6 = resolved_pair.first;
            r0 = resolved_pair.second;
        } else {
            r6 = MemoryInline::ReadResolved32(guest_range_3, 16u, (r5 + 148));
            r0 = MemoryInline::ReadResolved32(guest_range_3, 20u, (r5 + 152));
        }
    }
    r15 = MemoryInline::ReadResolved32(guest_range_3, 24u, (r5 + 156));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_3, 32u);
        if (resolved_pair.valid) {
            r16 = resolved_pair.first;
            r17 = resolved_pair.second;
        } else {
            r16 = MemoryInline::ReadResolved32(guest_range_3, 32u, (r5 + 164));
            r17 = MemoryInline::ReadResolved32(guest_range_3, 36u, (r5 + 168));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_3, 40u);
        if (resolved_pair.valid) {
            r18 = resolved_pair.first;
            r19 = resolved_pair.second;
        } else {
            r18 = MemoryInline::ReadResolved32(guest_range_3, 40u, (r5 + 172));
            r19 = MemoryInline::ReadResolved32(guest_range_3, 44u, (r5 + 176));
        }
    }
    f0.d = MemoryInline::FlatReadFloat32((r1 + 204));
    f3.d = MemoryInline::FlatReadFloat32((r1 + 188));
    MemoryInline::FlatWriteFloat32((r10 + 44), f3.d);
    f2.d = static_cast<double>(PpcForceSingleValueInline(f5.d + f0.d));
    MemoryInline::FlatWriteFloat32((r10 + 48), f2.d);
    MemoryInline::FlatWriteFloat32((r10 + 52), f4.d);
    r5 = MemoryInline::ReadResolved32(guest_range_2, 32u, (r4 + 148));
    MemoryInline::FlatWriteRam32((r1 + 176), r9);
    r5 = MemoryInline::FlatRead32((r5 + 580));
    MemoryInline::FlatWriteRam32((r1 + 196), r0);
    f1.d = MemoryInline::FlatReadFloat32((r1 + 176));
    MemoryInline::FlatWriteFloat32((r5 + 44), f3.d);
    f0.d = MemoryInline::FlatReadFloat32((r1 + 196));
    MemoryInline::FlatWriteFloat32((r5 + 48), f2.d);
    MemoryInline::FlatWriteFloat32((r5 + 52), f4.d);
    r5 = MemoryInline::ReadResolved32(guest_range_2, 48u, (r4 + 164));
    MemoryInline::FlatWriteRam32((r1 + 192), r6);
    r5 = MemoryInline::FlatRead32((r5 + 520));
    MemoryInline::FlatWriteRam32((r1 + 180), r8);
    MemoryInline::FlatWriteFloat32((r5 + 44), f3.d);
    MemoryInline::FlatWriteFloat32((r5 + 48), f2.d);
    MemoryInline::FlatWriteFloat32((r5 + 52), f4.d);
    MemoryInline::FlatWriteFloat32((r5 + 68), f1.d);
    MemoryInline::FlatWriteFloat32((r5 + 72), f0.d);
    r5 = MemoryInline::ReadResolved32(guest_range_2, 0u, (r4 + 116));
    MemoryInline::FlatWriteRam32((r1 + 184), r7);
    r6 = MemoryInline::FlatRead32((r5 + 624));
    r30 = MemoryInline::FlatRead32((r5 + 616));
    guest_range_4 = MemoryInline::ResolveRangeHost((r6 + 132), 0, 48u, true, false);
    r5 = MemoryInline::ReadResolved32(guest_range_4, 12u, (r6 + 144));
    MemoryInline::FlatWriteRam32((r1 + 44), r5);
    r0 = MemoryInline::ReadResolved32(guest_range_4, 28u, (r6 + 160));
    MemoryInline::FlatWriteRam32((r1 + 60), r0);
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_4, 0u);
        if (resolved_pair.valid) {
            r20 = resolved_pair.first;
            r21 = resolved_pair.second;
        } else {
            r20 = MemoryInline::ReadResolved32(guest_range_4, 0u, (r6 + 132));
            r21 = MemoryInline::ReadResolved32(guest_range_4, 4u, (r6 + 136));
        }
    }
    r22 = MemoryInline::ReadResolved32(guest_range_4, 8u, (r6 + 140));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_4, 16u);
        if (resolved_pair.valid) {
            r23 = resolved_pair.first;
            r24 = resolved_pair.second;
        } else {
            r23 = MemoryInline::ReadResolved32(guest_range_4, 16u, (r6 + 148));
            r24 = MemoryInline::ReadResolved32(guest_range_4, 20u, (r6 + 152));
        }
    }
    r25 = MemoryInline::ReadResolved32(guest_range_4, 24u, (r6 + 156));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_4, 32u);
        if (resolved_pair.valid) {
            r26 = resolved_pair.first;
            r27 = resolved_pair.second;
        } else {
            r26 = MemoryInline::ReadResolved32(guest_range_4, 32u, (r6 + 164));
            r27 = MemoryInline::ReadResolved32(guest_range_4, 36u, (r6 + 168));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_4, 40u);
        if (resolved_pair.valid) {
            r28 = resolved_pair.first;
            r29 = resolved_pair.second;
        } else {
            r28 = MemoryInline::ReadResolved32(guest_range_4, 40u, (r6 + 172));
            r29 = MemoryInline::ReadResolved32(guest_range_4, 44u, (r6 + 176));
        }
    }
    guest_range_5 = MemoryInline::ResolveRangeHost((r30 + 132), 0, 48u, true, false);
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_5, 0u);
        if (resolved_pair.valid) {
            r31 = resolved_pair.first;
            r12 = resolved_pair.second;
        } else {
            r31 = MemoryInline::ReadResolved32(guest_range_5, 0u, (r30 + 132));
            r12 = MemoryInline::ReadResolved32(guest_range_5, 4u, (r30 + 136));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_5, 8u);
        if (resolved_pair.valid) {
            r11 = resolved_pair.first;
            r10 = resolved_pair.second;
        } else {
            r11 = MemoryInline::ReadResolved32(guest_range_5, 8u, (r30 + 140));
            r10 = MemoryInline::ReadResolved32(guest_range_5, 12u, (r30 + 144));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_5, 16u);
        if (resolved_pair.valid) {
            r9 = resolved_pair.first;
            r8 = resolved_pair.second;
        } else {
            r9 = MemoryInline::ReadResolved32(guest_range_5, 16u, (r30 + 148));
            r8 = MemoryInline::ReadResolved32(guest_range_5, 20u, (r30 + 152));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_5, 24u);
        if (resolved_pair.valid) {
            r7 = resolved_pair.first;
            r6 = resolved_pair.second;
        } else {
            r7 = MemoryInline::ReadResolved32(guest_range_5, 24u, (r30 + 156));
            r6 = MemoryInline::ReadResolved32(guest_range_5, 28u, (r30 + 160));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_5, 32u);
        if (resolved_pair.valid) {
            r5 = resolved_pair.first;
            r0 = resolved_pair.second;
        } else {
            r5 = MemoryInline::ReadResolved32(guest_range_5, 32u, (r30 + 164));
            r0 = MemoryInline::ReadResolved32(guest_range_5, 36u, (r30 + 168));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_5, 40u);
        if (resolved_pair.valid) {
            r14 = resolved_pair.first;
            r30 = resolved_pair.second;
        } else {
            r14 = MemoryInline::ReadResolved32(guest_range_5, 40u, (r30 + 172));
            r30 = MemoryInline::ReadResolved32(guest_range_5, 44u, (r30 + 176));
        }
    }
    MemoryInline::FlatWriteRam32((r1 + 200), r15);
    f1.d = MemoryInline::FlatReadFloat32((r1 + 44));
    MemoryInline::FlatWriteRam32((r1 + 208), r16);
    f0.d = MemoryInline::FlatReadFloat32((r1 + 60));
    MemoryInline::FlatWriteRam32((r1 + 212), r17);
    MemoryInline::FlatWriteRam32((r1 + 216), r18);
    MemoryInline::FlatWriteRam32((r1 + 220), r19);
    MemoryInline::FlatWriteRam32((r1 + 32), r20);
    MemoryInline::FlatWriteRam32((r1 + 36), r21);
    MemoryInline::FlatWriteRam32((r1 + 40), r22);
    MemoryInline::FlatWriteRam32((r1 + 48), r23);
    MemoryInline::FlatWriteRam32((r1 + 52), r24);
    MemoryInline::FlatWriteRam32((r1 + 56), r25);
    MemoryInline::FlatWriteRam32((r1 + 64), r26);
    MemoryInline::FlatWriteRam32((r1 + 68), r27);
    MemoryInline::FlatWriteRam32((r1 + 72), r28);
    MemoryInline::FlatWriteRam32((r1 + 76), r29);
    MemoryInline::FlatWriteRam32((r1 + 80), r31);
    MemoryInline::FlatWriteRam32((r1 + 84), r12);
    MemoryInline::FlatWriteRam32((r1 + 88), r11);
    MemoryInline::FlatWriteRam32((r1 + 92), r10);
    MemoryInline::FlatWriteRam32((r1 + 96), r9);
    MemoryInline::FlatWriteRam32((r1 + 100), r8);
    MemoryInline::FlatWriteRam32((r1 + 104), r7);
    MemoryInline::FlatWriteRam32((r1 + 108), r6);
    MemoryInline::FlatWriteRam32((r1 + 112), r5);
    MemoryInline::FlatWriteRam32((r1 + 116), r0);
    MemoryInline::FlatWriteRam32((r1 + 120), r14);
    MemoryInline::FlatWriteRam32((r1 + 124), r30);
    r5 = MemoryInline::ReadResolved32(guest_range_2, 16u, (r4 + 132));
    f3.d = MemoryInline::FlatReadFloat32((r1 + 80));
    r5 = MemoryInline::FlatRead32((r5 + 592));
    f2.d = MemoryInline::FlatReadFloat32((r1 + 100));
    MemoryInline::FlatWriteFloat32((r5 + 44), f1.d);
    MemoryInline::FlatWriteFloat32((r5 + 48), f0.d);
    MemoryInline::FlatWriteFloat32((r5 + 52), f4.d);
    MemoryInline::FlatWriteFloat32((r5 + 68), f3.d);
    MemoryInline::FlatWriteFloat32((r5 + 72), f2.d);
    r6 = MemoryInline::ReadResolved32(guest_range_2, 0u, (r4 + 116));
    r5 = MemoryInline::ReadResolved32(guest_range_2, 16u, (r4 + 132));
    r16 = MemoryInline::FlatRead32((r6 + 620));
    r17 = MemoryInline::FlatRead32((r5 + 588));
    guest_range_6 = MemoryInline::ResolveRangeHost((r16 + 132), 0, 48u, true, false);
    r0 = MemoryInline::ReadResolved32(guest_range_6, 12u, (r16 + 144));
    MemoryInline::FlatWriteRam32((r1 + 140), r0);
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_6, 0u);
        if (resolved_pair.valid) {
            r15 = resolved_pair.first;
            r14 = resolved_pair.second;
        } else {
            r15 = MemoryInline::ReadResolved32(guest_range_6, 0u, (r16 + 132));
            r14 = MemoryInline::ReadResolved32(guest_range_6, 4u, (r16 + 136));
        }
    }
    r12 = MemoryInline::ReadResolved32(guest_range_6, 8u, (r16 + 140));
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_6, 16u);
        if (resolved_pair.valid) {
            r11 = resolved_pair.first;
            r10 = resolved_pair.second;
        } else {
            r11 = MemoryInline::ReadResolved32(guest_range_6, 16u, (r16 + 148));
            r10 = MemoryInline::ReadResolved32(guest_range_6, 20u, (r16 + 152));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_6, 24u);
        if (resolved_pair.valid) {
            r9 = resolved_pair.first;
            r5 = resolved_pair.second;
        } else {
            r9 = MemoryInline::ReadResolved32(guest_range_6, 24u, (r16 + 156));
            r5 = MemoryInline::ReadResolved32(guest_range_6, 28u, (r16 + 160));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_6, 32u);
        if (resolved_pair.valid) {
            r8 = resolved_pair.first;
            r7 = resolved_pair.second;
        } else {
            r8 = MemoryInline::ReadResolved32(guest_range_6, 32u, (r16 + 164));
            r7 = MemoryInline::ReadResolved32(guest_range_6, 36u, (r16 + 168));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_6, 40u);
        if (resolved_pair.valid) {
            r6 = resolved_pair.first;
            r0 = resolved_pair.second;
        } else {
            r6 = MemoryInline::ReadResolved32(guest_range_6, 40u, (r16 + 172));
            r0 = MemoryInline::ReadResolved32(guest_range_6, 44u, (r16 + 176));
        }
    }
    f1.d = MemoryInline::FlatReadFloat32((r1 + 140));
    MemoryInline::FlatWriteRam32((r1 + 156), r5);
    f0.d = MemoryInline::FlatReadFloat32((r1 + 156));
    MemoryInline::FlatWriteFloat32((r17 + 44), f1.d);
    MemoryInline::FlatWriteFloat32((r17 + 48), f0.d);
    MemoryInline::FlatWriteFloat32((r17 + 52), f4.d);
    MemoryInline::FlatWriteFloat32((r17 + 68), f3.d);
    MemoryInline::FlatWriteFloat32((r17 + 72), f2.d);
    r5 = MemoryInline::ReadResolved32(guest_range_2, 32u, (r4 + 148));
    MemoryInline::FlatWriteRamFloat32((r1 + 8), f3.d);
    r5 = MemoryInline::FlatRead32((r5 + 580));
    MemoryInline::FlatWriteRamFloat32((r1 + 12), f2.d);
    MemoryInline::FlatWriteFloat32((r5 + 68), f3.d);
    MemoryInline::FlatWriteRam32((r1 + 128), r15);
    MemoryInline::FlatWriteRam32((r1 + 132), r14);
    MemoryInline::FlatWriteRam32((r1 + 136), r12);
    MemoryInline::FlatWriteRam32((r1 + 144), r11);
    MemoryInline::FlatWriteRam32((r1 + 148), r10);
    MemoryInline::FlatWriteRam32((r1 + 152), r9);
    MemoryInline::FlatWriteRam32((r1 + 160), r8);
    MemoryInline::FlatWriteRam32((r1 + 164), r7);
    MemoryInline::FlatWriteRam32((r1 + 168), r6);
    MemoryInline::FlatWriteRam32((r1 + 172), r0);
    MemoryInline::FlatWriteRamFloat32((r1 + 16), f1.d);
    MemoryInline::FlatWriteRamFloat32((r1 + 20), f0.d);
    MemoryInline::FlatWriteFloat32((r5 + 72), f2.d);
}

loc_807A0418:
{
    r4 = (r4 + 4);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_807A01A0;
    }
}

loc_807A0420:
{
    r11 = (r1 + 304);
    // inline leaf 0x802DD07C (19 guest instruction(s))
    guest_range_1 = MemoryInline::ResolveRangeHost((r11 + -72), 0, 72u, true, false);
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 0u);
        if (resolved_pair.valid) {
            r14 = resolved_pair.first;
            r15 = resolved_pair.second;
        } else {
            r14 = MemoryInline::ReadResolved32(guest_range_1, 0u, (r11 + -72));
            r15 = MemoryInline::ReadResolved32(guest_range_1, 4u, (r11 + -68));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 8u);
        if (resolved_pair.valid) {
            r16 = resolved_pair.first;
            r17 = resolved_pair.second;
        } else {
            r16 = MemoryInline::ReadResolved32(guest_range_1, 8u, (r11 + -64));
            r17 = MemoryInline::ReadResolved32(guest_range_1, 12u, (r11 + -60));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 16u);
        if (resolved_pair.valid) {
            r18 = resolved_pair.first;
            r19 = resolved_pair.second;
        } else {
            r18 = MemoryInline::ReadResolved32(guest_range_1, 16u, (r11 + -56));
            r19 = MemoryInline::ReadResolved32(guest_range_1, 20u, (r11 + -52));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 24u);
        if (resolved_pair.valid) {
            r20 = resolved_pair.first;
            r21 = resolved_pair.second;
        } else {
            r20 = MemoryInline::ReadResolved32(guest_range_1, 24u, (r11 + -48));
            r21 = MemoryInline::ReadResolved32(guest_range_1, 28u, (r11 + -44));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 32u);
        if (resolved_pair.valid) {
            r22 = resolved_pair.first;
            r23 = resolved_pair.second;
        } else {
            r22 = MemoryInline::ReadResolved32(guest_range_1, 32u, (r11 + -40));
            r23 = MemoryInline::ReadResolved32(guest_range_1, 36u, (r11 + -36));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 40u);
        if (resolved_pair.valid) {
            r24 = resolved_pair.first;
            r25 = resolved_pair.second;
        } else {
            r24 = MemoryInline::ReadResolved32(guest_range_1, 40u, (r11 + -32));
            r25 = MemoryInline::ReadResolved32(guest_range_1, 44u, (r11 + -28));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 48u);
        if (resolved_pair.valid) {
            r26 = resolved_pair.first;
            r27 = resolved_pair.second;
        } else {
            r26 = MemoryInline::ReadResolved32(guest_range_1, 48u, (r11 + -24));
            r27 = MemoryInline::ReadResolved32(guest_range_1, 52u, (r11 + -20));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 56u);
        if (resolved_pair.valid) {
            r28 = resolved_pair.first;
            r29 = resolved_pair.second;
        } else {
            r28 = MemoryInline::ReadResolved32(guest_range_1, 56u, (r11 + -16));
            r29 = MemoryInline::ReadResolved32(guest_range_1, 60u, (r11 + -12));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 64u);
        if (resolved_pair.valid) {
            r30 = resolved_pair.first;
            r31 = resolved_pair.second;
        } else {
            r30 = MemoryInline::ReadResolved32(guest_range_1, 64u, (r11 + -8));
            r31 = MemoryInline::ReadResolved32(guest_range_1, 68u, (r11 + -4));
        }
    }
    // end of inlined leaf 0x802DD07C
    r0 = MemoryInline::FlatRead32((r1 + 308));
    ctx->lr = r0;
    r1 = (r1 + 304);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[14] = r14;
    ctx->gpr[15] = r15;
    ctx->gpr[16] = r16;
    ctx->gpr[17] = r17;
    ctx->gpr[18] = r18;
    ctx->gpr[19] = r19;
    ctx->gpr[20] = r20;
    ctx->gpr[21] = r21;
    ctx->gpr[22] = r22;
    ctx->gpr[23] = r23;
    ctx->gpr[24] = r24;
    ctx->gpr[25] = r25;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807A0170, NsmbwPeopleChangePlaceCards_807A0170, (CpuContext* ctx), (ctx));

// The selector's confirm voice (0x80771090): by the chosen character (+724) it plays
// SE_VOC_LU_PLAYER_JOIN (869) for 1, SE_VOC_KO_PLAYER_JOIN (940) for 2 and SE_VOC_KO2_PLAYER_JOIN
// (1010) for anything else, each +1 (the _MAME variant) when that player is small. The original
// never offered Mario, so Mario (0) got Yellow Toad's voice (developer report 2026-11-15: "when you
// choose mario he makes a toad noise"). Mario's are SE_VOC_MA_PLAYER_JOIN 797 / _MAME 798 (names
// from the disc's wii_mj2d_sound.brsar). Structural copy of
// build/nsmbw-d_basesNP/functions/func_80771090.cpp.
namespace {
bool MarioChosen(uint32_t selector) {
    uint32_t ch = 0xFFFFFFFFu;
    const bool mario = SelectorOffersMario() && Memory::TryRead32(selector + 724, ch) && ch == 0;
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] join voice for character %u%s\n", ch, mario ? " (Mario's)" : "");
    return mario;
}
} // namespace
extern "C" void NsmbwSelectorJoinVoice_80771090(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r3_addr_0 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r4_rot_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_80771090;

loc_80771090:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r0 = MemoryInline::FlatRead32((r3 + 724));
}

loc_807710AC:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(1))) {
        goto loc_807710F8;
    }
}

loc_807710B0:
{
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r3 = (r3 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 128));
    r0 = MemoryInline::FlatRead32((r3 + 568));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_807710C4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807710E0;
    }
}

loc_807710C8:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 870;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807710DCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_8077118C;
}

loc_807710E0:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 869;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807710F4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_8077118C;
}

loc_807710F8:
{
}

loc_807710FC:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(2))) {
        goto loc_80771148;
    }
}

loc_80771100:
{
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r3 = (r3 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 128));
    r0 = MemoryInline::FlatRead32((r3 + 568));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_80771114:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80771130;
    }
}

loc_80771118:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 941;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x8077112Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_8077118C;
}

loc_80771130:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 940;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x80771144u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_8077118C;
}

loc_80771148:
{
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & -4);
    r3 = (r3 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 128));
    r0 = MemoryInline::FlatRead32((r3 + 568));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_8077115C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80771178;
    }
}

loc_80771160:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = MarioChosen(r31) ? 798u : 1011u; // EDIT: Mario has his own voice (was always Yellow Toad's)
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x80771174u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_8077118C;
}

loc_80771178:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = MarioChosen(r31) ? 797u : 1010u; // EDIT: SE_VOC_MA_PLAYER_JOIN, not SE_VOC_KO2_PLAYER_JOIN
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x8077118Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_8077118C:
{
    r0 = MemoryInline::FlatRead32((r31 + 728));
    r3 = NSMBW_VA_PAIR(0x80350000u, 20832);
    r5 = MemoryInline::FlatRead32((r31 + 724));
    r3 = (r3 + 20832);
    r4_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r4 = (r4_rot_0 & -4);
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(2));
    r0 = (r0_rot_4 & -4);
    r3_addr_0 = (r3 + r4);
    MemoryInline::FlatWrite32(r3_addr_0, r5);
    r3 = (r31 + r0);
    r5 = MemoryInline::FlatRead32((r3 + 128));
    r0 = MemoryInline::FlatRead8((r5 + 610));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_807711B8:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807711EC;
    }
}

loc_807711BC:
{
    r0 = MemoryInline::FlatRead8((r5 + 611));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_807711C4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807711EC;
    }
}

loc_807711C8:
{
    r4 = 1;
    MemoryInline::FlatWrite8((r5 + 611), static_cast<uint8_t>(r4));
    r0 = 2;
    r3 = NSMBW_VA_PAIR(0x80930000u, 15696);
    MemoryInline::FlatWrite32((r5 + 572), r0);
    f0.d = MemoryInline::FlatReadFloat32((r3 + 15696));
    MemoryInline::FlatWrite32((r5 + 576), r4);
    MemoryInline::FlatWriteFloat32((r5 + 592), f0.d);
    MemoryInline::FlatWrite8((r5 + 613), static_cast<uint8_t>(r4));
}

loc_807711EC:
{
    r3 = MemoryInline::FlatRead32((r31 + 724));
    r0 = 1;
    f0.d = MemoryInline::FlatReadFloat32((r31 + 752));
    r3_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r3 = (r3_rot_0 & -4);
    r3 = (r31 + r3);
    r3 = MemoryInline::FlatRead32((r3 + 128));
    MemoryInline::FlatWriteFloat32((r3 + 604), f0.d);
    MemoryInline::FlatWrite8((r31 + 662), static_cast<uint8_t>(r0));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r0 = MemoryInline::FlatRead32((r1 + 20));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(80771090, NsmbwSelectorJoinVoice_80771090, (CpuContext* ctx), (ctx));

// "Number of Players" decision (dSelectPlayer_c, 0x807AC7A0): choosing "1" (+620 == 0) played
// SE_VOC_MA_PLAYER_DECIDE (803, Mario's "here we go"), because player 1 was always Mario. With the
// character select, "1" opens the selector first and the chosen character's own voice plays on its
// confirm (0x80771090), so the EDIT skips it (developer report 2026-11-15). dScGameSetup_c's
// VoiceEndWait (0x80919130) only waits while 803 plays, so it just moves on. Structural copy of
// build/nsmbw-d_basesNP/functions/func_807AC7A0.cpp.
extern "C" void NsmbwSelectPlayerDecide_807AC7A0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t cr1_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_807AC7A0;

loc_807AC7A0:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r4 = NSMBW_VA_PAIR(0x80430000u, -24016);
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = NSMBW_VA_PAIR(0x80990000u, 19112);
    r31 = (r31 + 19112);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    r6 = MemoryInline::FlatRead32((r3 + 620));
    r5 = MemoryInline::FlatRead32((r4 + -24016));
    SetCRResident(cr, xer, 1, static_cast<int32_t>(r6), static_cast<int32_t>(0));
}

loc_807AC7D0:
{
    if (((cr & 0x02000000u) != 0)) {
        goto loc_807AC83C;
    }
}

loc_807AC7D4:
{
    r4 = NSMBW_VA_PAIR(0x80430000u, -22716);
    r0 = MemoryInline::FlatRead32((r4 + -22716));
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r4 = (r5 + r0);
    r4 = MemoryInline::FlatRead32((r4 + 4));
    r4 = MemoryInline::FlatRead32((r4 + 28));
    r0 = (r4 & 2);
}

loc_807AC7F0:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_807AC804;
    }
}

loc_807AC7F4:
{
    if (((cr & 0x02000000u) != 0)) {
        goto loc_807AC7FC;
    }
}

loc_807AC7F8:
{
    MemoryInline::FlatWrite32((r3 + 636), r6);
}

loc_807AC7FC:
{
    r6 = 0;
    goto loc_807AC860;
}

loc_807AC804:
{
    r0 = (r4 & 8);
}

loc_807AC808:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_807AC820;
    }
}

loc_807AC80C:
{
    r6 = (r6 + -1);
}

loc_807AC814:
{
    if ((static_cast<int32_t>(r6) >= static_cast<int32_t>(1))) {
        goto loc_807AC860;
    }
}

loc_807AC818:
{
    r6 = 1;
    goto loc_807AC860;
}

loc_807AC820:
{
    r0 = (r4 & 4);
}

loc_807AC824:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_807AC860;
    }
}

loc_807AC828:
{
    r6 = (r6 + 1);
}

loc_807AC830:
{
    if ((static_cast<int32_t>(r6) <= static_cast<int32_t>(3))) {
        goto loc_807AC860;
    }
}

loc_807AC834:
{
    r6 = 3;
    goto loc_807AC860;
}

loc_807AC83C:
{
    r4 = NSMBW_VA_PAIR(0x80430000u, -22716);
    r0 = MemoryInline::FlatRead32((r4 + -22716));
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r4 = (r5 + r0);
    r4 = MemoryInline::FlatRead32((r4 + 4));
    r0 = MemoryInline::FlatRead32((r4 + 28));
    r0 = (r0 & 1);
}

loc_807AC858:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_807AC860;
    }
}

loc_807AC85C:
{
    r6 = MemoryInline::FlatRead32((r3 + 636));
}

loc_807AC860:
{
    r0 = MemoryInline::FlatRead32((r3 + 620));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r6), static_cast<int32_t>(r0));
}

loc_807AC868:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807AC888;
    }
}

loc_807AC86C:
{
    MemoryInline::FlatWrite32((r3 + 620), r6);
    r4 = (r31 + 208);
    r3 = (r3 + 112);
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807AC884u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_807AC934;
}

loc_807AC888:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22712);
    r4 = 2304;
    r3 = MemoryInline::FlatRead32((r3 + -22712));
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 32));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807AC8A4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_807AC8A8:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807AC8F0;
    }
}

loc_807AC8AC:
{
    r0 = 1;
    MemoryInline::FlatWrite32((r30 + 624), r0);
    r3 = (r30 + 112);
    r4 = (r31 + 400);
    r12 = MemoryInline::FlatRead32((r30 + 112));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807AC8CCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = MemoryInline::FlatRead32((r30 + 620));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_807AC8D4:
{
    if (((cr & 0x20000000u) != 0) && SelectorOffersMario() && LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] \"1\" chosen: Mario's decide voice skipped\n");
    if (((cr & 0x20000000u) == 0) || SelectorOffersMario()) { // EDIT: no Mario voice for "1"
        goto loc_807AC934;
    }
}

loc_807AC8D8:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 803;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807AC8ECu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_807AC934;
}

loc_807AC8F0:
{
    r3 = 0;
    ctx->lr = NSMBW_VA(0x807AC8F8u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x800B53F0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_807AC8FC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807AC934;
    }
}

loc_807AC900:
{
    r0 = -1;
    MemoryInline::FlatWrite32((r30 + 624), r0);
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 122;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807AC91Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r12 = MemoryInline::FlatRead32((r30 + 112));
    r3 = (r30 + 112);
    r4 = (r31 + 464);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807AC934u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_807AC934:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r30 = MemoryInline::FlatRead32((r1 + 8));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807AC7A0, NsmbwSelectPlayerDecide_807AC7A0, (CpuContext* ctx), (ctx));

// ---- Start Game / Back after the characters are chosen ---------------------------------------------
//
// dNumberOfPeopleChange_c's ButtonSelect (0x807A1B60) has the cursor in +1736: 0 Start Game, 1 Back.
//   - Start Game played SE_VOC_MA_PLAYER_DECIDE (803, 804 when small) whoever player 1 is, and
//     dScGameSetup_c's VoiceEndWait (0x80919130) waits while 803 plays. EDIT: player 1's own
//     character's decide voice (Luigi 875, Blue Toad 946, Yellow Toad 1016; only Mario has a small
//     variant; names from the disc's wii_mj2d_sound.brsar), and VoiceEndWait waits on that one.
//   - Back ends the screen with +1736 = 1, and dScGameSetup_c's ConnectionCheck (0x80918D10) then
//     goes back to StartMember ("Number of Players"). With player 1 choosing, Back should return to
//     the character select (developer report 2026-11-15). ButtonSelect's Back marks
//     g_reopenSelector and ConnectionCheck re-opens the screen with the same player count, the way
//     StartMember opens it (join states cleared, +1662 = 1, the screen's "active" flag). Pressing
//     back on the selectors themselves (0x807A1510's 0x807A0D20 path) still leaves to "Number of
//     Players", since it doesn't set the flag.
namespace {
bool g_reopenSelector = false;
uint32_t g_decideVoice = 803; // SE_VOC_MA_PLAYER_DECIDE

// Player 1's character on the Add/Drop screen: the started selector (+661) whose slot (+728) is
// remote 0. Falls back to daPyMng_c::mPlayerType[0].
uint32_t Player1Character(uint32_t screen) {
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t sel = 0, slot = 0xFFFFFFFFu, ch = 0;
        if (!Memory::TryRead32(screen + 116 + 4 * i, sel) || sel == 0) continue;
        uint32_t started = 0;
        if (!Memory::TryRead32(sel + 660, started) || ((started >> 16) & 0xFFu) == 0) continue; // +661
        if (Memory::TryRead32(sel + 728, slot) && slot == 0 && Memory::TryRead32(sel + 724, ch) && ch < 4) return ch;
    }
    uint32_t ch = 0;
    Memory::TryRead32(NSMBW_VA(0x80355160u), ch); // daPyMng_c::mPlayerType[0]
    return ch & 3u;
}

uint32_t DecideVoice(uint32_t screen, bool small) {
    static constexpr uint32_t kVoice[4] = {803, 875, 946, 1016}; // MA, LU, KO (Blue), KO2 (Yellow)
    const uint32_t ch = SelectorOffersMario() ? Player1Character(screen) : 0u;
    const uint32_t voice = (ch == 0 && small) ? 804u : kVoice[ch];
    g_decideVoice = voice;
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] Start Game: player 1 character %u, decide voice %u\n", ch, voice);
    return voice;
}
} // namespace

// ButtonSelect execute (0x807A1B60; see above). Structural copy of
// build/nsmbw-d_basesNP/functions/func_807A1B60.cpp.
extern "C" void NsmbwPeopleChangeButtonSelect_807A1B60(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_807A1B60;

loc_807A1B60:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r4 = NSMBW_VA_PAIR(0x80430000u, -24016);
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    r5 = MemoryInline::FlatRead32((r3 + 1736));
    r4 = MemoryInline::FlatRead32((r4 + -24016));
}

loc_807A1B88:
{
    r31 = r5;
    if ((static_cast<int32_t>(r5) != static_cast<int32_t>(0))) {
        goto loc_807A1BB8;
    }
}

loc_807A1B90:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22716);
    r0 = MemoryInline::FlatRead32((r3 + -22716));
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r3 = (r4 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 4));
    r0 = MemoryInline::FlatRead32((r3 + 28));
    r0 = (r0 & 8);
}

loc_807A1BAC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_807A1BE4;
    }
}

loc_807A1BB0:
{
    r31 = 1;
    goto loc_807A1BE4;
}

loc_807A1BB8:
{
}

loc_807A1BBC:
{
    if ((static_cast<int32_t>(r5) != static_cast<int32_t>(1))) {
        goto loc_807A1BE4;
    }
}

loc_807A1BC0:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22716);
    r0 = MemoryInline::FlatRead32((r3 + -22716));
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & -4);
    r3 = (r4 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 4));
    r0 = MemoryInline::FlatRead32((r3 + 28));
    r0 = (r0 & 4);
}

loc_807A1BDC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_807A1BE4;
    }
}

loc_807A1BE0:
{
    r31 = 0;
}

loc_807A1BE4:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r5), static_cast<int32_t>(r31));
}

loc_807A1BE8:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807A1C2C;
    }
}

loc_807A1BEC:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 120;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807A1C00u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = MemoryInline::FlatRead32((r30 + 1736));
    r4 = NSMBW_VA_PAIR(0x80990000u, 15776);
    MemoryInline::FlatWrite32((r30 + 1740), r0);
    r3 = (r30 + 1436);
    r4 = (r4 + 15776);
    MemoryInline::FlatWrite32((r30 + 1736), r31);
    r12 = MemoryInline::FlatRead32((r30 + 1436));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807A1C28u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_807A1CE8;
}

loc_807A1C2C:
{
    r3 = NSMBW_VA_PAIR(0x80430000u, -22712);
    r4 = 2304;
    r3 = MemoryInline::FlatRead32((r3 + -22712));
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 32));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807A1C48u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_807A1C4C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_807A1CE8;
    }
}

loc_807A1C50:
{
    r0 = MemoryInline::FlatRead32((r30 + 1736));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_807A1C58:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A1CB8;
    }
}

loc_807A1C5C:
{
    r31 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 121;
    r3 = MemoryInline::FlatRead32((r31 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807A1C70u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = NSMBW_VA_PAIR(0x80430000u, -23968);
    r0 = MemoryInline::FlatRead32((r3 + -23968));
    r0 = (r0 & 16);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_807A1C7C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A1CCC;
    }
}

loc_807A1C80:
{
    r3 = MemoryInline::FlatRead32((r30 + 1612));
    r0 = MemoryInline::FlatRead32((r3 + 568));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_807A1C8C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_807A1CA4;
    }
}

loc_807A1C90:
{
    r3 = MemoryInline::FlatRead32((r31 + -22680));
    r4 = DecideVoice(r30, true); // EDIT: was 804 (Mario's, small)
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807A1CA0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_807A1CCC;
}

loc_807A1CA4:
{
    r3 = MemoryInline::FlatRead32((r31 + -22680));
    r4 = DecideVoice(r30, false); // EDIT: was 803 (Mario's)
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807A1CB4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_807A1CCC;
}

loc_807A1CB8:
{
    if (SelectorOffersMario() && InGameSetup()) { // EDIT: Back returns to the character select
        g_reopenSelector = true;
        if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] Back on Start Game/Back: re-opening the character select\n");
    }
    r3 = NSMBW_VA_PAIR(0x80430000u, -22680);
    r4 = 122;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = NSMBW_VA(0x807A1CCCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801954C0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_807A1CCC:
{
    r12 = MemoryInline::FlatRead32((r30 + 1436));
    r4 = NSMBW_VA_PAIR(0x80990000u, 15904);
    r3 = (r30 + 1436);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    r4 = (r4 + 15904);
    ctr = r12;
    ctx->lr = NSMBW_VA(0x807A1CE8u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_807A1CE8:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r30 = MemoryInline::FlatRead32((r1 + 8));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(807A1B60, NsmbwPeopleChangeButtonSelect_807A1B60, (CpuContext* ctx), (ctx));

// dScGameSetup_c ConnectionCheck execute (0x80918D10): waits for the Add/Drop screen (+184) to finish
// (+1662 back to 0), then Start Game -> LowBatteryCheck, Back (+1736 != 0) -> StartMember. EDIT: see
// g_reopenSelector above. Structural copy of build/nsmbw-d_basesNP/functions/func_80918D10.cpp.
extern "C" void NsmbwGameSetupConnectionCheck_80918D10(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r12 = ctx->gpr[12];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80918D10;

loc_80918D10:
{
    r4 = MemoryInline::FlatRead32((r3 + 184));
    r6 = NSMBW_VA_PAIR(0x809A0000u, 3048);
    r6 = (r6 + 3048);
    r0 = MemoryInline::FlatRead8((r4 + 1664));
}

loc_80918D24:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80918D68;
    }
}

loc_80918D28:
{
    r5 = NSMBW_VA_PAIR(0x80430000u, -22716);
    r4 = NSMBW_VA_PAIR(0x80430000u, -24016);
    r0 = MemoryInline::FlatRead32((r5 + -22716));
    r4 = MemoryInline::FlatRead32((r4 + -24016));
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r4 = (r4 + r0);
    r4 = MemoryInline::FlatRead32((r4 + 4));
    r0 = MemoryInline::FlatRead32((r4 + 28));
    r0 = (r0 & 16);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80918D4C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_return;
    }
}

loc_80918D50:
{
    r3 = (r3 + 116);
    r12 = MemoryInline::FlatRead32(r3);
    r4 = (r6 + 400);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectJump(ctr, ctx);
    return;
    return;
}

loc_80918D68:
{
    r0 = MemoryInline::FlatRead8((r4 + 1662));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80918D70:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_return;
    }
}

loc_80918D74:
{
    r0 = MemoryInline::FlatRead32((r4 + 1736));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_80918D7C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80918D94;
    }
}

loc_80918D80:
{
    r3 = (r3 + 116);
    r12 = MemoryInline::FlatRead32(r3);
    r4 = (r6 + 464);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectJump(ctr, ctx);
    return;
    return;
}

loc_80918D94:
{
    if (g_reopenSelector) { // EDIT: re-open the character select instead of "Number of Players"
        g_reopenSelector = false;
        const uint32_t info = MemoryInline::FlatRead32(NSMBW_VA(0x8042A25Cu)); // as StartMember: join states
        for (uint32_t i = 0; i < 4; ++i) MemoryInline::FlatWrite32(info + 900 + 4 * i, 0);
        MemoryInline::FlatWrite32((r4 + 1736), 0);
        MemoryInline::FlatWrite8((r4 + 1662), 1);
        return;
    }
    r3 = (r3 + 116);
    r12 = MemoryInline::FlatRead32(r3);
    r4 = (r6 + 272);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectJump(ctr, ctx);
    return;
    return;
}

loc_return:
{
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[12] = r12;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(80918D10, NsmbwGameSetupConnectionCheck_80918D10, (CpuContext* ctx), (ctx));

// dScGameSetup_c VoiceEndWait execute (0x80919130): waits while the decide voice plays, then moves on.
// EDIT: waits on the voice ButtonSelect actually played. Structural copy of
// build/nsmbw-d_basesNP/functions/func_80919130.cpp.
extern "C" void NsmbwGameSetupVoiceEndWait_80919130(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80919130;

loc_80919130:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r4 = NSMBW_VA_PAIR(0x80430000u, -22680);
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r3 = MemoryInline::FlatRead32((r4 + -22680));
    r4 = SelectorOffersMario() ? g_decideVoice : 803u; // EDIT: the voice ButtonSelect played
    ctx->lr = NSMBW_VA(0x80919154u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x80196740u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_80919158:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_80919178;
    }
}

loc_8091915C:
{
    r12 = MemoryInline::FlatRead32((r31 + 116));
    r4 = NSMBW_VA_PAIR(0x809A0000u, 3704);
    r3 = (r31 + 116);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    r4 = (r4 + 3704);
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80919178u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80919178:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(80919130, NsmbwGameSetupVoiceEndWait_80919130, (CpuContext* ctx), (ctx));

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
constexpr std::array<uint32_t, 4> kOrigChars = {0, 1, 3, 2};   // both character tables, per slot
constexpr std::array<uint32_t, 4> kCharToPos = {0, 3, 1, 2};   // 0x80933D10, unchanged
constexpr std::array<uint32_t, 4> kOrigStartPos = {0, 3, 2, 1}; // 0x80933D20 = kCharToPos[kOrigChars]

uint32_t Sigma(uint32_t ch, uint32_t c) {
    if (ch == 0) return c;
    if (ch == c) return 0;
    return ch;
}

// The three tables' wanted contents for player 1 = c.
struct Wanted {
    std::array<uint32_t, 4> chars, startPos;
};
Wanted WantedFor(uint32_t c) {
    Wanted w{};
    for (uint32_t s = 0; s < 4; ++s) {
        w.chars[s] = Sigma(kOrigChars[s], c);
        w.startPos[s] = kCharToPos[w.chars[s]];
    }
    return w;
}

bool LogEnabled() {
    static const bool on = std::getenv("NSMBW_LOG_P1_CHARACTER") != nullptr;
    return on;
}

// -1 until the tables were found to hold either the original values or a swap this file wrote;
// then the character they are currently swapped for (0 = original). If memory ever holds something
// else (another game version's layout, a mod), nothing is written and it is logged once.
int g_applied = -1;
bool g_gaveUp = false;

bool Holds(uint32_t addr, const std::array<uint32_t, 4>& w) {
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t v = 0;
        if (!Memory::TryRead32(addr + 4 * i, v) || v != w[i]) return false;
    }
    return true;
}
bool TablesMatch(uint32_t c) {
    const Wanted w = WantedFor(c);
    return Holds(kResetTable, w.chars) && Holds(kStartCharTable, w.chars) && Holds(kStartPosTable, w.startPos);
}
void Write(uint32_t addr, const std::array<uint32_t, 4>& w) {
    for (uint32_t i = 0; i < 4; ++i) Memory::Write32(addr + 4 * i, w[i]);
}

void ApplyTables(uint32_t c) {
    if (g_gaveUp) return;
    if (g_applied < 0) {
        for (uint32_t k = 0; k < 4 && g_applied < 0; ++k) {
            if (TablesMatch(k)) g_applied = int(k);
        }
        if (g_applied < 0) {
            g_gaveUp = true;
            std::fprintf(stderr, "[nsmbw][p1chara] selector tables hold unexpected values; player 1 stays as loaded\n");
            return;
        }
    }
    // Without Mario in the selector, a slot started on Mario would sit on a position it can't show.
    if (!NsmbwSelectorHasMario()) c = 0;
    if (uint32_t(g_applied) == c && TablesMatch(c)) return;
    const Wanted w = WantedFor(c);
    Write(kResetTable, w.chars);
    Write(kStartCharTable, w.chars);
    Write(kStartPosTable, w.startPos);
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] start tables swapped for character %u (scene %u)\n", c, g_nsmbwCurrentSceneProfile);
    g_applied = int(c);
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

void ReloadMapIfModelIsStale(uint32_t c) {
    if (g_nsmbwCurrentSceneProfile != g_mapSceneSeen) { // a new scene: its map player is new
        g_mapSceneSeen = g_nsmbwCurrentSceneProfile;
        g_reloadRequested = false;
    }
    g_menuClosedFrames = NsmbwOptionsMenuIsOpen() ? 0 : g_menuClosedFrames + 1;
    static uint32_t frames = 0;
    if (LogEnabled() && g_nsmbwCurrentSceneProfile == 3u && (++frames % 120) == 0) {
        uint32_t w = 0;
        Memory::TryRead32(kNextSceneAddr, w);
        std::fprintf(stderr, "[nsmbw][p1chara] map check: model %d, player 1 %u, menu closed %d frames, requested %d, next scene 0x%X\n",
                     g_mapModelType, c, g_menuClosedFrames, int(g_reloadRequested), w >> 16);
    }
    if (g_nsmbwCurrentSceneProfile != 3u || g_reloadRequested || g_mapModelType < 0 || uint32_t(g_mapModelType) == c ||
        g_menuClosedFrames < 30) {
        return;
    }
    uint32_t word = 0; // m_nextScene is the high half of this big-endian word
    if (!Memory::TryRead32(kNextSceneAddr, word) || (word >> 16) != 0x2EEu) return; // a scene change is already on its way
    g_reloadRequested = true;
    if (LogEnabled()) std::fprintf(stderr, "[nsmbw][p1chara] map player was built as %d, player 1 is %u: reloading the map\n", g_mapModelType, c);
    CpuContext* ctx = CurrentCpuContext();
    const CpuContext saved = *ctx;
    InvokeDirectCpu<kReloadMap>(ctx);
    *ctx = saved;
}
} // namespace

// Every frame, from the VI pump (nsmbw_tick_read_pump.cpp): keep the tables matching player 1, and
// rebuild the map if its player model is someone else.
extern "C" void NsmbwP1CharacterTick() {
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
    if (pos == 0 && SelectorOffersMario()) {
        value = kMarioValue;
        ch = 0;
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
// The Add/Drop screen at `screen` is in "1 player picks a character" mode.
bool OnePlayerSelect(uint32_t screen) {
    uint32_t count = 0;
    return SelectorOffersMario() && InGameSetup() && Memory::TryRead32(screen + 1732, count) && count == 1;
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
    if (OnePlayerChosen(r3)) r0 = 0; // EDIT: player 1 starts not joined, so it gets a selector
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
    r9 = OnePlayerSelect(r31) ? 0u : 1u; // EDIT: was 1 (count selectors from selector 1, not count - 1)
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
    if (OnePlayerSelect(r3)) { // EDIT: count selectors (from selector 1) instead of count - 1
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

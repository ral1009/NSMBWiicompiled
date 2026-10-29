// The Options button in the in-course pause window (Continue / Options / Exit).
//
// The layout side - the third button's panes, its B03_optionButton group and its animation
// tracks - is added to the pause menu's archive as it is served (runtime/src/product/
// nsmbw_layout_patch.cpp). This file is the game side. Two classes drive the window:
//
//   Pausewindow_c (NSMBW-Decomp source/dol/bases/d_pausewindow.cpp) draws it and animates the
//   buttons. Button n's animations are found by arithmetic on the group-animation index -
//   on = 3 + n, off = 7 + n, hit = 9 + n (ANIM_ON_TUZUKU / ANIM_OFF_TUZUKU / ANIM_HIT_TUZUKU) -
//   and its cursor pane by a two-entry table, so a button 2 would reach the wrong animations and
//   read past the table. The new group's four animations are registered after the original eleven
//   (on 11, idle 12, off 13, hit 14), and the functions that pick an animation or the cursor pane
//   are replaced with versions that know button 2:
//     0x8015A750  create                                   (copy, EDIT at GroupRegister)
//     0x8015AD70  SelectCursorSetup
//     0x8015AEC0  initializeState_OpenAnimeEndWait
//     0x8015AFB0  initializeState_ButtonChangeAnimeEndWait
//     0x8015B170  initializeState_HitAnimeEndWait
//   Addresses: the decomp's object puts five weak template destructors before create that the
//   original keeps elsewhere, so every function from create on is 0x1A0 lower than in
//   d_pausewindow.o (create 0x8015A8F0 -> 0x8015A750, the address nsmbw_pause_exit.cpp found
//   from its isNowCourseClear call; executeState_PauseDisp 0x8015B240 -> 0x8015B0A0, which plays
//   SE_SYS_CURSOR as the source does).
//
//   The pause input handler (undecompiled; between d_pad and d_pc in the DOL) reads the remote
//   once per frame (0x800D0DA0, called from 0x800D0F40) and moves the cursor through 0x800D0CF0,
//   which clamps the button to 0..1. Its object keeps the button at +8 and the player at +12.
//     0x800D0CF0  cursor move: steps through Continue (0), Options (2), Exit (1) in screen order,
//                 keeping the original's gates (Exit only when isNowCourseClear or the cannon
//                 course; no movement at all in the enemy-ambush courses 32..34).
//     0x800D0DA0  input (copy, EDIT at the confirm branch): A / 2 on Options plays SE_SYS_DECIDE
//                 and starts the Options sequence below instead of the game's "hit and close".
//
// The Options sequence moves the way the game moves between its own windows (Exit -> its yes/no
// window and back): the button's hit animation, the pause window's out animation (it is then
// scaled to nothing, so it isn't seen behind the options menu), the options menu's in animation;
// and when the options menu has played its own out animation, the pause window comes back through
// its normal OpenAnimeEndWait state (in animation, then Options highlighted again). The window is
// never closed or deactivated, so nothing in the game sees the pause end. While the sequence runs,
// the handler does nothing else and mIsAnimating stays set, so no other press gets through.
//
// Fields (Pausewindow_c, d_pausewindow.hpp, offsets from the translated bodies): mLayout +112,
// mStateMgr +520 (changeState is its vtable slot +24, as executeState_InitWait calls it),
// mpPicturePanes +580, mNextButton +644, mActiveButton +648, mIsAnimating +660 (u8).
// StateID_OpenAnimeEndWait 0x803774C0 (executeState_InitWait). dSelectCursor_c::m_instance is
// r13 - 21464 = 0x8042A5A8 and Cancel(0) 0x8010C890 (finalizePauseDisp). LytBase_c::isAnime
// 0x800C9700.
// Pausewindow_c::m_instance is r13 - 21348 = 0x8042A61C. LytBase_c methods (d_lytbase.o, 0x10
// lower than the decomp object from ReadResourceEx on, for the same reason): PPaneRegister
// 0x800C8F30, GroupRegister 0x800C91E0, AnimeStartSetup 0x800C93E0, ReverseAnimeStartSetup
// 0x800C94C0, AllAnimeEndSetup 0x800C95F0. dGameCom::SelectCursorSetup 0x800B44D0 (syms.txt).
//
// If the layout patch failed (NsmbwPauseMenuHasOptions false), everything here behaves exactly
// like the original two-button window.
#include <cstdint>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" bool NsmbwPauseMenuHasOptions();      // runtime/src/product/nsmbw_layout_patch.cpp
extern "C" void NsmbwOptionsMenuRequestOpen();  // runtime/src/product/nsmbw_options_menu.cpp
extern "C" bool NsmbwOptionsMenuTakeReturnToPause(); // same file: true once, when it has closed

namespace {
constexpr uint32_t kOptionsButton = 2;
constexpr uint32_t kPausewindowInstance = 0x8042A61Cu;
constexpr uint32_t kLayout = 112, kPictures = 580, kNextButton = 644, kActiveButton = 648, kIsAnimating = 660;
constexpr uint32_t kSndAudioMgr = 0x8042A768u; // SndAudioMgr::sInstance
constexpr uint32_t kStartGameInfo = 0x80315E90u; // dInfo_c::m_startGameInfo; mLevel1 at +13

// Group-animation indices (ANIM_e), with the Options button's four after the original eleven.
enum : uint32_t { kOn = 3, kIdle = 5, kOff = 7, kHit = 9, kOptionsOn = 11, kOptionsIdle = 12, kOptionsOff = 13, kOptionsHit = 14 };
uint32_t OnAnim(uint32_t b) { return b == kOptionsButton ? kOptionsOn : kOn + b; }
uint32_t OffAnim(uint32_t b) { return b == kOptionsButton ? kOptionsOff : kOff + b; }
uint32_t HitAnim(uint32_t b) { return b == kOptionsButton ? kOptionsHit : kHit + b; }

// P_SBBase_03, found when the window is created (the original keeps its two in mpPicturePanes).
uint32_t g_optionsPane = 0;

void AnimeStartSetup(CpuContext* ctx, uint32_t lyt, uint32_t anim, bool atEnd) {
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = anim;
    ctx->gpr[5] = atEnd ? 1u : 0u;
    InvokeDirectCpu<0x800C93E0u>(ctx);
}
void ReverseAnimeStartSetup(CpuContext* ctx, uint32_t lyt, uint32_t anim) {
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = anim;
    ctx->gpr[5] = 0;
    InvokeDirectCpu<0x800C94C0u>(ctx);
}
void AllAnimeEndSetup(CpuContext* ctx, uint32_t lyt) {
    ctx->gpr[3] = lyt;
    InvokeDirectCpu<0x800C95F0u>(ctx);
}

void WriteString(uint32_t addr, const char* s) {
    do {
        MemoryInline::FlatWrite8(addr++, static_cast<uint8_t>(*s));
    } while (*s++ != '\0');
}

// Replaces create's GroupRegister(&mLayout, GROUP_NAME_DT, ANIME_INDEX_TBL, 11) (r3..r6 as the
// copy set them). The extended tables only have to live for the call - AnmGroupBase_c::create
// compares the name and keeps nothing - so they go on the guest stack below the caller's frame,
// with the stack pointer lowered past them for the callee. The same space then names
// P_SBBase_03 for PPaneRegister.
void RegisterGroupsWithOptions(CpuContext* ctx) {
    if (!NsmbwPauseMenuHasOptions()) {
        InvokeDirectCpu<0x800C91E0u>(ctx);
        return;
    }
    const uint32_t lyt = ctx->gpr[3], names = ctx->gpr[4], indices = ctx->gpr[5], count = ctx->gpr[6];
    const uint32_t sp = ctx->gpr[1];
    const uint32_t scratch = (sp - 0x100u) & ~0x1Fu;
    const uint32_t nameTable = scratch, indexTable = scratch + 0x40, groupName = scratch + 0x80,
                   paneName = scratch + 0xA0, paneNameTable = scratch + 0xB0, paneOut = scratch + 0xB4;
    WriteString(groupName, "B03_optionButton");
    WriteString(paneName, "P_SBBase_03");
    for (uint32_t i = 0; i < count; ++i) {
        MemoryInline::FlatWrite32(nameTable + 4 * i, MemoryInline::FlatRead32(names + 4 * i));
        MemoryInline::FlatWrite32(indexTable + 4 * i, MemoryInline::FlatRead32(indices + 4 * i));
    }
    // AnmNameTbl: onButton 3, idleButton 4, offButon 5, hitButton 6 (d_pausewindow.cpp).
    const uint32_t anims[4] = {3, 4, 5, 6};
    for (uint32_t i = 0; i < 4; ++i) {
        MemoryInline::FlatWrite32(nameTable + 4 * (count + i), groupName);
        MemoryInline::FlatWrite32(indexTable + 4 * (count + i), anims[i]);
    }
    MemoryInline::FlatWrite32(paneNameTable, paneName);
    MemoryInline::FlatWrite32(paneOut, 0);

    const uint32_t calleeSp = scratch - 0x40u;
    MemoryInline::FlatWrite32(calleeSp, sp); // back chain
    ctx->gpr[1] = calleeSp;
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = nameTable;
    ctx->gpr[5] = indexTable;
    ctx->gpr[6] = count + 4;
    InvokeDirectCpu<0x800C91E0u>(ctx);
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = paneNameTable;
    ctx->gpr[5] = paneOut;
    ctx->gpr[6] = 1;
    InvokeDirectCpu<0x800C8F30u>(ctx);
    g_optionsPane = MemoryInline::FlatRead32(paneOut);
    ctx->gpr[1] = sp;
}

constexpr uint32_t kAnimOutWindow = 2; // ANIM_OUT_WINDOW
constexpr uint32_t kStateMgr = 520, kStateOpenAnimeEndWait = 0x803774C0u;
constexpr uint32_t kSelectCursorInstance = 0x8042A5A8u;

enum class Phase { None, Hit, Out, Menu };
Phase g_phase = Phase::None;

bool IsAnime(CpuContext* ctx, uint32_t lyt, uint32_t anim) {
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = anim;
    InvokeDirectCpu<0x800C9700u>(ctx);
    return (ctx->gpr[3] & 0xFF) != 0;
}

// The handler's confirm on Options: the decide sound the game plays for its own buttons and the
// button's hit animation; Step carries on from there.
void StartOptions(CpuContext* ctx) {
    const uint32_t window = MemoryInline::FlatRead32(kPausewindowInstance);
    ctx->gpr[3] = MemoryInline::FlatRead32(kSndAudioMgr);
    ctx->gpr[4] = 121; // SE_SYS_DECIDE
    ctx->gpr[5] = 1;
    InvokeDirectCpu<0x801954C0u>(ctx);
    MemoryInline::FlatWrite8(window + kIsAnimating, 1);
    AnimeStartSetup(ctx, window + kLayout, kOptionsHit, false);
    g_phase = Phase::Hit;
}

// One frame of the Options sequence, at the top of the handler. True while it runs: the handler
// then does nothing else this frame.
bool Step(CpuContext* ctx) {
    if (g_phase == Phase::None) return false;
    const uint32_t window = MemoryInline::FlatRead32(kPausewindowInstance);
    if (window == 0) { // the pause window is gone (scene change): nothing to return to
        g_phase = Phase::None;
        return false;
    }
    const uint32_t lyt = window + kLayout;
    switch (g_phase) {
    case Phase::Hit:
        if (IsAnime(ctx, lyt, kOptionsHit)) return true;
        // The cursor goes with the window, as the game's finalizeState_PauseDisp does.
        ctx->gpr[3] = MemoryInline::FlatRead32(kSelectCursorInstance);
        ctx->gpr[4] = 0;
        InvokeDirectCpu<0x8010C890u>(ctx);
        AnimeStartSetup(ctx, lyt, kAnimOutWindow, false);
        g_phase = Phase::Out;
        return true;
    case Phase::Out:
        if (IsAnime(ctx, lyt, kAnimOutWindow)) return true;
        NsmbwOptionsMenuRequestOpen();
        g_phase = Phase::Menu;
        return true;
    case Phase::Menu: {
        if (!NsmbwOptionsMenuTakeReturnToPause()) return true;
        // mStateMgr.changeState(StateID_OpenAnimeEndWait): the window's own way back in, which
        // ends by highlighting mNextButton (still Options) and clearing mIsAnimating.
        const uint32_t mgr = window + kStateMgr;
        const uint32_t fn = MemoryInline::FlatRead32(MemoryInline::FlatRead32(mgr) + 24);
        ctx->gpr[3] = mgr;
        ctx->gpr[4] = kStateOpenAnimeEndWait;
        InvokeIndirectCpu(fn, ctx);
        g_phase = Phase::None;
        return true;
    }
    case Phase::None: break;
    }
    return false;
}
} // namespace

// Pausewindow_c::SelectCursorSetup: dGameCom::SelectCursorSetup(button pane, 0, false).
extern "C" void NsmbwPausewindowSelectCursorSetup_8015AD70(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3];
    const uint32_t button = MemoryInline::FlatRead32(self + kNextButton);
    uint32_t pane = 0;
    if (button == kOptionsButton) {
        pane = g_optionsPane;
    } else {
        // PANE_IDX_TBL = {P_SBBase_00, P_SBBase_02} = {0, 1}.
        pane = MemoryInline::FlatRead32(self + kPictures + 4 * (button != 0 ? 1u : 0u));
    }
    if (pane == 0) return;
    ctx->gpr[3] = pane;
    ctx->gpr[4] = 0;
    ctx->gpr[5] = 0;
    InvokeDirectCpu<0x800B44D0u>(ctx);
}
PPC_NATIVE_OVERRIDE_VOID(8015AD70, NsmbwPausewindowSelectCursorSetup_8015AD70, (CpuContext* ctx), (ctx));

// initializeState_OpenAnimeEndWait: every button starts "off" (the on-animations played backwards
// to their start), then the window opens.
extern "C" void NsmbwPausewindowOpenAnime_8015AEC0(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3], lyt = self + kLayout;
    MemoryInline::FlatWrite8(self + kIsAnimating, 1);
    AllAnimeEndSetup(ctx, lyt);
    ReverseAnimeStartSetup(ctx, lyt, OnAnim(0));
    ReverseAnimeStartSetup(ctx, lyt, OnAnim(1));
    if (NsmbwPauseMenuHasOptions()) ReverseAnimeStartSetup(ctx, lyt, OnAnim(kOptionsButton));
    AnimeStartSetup(ctx, lyt, 0 /* ANIM_IN_WINDOW */, false);
}
PPC_NATIVE_OVERRIDE_VOID(8015AEC0, NsmbwPausewindowOpenAnime_8015AEC0, (CpuContext* ctx), (ctx));

// initializeState_ButtonChangeAnimeEndWait: the old button's off-animation, the new one's on.
extern "C" void NsmbwPausewindowButtonChange_8015AFB0(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3], lyt = self + kLayout;
    AllAnimeEndSetup(ctx, lyt);
    MemoryInline::FlatWrite8(self + kIsAnimating, 1);
    const int32_t active = static_cast<int32_t>(MemoryInline::FlatRead32(self + kActiveButton));
    if (active >= 0) AnimeStartSetup(ctx, lyt, OffAnim(static_cast<uint32_t>(active)), false);
    const uint32_t next = MemoryInline::FlatRead32(self + kNextButton);
    MemoryInline::FlatWrite32(self + kActiveButton, next);
    AnimeStartSetup(ctx, lyt, OnAnim(next), false);
}
PPC_NATIVE_OVERRIDE_VOID(8015AFB0, NsmbwPausewindowButtonChange_8015AFB0, (CpuContext* ctx), (ctx));

// initializeState_HitAnimeEndWait. Options never gets here (the handler opens the menu instead),
// but the index is still right if it ever does.
extern "C" void NsmbwPausewindowHitAnime_8015B170(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3];
    MemoryInline::FlatWrite8(self + kIsAnimating, 1);
    AnimeStartSetup(ctx, self + kLayout, HitAnim(MemoryInline::FlatRead32(self + kNextButton)), false);
}
PPC_NATIVE_OVERRIDE_VOID(8015B170, NsmbwPausewindowHitAnime_8015B170, (CpuContext* ctx), (ctx));

// The handler's cursor move (this, target), target = current +/- 1. Steps one place through the
// buttons in screen order and keeps the original's gates (translated body: func_800D0CF0.cpp).
extern "C" void NsmbwPauseCursorMove_800D0CF0(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3];
    const int32_t target = static_cast<int32_t>(ctx->gpr[4]);
    const uint32_t current = MemoryInline::FlatRead32(self + 8);
    const bool options = NsmbwPauseMenuHasOptions();
    const uint32_t order3[3] = {0, kOptionsButton, 1}, order2[2] = {0, 1};
    const uint32_t* order = options ? order3 : order2;
    const int32_t n = options ? 3 : 2;
    int32_t pos = 0;
    for (int32_t i = 0; i < n; ++i) {
        if (order[i] == current) pos = i;
    }
    const int32_t dir = target > static_cast<int32_t>(current) ? 1 : target < static_cast<int32_t>(current) ? -1 : 0;
    int32_t next = pos + dir;
    if (next < 0) next = 0;
    if (next >= n) next = n - 1;
    const uint32_t button = order[next];
    if (button == current) return;
    if (button == 1) { // Exit: only once the course is cleared (always, nsmbw_pause_exit.cpp) or in the cannon course
        InvokeDirectCpu<0x800B4E30u>(ctx);
        const bool cleared = ctx->gpr[3] != 0;
        if (!cleared && MemoryInline::FlatRead8(kStartGameInfo + 13) != 35) return;
    }
    // Enemy-ambush courses (mLevel1 32..34): the cursor stays on Continue.
    if (static_cast<uint8_t>(MemoryInline::FlatRead8(kStartGameInfo + 13) - 32) <= 2) return;
    const uint32_t window = MemoryInline::FlatRead32(kPausewindowInstance);
    MemoryInline::FlatWrite32(window + kNextButton, button);
    MemoryInline::FlatWrite32(self + 8, button);
}
PPC_NATIVE_OVERRIDE_VOID(800D0CF0, NsmbwPauseCursorMove_800D0CF0, (CpuContext* ctx), (ctx));

extern "C" void NsmbwPausewindowCreate_8015A750(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_8015A750;

loc_8015A750:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    r31 = r3;
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    r30 = 0x80320000u;
    r30 = (r30 + 32000);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    r29 = 0x802F0000u;
    r29 = (r29 + 23776);
    r0 = MemoryInline::FlatRead8((r3 + 656));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8015A784:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8015A790;
    }
}

loc_8015A788:
{
    r3 = 1;
    goto loc_8015A930;
}

loc_8015A790:
{
    r4 = (r30 + 760);
    r5 = 0;
    r3 = (r3 + 112);
    ctx->lr = 0x8015A7A0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8D00u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8015A7A4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8015A7B0;
    }
}

loc_8015A7A8:
{
    r3 = 0;
    goto loc_8015A930;
}

loc_8015A7B0:
{
    r12 = MemoryInline::FlatRead32((r31 + 120));
    r3 = (r31 + 112);
    r4 = (r30 + 784);
    r5 = 0;
    r12 = MemoryInline::FlatRead32((r12 + 20));
    ctr = r12;
    ctx->lr = 0x8015A7CCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
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
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r4 = (r30 + 260);
    r5 = 7;
    ctx->lr = 0x8015A7DCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C90A0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r4 = (r30 + 340);
    r5 = (r29 + 0);
    r6 = 11;
    ctx->lr = 0x8015A7F0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    RegisterGroupsWithOptions(ctx); // EDIT: was InvokeDirectCpu<0x800C91E0u>(ctx), LytBase_c::GroupRegister
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r4 = (r30 + 440);
    r5 = (r29 + 48);
    r6 = 1;
    r7 = 4;
    ctx->lr = 0x8015A808u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C9010u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r4 = (r30 + 496);
    r5 = (r29 + 64);
    r6 = 2;
    r7 = 3;
    ctx->lr = 0x8015A820u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C9010u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r4 = (r30 + 560);
    r5 = (r31 + 580);
    r6 = 4;
    ctx->lr = 0x8015A834u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8F30u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r5 = (r31 + 596);
    r4 = (r13 + -25248);
    r6 = 1;
    ctx->lr = 0x8015A848u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8EC0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r4 = (r30 + 664);
    r5 = (r31 + 600);
    r6 = 8;
    ctx->lr = 0x8015A85Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8FA0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    r4 = (r30 + 748);
    r5 = (r31 + 632);
    r6 = 3;
    ctx->lr = 0x8015A870u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C8E50u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = 0;
    r4 = 141;
    r0 = 1;
    MemoryInline::FlatWrite8((r31 + 124), static_cast<uint8_t>(r4));
    MemoryInline::FlatWrite8((r31 + 657), static_cast<uint8_t>(r3));
    MemoryInline::FlatWrite8((r31 + 656), static_cast<uint8_t>(r0));
    MemoryInline::FlatWrite8((r31 + 660), static_cast<uint8_t>(r3));
    MemoryInline::FlatWrite32((r31 + 652), r3);
    MemoryInline::FlatWrite8((r31 + 661), static_cast<uint8_t>(r3));
    MemoryInline::FlatWrite8((r31 + 659), static_cast<uint8_t>(r3));
    ctx->lr = 0x8015A89Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800B4E30u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
}

loc_8015A8A0:
{
    if ((static_cast<int32_t>(r3) != static_cast<int32_t>(0))) {
        goto loc_8015A8C4;
    }
}

loc_8015A8A4:
{
    r0 = MemoryInline::FlatRead16((r13 + -28478));
}

loc_8015A8AC:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(5))) {
        goto loc_8015A8C4;
    }
}

loc_8015A8B0:
{
    r3 = 0x80310000u;
    r3 = (r3 + 24208);
    r0 = MemoryInline::FlatRead8((r3 + 13));
}

loc_8015A8C0:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(35))) {
        goto loc_8015A8D8;
    }
}

loc_8015A8C4:
{
    r3 = MemoryInline::FlatRead32((r31 + 588));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    goto loc_8015A8EC;
}

loc_8015A8D8:
{
    r3 = MemoryInline::FlatRead32((r31 + 588));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
}

loc_8015A8EC:
{
    r3 = 0x80310000u;
    r3 = (r3 + 24208);
    r3 = MemoryInline::FlatRead8((r3 + 13));
    r0 = (r3 + 224);
    r0 = (r0 & 255);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
}

loc_8015A904:
{
    if (((cr & 0x40000000u) != 0)) {
        goto loc_8015A91C;
    }
}

loc_8015A908:
{
    r3 = MemoryInline::FlatRead32((r31 + 588));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
}

loc_8015A91C:
{
    r3 = r31;
    ctx->lr = 0x8015A924u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x8015AC60u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r31 + 112);
    ctx->lr = 0x8015A92Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800C95F0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = 1;
}

loc_8015A930:
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
    ctx->gpr[12] = r12;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(8015A750, NsmbwPausewindowCreate_8015A750, (CpuContext* ctx), (ctx));

static void NsmbwPauseInputBody(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r28_rot_0 = 0;
    uint32_t r28_rot_1 = 0;
    uint32_t r28_rot_2 = 0;
    uint32_t r3_addr_0 = 0;
    uint32_t r3_addr_1 = 0;
    uint32_t r3_addr_2 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r26 = ctx->gpr[26];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_800D0DA0;

loc_800D0DA0:
{
    guest_range_0 = MemoryInline::ResolveRangeHost((r1 + -32), 0, 40u, false, true);
    MemoryInline::WriteResolved32(guest_range_0, 0u, (r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::WriteResolved32(guest_range_0, 36u, (r1 + 36), r0);
    r11 = (r1 + 32);
    // inline leaf 0x802DD060 (7 guest instruction(s))
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 8u, ((static_cast<uint64_t>(static_cast<uint32_t>(r26)) << 32) | static_cast<uint32_t>(r27)))) {
        MemoryInline::WriteResolved32(guest_range_0, 8u, (r11 + -24), r26);
        MemoryInline::WriteResolved32(guest_range_0, 12u, (r11 + -20), r27);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 16u, ((static_cast<uint64_t>(static_cast<uint32_t>(r28)) << 32) | static_cast<uint32_t>(r29)))) {
        MemoryInline::WriteResolved32(guest_range_0, 16u, (r11 + -16), r28);
        MemoryInline::WriteResolved32(guest_range_0, 20u, (r11 + -12), r29);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 24u, ((static_cast<uint64_t>(static_cast<uint32_t>(r30)) << 32) | static_cast<uint32_t>(r31)))) {
        MemoryInline::WriteResolved32(guest_range_0, 24u, (r11 + -8), r30);
        MemoryInline::WriteResolved32(guest_range_0, 28u, (r11 + -4), r31);
    }
    // end of inlined leaf 0x802DD060
    r31 = r3;
    r3 = -6;
    // inline leaf 0x800B3B50 (3 guest instruction(s))
    r0 = MemoryInline::FlatRead32((r13 + -22360));
    r3 = (r0 & r3);
    // end of inlined leaf 0x800B3B50
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_800D0DC4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_800D0F1C;
    }
}

loc_800D0DC8:
{
    r26 = MemoryInline::FlatRead32((r31 + 12));
    r27 = MemoryInline::FlatRead32((r31 + 8));
    r3 = r26;
    ctx->lr = 0x800D0DD8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeDirectCpu<0x8005F900u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_800D0DDC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_800D0F1C;
    }
}

loc_800D0DE0:
{
    r3 = MemoryInline::FlatRead32((r13 + -21348));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_800D0DE8:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_800D0F1C;
    }
}

loc_800D0DEC:
{
    r0 = MemoryInline::FlatRead8((r3 + 660));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_800D0DF4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_800D0DFC;
    }
}

loc_800D0DF8:
{
    goto loc_800D0F1C;
}

loc_800D0DFC:
{
    r3 = 0x80370000u;
    r28_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r26), static_cast<uint32_t>(2));
    r28 = (r28_rot_1 & -4);
    r3 = (r3 + 32648);
    r29 = MemoryInline::FlatRead32((r13 + -22352));
    r3_addr_1 = (r3 + r28);
    r3 = MemoryInline::FlatRead32(r3_addr_1);
    r4 = 2304;
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 32));
    ctr = r12;
    ctx->lr = 0x800D0E24u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
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
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r30 = r3;
    r3 = r26;
    ctx->lr = 0x800D0E30u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x800B53F0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r4 = (r29 + r28);
    r4 = MemoryInline::FlatRead32((r4 + 4));
    r0 = MemoryInline::FlatRead32((r4 + 28));
    r0 = (r0 & 16);
}

loc_800D0E40:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_800D0E54;
    }
}

loc_800D0E44:
{
}

loc_800D0E48:
{
    if ((static_cast<int32_t>(r3) != static_cast<int32_t>(0))) {
        goto loc_800D0E54;
    }
}

loc_800D0E4C:
{
}

loc_800D0E50:
{
    if ((static_cast<int32_t>(r30) == static_cast<int32_t>(0))) {
        goto loc_800D0EEC;
    }
}

loc_800D0E54:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_800D0E58:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_800D0E64;
    }
}

loc_800D0E5C:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_800D0E60:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_800D0E6C;
    }
}

loc_800D0E64:
{
    r3 = r31;
    ctx->lr = 0x800D0E6Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800D0F80u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
}

loc_800D0E6C:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r30), static_cast<int32_t>(0));
}

loc_800D0E70:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_800D0ECC;
    }
}

loc_800D0E74:
{
    r0 = MemoryInline::FlatRead32((r31 + 8));
    // EDIT: a press on Options starts the Options sequence (StartOptions / Step).
    if (r0 == kOptionsButton && NsmbwPauseMenuHasOptions()) {
        ctx->gpr[1] = r1;
        StartOptions(ctx);
        goto loc_800D0F1C;
    }
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(1));
}

loc_800D0E7C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_800D0EA0;
    }
}

loc_800D0E80:
{
    ctx->lr = 0x800D0E84u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800B4E30u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_800D0E88:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_800D0EA0;
    }
}

loc_800D0E8C:
{
    r3 = 0x80310000u;
    r3 = (r3 + 24208);
    r0 = MemoryInline::FlatRead8((r3 + 13));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(35));
}

loc_800D0E9C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_800D0F1C;
    }
}

loc_800D0EA0:
{
    r3 = r31;
    ctx->lr = 0x800D0EA8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800D0F80u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    r3 = MemoryInline::FlatRead32((r13 + -21348));
    r0 = 1;
    r4 = 121;
    r5 = 1;
    MemoryInline::FlatWrite8((r3 + 659), static_cast<uint8_t>(r0));
    MemoryInline::FlatWrite8((r31 + 25), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r13 + -21016));
    ctx->lr = 0x800D0EC8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x801954C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_800D0F1C;
}

loc_800D0ECC:
{
    r3 = MemoryInline::FlatRead32((r13 + -21016));
    r4 = 122;
    r5 = 1;
    ctx->lr = 0x800D0EDCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x801954C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r11 = ctx->gpr[11];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = MemoryInline::FlatRead32((r13 + -21348));
    r0 = 1;
    MemoryInline::FlatWrite8((r3 + 658), static_cast<uint8_t>(r0));
    goto loc_800D0F1C;
}

loc_800D0EEC:
{
    r3 = MemoryInline::FlatRead32((r4 + 28));
    r0 = (r3 & 1);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_800D0EF4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_800D0F08;
    }
}

loc_800D0EF8:
{
    r3 = r31;
    r4 = (r27 + 1);
    ctx->lr = 0x800D0F04u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800D0CF0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    goto loc_800D0F1C;
}

loc_800D0F08:
{
    r0 = (r3 & 2);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_800D0F0C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_800D0F1C;
    }
}

loc_800D0F10:
{
    r3 = r31;
    r4 = (r27 + -1);
    ctx->lr = 0x800D0F1Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800D0CF0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
}

loc_800D0F1C:
{
    r11 = (r1 + 32);
    // inline leaf 0x802DD0AC (7 guest instruction(s))
    r26 = MemoryInline::FlatRead32((r11 + -24));
    r27 = MemoryInline::FlatRead32((r11 + -20));
    r28 = MemoryInline::FlatRead32((r11 + -16));
    r29 = MemoryInline::FlatRead32((r11 + -12));
    r30 = MemoryInline::FlatRead32((r11 + -8));
    r31 = MemoryInline::FlatRead32((r11 + -4));
    // end of inlined leaf 0x802DD0AC
    r0 = MemoryInline::FlatRead32((r1 + 36));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
    ctx->gpr[12] = r12;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

// The handler: the Options sequence first; while it runs, nothing else.
extern "C" void NsmbwPauseInput_800D0DA0(CpuContext* MKW_RESTRICT ctx)
{
    if (Step(ctx)) return;
    NsmbwPauseInputBody(ctx);
}
PPC_NATIVE_OVERRIDE_VOID(800D0DA0, NsmbwPauseInput_800D0DA0, (CpuContext* ctx), (ctx));

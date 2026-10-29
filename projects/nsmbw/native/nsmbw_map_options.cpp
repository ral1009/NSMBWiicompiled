// The Options button in the world-map menu (+ on the map): Star Coins, Add/Drop Players, Save,
// Options, then Title Screen in its own section.
//
// The same approach as the in-course pause window (nsmbw_pause_options.cpp, which explains it in
// full); the layout side is runtime/src/product/nsmbw_layout_patch.cpp (button P_SBBase_05, group
// B05_Button). The menu is dCourseSelectMenu_c in d_basesNP (undecompiled; its world-map save
// choice is already overridden in nsmbw_wm_full_save.cpp). Read from the translated bodies in
// build/nsmbw-d_basesNP/functions:
//   0x8077AB70  create: GroupRegister(&mLayout, names, 0x80933FD8, 19). Group animations: 0 in,
//               1 loop, then per button n (0..3) on 2+n, idle 6+n, hit 10+n, off 14+n, and 18 out.
//               The Options group's four (anim resources onButton 2, idleButton 3, hitButton 4,
//               offButon 5) are added after them: on 19, idle 20, hit 21, off 22.  (copy, EDIT)
//   0x8077AD70  cursor setup: dGameCom::SelectCursorSetup(mpPictures[tbl[next]], 4, false), the
//               table at 0x80934040 being {0, 1, 2, 3}; clears the flag at +628 first.
//   0x8077ADC0  window open: every button's on-animation backwards (off), the in-animation, +626
//               cleared, the root pane (+580) made visible, then changeState(0x80990D28).
//   0x8077AF10  button change: SE_SYS_CURSOR and the old button's off when there was one, then
//               mActive = mNext and the new button's on.
//   0x8077B1A0  hit: the hit animation of mNext.
//   0x8077B010  the selection state: up (WPAD 0x2) next - 1 wrapping to 3, down (0x1) next + 1
//               wrapping to 0, then changeState(button change); A / 2 plays SE_SYS_DECIDE and
//               changes to the hit state, after which the menu acts on mNext. (copy, EDITs: the
//               up / down steps follow screen order, and A on Options starts the Options sequence)
// Fields: mLayout +112, mStateMgr +520 (changeState = vtable +24), root pane +580, pictures +584
// (P_SBBase_01..04, P_back, P_backWhite), mNext +616, mActive +620.
//
// The Options sequence is the pause window's: the button's hit animation, the menu's out
// animation (18; the menu then can't be seen behind the options menu), the options menu; and when
// that has closed, the menu's own open (0x8077ADC0) with Options still selected. While it runs,
// the selection state does nothing else.
#include <cstdint>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" bool NsmbwMapMenuHasOptions();           // runtime/src/product/nsmbw_layout_patch.cpp
extern "C" void NsmbwOptionsMenuRequestOpen();      // runtime/src/product/nsmbw_options_menu.cpp
extern "C" bool NsmbwOptionsMenuTakeReturnToPause(); // same file: true once, when it has closed

namespace {
constexpr uint32_t kOptions = 4; // button index; the original four are 0..3
constexpr uint32_t kLayout = 112, kStateMgr = 520, kRootPane = 580, kPictures = 584, kNext = 616, kActive = 620;
constexpr uint32_t kSndAudioMgr = 0x8042A768u;     // SndAudioMgr::sInstance
constexpr uint32_t kSelectCursorInstance = 0x8042A5A8u;
constexpr uint32_t kStateOpenWait = 0x80990D28u;    // the state 0x8077ADC0 changes to
constexpr uint32_t kAnimOut = 18;

uint32_t OnAnim(uint32_t b) { return b == kOptions ? 19 : 2 + b; }
uint32_t HitAnim(uint32_t b) { return b == kOptions ? 21 : 10 + b; }
uint32_t OffAnim(uint32_t b) { return b == kOptions ? 22 : 14 + b; }

uint32_t g_optionsPane = 0; // P_SBBase_05

void AnimeStartSetup(CpuContext* ctx, uint32_t lyt, uint32_t anim) {
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = anim;
    ctx->gpr[5] = 0;
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
bool IsAnime(CpuContext* ctx, uint32_t lyt, uint32_t anim) {
    ctx->gpr[3] = lyt;
    ctx->gpr[4] = anim;
    InvokeDirectCpu<0x800C9700u>(ctx);
    return (ctx->gpr[3] & 0xFF) != 0;
}
void PlaySe(CpuContext* ctx, uint32_t id) {
    ctx->gpr[3] = MemoryInline::FlatRead32(kSndAudioMgr);
    ctx->gpr[4] = id;
    ctx->gpr[5] = 1;
    InvokeDirectCpu<0x801954C0u>(ctx);
}
void ChangeState(CpuContext* ctx, uint32_t self, uint32_t state) {
    const uint32_t mgr = self + kStateMgr;
    const uint32_t fn = MemoryInline::FlatRead32(MemoryInline::FlatRead32(mgr) + 24);
    ctx->gpr[3] = mgr;
    ctx->gpr[4] = state;
    InvokeIndirectCpu(fn, ctx);
}

void WriteString(uint32_t addr, const char* s) {
    do {
        MemoryInline::FlatWrite8(addr++, static_cast<uint8_t>(*s));
    } while (*s++ != '\0');
}

// Replaces create's GroupRegister(&mLayout, names, indices, 19), as in nsmbw_pause_options.cpp:
// the extended tables on the guest stack for the call, then P_SBBase_05 found by PPaneRegister.
void RegisterGroupsWithOptions(CpuContext* ctx) {
    if (!NsmbwMapMenuHasOptions()) {
        InvokeDirectCpu<0x800C91E0u>(ctx);
        return;
    }
    const uint32_t lyt = ctx->gpr[3], names = ctx->gpr[4], indices = ctx->gpr[5], count = ctx->gpr[6];
    const uint32_t sp = ctx->gpr[1];
    const uint32_t scratch = (sp - 0x180u) & ~0x1Fu;
    const uint32_t nameTable = scratch, indexTable = scratch + 0x60, groupName = scratch + 0xC0,
                   paneName = scratch + 0xE0, paneNameTable = scratch + 0xF0, paneOut = scratch + 0xF4;
    WriteString(groupName, "B05_Button");
    WriteString(paneName, "P_SBBase_05");
    for (uint32_t i = 0; i < count; ++i) {
        MemoryInline::FlatWrite32(nameTable + 4 * i, MemoryInline::FlatRead32(names + 4 * i));
        MemoryInline::FlatWrite32(indexTable + 4 * i, MemoryInline::FlatRead32(indices + 4 * i));
    }
    const uint32_t anims[4] = {2, 3, 4, 5}; // onButton, idleButton, hitButton, offButon
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

// Up / down in screen order, wrapping like the original.
uint32_t Step(uint32_t current, int dir) {
    static const uint32_t order5[5] = {0, 1, 2, kOptions, 3}, order4[4] = {0, 1, 2, 3};
    const bool options = NsmbwMapMenuHasOptions();
    const uint32_t* order = options ? order5 : order4;
    const int n = options ? 5 : 4;
    int pos = 0;
    for (int i = 0; i < n; ++i) {
        if (order[i] == current) pos = i;
    }
    return order[((pos + dir) % n + n) % n];
}
uint32_t ButtonCount() { return NsmbwMapMenuHasOptions() ? 5u : 4u; }

enum class Phase { None, Hit, Out, Menu };
Phase g_phase = Phase::None;
uint32_t g_menu = 0; // the dCourseSelectMenu_c the sequence runs on

void StartOptions(CpuContext* ctx, uint32_t self) {
    PlaySe(ctx, 121); // SE_SYS_DECIDE
    AnimeStartSetup(ctx, self + kLayout, HitAnim(kOptions));
    g_menu = self;
    g_phase = Phase::Hit;
}

// One frame of the Options sequence, at the top of the selection state. True while it runs.
bool RunOptions(CpuContext* ctx, uint32_t self) {
    if (g_phase == Phase::None) return false;
    if (self != g_menu) { // a different menu object: the old one is gone
        g_phase = Phase::None;
        return false;
    }
    const uint32_t lyt = self + kLayout;
    switch (g_phase) {
    case Phase::Hit:
        if (IsAnime(ctx, lyt, HitAnim(kOptions))) return true;
        ctx->gpr[3] = MemoryInline::FlatRead32(kSelectCursorInstance);
        ctx->gpr[4] = 0;
        InvokeDirectCpu<0x8010C890u>(ctx); // dSelectCursor_c::Cancel(0)
        AnimeStartSetup(ctx, lyt, kAnimOut);
        g_phase = Phase::Out;
        return true;
    case Phase::Out:
        if (IsAnime(ctx, lyt, kAnimOut)) return true;
        NsmbwOptionsMenuRequestOpen();
        g_phase = Phase::Menu;
        return true;
    case Phase::Menu:
        if (!NsmbwOptionsMenuTakeReturnToPause()) return true;
        // The menu's own open, Options still selected (mActive -1: nothing to turn off first).
        MemoryInline::FlatWrite32(self + kActive, static_cast<uint32_t>(-1));
        ctx->gpr[3] = self;
        InvokeDirectCpu<0x8077ADC0u>(ctx);
        g_phase = Phase::None;
        return true;
    case Phase::None: break;
    }
    return false;
}
} // namespace

// Cursor setup (0x8077AD70).
extern "C" void NsmbwMapMenuCursor_8077AD70(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3];
    if (MemoryInline::FlatRead8(self + 628) != 0) MemoryInline::FlatWrite8(self + 628, 0);
    const uint32_t next = MemoryInline::FlatRead32(self + kNext);
    const uint32_t pane = next == kOptions ? g_optionsPane : MemoryInline::FlatRead32(self + kPictures + 4 * (next & 3));
    if (pane == 0) return;
    ctx->gpr[3] = pane;
    ctx->gpr[4] = 4;
    ctx->gpr[5] = 0;
    InvokeDirectCpu<0x800B44D0u>(ctx);
}
PPC_NATIVE_OVERRIDE_VOID(8077AD70, NsmbwMapMenuCursor_8077AD70, (CpuContext* ctx), (ctx));

// Window open (0x8077ADC0).
extern "C" void NsmbwMapMenuOpen_8077ADC0(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3], lyt = self + kLayout;
    AllAnimeEndSetup(ctx, lyt);
    for (uint32_t b = 0; b < 4; ++b) ReverseAnimeStartSetup(ctx, lyt, OnAnim(b));
    if (NsmbwMapMenuHasOptions()) ReverseAnimeStartSetup(ctx, lyt, OnAnim(kOptions));
    AnimeStartSetup(ctx, lyt, 0 /* in */);
    MemoryInline::FlatWrite8(self + 626, 0);
    const uint32_t root = MemoryInline::FlatRead32(self + kRootPane);
    MemoryInline::FlatWrite8(root + 187, static_cast<uint8_t>((MemoryInline::FlatRead8(root + 187) & 254) | 1));
    ChangeState(ctx, self, kStateOpenWait);
}
PPC_NATIVE_OVERRIDE_VOID(8077ADC0, NsmbwMapMenuOpen_8077ADC0, (CpuContext* ctx), (ctx));

// Button change (0x8077AF10).
extern "C" void NsmbwMapMenuButtonChange_8077AF10(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3], lyt = self + kLayout;
    AllAnimeEndSetup(ctx, lyt);
    const int32_t active = static_cast<int32_t>(MemoryInline::FlatRead32(self + kActive));
    if (active >= 0) {
        PlaySe(ctx, 120); // SE_SYS_CURSOR
        AnimeStartSetup(ctx, lyt, OffAnim(static_cast<uint32_t>(active)));
    }
    const uint32_t next = MemoryInline::FlatRead32(self + kNext);
    MemoryInline::FlatWrite32(self + kActive, next);
    AnimeStartSetup(ctx, lyt, OnAnim(next));
}
PPC_NATIVE_OVERRIDE_VOID(8077AF10, NsmbwMapMenuButtonChange_8077AF10, (CpuContext* ctx), (ctx));

// Hit (0x8077B1A0).
extern "C" void NsmbwMapMenuHit_8077B1A0(CpuContext* MKW_RESTRICT ctx)
{
    const uint32_t self = ctx->gpr[3];
    AnimeStartSetup(ctx, self + kLayout, HitAnim(MemoryInline::FlatRead32(self + kNext)));
}
PPC_NATIVE_OVERRIDE_VOID(8077B1A0, NsmbwMapMenuHit_8077B1A0, (CpuContext* ctx), (ctx));

extern "C" void NsmbwMapMenuCreate_8077AB70(CpuContext* MKW_RESTRICT ctx)
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
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_8077AB70;

loc_8077AB70:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r5 = 0;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = 0x80940000u;
    r31 = (r31 + 4744);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    r4 = (r31 + 780);
    r3 = (r3 + 112);
    ctx->lr = 0x8077ABA0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
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
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8077ABA4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8077ABB0;
    }
}

loc_8077ABA8:
{
    r3 = 0;
    goto loc_8077AC48;
}

loc_8077ABB0:
{
    r12 = MemoryInline::FlatRead32((r30 + 120));
    r3 = (r30 + 112);
    r4 = (r31 + 816);
    r5 = 0;
    r12 = MemoryInline::FlatRead32((r12 + 20));
    ctr = r12;
    ctx->lr = 0x8077ABCCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
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
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 112);
    r4 = (r31 + 316);
    r5 = 7;
    ctx->lr = 0x8077ABDCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
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
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r5 = 0x80930000u;
    r3 = (r30 + 112);
    r4 = (r31 + 404);
    r6 = 19;
    r5 = (r5 + 16344);
    ctx->lr = 0x8077ABF4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
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
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 112);
    // inline leaf 0x80007220 (2 guest instruction(s))
    r3 = MemoryInline::FlatRead32((r3 + 32));
    // end of inlined leaf 0x80007220
    MemoryInline::FlatWrite32((r30 + 580), r3);
    r3 = (r30 + 112);
    r4 = (r31 + 552);
    r5 = (r30 + 584);
    r6 = 6;
    ctx->lr = 0x8077AC14u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
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
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = (r30 + 112);
    r4 = (r31 + 616);
    r5 = (r30 + 608);
    r6 = 2;
    ctx->lr = 0x8077AC28u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
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
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r5 = 0x80930000u;
    r3 = (r30 + 112);
    r4 = (r31 + 752);
    r6 = 2;
    r5 = (r5 + 16420);
    r7 = 7;
    ctx->lr = 0x8077AC44u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
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
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = 1;
}

loc_8077AC48:
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
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(8077AB70, NsmbwMapMenuCreate_8077AB70, (CpuContext* ctx), (ctx));

static void NsmbwMapMenuSelectBody(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_8077B010;

loc_8077B010:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    r4 = 0x80430000u;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    r31 = 0x80990000u;
    r31 = (r31 + 3272);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    r30 = r3;
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    r29 = 0;
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r0 = MemoryInline::FlatRead8((r3 + 627));
    r28 = MemoryInline::FlatRead32((r4 + -24016));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8077B04C:
{
    MemoryInline::FlatWrite8((r3 + 626), static_cast<uint8_t>(r29));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8077B16C;
    }
}

loc_8077B054:
{
    ctx->lr = 0x8077B058u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeDirectCpu<0x8077AD70u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r3 = 0x80430000u;
    r0 = MemoryInline::FlatRead32((r3 + -22716));
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r3 = (r28 + r0);
    r3 = MemoryInline::FlatRead32((r3 + 4));
    r3 = MemoryInline::FlatRead32((r3 + 28));
    r0 = (r3 & 2);
}

loc_8077B074:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8077B0AC;
    }
}

loc_8077B078:
{
    r3 = MemoryInline::FlatRead32((r30 + 616));
    r0 = Step(r3, -1); // EDIT: was (r3 + -1); screen order, already wrapped
    {
        const uint64_t ppcCarryWide = static_cast<uint64_t>(static_cast<uint32_t>(r3)) + static_cast<uint64_t>(static_cast<uint32_t>(-1)) + (static_cast<uint64_t>(static_cast<uint32_t>(0)) & 1u);
        xer = (xer & 0xDFFFFFFFu) | (static_cast<uint32_t>((ppcCarryWide >> 32) & 1u) << 29);
    }
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8077B080:
{
    MemoryInline::FlatWrite32((r30 + 616), r0);
    if (((cr & 0x80000000u) == 0)) {
        goto loc_8077B090;
    }
}

loc_8077B088:
{
    r0 = 3;
    MemoryInline::FlatWrite32((r30 + 616), r0);
}

loc_8077B090:
{
    r12 = MemoryInline::FlatRead32((r30 + 520));
    r3 = (r30 + 520);
    r4 = (r31 + 160);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8077B0A8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8077B16C;
}

loc_8077B0AC:
{
    r0 = (r3 & 1);
}

loc_8077B0B0:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8077B0E8;
    }
}

loc_8077B0B4:
{
    r3 = MemoryInline::FlatRead32((r30 + 616));
    r0 = Step(r3, 1); // EDIT: was (r3 + 1); screen order, already wrapped
    MemoryInline::FlatWrite32((r30 + 616), r0);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(ButtonCount())); // EDIT: was 4
}

loc_8077B0C4:
{
    if (((cr & 0x80000000u) != 0)) {
        goto loc_8077B0CC;
    }
}

loc_8077B0C8:
{
    MemoryInline::FlatWrite32((r30 + 616), r29);
}

loc_8077B0CC:
{
    r12 = MemoryInline::FlatRead32((r30 + 520));
    r3 = (r30 + 520);
    r4 = (r31 + 160);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8077B0E4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8077B16C;
}

loc_8077B0E8:
{
    r0 = (r3 & 16);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8077B0EC:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8077B100;
    }
}

loc_8077B0F0:
{
    r3 = 0;
    ctx->lr = 0x8077B0F8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeDirectCpu<0x800B53F0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8077B0FC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8077B11C;
    }
}

loc_8077B100:
{
    r12 = MemoryInline::FlatRead32((r30 + 520));
    r3 = (r30 + 520);
    r4 = (r31 + 352);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8077B118u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8077B16C;
}

loc_8077B11C:
{
    r3 = 0x80430000u;
    r4 = 2304;
    r3 = MemoryInline::FlatRead32((r3 + -22712));
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 32));
    ctr = r12;
    ctx->lr = 0x8077B138u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8077B13C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8077B16C;
    }
}

loc_8077B140:
{
    // EDIT: A / 2 on Options starts the Options sequence instead of the hit state.
    if (MemoryInline::FlatRead32(r30 + kNext) == kOptions && NsmbwMapMenuHasOptions()) {
        ctx->gpr[1] = r1;
        StartOptions(ctx, r30);
        goto loc_8077B16C;
    }
    r3 = 0x80430000u;
    r4 = 121;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = 0x8077B154u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeDirectCpu<0x801954C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r12 = MemoryInline::FlatRead32((r30 + 520));
    r3 = (r30 + 520);
    r4 = (r31 + 288);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8077B16Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_8077B16C:
{
    r0 = MemoryInline::FlatRead32((r1 + 36));
    r31 = MemoryInline::FlatRead32((r1 + 28));
    r30 = MemoryInline::FlatRead32((r1 + 24));
    r29 = MemoryInline::FlatRead32((r1 + 20));
    r28 = MemoryInline::FlatRead32((r1 + 16));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    return;
}

}

// The selection state: the Options sequence first; while it runs, nothing else.
extern "C" void NsmbwMapMenuSelect_8077B010(CpuContext* MKW_RESTRICT ctx)
{
    if (RunOptions(ctx, ctx->gpr[3])) return;
    NsmbwMapMenuSelectBody(ctx);
}
PPC_NATIVE_OVERRIDE_VOID(8077B010, NsmbwMapMenuSelect_8077B010, (CpuContext* ctx), (ctx));

// "Exit" in the in-course pause menu even for a course that has not been cleared.
//
// In the original, the pause menu's Exit button (back to the world map) is only usable once the
// course has been cleared; otherwise it is greyed out and the cursor cannot reach it. All three
// parts of that gate ask dGameCom::isNowCourseClear (0x800B4E30), and nothing else in the program
// calls it (every InvokeDirectCpu<0x800B4E30> in build/*/functions, DOL and all four RELs; it is
// never inlined):
//   0x8015A750  Pausewindow_c::create - hides the P_shadowBlack pane (the grey-out over Exit) when
//               it returns true (NSMBW-Decomp source/dol/bases/d_pausewindow.cpp).
//   0x800D0CF0  pause cursor move - refuses to move onto button 1 (Exit) unless it returns true
//               or the course is the cannon stage (m_startGameInfo.mLevel1 == 35).
//   0x800D0DA0  pause confirm - rejects a press on Exit under the same condition.
// So answering "cleared" here opens Exit everywhere the pause menu is shown, and the game's own
// Exit path runs unchanged (it returns to the map without marking the course cleared). Enemy
// courses (mLevel1 32..34) stay locked: the cursor routine checks those separately and create()
// greys them out regardless.
//
// The original body (build/nsmbw/functions/func_800B4E30.cpp) is a pure query - it only reads
// (a scene global, mLevel1, a u16 at r13-28478, a flag word at r13-22304, and the current save
// slot's course flags & (GOAL_NORMAL | GOAL_SECRET)) and touches nothing but its own stack frame,
// so there is no guest-side bookkeeping to mirror.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstdint>

extern "C" uint32_t NsmbwIsNowCourseClear_800B4E30()
{
    return 1;
}
PPC_NATIVE_OVERRIDE(800B4E30, NsmbwIsNowCourseClear_800B4E30, uint32_t, (), ());

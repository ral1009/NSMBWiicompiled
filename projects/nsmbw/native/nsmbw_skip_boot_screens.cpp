// Skips the Wii Remote strap warning and the "Hold the Wii Remote sideways" screen at boot.
//
// Both screens are about a Wii Remote, and this port has none: input comes from the keyboard and
// SDL gamepads (nsmbw_kpad_overrides.cpp), and WPAD's Bluetooth side is HLE'd with no real device
// (nsmbw_wpad_overrides.cpp). So they are always skipped for now; once real Wii Remotes are
// supported, RealWiiRemoteConnected() is where to keep them for those players.
// NSMBW_SHOW_BOOT_SCREENS=1 keeps them (for testing the boot scene itself).
//
// Why not just the game's own GAME_FLAG_AUTO_SKIP (dInfo_c::mGameFlag bit 19, 0x8042A260): it only
// ends the three *DispEndWait/ButtonInputWait* states early (NSMBW-Decomp d_s_boot.cpp:589/683/820).
// A run with only the flag set still faded the strap in, held it for WiiStrapKeyWait's 60-frame
// mMinWaitTimer, then showed the controller screen through its FadeIn/SoundWait/KeyWait states.
// Jumping past those states is not safe either: their finalizers end the strap warning, allow the
// HOME menu, and SoundWait waits for the scene's sound to load (d_s_boot.cpp:613-652). So the game
// runs every state as normal, and this makes each one invisible and instant:
//   - GAME_FLAG_AUTO_SKIP set             -> the DispEndWait states exit on their first frame
//   - dScBoot_c::mMinWaitTimer (+268) = 0 -> both KeyWait states exit on their first frame
//   - dWiiStrap_c / dControllerInformation_c::mVisible (+521) = false -> nothing is drawn
// Offsets read from the translated bodies: func_8015CE80 (initializeState_WiiStrapKeyWait) stores
// 1200/60 to this+264/+268 and reads mpWiiStrap from this+240; func_8015D010 / func_8015D0B0 read
// mpControllerInformation from this+252 and write mVisible at +521 (mIsCreated is +520).
// What is left is the black fades and the sound-load wait.
#include <aurora/env.hpp>
#include "memory.h"

#include <cstdint>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;
extern "C" uint32_t g_nsmbwCurrentScenePtr;

namespace {
constexpr uint32_t kBootProfile = 0;
constexpr uint32_t kGameFlagAddr = 0x8042A260u;
constexpr uint32_t kGameFlagAutoSkip = 1u << 19;
constexpr uint32_t kWiiStrapOff = 240;
constexpr uint32_t kControllerInfoOff = 252;
constexpr uint32_t kMinWaitTimerOff = 268;
constexpr uint32_t kLayoutVisibleOff = 521;

bool RealWiiRemoteConnected() { return false; }

bool SkipEnabled() {
    static const bool enabled = AURORA_ENV("NSMBW_SHOW_BOOT_SCREENS") == nullptr;
    return enabled && !RealWiiRemoteConnected();
}

void HideLayout(uint32_t scene, uint32_t memberOff) {
    uint32_t layout = 0;
    if (Memory::TryRead32(scene + memberOff, layout) && layout != 0) {
        try {
            Memory::Write8(layout + kLayoutVisibleOff, 0);
        } catch (const Memory::AccessViolation&) {}
    }
}
} // namespace

// Called once per VI frame (keyboard sampling) and after every state change.
extern "C" void NsmbwSkipBootScreensApply() {
    if (!SkipEnabled() || g_nsmbwCurrentSceneProfile != kBootProfile || g_nsmbwCurrentScenePtr == 0) return;
    const uint32_t scene = g_nsmbwCurrentScenePtr;
    try {
        Memory::Write32(kGameFlagAddr, Memory::Read32(kGameFlagAddr) | kGameFlagAutoSkip);
        Memory::Write32(scene + kMinWaitTimerOff, 0);
    } catch (const Memory::AccessViolation&) {}
    HideLayout(scene, kWiiStrapOff);
    HideLayout(scene, kControllerInfoOff);
}

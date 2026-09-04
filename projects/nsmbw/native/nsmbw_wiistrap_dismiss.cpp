#include "hle_stubs.h"

// Forward-declared instead of #include "settings_overlay.h" - that header transitively pulls in
// aurora/event.h (SDL3/SDL_events.h), which isn't on this project's native-source include path
// (only the aurora/GX-linked runtime targets have SDL3 available).
namespace settings_overlay {
void NotifyStrapInputAccepted() noexcept;
}

// The host's full-screen opaque black "startup screen" overlay (settings_overlay.cpp,
// DrawStartupScreen) is meant to hide the game surface only until the real WiiStrap sequence
// finishes, dismissed via settings_overlay::NotifyStrapInputAccepted(). The only existing call
// site for that (runtime/src/hle/strap_scene.cpp) is bound at 0x800077C8 - MKW's own
// StrapScene::CheckInput address, which NSMBW's own boot code never calls - so for NSMBW that
// notification never fires and the overlay stays up forever, hiding the game's own (correctly
// rendering) content behind a black screen indistinguishable from an actual black-screen bug.
//
// dScBoot_c::finalizeState_WiiStrapDispEndWait (NSMBW-Decomp source: an empty `{}` body) is the
// real, once-only exit point of NSMBW's own WiiStrap wait state (dScBoot_c::executeState_
// WiiStrapDispEndWait, 0x8015CF30, transitions here on its countdown timer reaching 0, a button
// press, or GAME_FLAG_AUTO_SKIP). Since the real body is empty, overriding it to just notify the
// overlay changes nothing about guest-visible behavior - it only tells the host it is now safe to
// stop covering the screen.
extern "C" void NsmbwWiiStrap_FinalizeDispEndWait_8015CFB0(uint32_t thisPtr)
{
    (void)thisPtr;
    settings_overlay::NotifyStrapInputAccepted();
}
PPC_NATIVE_OVERRIDE_VOID(8015CFB0, NsmbwWiiStrap_FinalizeDispEndWait_8015CFB0, (uint32_t thisPtr), (thisPtr));

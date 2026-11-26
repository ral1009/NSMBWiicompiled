#include "nsmbw_va.h"  // NSMBW_VA / NSMBW_FUNC: guest addresses are PAL rev 1, mapped per version
#include "hle_stubs.h"

#include "console_identity.h"
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "memory.h"
#include "runtime_config.h"
#include "runtime_log.h"

namespace {

// The console's region for SCGetProductArea / SCGetProductGameRegion: EUR/EU = 2 for the PAL game,
// USA/US = 1 for the USA one (the SDK tables noted below use the same numbers for both).
constexpr uint32_t kPalProductRegion = NSMBW_BY_VERSION(2, 1);

} // namespace

// SCCheckStatus is polled in OSInit's busy loop (while(SCCheckStatus()==1) waits on async SYSCONF
// load via NAND IPC); we have no async IPC callbacks, so return 0 (SUCCESS) immediately.

// 0x801B0220 -> SCCheckStatus()
// Returns: 0 = success, 1 = busy, 2 = error
extern "C" uint32_t SCCheckStatus_HLE()
{
    // Return 0 (success) immediately to avoid infinite busy-wait in OSInit
    return 0;
}

PPC_NATIVE_OVERRIDE(801B0220, SCCheckStatus_HLE, uint32_t, (), ());

// 0x801B1BE4 -> SCGetAspectRatio()
// Returns: 0 = 4:3, 1 = 16:9
extern "C" uint32_t SCGetAspectRatio_HLE()
{
    // Fixed at the first call (during boot) instead of re-reading Config.toml each time. The game
    // asks for the ratio at different moments: EGG::Screen picks its TV mode once at init, and
    // 2D objects and faders (m2d::Simple_c, mColorFader, mWipeFader) ask again when each is
    // constructed. If the F10 "Aspect ratio" option changed the config mid-session, new objects
    // would be built for one ratio and the screen for the other. So a change applies after a
    // restart (2026-11-07). settings_overlay compares the saved value with this one.
    // 0 = the game's 4:3 mode (a letterboxed view), 1 = its 16:9 mode, which every other aspect
    // ratio uses with a widened or narrowed canvas (nsmbw_product.cpp, ApplyAspectSetting).
    return RuntimeConfigFile::RunningAspect().fourThree ? 0u : 1u;
}

PPC_NATIVE_OVERRIDE(801B1BE4, SCGetAspectRatio_HLE, uint32_t, (), ());

// 0x801B1CAC -> SCGetEuRgb60Mode()
// Returns: 0 = PAL50, 1 = PAL60/RGB60
extern "C" uint32_t SCGetEuRgb60Mode_HLE()
{
    // Default to PAL60 so PAL builds do not fall back to the half-rate PAL50
    // sync path on modern displays. Actual texture/cache correctness is handled
    // elsewhere; this only exposes the intended SYSCONF setting.
    return 1;
}

PPC_NATIVE_OVERRIDE(801B1CAC, SCGetEuRgb60Mode_HLE, uint32_t, (), ());

// SCGetLanguage() (NSMBW: 0x801DD460, bound in projects/nsmbw/native/nsmbw_sc_language.cpp)
// Returns the SDK's SCLanguage: 1 = English, 2 = German, 3 = French, 4 = Spanish, 5 = Italian.
//
// The game picks its language folder (/EU/EngEU, /EU/FraEU, ...) from this value. On a console it
// is IPL.LNG in SYSCONF. We have no SYSCONF (the SC buffer is never filled, see
// nsmbw_sc_deferred_write_sync.cpp), so the translated lookup always failed and returned its
// fallback, English, whichever languages the disc held. Config.toml's [game] language decides now,
// limited to the languages the extracted disc has (RuntimeConfigFile::DiscLanguages).
extern "C" uint32_t SCGetLanguage_HLE()
{
    // One the disc has: RunningLanguage() falls back to English for a choice its folders lack.
    return RuntimeConfigFile::RunningLanguage();
}

// The managed NAND intentionally starts without a console-owned setting.txt.
// DWC nevertheless requires the Wii product code and serial number so it can
// include csnum in NAS authentication. Expose one stable virtual-console
// identity without requiring or mutating a user's real NAND.

extern "C" uint32_t SCGetProductArea_HLE()
{
    // The PAL setting.txt AREA value is "EUR". The SDK's lookup table at
    // 0x8029CEB0 maps JPN=0, USA=1, EUR=2.
    return kPalProductRegion;
}

PPC_NATIVE_OVERRIDE(801B23A0, SCGetProductArea_HLE, uint32_t, (), ());

extern "C" uint32_t SCGetProductCode_HLE()
{
    // Original PAL SC storage for the six-byte CODE value.
    constexpr uint32_t kProductCodeAddress = NSMBW_VA(0x803869E0u);
    static constexpr char kProductCode[] = "LEH";
    if (!Memory::Contains(kProductCodeAddress, sizeof(kProductCode))) {
        return 0;
    }
    std::memcpy(Memory::GetPointer(kProductCodeAddress, sizeof(kProductCode)),
                kProductCode, sizeof(kProductCode));
    return kProductCodeAddress;
}

PPC_NATIVE_OVERRIDE(801B2424, SCGetProductCode_HLE, uint32_t, (), ());

extern "C" uint32_t SCGetProductSN_HLE(uint32_t serialAddress)
{
    const std::string& serial = RuntimeConsoleIdentity::Current().serial;
    if (!serialAddress || !Memory::Contains(serialAddress, serial.size() + 1)) {
        return 0;
    }
    std::memcpy(Memory::GetPointer(serialAddress, serial.size() + 1),
                serial.c_str(), serial.size() + 1);
    return 1;
}

PPC_NATIVE_OVERRIDE(801B2460, SCGetProductSN_HLE, uint32_t, (uint32_t serialAddress), (serialAddress));

extern "C" uint32_t SCGetProductGameRegion_HLE()
{
    // The PAL setting.txt GAME value is "EU". The SDK's own lookup table at
    // 0x8029CEF8 maps JP=0, US=1, EU=2.
    return kPalProductRegion;
}

PPC_NATIVE_OVERRIDE(801B24C8, SCGetProductGameRegion_HLE, uint32_t, (), ());

// These stubs make the game think all titles are installed; otherwise it checks title ID
// 0x00010004524d4350 ("RMCP", Mario Kart Wii PAL) and reports error code 5.

// 0x801AE4A0 -> OS__IsTitleInstalled(titleIdHi, titleIdLo)
// Returns: 1 = installed, 0 = not installed
extern "C" uint32_t OS__IsTitleInstalled(uint32_t titleIdHi, uint32_t titleIdLo)
{
    RT_LOGF(RT_TAG_HLE, "CINS: OSIsTitleInstalled(0x%08X%08X) -> 1 (stubbed as installed)\n",
            titleIdHi, titleIdLo);
    return 1; // Always report installed
}

PPC_NATIVE_OVERRIDE(801AE4A0, OS__IsTitleInstalled, uint32_t, (uint32_t titleIdHi, uint32_t titleIdLo), (titleIdHi, titleIdLo));

// 0x801AD1D4 -> OS__CheckInstall(requiredBlocks, titleIdHi, titleIdLo, outFlagsPtr): returns 0 with
// outFlagsPtr = 0x3 (bit0 has data, bit1 has update; bit2 would be needs-blocks) i.e. fully installed.
extern "C" uint32_t OS__CheckInstall(uint32_t requiredBlocks, uint32_t titleIdHi, 
                                      uint32_t titleIdLo, uint32_t outFlagsPtr)
{
    RT_LOGF(RT_TAG_HLE, "OS__CheckInstall(blocks=%u, 0x%08X%08X) -> success (stubbed)\n",
            requiredBlocks, titleIdHi, titleIdLo);
    if (outFlagsPtr != 0) {
        Memory::Write32(outFlagsPtr, 0x3); // has data + has update = fully installed
    }
    return 0; // Success
}

PPC_NATIVE_OVERRIDE(801AD1D4, OS__CheckInstall, uint32_t, (uint32_t requiredBlocks, uint32_t titleIdHi, uint32_t titleIdLo, uint32_t outFlagsPtr), (requiredBlocks, titleIdHi, titleIdLo, outFlagsPtr));

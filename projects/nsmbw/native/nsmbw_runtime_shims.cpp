// Minimal NSMBW equivalents of a handful of symbols runtime/src/main.cpp normally provides.
// main.cpp itself can't be linked in here (see nsmbw_product.cpp's header comment: it hardcodes
// MKW's own kDefaultEntryAddress and would be a second main()/WinMain()), but these particular
// symbols are generic infrastructure other runtime/src files call unconditionally, not anything
// MKW-specific - system_bridge.h declares them, main.cpp happens to be the only place that
// currently defines them. These versions are deliberately simpler than main.cpp's (no crash-log
// file, no stack trace, no memory snapshot): the Phase 5 milestone only needs a controlled,
// diagnosable crash, not full forensics - that polish can come later if NSMBW ever needs its own
// product-quality crash reporting.
//
// Lives under projects/nsmbw/native/, not runtime/src/product/, on purpose: runtime/src/*.cpp is
// globbed by MKW's own mkw_runtime_common too, and every symbol defined here already exists in
// MKW's main.cpp/music_attenuation.cpp/base_product.cpp - putting it under runtime/src would
// duplicate-symbol MKW's real build the same way nsmbw_product.cpp's main() would have.
#include "abi_bridge.h"
#include "memory.h"
#include "music_attenuation.h"
#include "runtime_product.h"
#include "system_bridge.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "NSMBWCompiled",
    };
    return descriptor;
}

} // namespace RuntimeProduct

namespace {
std::atomic_int g_nsmbwExitCode{0};
std::atomic_bool g_nsmbwFatalReported{false};
} // namespace

extern "C" void DumpHostStackTraceForRuntimeHelper() {
    // No DbgHelp/stack-walking wired up for this minimal product yet.
}

void MarkFatalErrorReported() {
    g_nsmbwFatalReported.store(true, std::memory_order_release);
}

void SetRuntimeExitCode(int code) {
    g_nsmbwExitCode.store(code, std::memory_order_relaxed);
}

void ShowRuntimeFatalPopup(std::string_view category, std::string_view details) noexcept {
    try {
        std::cerr << "[nsmbw] FATAL: " << category << "\n" << details << std::endl;
    } catch (...) {
        // Reporting a crash must never throw or mask the original failure.
    }
}

namespace RuntimeCrash {

void WriteCrashArtifacts(std::string_view reason, std::string_view extraDetails,
                          const uint32_t* missingGuestTarget) noexcept {
    try {
        std::cerr << "[nsmbw] crash: " << reason;
        if (!extraDetails.empty()) {
            std::cerr << " - " << extraDetails;
        }
        if (missingGuestTarget) {
            std::cerr << " (guest target 0x" << std::hex << *missingGuestTarget << std::dec << ")";
        }
        std::cerr << std::endl;
    } catch (...) {
    }
}

[[noreturn]] void FatalMissingGuestTarget(uint32_t target, CpuContext* cpu) noexcept {
    std::ostringstream message;
    message << "guest address 0x" << std::hex << target
            << " was called but is not translated or registered (caller LR=0x"
            << (cpu ? cpu->lr : 0u) << ")" << std::dec;
    WriteCrashArtifacts("missing_target", message.str(), &target);
    ShowRuntimeFatalPopup("Missing translated function", message.str());
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

} // namespace RuntimeCrash

extern "C" bool g_dynamicAspectRatioEnabled = false;

// A handful of hle/os/*.cpp native overrides are generic Wii OS reimplementations (thread
// scheduling, alarms, OSInit) worth keeping for NSMBW, but each also hardcodes a callback/chain
// call into one specific MKW dol address (mod-loader hooks, alarm callback dispatch) that has no
// NSMBW equivalent. No-ops: worst case is "that one optional callback doesn't fire", never a
// crash or corrupted state, since none of these are called before the override's own real work.
extern "C" void func_801AADE0(CpuContext*) {}
extern "C" void func_801A0620(CpuContext*) {}
extern "C" void func_801A961C(CpuContext*) {}
extern "C" void func_8055531C(CpuContext*) {}
extern "C" void func_801A1ED8(CpuContext*) {}
extern "C" void func_801D8D30(CpuContext*) {}
extern "C" void func_801D9E94(CpuContext*) {}

// The real music_attenuation.cpp needs the optional C++/WinRT SDK (see its own
// #include <winrt/base.h>, not configured for this build) for actual Windows media-session
// integration - detecting when e.g. a browser is playing audio, so guest music can duck under it.
// Every function it exposes is either pure volume-math (TickGuest, SetSoundPlayerVolume) or a
// query/setter for that optional feature; stubbed as "feature not running" throughout, which is
// exactly the state the real implementation starts in until SetEnabled(true) succeeds anyway.
namespace MusicAttenuation {

void TickGuest() noexcept {}
void SetSoundPlayerVolume(uint32_t, float) {}
void SetEnabled(bool) noexcept {}
void SetMusicVolume(float) noexcept {}
void SetSoundEffectsVolume(float) noexcept {}
void SetUiVolume(float) noexcept {}
void SetVoicesVolume(float) noexcept {}
bool IsExternalMediaPlaying() noexcept { return false; }
bool IsMediaControlAvailable() noexcept { return false; }
bool IsMediaControlInitializationComplete() noexcept { return true; }

} // namespace MusicAttenuation

// NSMBWCompiled.exe: the launcher. The game itself (translated code, overrides, runtime) is a DLL,
// NSMBWCompiled-<version>[-compat].dll, next to this exe; this program picks one, loads it and calls
// its exported NsmbwGameMain(). Decided 2026-11-14 (docs/futureplans.md section 3): one DLL per game
// version (P1 = PAL rev 1 today; E1 = USA rev 1 next) and per CPU level, instead of separate exes
// that start each other.
//
// What has to live here and not in the DLL:
// - The CPU check. A v3 (AVX2) DLL runs AVX2 code in its own static initializers, i.e. during
//   LoadLibrary, before any of its code could decide not to. So this file is compiled without
//   -march (like host_cpu_baseline.cpp) and asks MkwHostCpuMeetsLevel() first.
// - The main thread's stack size. Windows reads it from the exe's header only; the game needs 16 MB
//   (deep translated call chains, see NsmbwProduct.cmake), so the -Wl,--stack flag is on this target.
// - The icon and version resource: SDL loads the window icon from the exe (GetModuleHandle(NULL)).
//
// - The setup screen (nsmbw_launcher_setup.cpp): game files, HD texture pack, Riivolution slot. The
//   disc ID it checks is what chooses the DLL, so it has to run before any DLL is loaded.
#include "nsmbw_launcher_setup.h"

#include <windows.h>

#include <cstdio>
#include <string>

extern "C" bool MkwHostCpuMeetsLevel(unsigned level, char* missing, unsigned missingSize);

namespace {

std::wstring ExeDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

bool FileExists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

[[noreturn]] void Fail(const std::wstring& text) {
    std::fwprintf(stderr, L"[launcher] %ls\n", text.c_str());
    ::MessageBoxW(nullptr, text.c_str(), L"NSMBWCompiled", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ::ExitProcess(1);
}

}  // namespace

int main() {
    // The CPU check comes first, so nobody extracts a disc only to learn their processor is too old.
    // MKW_FORCE_UNSUPPORTED_CPU=1 (the existing test hook) makes this machine count as pre-AVX2.
    char missing[512] = {};
    char forced[8] = {};
    const bool forceCompat = ::GetEnvironmentVariableA("MKW_FORCE_UNSUPPORTED_CPU", forced, sizeof(forced)) > 0 &&
                             forced[0] != '0';
    const bool cpuIsV3 = MkwHostCpuMeetsLevel(3, missing, sizeof(missing)) && !forceCompat;
    // The game DLL runs its own CPU check while loading (host_cpu_baseline.cpp), which honours the
    // same test switch: left set, the compat DLL would report this CPU as unsupported too.
    ::SetEnvironmentVariableA("MKW_FORCE_UNSUPPORTED_CPU", nullptr);
    if (!cpuIsV3 && !MkwHostCpuMeetsLevel(2, missing, sizeof(missing))) {
        const std::string m(missing);
        Fail(L"This processor is too old for NSMBWCompiled. It needs SSE4.2 and the rest of the x86-64-v2 "
             L"instruction set (Intel Nehalem, 2008, or AMD Bulldozer, 2011, and newer).\n\nMissing: " +
             std::wstring(m.begin(), m.end()));
    }

    // Shift held while starting reopens the setup screen even when everything is set up.
    const bool shiftHeld = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const std::wstring version = nsmbw_launcher::EnsureSetup(shiftHeld);
    if (version.empty()) return 0;  // the player quit setup

    const std::wstring stem = ExeDirectory() + L"NSMBWCompiled-" + version;
    const std::wstring fast = stem + L".dll";
    const std::wstring compat = stem + L"-compat.dll";
    std::wstring dll;
    if (cpuIsV3 && FileExists(fast)) {
        dll = fast;
    } else if (FileExists(compat)) {
        dll = compat;
    } else if (FileExists(fast)) {
        const std::string m(forceCompat ? "(none - MKW_FORCE_UNSUPPORTED_CPU is set)" : missing);
        Fail(L"This processor lacks AVX2, and this package has no compat build (" + compat +
             L").\n\nMissing: " + std::wstring(m.begin(), m.end()));
    } else {
        Fail(L"The game file " + fast + L" is missing. Re-extract the whole zip.");
    }

    // The DLL's own dependencies (aurora, SDL3, Dawn, libc++) sit in the same folder, which the
    // default search order already covers (the application directory).
    HMODULE game = ::LoadLibraryW(dll.c_str());
    if (game == nullptr) {
        Fail(L"Could not load " + dll + L" (Windows error " + std::to_wstring(::GetLastError()) +
             L"). A file next to it may be missing.");
    }
    using GameMain = int (*)();
    const auto entry = reinterpret_cast<GameMain>(::GetProcAddress(game, "NsmbwGameMain"));
    if (entry == nullptr) {
        Fail(dll + L" has no NsmbwGameMain export; it does not belong to this launcher.");
    }
    return entry();
}

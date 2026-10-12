// First-launch disc setup for the public release (built only with -DNSMBW_RELEASE=ON).
//
// The runtime reads an extracted disc (a folder with sys\ and files\), named by [paths] dvd_root in
// Config.toml. Players have an .rvz/.iso/.wbfs instead, and editing Config.toml by hand was the
// hardest step in PLAYING.txt. So, before DVDInit: if dvd_root is not a valid extracted disc, ask
// for the disc image, extract it with DolphinTool.exe (shipped next to the exe; Dolphin, GPLv2+)
// into disc\ next to the exe, check it is SMNP01 revision 1, and write dvd_root. An already
// extracted folder can be picked instead. Dev builds compile this to a no-op.
#include "runtime_config.h"

#if defined(NSMBW_RELEASE) && defined(_WIN32)
#include <windows.h>
#include <shobjidl.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

bool IsExtractedDisc(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p / "files", ec) && fs::is_regular_file(p / "sys" / "fst.bin", ec);
}

// sys\boot.bin is the disc header: bytes 0-5 game ID, byte 7 disc revision.
std::wstring DiscProblem(const fs::path& p) {
    std::ifstream in(p / "sys" / "boot.bin", std::ios::binary);
    char header[8]{};
    if (!in.read(header, sizeof(header))) return L"The disc header (sys\\boot.bin) could not be read.";
    const std::string id(header, 6);
    if (id != "SMNP01") {
        return L"This is not the PAL New Super Mario Bros. Wii disc (game ID " +
               std::wstring(id.begin(), id.end()) + L", expected SMNP01).";
    }
    if (header[7] != 1) return L"This is the PAL disc, but not revision 1. Only revision 1 works.";
    return {};
}

// Dolphin may put the partition directly in the output folder or in a subfolder (DATA\ etc.).
fs::path FindExtractedRoot(const fs::path& out) {
    if (IsExtractedDisc(out)) return out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(out, ec)) {
        if (e.is_directory() && IsExtractedDisc(e.path())) return e.path();
    }
    return {};
}

int Ask(const std::wstring& text, UINT flags) {
    return MessageBoxW(nullptr, text.c_str(), L"NSMBWCompiled - first time setup", flags | MB_SETFOREGROUND);
}

fs::path PickPath(bool folder) {
    fs::path result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
        return result;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : 0));
    if (folder) {
        dlg->SetTitle(L"Pick the extracted disc folder (it contains sys and files)");
    } else {
        const COMDLG_FILTERSPEC filters[] = {{L"Wii disc images", L"*.rvz;*.iso;*.wbfs;*.gcz;*.wia;*.ciso"},
                                             {L"All files", L"*.*"}};
        dlg->SetFileTypes(2, filters);
        dlg->SetTitle(L"Pick your New Super Mario Bros. Wii disc image");
    }
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->Show(nullptr)) && SUCCEEDED(dlg->GetResult(&item))) {
        PWSTR name = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
            result = name;
            CoTaskMemFree(name);
        }
        item->Release();
    }
    dlg->Release();
    return result;
}

// Runs DolphinTool with a small "please wait" window; returns its exit code (-1 if it can't start).
int RunDolphinTool(const fs::path& tool, const fs::path& image, const fs::path& out) {
    std::wstring cmd = L"\"" + tool.wstring() + L"\" extract -i \"" + image.wstring() + L"\" -o \"" +
                       out.wstring() + L"\" -g -q";  // -g: DATA partition only (DolphinTool 2609a)
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    HWND wait = CreateWindowExW(WS_EX_TOPMOST, L"STATIC",
                                L"\r\n  Extracting your disc - this takes a few seconds.\r\n  The game starts by itself when it is done.",
                                WS_POPUP | WS_BORDER | WS_VISIBLE,
                                (GetSystemMetrics(SM_CXSCREEN) - 460) / 2, (GetSystemMetrics(SM_CYSCREEN) - 80) / 2,
                                460, 80, nullptr, nullptr, nullptr, nullptr);
    while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    if (wait) DestroyWindow(wait);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

bool SaveDvdRoot(const fs::path& root) {
    const std::string value = RuntimeConfigFile::PathToUtf8(fs::absolute(root));
    RuntimeConfigFile::Mutable().dvdRoot = value;
    return RuntimeConfigFile::WriteSetting("paths", "dvd_root", RuntimeConfigFile::FormatString(value));
}

}  // namespace

void NsmbwEnsureDiscFolder() {
    RuntimeConfigFile::EnsureConfigFile();
    if (IsExtractedDisc(RuntimeConfigFile::ResolvedDvdRoot())) return;
    const fs::path exeDir = RuntimeConfigFile::ExecutableDirectory().value_or(fs::current_path());
    const fs::path disc = exeDir / "disc";
    // A disc extracted on an earlier launch, with the folder since moved: just re-point at it.
    if (IsExtractedDisc(disc) && DiscProblem(disc).empty()) {
        SaveDvdRoot(disc);
        return;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // Test hook: NSMBW_SETUP_IMAGE=<image> answers "Yes" and picks that file, so a script can run
    // the extract/validate/save path without clicking the dialogs.
    const char* scriptedImage = std::getenv("NSMBW_SETUP_IMAGE");
    for (;;) {
        const int choice = scriptedImage ? IDYES : Ask(L"NSMBWCompiled needs your own copy of New Super Mario Bros. Wii "
                               L"(PAL, SMNP01, revision 1).\n\n"
                               L"Yes: pick a disc image (.rvz, .iso, .wbfs) - it is extracted once, next to the game.\n"
                               L"No: pick a folder you already extracted with Dolphin.\n"
                               L"Cancel: quit.",
                               MB_YESNOCANCEL | MB_ICONINFORMATION);
        if (choice == IDCANCEL) std::exit(0);
        std::error_code ec;
        fs::path root;
        if (choice == IDNO) {
            const fs::path picked = PickPath(true);
            if (picked.empty()) continue;
            root = FindExtractedRoot(picked);
            if (root.empty()) {
                Ask(L"That folder has no sys and files folders inside. Pick the folder Dolphin extracted the "
                    L"Data Partition into.", MB_OK | MB_ICONWARNING);
                continue;
            }
        } else {
            const fs::path image = scriptedImage ? RuntimeConfigFile::PathFromUtf8(scriptedImage) : PickPath(false);
            scriptedImage = nullptr;  // a failure falls back to the dialogs instead of looping
            if (image.empty()) continue;
            const fs::path tool = exeDir / "DolphinTool.exe";
            const fs::path tmp = exeDir / "disc_extracting";
            fs::remove_all(tmp, ec);
            const int code = RunDolphinTool(tool, image, tmp);
            root = FindExtractedRoot(tmp);
            if (code != 0 || root.empty()) {
                fs::remove_all(tmp, ec);
                Ask(code == -1 ? L"DolphinTool.exe is missing from the game folder. Re-extract the whole zip."
                               : L"The disc image could not be extracted. Check that it is a complete Wii disc "
                                 L"image and that the drive has about 1 GB free.",
                    MB_OK | MB_ICONERROR);
                continue;
            }
        }
        if (const std::wstring problem = DiscProblem(root); !problem.empty()) {
            if (root.wstring().find(L"disc_extracting") != std::wstring::npos) fs::remove_all(exeDir / "disc_extracting", ec);
            Ask(problem, MB_OK | MB_ICONWARNING);
            continue;
        }
        if (root.wstring().find(L"disc_extracting") != std::wstring::npos) {
            fs::remove_all(disc, ec);
            fs::rename(root, disc, ec);
            if (ec) {
                Ask(L"Could not move the extracted disc into the game folder.", MB_OK | MB_ICONERROR);
                continue;
            }
            fs::remove_all(exeDir / "disc_extracting", ec);
            root = disc;
        }
        if (!SaveDvdRoot(root)) {
            Ask(L"Could not save the disc location to Config.toml.", MB_OK | MB_ICONERROR);
            continue;
        }
        break;
    }
    CoUninitialize();
}

#else
void NsmbwEnsureDiscFolder() {}
#endif

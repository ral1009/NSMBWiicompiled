// The launcher's setup screen (2026-11-14): game files, HD texture pack, and a slot for Riivolution
// mods. Moved here from the game's own first-launch dialogs (the old nsmbw_disc_setup.cpp, release
// builds only) because the launcher has to know which disc it has before it can choose a game DLL:
// the disc ID in sys\boot.bin picks NSMBWCompiled-<version>.dll.
//
// When it shows: the configured disc is missing or not a supported version, or Shift is held while
// the launcher starts (NSMBW_SETUP_SHOW=1 does the same). Otherwise the game starts directly.
//
// Settings go to the same Config.toml the game reads, through the same header
// (runtime_config.h), before the DLL is loaded: [paths] dvd_root and [video] texture_replacements.
//
// Script hooks, so a test can run every path without clicking:
//   NSMBW_SETUP_IMAGE=<disc image>   extract it with DolphinTool.exe (as the "Disc image..." button)
//   NSMBW_SETUP_FOLDER=<folder>      use an extracted disc folder ("Extracted folder...")
//   NSMBW_SETUP_TEXTURES=<folder>    install that HD pack ("Install pack...")
// With any of them set, no window is shown unless Shift / NSMBW_SETUP_SHOW asks for it.
#include "nsmbw_launcher_setup.h"

#include "runtime_config.h"

#include <windows.h>
#include <shobjidl.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace nsmbw_launcher {
namespace {

// ---- Game versions -------------------------------------------------------------------------------

struct GameVersion {
    const char* gameId;    // sys\boot.bin bytes 0-5
    int revision;          // sys\boot.bin byte 7
    const wchar_t* tag;    // NSMBWCompiled-<tag>.dll; the address-map names (NSMBW-Maps/address-map.txt)
    const wchar_t* name;
};

// The versions this launcher knows a DLL for (built with -DNSMBW_GAME_VERSION=<tag>). A package
// without a version's DLL says so when that disc is picked (nsmbw_launcher.cpp).
constexpr GameVersion kSupportedVersions[] = {
    {"SMNP01", 1, L"P1", L"Europe (PAL), revision 1"},
    {"SMNE01", 1, L"E1", L"USA, revision 1"},
};

const wchar_t* RegionName(char regionLetter) {
    switch (regionLetter) {
    case 'P': return L"Europe (PAL)";
    case 'E': return L"USA";
    case 'J': return L"Japan";
    case 'K': return L"Korea";
    case 'W': return L"Taiwan";
    case 'C': return L"China";
    default: return L"unknown region";
    }
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool IsExtractedDisc(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p / "files", ec) && fs::is_regular_file(p / "sys" / "fst.bin", ec);
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

struct DiscStatus {
    const GameVersion* version = nullptr;  // non-null when this build can play it
    std::wstring text;                     // shown in the window
};

DiscStatus CheckDisc(const fs::path& root) {
    if (root.empty()) return {nullptr, L"Not set yet. Pick your disc image, or a folder you extracted with Dolphin."};
    if (!IsExtractedDisc(root)) return {nullptr, L"No extracted disc at " + root.wstring() + L" (no sys and files folders)."};
    std::ifstream in(root / "sys" / "boot.bin", std::ios::binary);
    char header[8]{};
    if (!in.read(header, sizeof(header))) return {nullptr, L"The disc header (sys\\boot.bin) could not be read."};
    const std::string id(header, 6);
    const int revision = static_cast<unsigned char>(header[7]);
    if (id.compare(0, 3, "SMN") != 0) {
        return {nullptr, L"This is not New Super Mario Bros. Wii (game ID " + Widen(id) + L")."};
    }
    for (const GameVersion& v : kSupportedVersions) {
        if (id == v.gameId && revision == v.revision) return {&v, v.name};
    }
    std::wstring supported;
    for (const GameVersion& v : kSupportedVersions) {
        supported += (supported.empty() ? L"" : L", ") + Widen(v.gameId) + L" revision " + std::to_wstring(v.revision);
    }
    return {nullptr, L"This disc (" + Widen(id) + L" revision " + std::to_wstring(revision) + L", " + RegionName(id[3]) +
                         L") is not supported yet. Supported: " + supported + L"."};
}

// ---- Files and Config.toml -----------------------------------------------------------------------

fs::path ExeDir() { return RuntimeConfigFile::ExecutableDirectory().value_or(fs::current_path()); }

// The game's NsmbwDataDir() (nsmbw_product.cpp): nsmbw_data next to the exe.
fs::path TexturePackDir() { return ExeDir() / "nsmbw_data" / "texture_replacements" / "SMN"; }

bool TexturePackInstalled() {
    std::error_code ec;
    return fs::is_directory(TexturePackDir(), ec) && !fs::is_empty(TexturePackDir(), ec);
}

bool SaveDvdRoot(const fs::path& root) {
    const std::string value = RuntimeConfigFile::PathToUtf8(fs::absolute(root));
    RuntimeConfigFile::Mutable().dvdRoot = value;
    return RuntimeConfigFile::WriteSetting("paths", "dvd_root", RuntimeConfigFile::FormatString(value));
}

const char* Env(const char* name) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : nullptr;
}

// ---- Small Win32 helpers -------------------------------------------------------------------------

HWND g_window = nullptr;

void Tell(const std::wstring& text, UINT icon) {
    std::fwprintf(stderr, L"[launcher] %ls\n", text.c_str());
    MessageBoxW(g_window, text.c_str(), L"NSMBWCompiled setup", MB_OK | icon | MB_SETFOREGROUND);
}

fs::path PickPath(bool folder, const wchar_t* title) {
    fs::path result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : 0));
    dlg->SetTitle(title);
    if (!folder) {
        const COMDLG_FILTERSPEC filters[] = {{L"Wii disc images", L"*.rvz;*.iso;*.wbfs;*.gcz;*.wia;*.ciso"},
                                             {L"All files", L"*.*"}};
        dlg->SetFileTypes(2, filters);
    }
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->Show(g_window)) && SUCCEEDED(dlg->GetResult(&item))) {
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

// Runs a command line hidden, with a small "please wait" window, and returns its exit code (-1 if
// it can't start). The setup window is disabled meanwhile so it can't start a second job.
int RunWithWaitWindow(std::wstring cmd, const wchar_t* text) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    if (g_window) EnableWindow(g_window, FALSE);
    HWND wait = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", text, WS_POPUP | WS_BORDER | WS_VISIBLE,
                                (GetSystemMetrics(SM_CXSCREEN) - 460) / 2, (GetSystemMetrics(SM_CYSCREEN) - 80) / 2,
                                460, 80, g_window, nullptr, nullptr, nullptr);
    while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    if (wait) DestroyWindow(wait);
    if (g_window) {
        EnableWindow(g_window, TRUE);
        SetForegroundWindow(g_window);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

// ---- The three actions (shared by the buttons and the script hooks) -----------------------------

// Uses an already-extracted folder. Returns true when it is a supported disc and was saved.
bool UseExtractedFolder(const fs::path& picked) {
    const fs::path root = FindExtractedRoot(picked);
    if (root.empty()) {
        Tell(L"That folder has no sys and files folders inside. Pick the folder Dolphin extracted the "
             L"Data Partition into.", MB_ICONWARNING);
        return false;
    }
    const DiscStatus status = CheckDisc(root);
    if (status.version == nullptr) {
        Tell(status.text, MB_ICONWARNING);
        return false;
    }
    if (!SaveDvdRoot(root)) {
        Tell(L"Could not save the disc location to Config.toml.", MB_ICONERROR);
        return false;
    }
    return true;
}

// Extracts a disc image into disc\ next to the exe with DolphinTool.exe (shipped next to the exe;
// Dolphin, GPLv2+). Returns true when the result is a supported disc and was saved.
bool ExtractDiscImage(const fs::path& image) {
    const fs::path exeDir = ExeDir();
    const fs::path tool = exeDir / "DolphinTool.exe";
    const fs::path tmp = exeDir / "disc_extracting";
    const fs::path disc = exeDir / "disc";
    std::error_code ec;
    if (!fs::is_regular_file(tool, ec)) {
        Tell(L"DolphinTool.exe is missing from the game folder, so disc images can't be extracted. "
             L"Re-extract the whole zip, or pick a folder you already extracted with Dolphin.", MB_ICONERROR);
        return false;
    }
    fs::remove_all(tmp, ec);
    const int code = RunWithWaitWindow(L"\"" + tool.wstring() + L"\" extract -i \"" + image.wstring() + L"\" -o \"" +
                                           tmp.wstring() + L"\" -g -q",  // -g: DATA partition only (DolphinTool 2609a)
                                       L"\r\n  Extracting your disc - this takes a few seconds.");
    fs::path root = FindExtractedRoot(tmp);
    if (code != 0 || root.empty()) {
        fs::remove_all(tmp, ec);
        Tell(L"The disc image could not be extracted. Check that it is a complete Wii disc image and that "
             L"the drive has about 1 GB free.", MB_ICONERROR);
        return false;
    }
    const DiscStatus status = CheckDisc(root);
    if (status.version == nullptr) {
        fs::remove_all(tmp, ec);
        Tell(status.text, MB_ICONWARNING);
        return false;
    }
    fs::remove_all(disc, ec);
    fs::rename(root, disc, ec);
    if (ec) {
        Tell(L"Could not move the extracted disc into the game folder.", MB_ICONERROR);
        return false;
    }
    fs::remove_all(tmp, ec);
    if (!SaveDvdRoot(disc)) {
        Tell(L"Could not save the disc location to Config.toml.", MB_ICONERROR);
        return false;
    }
    return true;
}

// Dolphin HD texture pack: the renderer reads nsmbw_data\texture_replacements\SMN next to the exe
// (aurora texture_replacement.cpp), so the pack's SMN folder is copied there with robocopy (exit
// codes below 8 mean success) and texture_replacements is switched on.
bool InstallTexturePack(const fs::path& picked) {
    std::error_code ec;
    fs::path smn;
    if (picked.filename() == "SMN") smn = picked;
    else if (fs::is_directory(picked / "SMN", ec)) smn = picked / "SMN";
    else if (fs::is_directory(picked / "Load" / "Textures" / "SMN", ec)) smn = picked / "Load" / "Textures" / "SMN";
    if (smn.empty()) {
        Tell(L"No SMN folder found there. Pick the pack's folder (the one containing \"SMN\"), or SMN itself.",
             MB_ICONWARNING);
        return false;
    }
    const int code = RunWithWaitWindow(L"robocopy \"" + smn.wstring() + L"\" \"" + TexturePackDir().wstring() +
                                           L"\" /E /NFL /NDL /NJH /NJS /NP",
                                       L"\r\n  Copying the texture pack - this can take a minute.");
    if (code < 0 || code >= 8) {
        Tell(L"The texture pack could not be copied (is the drive full?).", MB_ICONWARNING);
        return false;
    }
    RuntimeConfigFile::SetTextureReplacements(true);
    return true;
}

// ---- The window ----------------------------------------------------------------------------------

enum ControlId : int {
    kPlay = IDOK,
    kQuit = IDCANCEL,
    kDiscImage = 100,
    kDiscFolder,
    kDiscStatus,
    kDiscPath,
    kPackInstall,
    kPackStatus,
    kPackUse,
    kModsAdd,
};

struct WindowState {
    float scale = 1.0f;
    HFONT font = nullptr;
    bool done = false;
    bool play = false;
} g_state;

int S(int v) { return static_cast<int>(v * g_state.scale + 0.5f); }

HWND AddControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_state.font), TRUE);
    return c;
}

void Refresh(HWND hwnd) {
    const fs::path root = RuntimeConfigFile::ResolvedDvdRoot();
    const DiscStatus disc = CheckDisc(root);
    SetDlgItemTextW(hwnd, kDiscStatus, disc.text.c_str());
    // The path gets its own one-line label, shortened in the middle when too long (SS_PATHELLIPSIS);
    // shown only for a good disc, whose status text is one line.
    SetDlgItemTextW(hwnd, kDiscPath, disc.version != nullptr ? root.wstring().c_str() : L"");
    ShowWindow(GetDlgItem(hwnd, kDiscPath), disc.version != nullptr ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(hwnd, kPlay), disc.version != nullptr);
    const bool installed = TexturePackInstalled();
    SetDlgItemTextW(hwnd, kPackStatus, installed ? L"Installed." : L"Not installed. The game uses its original textures.");
    EnableWindow(GetDlgItem(hwnd, kPackUse), installed);
    CheckDlgButton(hwnd, kPackUse, installed && RuntimeConfigFile::TextureReplacements(false) ? BST_CHECKED : BST_UNCHECKED);
}

void BuildControls(HWND hwnd) {
    // Layout in 96-DPI units; S() scales to the monitor. Client area 520 x 400.
    AddControl(hwnd, L"BUTTON", L"Game files", BS_GROUPBOX, 12, 8, 496, 112, -1);
    AddControl(hwnd, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 24, 30, 472, 44, kDiscStatus);
    AddControl(hwnd, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS, 24, 52, 472, 20, kDiscPath);
    AddControl(hwnd, L"BUTTON", L"Disc image...", WS_TABSTOP | BS_PUSHBUTTON, 24, 80, 150, 28, kDiscImage);
    AddControl(hwnd, L"BUTTON", L"Extracted folder...", WS_TABSTOP | BS_PUSHBUTTON, 182, 80, 150, 28, kDiscFolder);

    AddControl(hwnd, L"BUTTON", L"HD texture pack (optional)", BS_GROUPBOX, 12, 128, 496, 92, -1);
    AddControl(hwnd, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 24, 150, 472, 20, kPackStatus);
    AddControl(hwnd, L"BUTTON", L"Install pack...", WS_TABSTOP | BS_PUSHBUTTON, 24, 178, 150, 28, kPackInstall);
    AddControl(hwnd, L"BUTTON", L"Use HD textures", WS_TABSTOP | BS_AUTOCHECKBOX, 190, 182, 200, 22, kPackUse);

    AddControl(hwnd, L"BUTTON", L"Riivolution mods", BS_GROUPBOX, 12, 228, 496, 76, -1);
    HWND modsText = AddControl(hwnd, L"STATIC", L"Mods are not supported yet; this is where they will be added.",
                               SS_LEFT, 24, 252, 300, 40, -1);
    EnableWindow(modsText, FALSE);
    HWND modsButton = AddControl(hwnd, L"BUTTON", L"Add mod...", WS_TABSTOP | BS_PUSHBUTTON, 346, 256, 150, 28, kModsAdd);
    EnableWindow(modsButton, FALSE);

    AddControl(hwnd, L"STATIC",
               L"Hold Shift while starting NSMBWCompiled to come back here. In the game, F10 opens the settings.",
               SS_LEFT, 12, 314, 496, 34, -1);
    AddControl(hwnd, L"BUTTON", L"Play", WS_TABSTOP | BS_DEFPUSHBUTTON, 308, 358, 96, 30, kPlay);
    AddControl(hwnd, L"BUTTON", L"Quit", WS_TABSTOP | BS_PUSHBUTTON, 412, 358, 96, 30, kQuit);
    Refresh(hwnd);
}

LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kDiscImage: {
            const fs::path image = PickPath(false, L"Pick your New Super Mario Bros. Wii disc image");
            if (!image.empty()) ExtractDiscImage(image);
            Refresh(hwnd);
            return 0;
        }
        case kDiscFolder: {
            const fs::path folder = PickPath(true, L"Pick the extracted disc folder (it contains sys and files)");
            if (!folder.empty()) UseExtractedFolder(folder);
            Refresh(hwnd);
            return 0;
        }
        case kPackInstall: {
            const fs::path folder = PickPath(true, L"Pick the texture pack folder (the one containing SMN)");
            if (!folder.empty()) InstallTexturePack(folder);
            Refresh(hwnd);
            return 0;
        }
        case kPlay:
            if (!IsWindowEnabled(GetDlgItem(hwnd, kPlay))) return 0;  // Enter with no disc set
            if (TexturePackInstalled()) {
                const bool use = IsDlgButtonChecked(hwnd, kPackUse) == BST_CHECKED;
                if (use != RuntimeConfigFile::TextureReplacements(false)) RuntimeConfigFile::SetTextureReplacements(use);
            }
            g_state.play = true;
            g_state.done = true;
            return 0;
        case kQuit:
            g_state.done = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        g_state.done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Per-monitor DPI awareness, so the window is sharp instead of bitmap-stretched. Only called when
// the window is shown. Loaded by name because older SDK headers lack the declarations.
float EnableDpiAwarenessAndGetScale() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetCtx = BOOL(WINAPI*)(HANDLE);
    using GetDpi = UINT(WINAPI*)();
    if (auto set = reinterpret_cast<SetCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
        set(reinterpret_cast<HANDLE>(-4));  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }
    if (auto get = reinterpret_cast<GetDpi>(GetProcAddress(user32, "GetDpiForSystem"))) {
        return static_cast<float>(get()) / 96.0f;
    }
    return 1.0f;
}

// Returns true when the player pressed Play.
bool RunSetupWindow() {
    g_state.scale = EnableDpiAwarenessAndGetScale();
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_state.font = CreateFontIndirectW(&ncm.lfMessageFont);

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = SetupWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW (its macro is the ANSI form here)
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NSMBWCompiledSetup";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));  // the exe icon, when branding/nsmbw.ico was embedded
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{0, 0, S(520), S(400)};
    AdjustWindowRectEx(&r, style, FALSE, 0);
    const int w = r.right - r.left, h = r.bottom - r.top;
    g_window = CreateWindowExW(0, wc.lpszClassName, L"NSMBWCompiled setup", style,
                               (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                               nullptr, nullptr, instance, nullptr);
    if (g_window == nullptr) return false;
    BuildControls(g_window);
    ShowWindow(g_window, SW_SHOWNORMAL);
    SetForegroundWindow(g_window);

    MSG msg;
    while (!g_state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_window, &msg)) {  // Tab between controls, Enter = Play, Esc = Quit
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(g_window);
    g_window = nullptr;
    DeleteObject(g_state.font);
    return g_state.play;
}

}  // namespace

std::wstring EnsureSetup(bool showWindow) {
    RuntimeConfigFile::EnsureConfigFile();
    if (Env("NSMBW_SETUP_SHOW")) showWindow = true;

    // CoInitialize only for the file dialogs; undone before the game DLL loads, so the game starts
    // with COM as it did before the launcher existed.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    const bool scripted = Env("NSMBW_SETUP_IMAGE") || Env("NSMBW_SETUP_FOLDER") || Env("NSMBW_SETUP_TEXTURES");
    if (const char* image = Env("NSMBW_SETUP_IMAGE")) ExtractDiscImage(RuntimeConfigFile::PathFromUtf8(image));
    if (const char* folder = Env("NSMBW_SETUP_FOLDER")) UseExtractedFolder(RuntimeConfigFile::PathFromUtf8(folder));
    if (const char* pack = Env("NSMBW_SETUP_TEXTURES")) InstallTexturePack(RuntimeConfigFile::PathFromUtf8(pack));

    DiscStatus disc = CheckDisc(RuntimeConfigFile::ResolvedDvdRoot());
    // A disc extracted on an earlier launch whose Config.toml entry was lost: just re-point at it.
    if (disc.version == nullptr) {
        const fs::path extracted = ExeDir() / "disc";
        if (CheckDisc(extracted).version != nullptr && SaveDvdRoot(extracted)) disc = CheckDisc(extracted);
    }

    bool play = disc.version != nullptr;
    if (showWindow || (!play && !scripted)) {
        if (Env("NSMBW_AUTO_PRESS_SELFTEST") && !showWindow) {
            // A test run must never sit behind a window nobody will click.
            std::fwprintf(stderr, L"[launcher] no playable disc for a self-test run: %ls\n", disc.text.c_str());
            play = false;
        } else {
            play = RunSetupWindow();
            disc = CheckDisc(RuntimeConfigFile::ResolvedDvdRoot());
            play = play && disc.version != nullptr;
        }
    } else if (!play) {
        std::fwprintf(stderr, L"[launcher] scripted setup left no playable disc: %ls\n", disc.text.c_str());
    }

    if (SUCCEEDED(com)) CoUninitialize();
    if (!play) return {};
    std::fwprintf(stderr, L"[launcher] disc: %ls\n", disc.text.c_str());
    return disc.version->tag;
}

}  // namespace nsmbw_launcher

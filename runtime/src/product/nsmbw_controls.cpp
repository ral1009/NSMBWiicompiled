// Action-based controls for NSMBW.
//
// The game only knows Wii Remote buttons, and it gives the same button different meanings by
// context: 2 is Jump in a course, Confirm in menus and "enter course" on the world map; 1 is
// Run/Fireball in a course but Back in menus. With one fixed physical->Wii mapping (the old
// GC-PAD path, nsmbw_kpad_overrides.cpp TranslatePadToWpad), "Jump on South" forces "Confirm on
// South", and "Run on West" forces "Back on West". So here the player binds physical controls to
// *actions*, separately per context, and each action produces its Wii button:
//   physical control --(bindings for the current context)--> action --> WPAD bit
//
// Context (CurrentContext):
//   Menu      - any scene but the world map and courses; the title screen and its file select
//               (a STAGE scene with dInfo_c::m_startGameInfo.mGameMode TITLE/TITLE_REPLAY); any
//               course or map while the game is stopped for a pause/help menu (dGameCom
//               isGameStop, GAME_STOP_PAUSE | GAME_STOP_OTASUKE_PAUSE).
//   Map       - WORLD_MAP scene (profile 3).
//   Gameplay  - STAGE scene (profile 5) in any other game mode.
// Addresses: m_startGameInfo = 0x80315E90 (NSMBW-Decomp syms.txt), mGameMode at +8 (u32, 4 x u8
// before it; d_info.hpp). The game-stop word is read by isGameStop (func_800B3B50) as
// [r13 - 22360] with r13 = SDA base 0x8042F980 (nsmbw.yml) -> 0x8042A228.
//
// Gamepads are read straight from SDL (the pad assigned to port 0 in the overlay, or every
// connected pad when none is), and the keyboard from SDL's key state, so the overlay's GC-PAD
// button mapping no longer applies to NSMBW. Bindings are saved to nsmbw_controls.ini next to
// Config.toml as "<context>.<action>.pad = south,west" / ".key = z,enter".

#include "nsmbw_controls.h"

#include "memory.h"
#include "runtime_config.h"
#include "settings_overlay.h"

#include <dolphin/pad.h>
#include <imgui.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace nsmbw_controls {
namespace {

// WPAD button bits (NSMBW-Decomp include/lib/revolution/WPAD/WPAD.h), same as the KPAD override.
constexpr uint32_t kWpadLeft = 1u << 0;
constexpr uint32_t kWpadRight = 1u << 1;
constexpr uint32_t kWpadDown = 1u << 2;
constexpr uint32_t kWpadUp = 1u << 3;
constexpr uint32_t kWpadPlus = 1u << 4;
constexpr uint32_t kWpad2 = 1u << 8;
constexpr uint32_t kWpad1 = 1u << 9;
constexpr uint32_t kWpadA = 1u << 11;
constexpr uint32_t kWpadMinus = 1u << 12;
constexpr uint32_t kHostShake = 1u << 31; // turned into an accelerometer burst by the KPAD override
// Tilt actions produce no button bit (wpad 0): the game reads tilt from the accelerometer, and a
// stick should tilt proportionally, so they are read as an amount by NsmbwControlsReadTilt.
constexpr uint32_t kNoWpadBit = 0;

// The remote is held sideways, so the game reads its d-pad rotated: screen-left is WPAD UP,
// screen-right DOWN, screen-up RIGHT, screen-down LEFT.
constexpr uint32_t kScreenLeft = kWpadUp;
constexpr uint32_t kScreenRight = kWpadDown;
constexpr uint32_t kScreenUp = kWpadRight;
constexpr uint32_t kScreenDown = kWpadLeft;

constexpr uint32_t kGameStopAddr = 0x8042A228u;
constexpr uint32_t kGameStopPauseMask = 0x1u | 0x4u; // GAME_STOP_PAUSE | GAME_STOP_OTASUKE_PAUSE
constexpr uint32_t kGameModeAddr = 0x80315E90u + 8u;
constexpr uint32_t kGameModeTitle = 2, kGameModeTitleReplay = 3;
constexpr uint32_t kProfileWorldMap = 3, kProfileStage = 5;

// ---- Physical controls -------------------------------------------------------------------------

enum class PadKind { Button, AxisPositive, AxisNegative };
struct PadControl {
    const char* name;  // saved name, and (after GlyphName) the glyph file
    const char* label; // shown in the menu
    PadKind kind;
    int id;
};
constexpr PadControl kPadControls[] = {
    {"south", "South (A / Cross / B)", PadKind::Button, SDL_GAMEPAD_BUTTON_SOUTH},
    {"east", "East (B / Circle / A)", PadKind::Button, SDL_GAMEPAD_BUTTON_EAST},
    {"west", "West (X / Square / Y)", PadKind::Button, SDL_GAMEPAD_BUTTON_WEST},
    {"north", "North (Y / Triangle / X)", PadKind::Button, SDL_GAMEPAD_BUTTON_NORTH},
    {"back", "Back / Select", PadKind::Button, SDL_GAMEPAD_BUTTON_BACK},
    {"guide", "Guide / Home", PadKind::Button, SDL_GAMEPAD_BUTTON_GUIDE},
    {"start", "Start / Options", PadKind::Button, SDL_GAMEPAD_BUTTON_START},
    {"left_stick", "Left stick click", PadKind::Button, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"right_stick", "Right stick click", PadKind::Button, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"left_shoulder", "Left shoulder", PadKind::Button, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"right_shoulder", "Right shoulder", PadKind::Button, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"dpad_up", "D-pad up", PadKind::Button, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"dpad_down", "D-pad down", PadKind::Button, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"dpad_left", "D-pad left", PadKind::Button, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"dpad_right", "D-pad right", PadKind::Button, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"misc1", "Share / Capture", PadKind::Button, SDL_GAMEPAD_BUTTON_MISC1},
    {"touchpad", "Touchpad", PadKind::Button, SDL_GAMEPAD_BUTTON_TOUCHPAD},
    {"left_trigger", "Left trigger", PadKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFT_TRIGGER},
    {"right_trigger", "Right trigger", PadKind::AxisPositive, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
    {"lstick_up", "Left stick up", PadKind::AxisNegative, SDL_GAMEPAD_AXIS_LEFTY},
    {"lstick_down", "Left stick down", PadKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFTY},
    {"lstick_left", "Left stick left", PadKind::AxisNegative, SDL_GAMEPAD_AXIS_LEFTX},
    {"lstick_right", "Left stick right", PadKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFTX},
    {"rstick_up", "Right stick up", PadKind::AxisNegative, SDL_GAMEPAD_AXIS_RIGHTY},
    {"rstick_down", "Right stick down", PadKind::AxisPositive, SDL_GAMEPAD_AXIS_RIGHTY},
    {"rstick_left", "Right stick left", PadKind::AxisNegative, SDL_GAMEPAD_AXIS_RIGHTX},
    {"rstick_right", "Right stick right", PadKind::AxisPositive, SDL_GAMEPAD_AXIS_RIGHTX},
};
// Sticks count as pressed at about 40 % tilt (the old path's threshold, 50/127), triggers at 30 %.
constexpr int kStickThreshold = 13000;
constexpr int kTriggerThreshold = 10000;

const PadControl* FindPad(const std::string& name) {
    for (const auto& c : kPadControls) {
        if (name == c.name) return &c;
    }
    return nullptr;
}

struct KeyName {
    SDL_Scancode code;
    const char* name; // saved name, and the keyboard glyph file
};
constexpr KeyName kKeyNames[] = {
    {SDL_SCANCODE_RETURN, "enter"}, {SDL_SCANCODE_KP_ENTER, "enter"}, {SDL_SCANCODE_SPACE, "space"},
    {SDL_SCANCODE_TAB, "tab"}, {SDL_SCANCODE_ESCAPE, "escape"}, {SDL_SCANCODE_BACKSPACE, "backspace"},
    {SDL_SCANCODE_LSHIFT, "shift"}, {SDL_SCANCODE_RSHIFT, "shift"}, {SDL_SCANCODE_LCTRL, "ctrl"},
    {SDL_SCANCODE_RCTRL, "ctrl"}, {SDL_SCANCODE_LALT, "alt"}, {SDL_SCANCODE_RALT, "alt"},
    {SDL_SCANCODE_UP, "up"}, {SDL_SCANCODE_DOWN, "down"}, {SDL_SCANCODE_LEFT, "left"},
    {SDL_SCANCODE_RIGHT, "right"}, {SDL_SCANCODE_EQUALS, "equals"}, {SDL_SCANCODE_MINUS, "minus"},
    {SDL_SCANCODE_COMMA, "comma"}, {SDL_SCANCODE_PERIOD, "period"}, {SDL_SCANCODE_SEMICOLON, "semicolon"},
    {SDL_SCANCODE_APOSTROPHE, "apostrophe"}, {SDL_SCANCODE_SLASH, "slash"},
    {SDL_SCANCODE_BACKSLASH, "backslash"}, {SDL_SCANCODE_GRAVE, "grave"},
    {SDL_SCANCODE_LEFTBRACKET, "left_bracket"}, {SDL_SCANCODE_RIGHTBRACKET, "right_bracket"},
    {SDL_SCANCODE_DELETE, "delete"}, {SDL_SCANCODE_INSERT, "insert"}, {SDL_SCANCODE_HOME, "home"},
    {SDL_SCANCODE_END, "end"}, {SDL_SCANCODE_PAGEUP, "page_up"}, {SDL_SCANCODE_PAGEDOWN, "page_down"},
};

std::string KeyToName(SDL_Scancode code) {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return std::string(1, char('a' + (code - SDL_SCANCODE_A)));
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) return std::string(1, char('1' + (code - SDL_SCANCODE_1)));
    if (code == SDL_SCANCODE_0) return "0";
    for (const auto& k : kKeyNames) {
        if (k.code == code) return k.name;
    }
    return "sc" + std::to_string(static_cast<int>(code));
}

SDL_Scancode NameToKey(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return SDL_Scancode(SDL_SCANCODE_A + (name[0] - 'a'));
    if (name.size() == 1 && name[0] >= '1' && name[0] <= '9') return SDL_Scancode(SDL_SCANCODE_1 + (name[0] - '1'));
    if (name == "0") return SDL_SCANCODE_0;
    for (const auto& k : kKeyNames) {
        if (name == k.name) return k.code;
    }
    if (name.rfind("sc", 0) == 0) return SDL_Scancode(std::atoi(name.c_str() + 2));
    return SDL_SCANCODE_UNKNOWN;
}

// ---- Actions -----------------------------------------------------------------------------------

constexpr size_t kMaxBinds = 2;
struct ActionDef {
    const char* id;
    const char* label;
    uint32_t wpad;
    std::array<const char*, kMaxBinds> pad;
    std::array<const char*, kMaxBinds> key;
};

// Defaults keep the old single mapping's feel (Jump on South, keyboard Z/X/C/Enter) but split
// Run and Back: West runs in a course, East goes back in menus.
const std::vector<ActionDef> kGameplayActions = {
    {"jump", "Jump (2)", kWpad2, {"south", nullptr}, {"z", "space"}},
    {"run", "Run / fireball (1)", kWpad1, {"west", "north"}, {"x", nullptr}},
    {"spin", "Spin jump (shake)", kHostShake, {"left_shoulder", "right_shoulder"}, {"c", nullptr}},
    {"bubble", "Bubble (A)", kWpadA, {"east", nullptr}, {"enter", nullptr}},
    {"pause", "Pause (+)", kWpadPlus, {"start", nullptr}, {"equals", "escape"}},
    {"minus", "- button", kWpadMinus, {"back", nullptr}, {"minus", nullptr}},
    {"up", "Up / enter door", kScreenUp, {"dpad_up", "lstick_up"}, {"up", nullptr}},
    {"down", "Crouch / pipe", kScreenDown, {"dpad_down", "lstick_down"}, {"down", nullptr}},
    {"left", "Left", kScreenLeft, {"dpad_left", "lstick_left"}, {"left", nullptr}},
    {"right", "Right", kScreenRight, {"dpad_right", "lstick_right"}, {"right", nullptr}},
    {"tilt_left", "Tilt remote left", kNoWpadBit, {"rstick_left", nullptr}, {"a", nullptr}},
    {"tilt_right", "Tilt remote right", kNoWpadBit, {"rstick_right", nullptr}, {"d", nullptr}},
};
const std::vector<ActionDef> kMapActions = {
    {"enter", "Enter course (2)", kWpad2, {"south", nullptr}, {"z", "enter"}},
    {"items", "Items (1)", kWpad1, {"west", nullptr}, {"x", nullptr}},
    {"look", "Look around (A)", kWpadA, {"north", nullptr}, {"space", nullptr}},
    {"menu", "Menu (+)", kWpadPlus, {"start", nullptr}, {"equals", "escape"}},
    {"minus", "- button", kWpadMinus, {"back", nullptr}, {"minus", nullptr}},
    {"spin", "Shake", kHostShake, {"left_shoulder", "right_shoulder"}, {"c", nullptr}},
    {"up", "Up", kScreenUp, {"dpad_up", "lstick_up"}, {"up", nullptr}},
    {"down", "Down", kScreenDown, {"dpad_down", "lstick_down"}, {"down", nullptr}},
    {"left", "Left", kScreenLeft, {"dpad_left", "lstick_left"}, {"left", nullptr}},
    {"right", "Right", kScreenRight, {"dpad_right", "lstick_right"}, {"right", nullptr}},
};
const std::vector<ActionDef> kMenuActions = {
    {"confirm", "Confirm (2)", kWpad2, {"south", nullptr}, {"enter", "z"}},
    {"back", "Back (1)", kWpad1, {"east", nullptr}, {"backspace", "x"}},
    {"a", "A button", kWpadA, {"north", nullptr}, {"space", nullptr}},
    {"plus", "+ button", kWpadPlus, {"start", nullptr}, {"equals", "escape"}},
    {"minus", "- button", kWpadMinus, {"back", nullptr}, {"minus", nullptr}},
    {"up", "Up", kScreenUp, {"dpad_up", "lstick_up"}, {"up", nullptr}},
    {"down", "Down", kScreenDown, {"dpad_down", "lstick_down"}, {"down", nullptr}},
    {"left", "Left", kScreenLeft, {"dpad_left", "lstick_left"}, {"left", nullptr}},
    {"right", "Right", kScreenRight, {"dpad_right", "lstick_right"}, {"right", nullptr}},
};

const std::vector<ActionDef>& ActionsFor(Context c) {
    switch (c) {
    case Context::Gameplay: return kGameplayActions;
    case Context::Map: return kMapActions;
    default: return kMenuActions;
    }
}

struct Binding {
    std::array<std::string, kMaxBinds> pad;
    std::array<std::string, kMaxBinds> key;
};

std::mutex g_mutex; // bindings are read on the guest thread, edited from the overlay
std::array<std::vector<Binding>, 3> g_bindings;
bool g_loaded = false;

// A pending "press a button" from the menu.
struct Capture {
    bool active = false;
    Context context{};
    size_t action = 0;
    size_t slot = 0;
    bool key = false;
} g_capture;

std::filesystem::path SavePath() { return RuntimeConfigFile::ApplicationDataDirectory() / "nsmbw_controls.ini"; }

void SetDefaults(Context c) {
    const auto& defs = ActionsFor(c);
    auto& b = g_bindings[size_t(c)];
    b.assign(defs.size(), Binding{});
    for (size_t i = 0; i < defs.size(); ++i) {
        for (size_t s = 0; s < kMaxBinds; ++s) {
            b[i].pad[s] = defs[i].pad[s] ? defs[i].pad[s] : "";
            b[i].key[s] = defs[i].key[s] ? defs[i].key[s] : "";
        }
    }
}

std::string Join(const std::array<std::string, kMaxBinds>& v) {
    std::string out;
    for (const auto& s : v) {
        if (s.empty()) continue;
        if (!out.empty()) out += ',';
        out += s;
    }
    return out;
}

void Save() {
    std::ofstream f(SavePath());
    if (!f) return;
    f << "# NSMBW controls, edited from the F10 menu. <context>.<action>.pad / .key = up to two names.\n";
    for (Context c : {Context::Gameplay, Context::Map, Context::Menu}) {
        const auto& defs = ActionsFor(c);
        for (size_t i = 0; i < defs.size(); ++i) {
            const auto& b = g_bindings[size_t(c)][i];
            f << ContextName(c) << '.' << defs[i].id << ".pad = " << Join(b.pad) << '\n';
            f << ContextName(c) << '.' << defs[i].id << ".key = " << Join(b.key) << '\n';
        }
    }
}

void LoadLocked() {
    if (g_loaded) return;
    g_loaded = true;
    for (Context c : {Context::Gameplay, Context::Map, Context::Menu}) SetDefaults(c);
    std::ifstream f(SavePath());
    std::string line;
    while (f && std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            return s;
        };
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        for (Context c : {Context::Gameplay, Context::Map, Context::Menu}) {
            const auto& defs = ActionsFor(c);
            for (size_t i = 0; i < defs.size(); ++i) {
                const std::string base = std::string(ContextName(c)) + '.' + defs[i].id;
                const bool isPad = key == base + ".pad", isKey = key == base + ".key";
                if (!isPad && !isKey) continue;
                std::array<std::string, kMaxBinds> list{};
                size_t n = 0, start = 0;
                while (n < kMaxBinds && start <= value.size()) {
                    const size_t comma = value.find(',', start);
                    const std::string item = trim(value.substr(start, comma - start));
                    if (!item.empty()) list[n++] = item;
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
                (isPad ? g_bindings[size_t(c)][i].pad : g_bindings[size_t(c)][i].key) = list;
            }
        }
    }
}

// ---- Reading -----------------------------------------------------------------------------------

bool PadControlDown(SDL_Gamepad* pad, const PadControl& c) {
    if (c.kind == PadKind::Button) return SDL_GetGamepadButton(pad, SDL_GamepadButton(c.id));
    const int v = SDL_GetGamepadAxis(pad, SDL_GamepadAxis(c.id));
    const bool trigger = c.id == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || c.id == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    const int threshold = trigger ? kTriggerThreshold : kStickThreshold;
    return c.kind == PadKind::AxisPositive ? v >= threshold : v <= -threshold;
}

// The pad assigned to port 0 in the overlay; with none assigned, every connected pad.
std::vector<SDL_Gamepad*> ActivePads() {
    std::vector<SDL_Gamepad*> pads;
    const s32 index = PADGetIndexForPort(0);
    if (index >= 0) {
        if (SDL_Gamepad* p = PADGetSDLGamepadForIndex(static_cast<u32>(index))) {
            pads.push_back(p);
            return pads;
        }
    }
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) {
            if (SDL_Gamepad* p = SDL_GetGamepadFromID(ids[i])) pads.push_back(p);
        }
        SDL_free(ids);
    }
    return pads;
}

std::string GlyphNameForPad(const std::string& name) {
    if (name.rfind("dpad_", 0) == 0) return "dpad";
    if (name.rfind("lstick_", 0) == 0) return "lstick";
    if (name.rfind("rstick_", 0) == 0) return "rstick";
    return name;
}

std::string PadLabel(const std::string& name) {
    if (const PadControl* c = FindPad(name)) return c->label;
    return name;
}

} // namespace

const char* ContextName(Context context) noexcept {
    switch (context) {
    case Context::Gameplay: return "gameplay";
    case Context::Map: return "map";
    default: return "menu";
    }
}

Context CurrentContext() noexcept {
    uint32_t stop = 0, mode = 0;
    Memory::TryRead32(kGameStopAddr, stop);
    // NSMBW_LOG_CONTROLS: report game-stop word changes (verifies the pause flag address).
    static const bool log = std::getenv("NSMBW_LOG_CONTROLS") != nullptr;
    static uint32_t lastStop = 0xFFFFFFFFu;
    if (log && stop != lastStop) {
        std::fprintf(stderr, "[nsmbw][controls] game-stop word 0x%08X -> 0x%08X (scene %u)\n", lastStop, stop,
                     g_nsmbwCurrentSceneProfile);
        lastStop = stop;
    }
    const uint32_t profile = g_nsmbwCurrentSceneProfile;
    if (profile == kProfileWorldMap) return (stop & kGameStopPauseMask) ? Context::Menu : Context::Map;
    if (profile == kProfileStage) {
        Memory::TryRead32(kGameModeAddr, mode);
        if (mode == kGameModeTitle || mode == kGameModeTitleReplay) return Context::Menu;
        return (stop & kGameStopPauseMask) ? Context::Menu : Context::Gameplay;
    }
    return Context::Menu;
}

std::string GamepadGlyphFor(uint32_t wpadBit) {
    std::lock_guard lock(g_mutex);
    LoadLocked();
    const Context c = CurrentContext();
    const auto& defs = ActionsFor(c);
    for (size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].wpad != wpadBit) continue;
        for (const auto& p : g_bindings[size_t(c)][i].pad) {
            if (!p.empty()) return GlyphNameForPad(p);
        }
    }
    return "";
}

std::string KeyGlyphFor(uint32_t wpadBit) {
    std::lock_guard lock(g_mutex);
    LoadLocked();
    const Context c = CurrentContext();
    const auto& defs = ActionsFor(c);
    for (size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].wpad != wpadBit) continue;
        for (const auto& k : g_bindings[size_t(c)][i].key) {
            if (k.empty()) continue;
            // A d-pad prompt bound to an arrow key shows the whole arrow cluster.
            if (k == "up" || k == "down" || k == "left" || k == "right") return "arrows";
            return k;
        }
    }
    return "";
}

void HandleEvents(const AuroraEvent* events) noexcept {
    std::lock_guard lock(g_mutex);
    if (!g_capture.active) return;
    for (const AuroraEvent* ev = events; ev != nullptr && ev->type != AURORA_NONE; ++ev) {
        if (ev->type != AURORA_SDL_EVENT) continue;
        const SDL_Event& e = ev->sdl;
        std::string captured;
        if (g_capture.key && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode == SDL_SCANCODE_F10) continue;
            captured = e.key.scancode == SDL_SCANCODE_ESCAPE && g_capture.slot > 0 ? "" : KeyToName(e.key.scancode);
        } else if (!g_capture.key && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            for (const auto& c : kPadControls) {
                if (c.kind == PadKind::Button && c.id == e.gbutton.button) captured = c.name;
            }
        } else if (!g_capture.key && e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                   (e.gaxis.value > 20000 || e.gaxis.value < -20000)) {
            for (const auto& c : kPadControls) {
                if (c.kind == PadKind::Button || c.id != e.gaxis.axis) continue;
                if ((c.kind == PadKind::AxisPositive) == (e.gaxis.value > 0)) captured = c.name;
            }
        } else {
            continue;
        }
        auto& b = g_bindings[size_t(g_capture.context)][g_capture.action];
        (g_capture.key ? b.key : b.pad)[g_capture.slot] = captured;
        g_capture.active = false;
        Save();
        return;
    }
}

void DrawMenu() noexcept {
    std::lock_guard lock(g_mutex);
    LoadLocked();
    if (!ImGui::BeginMenu("NSMBW controls")) return;
    ImGui::TextDisabled("Current: %s. Click a slot, then press the button or key.", ContextName(CurrentContext()));
    // Tabs rather than a third menu level: a nested popup opened over its parent, and the parent
    // kept the mouse wherever they overlapped, so the top rows could not be clicked.
    if (!ImGui::BeginTabBar("contexts")) {
        ImGui::EndMenu();
        return;
    }
    for (Context c : {Context::Gameplay, Context::Map, Context::Menu}) {
        const char* title = c == Context::Gameplay ? "In a course" : c == Context::Map ? "World map" : "Menus";
        if (!ImGui::BeginTabItem(title)) continue;
        const auto& defs = ActionsFor(c);
        if (ImGui::BeginTable("binds", 5, ImGuiTableFlags_SizingFixedFit)) {
            for (size_t i = 0; i < defs.size(); ++i) {
                auto& b = g_bindings[size_t(c)][i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(defs[i].label);
                for (int col = 0; col < 4; ++col) {
                    const bool key = col >= 2;
                    const size_t slot = size_t(col % 2);
                    const std::string& v = (key ? b.key : b.pad)[slot];
                    const bool waiting = g_capture.active && g_capture.context == c && g_capture.action == i &&
                                         g_capture.key == key && g_capture.slot == slot;
                    std::string text = waiting ? (key ? "press a key..." : "press a button...")
                                               : v.empty() ? "-" : key ? v : PadLabel(v);
                    text += "##" + std::to_string(i) + "_" + std::to_string(col);
                    ImGui::TableNextColumn();
                    if (ImGui::Button(text.c_str())) {
                        g_capture = Capture{true, c, i, slot, key};
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { // right-click clears a slot
                        (key ? b.key : b.pad)[slot].clear();
                        g_capture.active = false;
                        Save();
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Right-click a slot to clear it.");
        if (ImGui::Button("Reset to defaults")) {
            SetDefaults(c);
            g_capture.active = false;
            Save();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::EndMenu();
}

} // namespace nsmbw_controls

extern "C" uint32_t NsmbwControlsReadWpad() {
    using namespace nsmbw_controls;
    if (settings_overlay::InputBlocked()) return 0;
    std::lock_guard lock(g_mutex);
    LoadLocked();
    if (g_capture.active) return 0; // a rebind press must not also reach the game
    const Context c = CurrentContext();
    const auto& defs = ActionsFor(c);
    const auto& binds = g_bindings[size_t(c)];
    const auto pads = ActivePads();
    int numKeys = 0;
    const bool* keys = SDL_GetKeyboardState(&numKeys);

    uint32_t out = 0;
    for (size_t i = 0; i < defs.size(); ++i) {
        bool down = false;
        for (const auto& name : binds[i].pad) {
            const PadControl* control = name.empty() ? nullptr : FindPad(name);
            for (SDL_Gamepad* p : pads) {
                if (control != nullptr && PadControlDown(p, *control)) down = true;
            }
        }
        for (const auto& name : binds[i].key) {
            const SDL_Scancode code = name.empty() ? SDL_SCANCODE_UNKNOWN : NameToKey(name);
            if (code != SDL_SCANCODE_UNKNOWN && keys != nullptr && code < numKeys && keys[code]) down = true;
        }
        if (down) out |= defs[i].wpad;
    }
    return out;
}

// How far the remote is tilted, -1 (fully left) .. +1 (fully right), from the course context's
// tilt_left / tilt_right bindings; 0 outside courses (nothing on the map or in menus reads tilt).
// A key or button tilts fully; a stick or trigger tilts in proportion past a dead zone, so half
// a stick push is half a tilt. The KPAD override turns this into KPADStatus.acc.z.
extern "C" float NsmbwControlsReadTilt() {
    using namespace nsmbw_controls;
    if (settings_overlay::InputBlocked()) return 0.0f;
    std::lock_guard lock(g_mutex);
    LoadLocked();
    if (g_capture.active || CurrentContext() != Context::Gameplay) return 0.0f;
    const auto& defs = ActionsFor(Context::Gameplay);
    const auto& binds = g_bindings[size_t(Context::Gameplay)];
    const auto pads = ActivePads();
    int numKeys = 0;
    const bool* keys = SDL_GetKeyboardState(&numKeys);

    constexpr float kDeadZone = 8000.0f; // about 25 % of the axis, a common stick dead zone
    auto amount = [&](const Binding& b) {
        float best = 0.0f;
        for (const auto& name : b.key) {
            const SDL_Scancode code = name.empty() ? SDL_SCANCODE_UNKNOWN : NameToKey(name);
            if (code != SDL_SCANCODE_UNKNOWN && keys != nullptr && code < numKeys && keys[code]) best = 1.0f;
        }
        for (const auto& name : b.pad) {
            const PadControl* control = name.empty() ? nullptr : FindPad(name);
            if (control == nullptr) continue;
            for (SDL_Gamepad* p : pads) {
                if (control->kind == PadKind::Button) {
                    if (SDL_GetGamepadButton(p, SDL_GamepadButton(control->id))) best = 1.0f;
                    continue;
                }
                float v = float(SDL_GetGamepadAxis(p, SDL_GamepadAxis(control->id)));
                if (control->kind == PadKind::AxisNegative) v = -v;
                const float a = (v - kDeadZone) / (32767.0f - kDeadZone);
                if (a > best) best = a > 1.0f ? 1.0f : a;
            }
        }
        return best;
    };
    float left = 0.0f, right = 0.0f;
    for (size_t i = 0; i < defs.size(); ++i) {
        if (std::strcmp(defs[i].id, "tilt_left") == 0) left = amount(binds[i]);
        if (std::strcmp(defs[i].id, "tilt_right") == 0) right = amount(binds[i]);
    }
    return right - left;
}

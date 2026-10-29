// See nsmbw_options_menu.h. Layout follows the agreed mockup (artifact "NSMBW Menu Mockups",
// 2026-10-26): the pause window grown to 588x334 units, title in the Mario font, four tabs as pause
// buttons, up to five rows, a yellow note when one matters (a restart, no controller), and the
// Select / Back prompts with the player's own button glyphs.
#include "nsmbw_options_menu.h"

#include "nsmbw_button_glyphs.h"
#include "nsmbw_controls.h"
#include "nsmbw_menu_art.h"
#include "nsmbw_menu_draw.h"

#include "memory.h"
#include "runtime_config.h"
#include "settings_overlay.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace nsmbw_options_menu {
namespace {
namespace art = nsmbw_menu_art;
namespace draw = nsmbw_menu_draw;
namespace settings = settings_overlay::settings;
using draw::Align;
using draw::Rgb;

constexpr draw::Color kNumberTop = Rgb(255, 255, 0), kNumberBottom = Rgb(20, 220, 0); // T_worldNum_00
constexpr std::array<const char*, 4> kTabs = {"Graphics", "Audio", "Controls", "Advanced"};
constexpr std::array<float, 5> kRowY = {50, 20, -10, -40, -70};
constexpr int kControlsTab = 2;

enum class Kind { Picker, Toggle, Slider, Player };

struct Row {
    std::string label;
    Kind kind = Kind::Picker;
    std::vector<std::string> options; // Picker: shown values
    int value = 0;                    // Picker: option index; Toggle: 0 / 1; Slider: percent
    std::function<void(int)> set;     // applies a new value; empty = read only
    std::function<bool(int)> allowed; // Picker: options that may be chosen (all when empty)
    std::string note;                 // shown under the rows when it matters (a restart, no controller)
    bool dim = false; // Player: no controller
};

struct State {
    bool open = false;      // drawn (also while closing)
    bool closing = false;   // playing the window's out animation; input is ignored
    float openedAt = 0;     // draw::Now() when the window (or a new screen) started coming in
    float closingAt = 0;
    bool fromPause = false; // opened by the pause window's Options button: hand back to it on close
    bool pressPending = false; // Select hit a button: play its press animation on the next draw
    // An action that waits for the press animation to finish (opening the bindings screen).
    std::function<void()> afterPress;
    float afterPressAt = 0;
    int tab = 0;
    std::array<int, kTabs.size()> focus{};
    bool rebindFocused = false; // Controls: cursor on Rebind rather than the controller
    // After closing, input stays blocked until the key or button that closed the menu is released,
    // so the press can't reach the game (Esc is also "pause" in a course).
    SDL_Scancode heldKey = SDL_SCANCODE_UNKNOWN;
    SDL_JoystickID heldPad = 0;
    int heldButton = -1;
    std::array<int, 2> stick{}; // last left-stick direction (x, y), for edge detection
} g;

std::vector<Row> BuildRows(int tab) {
    std::vector<Row> rows;
    switch (tab) {
    case 0: {
        rows.push_back({"Display mode", Kind::Picker, {"Windowed", "Borderless", "Fullscreen"}, settings::DisplayMode(),
                        [](int v) { settings::SetDisplayMode(v); }});
        Row res{"Resolution", Kind::Picker};
        for (int i = 0; i < settings::ResolutionCount(); ++i) res.options.push_back(settings::ResolutionLabel(i));
        res.value = settings::ResolutionIndex();
        if (res.value < 0) {
            res.options.push_back("Custom");
            res.value = int(res.options.size()) - 1;
        }
        res.set = [](int v) { settings::SetResolutionIndex(v); };
        res.allowed = [](int v) { return settings::ResolutionAllowed(v); };
        if (settings::FrameInterpolation() != 0) res.note = "6x and 8x need frame interpolation off";
        rows.push_back(std::move(res));
        rows.push_back({"Frame interpolation", Kind::Picker, {"Off", "120 fps", "180 fps"}, settings::FrameInterpolation(),
                        [](int v) { settings::SetFrameInterpolation(v); }});
        rows.push_back({"HD texture pack", Kind::Toggle, {}, settings::HdTextures() ? 1 : 0,
                        [](int v) { settings::SetHdTextures(v != 0); }, {},
                        settings::HdTexturesNeedRestart() ? "Applies after a restart" : ""});
        rows.push_back({"Bloom", Kind::Toggle, {}, settings::Bloom() ? 1 : 0, [](int v) { settings::SetBloom(v != 0); }, {},
                        "Applies in the next level or map"});
        break;
    }
    case 1: {
        using V = settings_overlay::settings::Volume;
        const auto slider = [&](const char* label, V v) {
            rows.push_back({label, Kind::Slider, {}, settings::GetVolume(v), [v](int p) { settings::SetVolume(v, p); }});
        };
        slider("Master", V::Master);
        slider("Music", V::Music);
        slider("Sound effects", V::SoundEffects);
        // No Voices slider: NSMBW plays the characters' voices on the same sound players as the
        // effects, so they can't be set apart (nsmbw_sound_volumes.cpp).
        slider("Menu sounds", V::Ui);
        break;
    }
    case kControlsTab: {
        for (uint32_t p = 0; p < 4; ++p) {
            Row r{"Player " + std::to_string(p + 1), Kind::Player};
            const auto choices = nsmbw_controls::ControllerChoices(p);
            std::vector<std::string> ids;
            for (const auto& choice : choices) {
                r.options.push_back(choice.label);
                ids.push_back(choice.id);
            }
            r.value = nsmbw_controls::ControllerChoiceIndex(p);
            r.set = [p, ids](int v) { nsmbw_controls::SetControllerChoice(p, ids[size_t(v)]); };
            const std::string name = nsmbw_controls::PlayerControllerName(p);
            r.dim = name.empty();
            r.note = name.empty() ? "No controller for this player" : "Using: " + name;
            rows.push_back(std::move(r));
        }
        break;
    }
    default: {
        rows.push_back({"Show FPS", Kind::Toggle, {}, settings::ShowFps() ? 1 : 0, [](int v) { settings::SetShowFps(v != 0); }});
        rows.push_back({"Skip draws while compiling", Kind::Toggle, {}, settings::SkipUnreadyPipelines() ? 1 : 0,
                        [](int v) { settings::SetSkipUnreadyPipelines(v != 0); }});
        rows.push_back({"Copy filter", Kind::Toggle, {}, settings::CopyFilterDisabled() ? 0 : 1,
                        [](int v) { settings::SetCopyFilterDisabled(v == 0); }});
        rows.push_back({"Audio mixing thread", Kind::Toggle, {}, settings::AudioMixWorker() ? 1 : 0,
                        [](int v) { settings::SetAudioMixWorker(v != 0); }});
        // No "Mute music for other media": its Windows media-session monitor is not built for
        // NSMBW (nsmbw_runtime_shims.cpp), so the switch would do nothing.
        break;
    }
    }
    if (rows.size() > kRowY.size()) rows.resize(kRowY.size());
    return rows;
}

// The game's own stop word (dGameCom game-stop flags, read by isGameStop at [r13 - 22360] with
// r13 = 0x8042F980; see nsmbw_controls.cpp) and its GAME_STOP_HOME_MENU bit (d_game_com.hpp,
// BIT_FLAG(3)): what the Wii HOME Menu set. While any bit is set, dActor_c / dMdActor_c / world-map
// actors and the score manager skip their update (d_actor.cpp:108, d_md_actor.cpp:34, d_wm_actor.cpp:34,
// d_SmallScoreManager.cpp:71) and the frozen scene keeps drawing, which is exactly the pause wanted
// under this menu. Written on the game thread (the overlay runs inside the VI retrace), so it can't
// race the game's own updates of the word.
constexpr uint32_t kGameStopAddr = 0x8042A228u;
constexpr uint32_t kGameStopHomeMenu = 0x8u;

// That flag alone only stops what checks it: actors stopped, but the camera, effects and the
// scene's own logic kept running (developer report, 2026-10-28). The framework has a full stop:
// fManager_c::mainLoop (0x80162F50, f_manager.cpp) skips each of its phases whose bit is set in the
// static fManager_c::m_StopProcInf - read at [r13 - 21232] = 0x8042A690. Setting the execute bit
// stops every process's update while the draw phase keeps registering, so the frozen frame keeps
// drawing; create and delete stop too, so nothing spawns, disappears or changes scene meanwhile.
// Bits: CONNECT 0x1, CREATE 0x2, EXECUTE 0x4, DELETE 0x8, DRAW 0x10 (GET_PROC_FLAG(LOOP_PROC_e)).
// (The real HOME Menu skips mainLoop entirely, d_system.cpp, but it also skips drawing and shows
// its own picture; skipping mainLoop here would leave nothing drawn behind this menu.)
constexpr uint32_t kStopProcInfAddr = 0x8042A690u;
constexpr uint32_t kProcStopBits = 0x2u | 0x4u | 0x8u;
uint32_t g_procBitsAdded = 0; // only the bits this menu set, so the game's own are never cleared

void SetGameStopped(bool stopped) {
    uint32_t word = 0;
    if (Memory::TryRead32(kGameStopAddr, word)) {
        const uint32_t next = stopped ? word | kGameStopHomeMenu : word & ~kGameStopHomeMenu;
        if (next != word) Memory::TryWrite32(kGameStopAddr, next);
    }
    uint32_t proc = 0;
    if (Memory::TryRead32(kStopProcInfAddr, proc)) {
        if (stopped) {
            g_procBitsAdded |= kProcStopBits & ~proc;
            proc |= kProcStopBits;
        } else {
            proc &= ~g_procBitsAdded;
            g_procBitsAdded = 0;
        }
        Memory::TryWrite32(kStopProcInfAddr, proc);
    }
    // Sound runs on its own timing (the AX callbacks), not in mainLoop, so it keeps playing under
    // the menu - as the developer wants (2026-10-28).
}

// ---- Sounds ---------------------------------------------------------------------------------------
// The game's own system sounds (NSMBW-Decomp constants/sound_list.h; SE_SYS_CURSOR = 120 matches
// the immediate Pausewindow_c::executeState_PauseDisp passes to startSystemSe at 0x8015B118).
// They have to start on the game thread, so they wait here until the KPAD override takes them
// (NsmbwOptionsMenuTakeSound, nsmbw_kpad_overrides.cpp).
enum Sound : int32_t { kSndCursor = 120, kSndDecide = 121, kSndBack = 122, kSndInvalid = 123, kSndPause = 124 };
std::mutex g_soundMutex;
std::vector<int32_t> g_sounds;

void Play(Sound sound) {
    std::lock_guard lock(g_soundMutex);
    if (g_sounds.size() < 8) g_sounds.push_back(sound); // a burst of key repeats needs no more
}

// NSMBW_LOG_OPTIONS_MENU=1: one line per open / close, saying what did it.
void LogMenu(const char* what, const char* why) {
    static const bool log = std::getenv("NSMBW_LOG_OPTIONS_MENU") != nullptr;
    if (log) std::fprintf(stderr, "[nsmbw][menu] %s: %s\n", what, why);
}

// ---- Screens and state --------------------------------------------------------------------------

enum class Screen { Options, Bindings };
Screen g_screen = Screen::Options;

// The bindings screen ("Player N buttons"), opened by a player's Rebind button.
using nsmbw_controls::Context;
constexpr std::array<const char*, 3> kBindTabs = {"In a course", "World map", "Menus"};
constexpr std::array<Context, 3> kBindContexts = {Context::Gameplay, Context::Map, Context::Menu};
constexpr int kBindRowsShown = 6;
struct BindState {
    uint32_t player = 0;
    int tab = 0;
    int row = 0;
    int slot = 0;   // 0-1 controller, 2-3 keyboard (player 1 only: the keyboard is player 1's)
    int scroll = 0; // first row shown
    bool capturing = false;
} g_bind;

int BindSlots() { return g_bind.player == 0 ? 4 : 2; }

// Set when a menu opened from the pause window has finished closing; the pause input handler
// (nsmbw_pause_options.cpp) takes it on the game thread and plays the window's open animation.
std::atomic<bool> g_returnToPause{false};

// Starts the window's out animation (the pause window's outWindow); Draw finishes the close.
void Close(const char* why) {
    if (!g.open || g.closing) return;
    LogMenu("closing", why);
    if (g_bind.capturing) {
        nsmbw_controls::CancelCapture();
        g_bind.capturing = false;
    }
    g.closing = true;
    g.closingAt = draw::Now();
    g.afterPress = nullptr;
}

void FinishClose() {
    g.open = false;
    g.closing = false;
    g_screen = Screen::Options;
    SetGameStopped(false);
    if (g.fromPause) g_returnToPause = true;
    g.fromPause = false;
}

void Open(const char* why, bool fromPause) {
    LogMenu("opened", why);
    g.open = true;
    g.closing = false;
    g.openedAt = draw::Now();
    g.fromPause = fromPause;
    g.heldKey = SDL_SCANCODE_UNKNOWN;
    g.heldButton = -1;
    g.stick = {};
    SetGameStopped(true);
}

// A new screen comes in the way a new window does in the game.
void ShowScreen(Screen screen) {
    g_screen = screen;
    g.openedAt = draw::Now();
}

void OpenBindings(uint32_t player) {
    g_bind = BindState{};
    g_bind.player = player;
    ShowScreen(Screen::Bindings);
}

// ---- Commands -------------------------------------------------------------------------------------
// Keys and buttons map to these; each screen decides what they do.
enum class Cmd { Up, Down, Left, Right, Select, Back, TabPrev, TabNext, Clear, Reset, Exit };

// Left / right on an options row, and Select (dx = +1 on pickers, flip on toggles).
void Change(int dx, bool select) {
    std::vector<Row> rows = BuildRows(g.tab);
    if (rows.empty()) return;
    const int f = std::clamp(g.focus[size_t(g.tab)], 0, int(rows.size()) - 1);
    Row& r = rows[size_t(f)];
    switch (r.kind) {
    case Kind::Picker: {
        if (!r.set || r.options.empty()) return;
        const int n = int(r.options.size());
        int v = r.value;
        for (int tries = 0; tries < n; ++tries) { // wrap, skipping options that aren't allowed
            v = ((v + dx) % n + n) % n;
            if (!r.allowed || r.allowed(v)) {
                r.set(v);
                Play(kSndCursor);
                g.pressPending = select;
                return;
            }
        }
        return;
    }
    case Kind::Toggle:
        if (r.set) {
            r.set(r.value != 0 ? 0 : 1);
            Play(select ? kSndDecide : kSndCursor);
            g.pressPending = select;
        }
        return;
    case Kind::Slider:
        if (r.set && !select) {
            const int v = std::clamp(r.value + dx * 5, 0, 100);
            if (v != r.value) {
                r.set(v);
                Play(kSndCursor);
            }
        }
        return;
    case Kind::Player: {
        // On the controller: left / right choose, Select moves to Rebind. On Rebind: left moves
        // back, Select opens the bindings screen.
        if (g.rebindFocused) {
            if (select) {
                if (r.dim) {
                    Play(kSndInvalid);
                } else {
                    // The game's order: the button's press animation, then the next window.
                    Play(kSndDecide);
                    g.pressPending = true;
                    g.afterPress = [player = uint32_t(f)] { OpenBindings(player); };
                    g.afterPressAt = draw::Now() + 8;
                }
            } else if (dx < 0) {
                g.rebindFocused = false;
                Play(kSndCursor);
            }
        } else if (select) {
            g.rebindFocused = true;
            Play(kSndCursor);
        } else if (r.set && r.options.size() > 1) {
            const int n = int(r.options.size());
            r.set(((r.value + dx) % n + n) % n);
            Play(kSndCursor);
        }
        return;
    }
    }
}

void Move(int dy) {
    const int n = int(BuildRows(g.tab).size());
    if (n == 0) return;
    int& f = g.focus[size_t(g.tab)];
    f = ((f + dy) % n + n) % n;
    if (n > 1) Play(kSndCursor);
}

void SwitchTab(int d) {
    const int n = int(kTabs.size());
    g.tab = ((g.tab + d) % n + n) % n;
    g.rebindFocused = false;
    Play(kSndCursor);
}

// Returns true when the command closed the menu.
bool OptionsCommand(Cmd cmd, const char* why) {
    switch (cmd) {
    case Cmd::Up: Move(-1); break;
    case Cmd::Down: Move(1); break;
    case Cmd::Left: Change(-1, false); break;
    case Cmd::Right: Change(1, false); break;
    case Cmd::Select: Change(1, true); break;
    case Cmd::TabPrev: SwitchTab(-1); break;
    case Cmd::TabNext: SwitchTab(1); break;
    case Cmd::Back: case Cmd::Exit: Close(why); Play(kSndBack); return true;
    default: break;
    }
    return false;
}

bool BindingsCommand(Cmd cmd, const char* why) {
    const Context ctx = kBindContexts[size_t(g_bind.tab)];
    const int n = int(nsmbw_controls::Bindings(g_bind.player, ctx).size());
    switch (cmd) {
    case Cmd::Up: case Cmd::Down:
        if (n == 0) break;
        g_bind.row = ((g_bind.row + (cmd == Cmd::Down ? 1 : -1)) % n + n) % n;
        if (g_bind.row < g_bind.scroll) g_bind.scroll = g_bind.row;
        if (g_bind.row >= g_bind.scroll + kBindRowsShown) g_bind.scroll = g_bind.row - kBindRowsShown + 1;
        if (n > 1) Play(kSndCursor);
        break;
    case Cmd::Left: case Cmd::Right:
        g_bind.slot = ((g_bind.slot + (cmd == Cmd::Right ? 1 : -1)) % BindSlots() + BindSlots()) % BindSlots();
        Play(kSndCursor);
        break;
    case Cmd::Select:
        nsmbw_controls::StartCapture(g_bind.player, ctx, size_t(g_bind.row), g_bind.slot >= 2, size_t(g_bind.slot % 2));
        g_bind.capturing = nsmbw_controls::Capturing();
        Play(kSndDecide);
        break;
    case Cmd::Clear:
        nsmbw_controls::ClearBinding(g_bind.player, ctx, size_t(g_bind.row), g_bind.slot >= 2, size_t(g_bind.slot % 2));
        Play(kSndBack);
        break;
    case Cmd::Reset: nsmbw_controls::ResetBindings(g_bind.player, ctx); Play(kSndDecide); break;
    case Cmd::TabPrev: case Cmd::TabNext: {
        const int t = int(kBindTabs.size());
        g_bind.tab = ((g_bind.tab + (cmd == Cmd::TabNext ? 1 : -1)) % t + t) % t;
        g_bind.row = g_bind.scroll = 0;
        Play(kSndCursor);
        break;
    }
    case Cmd::Back: ShowScreen(Screen::Options); Play(kSndBack); break;
    case Cmd::Exit: Close(why); Play(kSndBack); return true;
    }
    return false;
}

bool Dispatch(Cmd cmd, const char* why) {
    static constexpr const char* kNames[] = {"up", "down", "left", "right", "select", "back",
                                             "tab-", "tab+", "clear", "reset", "exit"};
    LogMenu(kNames[int(cmd)], why);
    return g_screen == Screen::Options ? OptionsCommand(cmd, why) : BindingsCommand(cmd, why);
}

void OnKey(const SDL_KeyboardEvent& k) {
    Cmd cmd;
    bool once = true; // ignores key repeat (movement repeats while held)
    switch (k.scancode) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_W: cmd = Cmd::Up; once = false; break;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: cmd = Cmd::Down; once = false; break;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: cmd = Cmd::Left; once = false; break;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: cmd = Cmd::Right; once = false; break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_Z: case SDL_SCANCODE_SPACE: cmd = Cmd::Select; break;
    case SDL_SCANCODE_Q: case SDL_SCANCODE_PAGEUP: cmd = Cmd::TabPrev; break;
    case SDL_SCANCODE_E: case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_TAB: cmd = Cmd::TabNext; break;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_X: cmd = Cmd::Back; break;
    case SDL_SCANCODE_DELETE: cmd = Cmd::Clear; break;
    case SDL_SCANCODE_R: cmd = Cmd::Reset; break;
    default: return;
    }
    if (once && k.repeat) return;
    if (Dispatch(cmd, SDL_GetScancodeName(k.scancode))) g.heldKey = k.scancode;
}

void OnButton(const SDL_GamepadButtonEvent& b) {
    Cmd cmd;
    switch (SDL_GamepadButton(b.button)) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: cmd = Cmd::Up; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: cmd = Cmd::Down; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: cmd = Cmd::Left; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: cmd = Cmd::Right; break;
    case SDL_GAMEPAD_BUTTON_SOUTH: cmd = Cmd::Select; break;
    case SDL_GAMEPAD_BUTTON_EAST: cmd = Cmd::Back; break;
    case SDL_GAMEPAD_BUTTON_WEST: cmd = Cmd::Clear; break;
    case SDL_GAMEPAD_BUTTON_NORTH: cmd = Cmd::Reset; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: cmd = Cmd::TabPrev; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: cmd = Cmd::TabNext; break;
    case SDL_GAMEPAD_BUTTON_START: cmd = Cmd::Exit; break;
    default: return;
    }
    if (Dispatch(cmd, SDL_GetGamepadStringForButton(SDL_GamepadButton(b.button)))) {
        g.heldPad = b.which;
        g.heldButton = b.button;
    }
}

void OnAxis(const SDL_GamepadAxisEvent& a) {
    if (a.axis != SDL_GAMEPAD_AXIS_LEFTX && a.axis != SDL_GAMEPAD_AXIS_LEFTY) return;
    // Past about 60% tilt counts as a press; it must return towards the centre before the next.
    const int dir = a.value > 20000 ? 1 : a.value < -20000 ? -1 : 0;
    int& last = g.stick[a.axis == SDL_GAMEPAD_AXIS_LEFTX ? 0 : 1];
    if (dir == last) return;
    last = dir;
    if (dir == 0) return;
    // SDL's y axis points down, like the rows.
    if (a.axis == SDL_GAMEPAD_AXIS_LEFTX) Dispatch(dir > 0 ? Cmd::Right : Cmd::Left, "left stick");
    else Dispatch(dir > 0 ? Cmd::Down : Cmd::Up, "left stick");
}

// The close key or button is up again (also checked by polling in Draw, in case its release event
// went to another window).
void ClearHeldIfReleased() {
    if (g.heldKey != SDL_SCANCODE_UNKNOWN) {
        int count = 0;
        const bool* keys = SDL_GetKeyboardState(&count);
        if (keys == nullptr || g.heldKey >= count || !keys[g.heldKey]) g.heldKey = SDL_SCANCODE_UNKNOWN;
    }
    if (g.heldButton >= 0) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(g.heldPad);
        if (pad == nullptr || !SDL_GetGamepadButton(pad, SDL_GamepadButton(g.heldButton))) g.heldButton = -1;
    }
}

bool IsOpen() noexcept { return g.open || g.heldKey != SDL_SCANCODE_UNKNOWN || g.heldButton >= 0; }

void SetOpen(bool open) noexcept {
    if (open == (g.open && !g.closing)) return;
    if (!open) {
        Close("F10");
        Play(kSndBack);
        return;
    }
    Play(kSndPause);
    Open("F10", false);
}

void HandleEvents(const AuroraEvent* events) noexcept {
    if (g_bind.capturing) {
        // nsmbw_controls sees each batch first (the product event hook) and takes the press that
        // ends a capture; skip that batch here so the same press isn't also a menu command.
        if (!nsmbw_controls::Capturing()) {
            g_bind.capturing = false;
            Play(kSndDecide);
        }
        return;
    }
    for (const AuroraEvent* ev = events; ev != nullptr && ev->type != AURORA_NONE; ++ev) {
        if (ev->type != AURORA_SDL_EVENT) continue;
        const SDL_Event& e = ev->sdl;
        if (!g.open || g.closing || g.afterPress) {
            if (e.type == SDL_EVENT_KEY_UP && e.key.scancode == g.heldKey) g.heldKey = SDL_SCANCODE_UNKNOWN;
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && e.gbutton.which == g.heldPad && e.gbutton.button == g.heldButton) {
                g.heldButton = -1;
            }
            continue;
        }
        switch (e.type) {
        case SDL_EVENT_KEY_DOWN: OnKey(e.key); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: OnButton(e.gbutton); break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: OnAxis(e.gaxis); break;
        default: break;
        }
        if (g_bind.capturing) return; // the rest of the batch belongs to the capture
    }
}

// ---- Drawing --------------------------------------------------------------------------------------

struct Prompt {
    std::string word;
    std::string pad; // SDL position name of the controller button
    std::string key; // keyboard key name
};
// Select / Back show the game's own menu bindings (confirm = 2, back = 1), the keys its menus show.
Prompt MenuPrompt(const char* word, const char* action) {
    return {word, nsmbw_controls::MenuPromptGlyph(action, false), nsmbw_controls::MenuPromptGlyph(action, true)};
}

// The prompts' row: inside the window's bottom edge (the frame starts at -162).
constexpr float kPromptY = -136;

// Prompts at the bottom right, glyph then word, laid out from the right edge (last one first).
void DrawPrompts(const draw::Canvas& c, float y, std::initializer_list<Prompt> prompts) {
    float x = 262;
    for (auto it = std::rbegin(prompts); it != std::rend(prompts); ++it) {
        x -= c.Text(it->word, x, y, 17, {.align = Align::Right, .outline = true}) + 14;
        if (const ImTextureID tex = nsmbw_button_glyphs::PromptTexture(it->pad.c_str(), it->key.c_str());
            tex != ImTextureID{}) {
            c.Image(tex, x, y, 24);
        }
        x -= 22;
    }
}

void DrawRow(const draw::Canvas& c, const Row& r, float y, bool focused) {
    c.Label(r.label, -262, y, 22, Align::Left, false, focused ? draw::kWhite : Rgb(225, 235, 225));
    switch (r.kind) {
    case Kind::Picker: {
        const std::string& text = r.options.empty() ? std::string() : r.options[size_t(std::clamp(r.value, 0, int(r.options.size()) - 1))];
        const float cx = 150;
        if (focused && g.pressPending) c.Press(cx, y, 180);
        const ImVec2 size = c.Button(text, cx, y, 180, 21, focused, 17, false, true);
        if (focused) {
            if (r.set) {
                c.Mario("<", cx - size.x / 2 - 16, y, 26, kNumberTop, kNumberBottom);
                c.Mario(">", cx + size.x / 2 + 16, y, 26, kNumberTop, kNumberBottom);
            }
            c.Cursor(cx, y, size.x + (r.set ? 52 : 4), size.y + 4, 0.55f);
        }
        break;
    }
    case Kind::Player: {
        // The controller name with the player's Rebind button right against it (the picker's frame
        // edge, 126 + 157/2, meets Rebind's, 242 - 75/2).
        const bool onPick = focused && !g.rebindFocused, onRebind = focused && g.rebindFocused;
        const std::string& text = r.options.empty() ? std::string() : r.options[size_t(std::clamp(r.value, 0, int(r.options.size()) - 1))];
        const ImVec2 size = c.Button(text, 126, y, 150, 21, onPick, 17, false, true);
        if (onPick) c.Cursor(126, y, size.x + 4, size.y + 4, 0.55f);
        if (onRebind && g.pressPending) c.Press(242, y, 68);
        const ImVec2 rebind = c.Button("Rebind", 242, y, 68, 21, onRebind, 17, r.dim, true);
        if (onRebind) c.Cursor(242, y, rebind.x, rebind.y, 0.55f);
        break;
    }
    case Kind::Toggle:
        for (int j = 0; j < 2; ++j) {
            const bool chosen = (r.value != 0) == (j == 0);
            const float x = 106 + j * 90.0f;
            if (focused && chosen && g.pressPending) c.Press(x, y, 80);
            const ImVec2 size = c.Button(j == 0 ? "On" : "Off", x, y, 80, 21, chosen, 17, !chosen);
            if (focused && chosen) c.Cursor(x, y, size.x, size.y, 0.55f);
        }
        break;
    case Kind::Slider: {
        const float cx = 128, w = 190, h = 14, pct = float(std::clamp(r.value, 0, 100)), fw = w * pct / 100;
        c.Rect(cx, y, w, h, Rgb(0, 0, 0, 150), Rgb(0, 0, 0, 150));
        if (fw > 0) {
            c.Rect(cx - w / 2 + fw / 2, y, fw, h, focused ? Rgb(0, 150, 70) : Rgb(140, 220, 90),
                   focused ? Rgb(240, 255, 50) : Rgb(255, 250, 128));
        }
        c.Window4(cx, y, w + 5, h + 6, 8);
        c.Mario(std::to_string(r.value), 258, y, 26, kNumberTop, kNumberBottom);
        if (focused) c.Cursor(cx, y, w + 6, h + 10, 0.55f);
        break;
    }
    }
}

void DrawOptions(const draw::Canvas& c) {
    c.Mario("Options", 0, 132, 40);
    constexpr float span = 512, step = span / kTabs.size();
    for (size_t i = 0; i < kTabs.size(); ++i) {
        c.Button(kTabs[i], -span / 2 + step * (float(i) + 0.5f), 94, step - 12, 30, int(i) == g.tab, 19);
    }
    c.Rect(0, 70, 540, 1.5f, Rgb(255, 255, 255, 70), Rgb(255, 255, 255, 70));

    const std::vector<Row> rows = BuildRows(g.tab);
    int& focus = g.focus[size_t(g.tab)];
    focus = rows.empty() ? 0 : std::clamp(focus, 0, int(rows.size()) - 1);
    for (size_t i = 0; i < rows.size(); ++i) DrawRow(c, rows[i], kRowY[i], int(i) == focus);

    c.Rect(0, -98, 540, 1.5f, Rgb(255, 255, 255, 70), Rgb(255, 255, 255, 70));
    // No description lines (turned down as too long, 2026-10-29); only a note that matters, such as
    // a restart being needed.
    if (!rows.empty() && !rows[size_t(focus)].note.empty()) {
        c.Text(rows[size_t(focus)].note, -262, kPromptY, 16, {.white = Rgb(255, 255, 80), .align = Align::Left, .outline = true});
    }
    DrawPrompts(c, kPromptY, {MenuPrompt("Select", "confirm"), MenuPrompt("Back", "back")});
}

// One binding slot: the control's glyph, a dash when empty, or its name when there's no glyph.
void DrawSlot(const draw::Canvas& c, const std::string& name, bool key, float x, float y) {
    if (name.empty()) {
        c.Text("-", x, y, 17, {.white = Rgb(150, 160, 170), .outline = true});
    } else if (const ImTextureID tex = nsmbw_button_glyphs::ControlTexture(name.c_str(), key); tex != ImTextureID{}) {
        c.Image(tex, x, y, 22);
    } else {
        c.Text(name, x, y, 13, {.outline = true});
    }
}

void DrawBindings(const draw::Canvas& c) {
    const Context ctx = kBindContexts[size_t(g_bind.tab)];
    const std::vector<nsmbw_controls::BoundAction> rows = nsmbw_controls::Bindings(g_bind.player, ctx);
    const int n = int(rows.size());
    g_bind.row = n == 0 ? 0 : std::clamp(g_bind.row, 0, n - 1);
    g_bind.scroll = std::clamp(g_bind.scroll, 0, std::max(0, n - kBindRowsShown));
    g_bind.slot = std::clamp(g_bind.slot, 0, BindSlots() - 1);

    c.Mario("Player " + std::to_string(g_bind.player + 1) + " buttons", 0, 132, 34);
    constexpr float span = 450, step = span / kBindTabs.size();
    for (size_t i = 0; i < kBindTabs.size(); ++i) {
        c.Button(kBindTabs[i], -span / 2 + step * (float(i) + 0.5f), 94, step - 12, 30, int(i) == g_bind.tab, 19);
    }
    c.Rect(0, 70, 540, 1.5f, Rgb(255, 255, 255, 70), Rgb(255, 255, 255, 70));

    const draw::TextStyle header{.white = Rgb(200, 215, 230), .outline = true};
    c.Text("Controller", 70, 58, 15, header);
    if (g_bind.player == 0) c.Text("Keyboard", 205, 58, 15, header);
    if (n > kBindRowsShown) {
        draw::TextStyle count = header;
        count.align = Align::Right;
        c.Text(std::to_string(g_bind.scroll + 1) + "-" + std::to_string(std::min(n, g_bind.scroll + kBindRowsShown)) +
                   " of " + std::to_string(n),
               -150, 58, 15, count);
    }
    constexpr float kSlotX[4] = {45, 95, 180, 230};
    for (int k = 0; k < kBindRowsShown && g_bind.scroll + k < n; ++k) {
        const int i = g_bind.scroll + k;
        const float y = 38 - 24.0f * float(k);
        const bool focusedRow = i == g_bind.row;
        c.Label(rows[size_t(i)].label, -262, y, 20, Align::Left, false, focusedRow ? draw::kWhite : Rgb(215, 225, 215));
        for (int s = 0; s < BindSlots(); ++s) {
            const bool key = s >= 2;
            DrawSlot(c, key ? rows[size_t(i)].keys[s - 2] : rows[size_t(i)].pads[s], key, kSlotX[s], y);
            if (focusedRow && s == g_bind.slot) c.Cursor(kSlotX[s], y, 26, 22, 0.42f);
        }
    }

    c.Rect(0, -98, 540, 1.5f, Rgb(255, 255, 255, 70), Rgb(255, 255, 255, 70));
    DrawPrompts(c, kPromptY, {MenuPrompt("Change", "confirm"), {"Clear", "west", "delete"}, {"Reset", "north", "r"},
                              MenuPrompt("Back", "back")});

    if (g_bind.capturing && n > 0) {
        c.FillDisplay(Rgb(0, 0, 0, 110));
        c.PauseWindow(0, 0, 340, 124, 225);
        c.Label(g_bind.slot >= 2 ? "Press a key for" : "Press a button for", 0, 30, 20, Align::Center, false);
        c.Mario(rows[size_t(g_bind.row)].label, 0, 0, 30, kNumberTop, kNumberBottom);
        if (const ImTextureID esc = nsmbw_button_glyphs::ControlTexture("escape", true); esc != ImTextureID{}) {
            c.Image(esc, -82, -36, 22);
        }
        c.Text("Cancel", -66, -36, 16, {.align = Align::Left, .outline = true});
        if (const ImTextureID del = nsmbw_button_glyphs::ControlTexture("delete", true); del != ImTextureID{}) {
            c.Image(del, 22, -36, 22);
        }
        c.Text("Clear", 38, -36, 16, {.align = Align::Left, .outline = true});
    }
}

// Set on the game thread by the pause window's Options button (nsmbw_pause_options.cpp) once that
// window has played its press and out animations; opened here on the next frame. The game already
// played its own decide sound.
std::atomic<bool> g_openFromPause{false};

// First launch: the menu opens by itself once, at startup before the title screen, so a new player
// sees the settings before playing (developer request, 2026-10-29). "Once" is a marker file next
// to nsmbw_controls.ini, written when the menu opens. The game is held behind it like any other
// open (SetGameStopped): in a 2026-10-29 run the boot scene made no progress until the menu was
// closed, and the title loaded right after. No pause sound: the game's sound archive may not be loaded yet this
// early. Scripted test runs (NSMBW_AUTO_PRESS_SELFTEST) never get it, since their presses would
// land in the menu; NSMBW_FIRST_LAUNCH_OPTIONS=1 forces it.
void OpenOnFirstLaunch() {
    static bool done = false;
    static float firstFrame = -1;
    if (done) return;
    const auto marker = RuntimeConfigFile::ApplicationDataDirectory() / "options_menu_shown";
    static const bool forced = std::getenv("NSMBW_FIRST_LAUNCH_OPTIONS") != nullptr;
    if (!forced) {
        std::error_code ec;
        if (std::getenv("NSMBW_AUTO_PRESS_SELFTEST") != nullptr || std::filesystem::exists(marker, ec)) {
            done = true;
            return;
        }
    }
    // Half a second after the window's first frame, so the in-animation isn't lost to start-up hitches.
    const float now = draw::Now();
    if (firstFrame < 0) firstFrame = now;
    if (now - firstFrame < 30) return;
    done = true;
    std::error_code ec;
    std::filesystem::create_directories(marker.parent_path(), ec);
    std::ofstream(marker) << "The options menu opened by itself on this install's first launch.\n";
    Open("first launch", false);
}

void Draw() noexcept {
    if (!g.open && g_openFromPause.exchange(false)) Open("pause menu", true);
    if (!g.open) OpenOnFirstLaunch();
    if (!g.open) {
        ClearHeldIfReleased();
        return;
    }
    if (!art::Load()) { // logged once by Load; without the art there is nothing to draw with
        Close("menu art missing");
        FinishClose();
        return;
    }
    // Re-assert every frame: a scene change can reset the stop word while the menu is open.
    SetGameStopped(true);
    const float now = draw::Now();
    if (g.afterPress && now >= g.afterPressAt) {
        const auto action = std::move(g.afterPress);
        g.afterPress = nullptr;
        action();
    }
    // The pause window's own in / out scale; the dimming follows it.
    float zoom = draw::WindowIn(now - g.openedAt);
    if (g.closing) {
        const float t = now - g.closingAt;
        if (t >= draw::kWindowFrames) {
            FinishClose();
            return;
        }
        zoom = std::min(zoom, draw::WindowOut(t));
    }
    const float fade = g.closing ? zoom : std::min(1.0f, (now - g.openedAt) / draw::kWindowFrames);
    const draw::Canvas c(/*shrinkOthers=*/true, zoom); // rows and tabs are too close for grown buttons
    if (g_screen == Screen::Options || g.closing) c.FillDisplay(Rgb(0, 0, 0, uint8_t(128 * std::clamp(fade, 0.0f, 1.0f))));
    else c.FillDisplay(Rgb(0, 0, 0, 128));
    c.PauseWindow(0, -2, 588, 334);
    if (g_screen == Screen::Bindings) DrawBindings(c);
    else DrawOptions(c);
    g.pressPending = false;
}

} // namespace

int32_t TakeSound() noexcept {
    std::lock_guard lock(g_soundMutex);
    if (g_sounds.empty()) return -1;
    const int32_t sound = g_sounds.front();
    g_sounds.erase(g_sounds.begin());
    return sound;
}

void Register() noexcept {
    settings_overlay::SetProductMenu({&IsOpen, &SetOpen, &HandleEvents, &Draw});
}

} // namespace nsmbw_options_menu

extern "C" int32_t NsmbwOptionsMenuTakeSound() { return nsmbw_options_menu::TakeSound(); }
extern "C" void NsmbwOptionsMenuRequestOpen() { nsmbw_options_menu::g_openFromPause = true; }
extern "C" bool NsmbwOptionsMenuTakeReturnToPause() { return nsmbw_options_menu::g_returnToPause.exchange(false); }

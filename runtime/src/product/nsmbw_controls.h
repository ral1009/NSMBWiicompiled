#pragma once

#include <aurora/event.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Action-based controls for NSMBW: physical buttons are bound to actions ("Jump", "Confirm",
// "Look around") per context (gameplay / world map / menus), and each action produces the Wii
// Remote button the game expects in that context. See nsmbw_controls.cpp.
namespace nsmbw_controls {

enum class Context { Gameplay, Map, Menu };

Context CurrentContext() noexcept;
const char* ContextName(Context context) noexcept;

// First physical control bound to whatever produces `wpadBit` in the current context, as a
// button-glyph name: gamepad controls by SDL position ("south", "left_shoulder", "dpad",
// "lstick", ...), keyboard keys by key name ("z", "enter", "arrows"). Empty when unbound.
std::string GamepadGlyphFor(uint32_t wpadBit);
std::string KeyGlyphFor(uint32_t wpadBit);
// The same for player 1's menu action `action` ("confirm", "back"), whatever the context: the
// options menu's prompts, so they show the keys the game's own menus show.
std::string MenuPromptGlyph(const char* action, bool key);

// What drives player `chan` (0..3) right now, for the options menu: the gamepad's name,
// "Keyboard", "Keyboard + <pad>", or empty when nothing does.
std::string PlayerControllerName(uint32_t chan);

// Which controller drives each player, chosen in the options menu and saved in
// nsmbw_controls.ini. "auto" (the default) takes the next free gamepad, and player 1 on auto
// also gets the keyboard unless another player chose it; "keyboard"; "none"; or a specific
// gamepad, "pad:<SDL GUID>:<name>".
struct ControllerChoice {
    std::string id;    // the saved value
    std::string label; // "Automatic", "Keyboard", the pad's name, "None"
};
// Automatic, Keyboard, every connected gamepad (one entry per model), the chosen pad when it is
// disconnected, None.
std::vector<ControllerChoice> ControllerChoices(uint32_t chan);
int ControllerChoiceIndex(uint32_t chan); // into ControllerChoices(chan)
void SetControllerChoice(uint32_t chan, const std::string& id);

// The options menu's bindings screen. Bindings are per player (0..3); only player 1 reads the
// keyboard, but every player has key slots in the file.
constexpr size_t kBindSlots = 2; // two controls per action; either one triggers it
struct BoundAction {
    std::string label;                      // "Jump", "Run / fireball"
    std::string pads[kBindSlots];           // glyph names: "south", "left_shoulder", "dpad", "lstick"; "" unbound
    std::string keys[kBindSlots];           // key names: "z", "space", "up"; "" unbound
};
std::vector<BoundAction> Bindings(uint32_t player, Context context);
// Waits for the next button (key == false) or key press and puts it in that slot. While waiting,
// Esc cancels and Delete clears the slot, and no input reaches the game.
void StartCapture(uint32_t player, Context context, size_t action, bool key, size_t slot);
bool Capturing() noexcept;
void CancelCapture() noexcept;
void ClearBinding(uint32_t player, Context context, size_t action, bool key, size_t slot);
void ResetBindings(uint32_t player, Context context);

// Rebind capture (a pending "press a button" from the menu) consumes input events here.
void HandleEvents(const AuroraEvent* events) noexcept;
// "NSMBW controls" submenu in the F10 Controller settings menu.
void DrawMenu() noexcept;

} // namespace nsmbw_controls

// Called once per VI frame by the KPAD override (nsmbw_kpad_overrides.cpp): WPAD hold bits for
// this frame, plus bit 31 while a "shake" action is held. Per player (Wii Remote channel 0..3):
// each player reads the device(s) its controller choice resolves to (ControllerChoices).
extern "C" uint32_t NsmbwControlsReadWpad(uint32_t chan);
// How many players have a controller (1..4); channels below this report a connected remote.
extern "C" uint32_t NsmbwControlsPlayerCount();
// Also once per VI frame: remote tilt from the tilt_left / tilt_right actions, -1 (left) .. +1
// (right), proportional for sticks; 0 outside courses.
extern "C" float NsmbwControlsReadTilt(uint32_t chan);

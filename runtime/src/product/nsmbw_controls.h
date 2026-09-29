#pragma once

#include <aurora/event.h>

#include <cstdint>
#include <string>

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

// Rebind capture (a pending "press a button" from the menu) consumes input events here.
void HandleEvents(const AuroraEvent* events) noexcept;
// "NSMBW controls" submenu in the F10 Controller settings menu.
void DrawMenu() noexcept;

} // namespace nsmbw_controls

// Called once per VI frame by the KPAD override (nsmbw_kpad_overrides.cpp): WPAD hold bits for
// this frame, plus bit 31 while a "shake" action is held.
extern "C" uint32_t NsmbwControlsReadWpad();
// Also once per VI frame: remote tilt from the tilt_left / tilt_right actions, -1 (left) .. +1
// (right), proportional for sticks; 0 outside courses.
extern "C" float NsmbwControlsReadTilt();

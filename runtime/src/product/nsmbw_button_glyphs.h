#pragma once

#include <aurora/event.h>

// Controller button glyphs for NSMBW's in-text button icons ("Press (2) to Start").
// See nsmbw_button_glyphs.cpp for how the icons are found and replaced.
namespace nsmbw_button_glyphs {
// Tracks the last-used input device from SDL events and re-points the icons when the device or
// the controller bindings change. Called with every event batch.
void HandleEvents(const AuroraEvent* events) noexcept;
// "Button icons" choice in the F10 Controller settings menu (saved as [controller] button_icons).
void DrawSettingsMenu() noexcept;
}

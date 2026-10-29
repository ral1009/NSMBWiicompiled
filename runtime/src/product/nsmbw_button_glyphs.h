#pragma once

#include <aurora/event.h>
#include <imgui.h>

// Controller button glyphs for NSMBW's in-text button icons ("Press (2) to Start").
// See nsmbw_button_glyphs.cpp for how the icons are found and replaced.
namespace nsmbw_button_glyphs {
// Tracks the last-used input device from SDL events and re-points the icons when the device or
// the controller bindings change. Called with every event batch.
void HandleEvents(const AuroraEvent* events) noexcept;
// "Button icons" choice in the F10 Controller settings menu (saved as [controller] button_icons).
void DrawSettingsMenu() noexcept;
// A prompt glyph in the icon set the player is using: the pad control's glyph (SDL position names,
// "south", "left_shoulder") on a controller, the key's on the keyboard ("enter"). Null texture if
// the set has no glyph for it. Call from the ImGui frame (it may upload a texture).
ImTextureID PromptTexture(const char* padControl, const char* keyControl) noexcept;
// One control's glyph: a key's keycap (key == true, "z", "space", "up"), or a pad control in the
// player's pad style ("south", "left_shoulder", "dpad", "lstick"). Null texture if there is none.
ImTextureID ControlTexture(const char* control, bool key) noexcept;
}

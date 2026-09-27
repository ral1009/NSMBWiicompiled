#pragma once

#include <aurora/aurora.h>
#include <aurora/event.h>

namespace settings_overlay {
// Apply persistent controller settings once Aurora has discovered host devices.
void InitializeRuntimeSettings() noexcept;
// Draw the F10 settings bar before each Aurora present.
void HandleEvents(const AuroraEvent* events) noexcept;
// Product hook run with every event batch before the overlay handles it (NSMBW: button glyphs).
void SetProductEventHook(void (*hook)(const AuroraEvent* events) noexcept) noexcept;
// Product hook drawn in the "Controller settings" menu in place of the GameCube presets and
// button mapping (NSMBW: action bindings and button icon choice).
void SetProductControllerMenuHook(void (*hook)() noexcept) noexcept;
void Draw() noexcept;
// True while the F10 bar is open; game input is held back then (PADBlockInput for PAD reads).
bool InputBlocked() noexcept;
bool StartupScreenVisible() noexcept;
void NotifyStrapInputAccepted() noexcept;
// Opt out of the black "WiiCompiled" title-card overlay entirely. For products (NSMBW) whose
// own guest boot sequence draws real content (the Wii Remote strap warning) as its first frame,
// this card only covers that content and then disappears over it - MKW still wants the card, so
// this is an explicit per-product opt-out rather than a default behavior change.
void DisableStartupScreen() noexcept;
void AdvancePresentedFrame() noexcept;
} // namespace settings_overlay

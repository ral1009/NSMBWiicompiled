#pragma once

#include <aurora/aurora.h>
#include <aurora/event.h>

namespace settings_overlay {
// Apply persistent controller settings once Aurora has discovered host devices.
void InitializeRuntimeSettings() noexcept;
// Draw the F10 settings bar before each Aurora present.
void HandleEvents(const AuroraEvent* events) noexcept;
void Draw() noexcept;
bool StartupScreenVisible() noexcept;
void NotifyStrapInputAccepted() noexcept;
// Opt out of the black "WiiCompiled" title-card overlay entirely. For products (NSMBW) whose
// own guest boot sequence draws real content (the Wii Remote strap warning) as its first frame,
// this card only covers that content and then disappears over it - MKW still wants the card, so
// this is an explicit per-product opt-out rather than a default behavior change.
void DisableStartupScreen() noexcept;
void AdvancePresentedFrame() noexcept;
} // namespace settings_overlay

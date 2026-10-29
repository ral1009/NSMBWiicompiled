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
// True while the F10 bar or a product menu is open; game input is held back then (PADBlockInput
// for PAD reads, nsmbw_controls for NSMBW's own reads).
bool InputBlocked() noexcept;

// A product's own full-screen menu (NSMBW: the options menu drawn with the game's art). When one
// is registered, F10 opens it and the F10 bar moves to Shift+F10.
struct ProductMenu {
    // True while the menu is drawn, and also while the key or button that closed it is still
    // held, so that press doesn't reach the game.
    bool (*isOpen)() noexcept;
    void (*setOpen)(bool open) noexcept;
    // Every event batch, open or not (it tracks key releases after closing).
    void (*handleEvents)(const AuroraEvent* events) noexcept;
    // Once per frame inside the ImGui frame, after the F10 bar.
    void (*draw)() noexcept;
};
void SetProductMenu(const ProductMenu& menu) noexcept;

// Settings shared by the F10 bar and a product menu. Each setter applies the change and saves it
// to Config.toml, the same way the F10 bar always has.
namespace settings {
enum class Volume { Master, Music, SoundEffects, Ui, Voices };
int GetVolume(Volume volume) noexcept; // percent, 0..100
void SetVolume(Volume volume, int percent) noexcept;
bool Muted() noexcept;
void SetMuted(bool muted) noexcept;bool MuteMusicForMedia() noexcept;
void SetMuteMusicForMedia(bool enabled) noexcept;
bool AudioMixWorker() noexcept;
void SetAudioMixWorker(bool enabled) noexcept;
int DisplayMode() noexcept; // AuroraDisplayMode
void SetDisplayMode(int mode) noexcept;
int FrameInterpolation() noexcept; // 0 off, 1 = 120 fps, 2 = 180 fps
void SetFrameInterpolation(int mode) noexcept;
int ResolutionCount() noexcept;
const char* ResolutionLabel(int index) noexcept;
// 6x and 8x are refused while frame interpolation is on (the F10 bar greys them out).
bool ResolutionAllowed(int index) noexcept;
int ResolutionIndex() noexcept; // -1 when the saved scale is not in the list
void SetResolutionIndex(int index) noexcept;
bool Bloom() noexcept; // applies when the next scene renderer is created
void SetBloom(bool enabled) noexcept;
bool ShowFps() noexcept;
void SetShowFps(bool show) noexcept;
bool HdTextures() noexcept;
void SetHdTextures(bool enabled) noexcept;
bool HdTexturesNeedRestart() noexcept; // no pack was loaded at startup
bool CopyFilterDisabled() noexcept;
void SetCopyFilterDisabled(bool disabled) noexcept;
bool SkipUnreadyPipelines() noexcept;
void SetSkipUnreadyPipelines(bool skip) noexcept;
} // namespace settings
bool StartupScreenVisible() noexcept;
void NotifyStrapInputAccepted() noexcept;
// Opt out of the black "WiiCompiled" title-card overlay entirely. For products (NSMBW) whose
// own guest boot sequence draws real content (the Wii Remote strap warning) as its first frame,
// this card only covers that content and then disappears over it - MKW still wants the card, so
// this is an explicit per-product opt-out rather than a default behavior change.
void DisableStartupScreen() noexcept;
void AdvancePresentedFrame() noexcept;
} // namespace settings_overlay

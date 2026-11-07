// Skipping the opening cutscene and hint movies (developer request, 2026-11-07), without changing
// game code: the host only watches input and then drives an exit path the game already has.
//
// Button: - (each player's "minus" action). + was the first idea, but + opens the game's own
// pause menu during a hint movie, and taking it for the skip would have lost that (developer's
// choice, 2026-11-07). The game does nothing with - in either scene. The first press shows a
// "Press (-) to skip" prompt in the bottom-right corner for kPromptSeconds; a press while it
// shows asks the KPAD override (nsmbw_kpad_overrides.cpp, game thread) to skip. - never reaches
// the game while a skippable scene runs.
//
// When a scene counts as skippable (Skippable), read from guest memory every frame:
//   - only while the game-stop word 0x8042A228 is 0: not under the game's pause menu, a warning,
//     or the F10 options menu (which sets HOME_MENU / OTASUKE_PAUSE there).
//   - Opening: scene profile 8 (MOVIE) with dScMovie_c::m_instance (0x8042A498) set, its mMovieId
//     (+0x198) 0 and mState (+0x164) 1 - the "playing" state of dScMovie_c's execute
//     (0x80922910, d_basesNP; the switch on mState at 0x809229F0).
//   - Hint movie: scene profile 5 (STAGE) with dInfo_c::m_startGameInfo.mGameMode (0x80315E98)
//     4 (GAME_MODE_HINT_MOVIE, d_info.hpp) and player 1's replay object (dScStage_c::play walks
//     the four at 0x803744D0) present, so the replay has actually started.
// What the skip does is in nsmbw_kpad_overrides.cpp (RunMovieSkip).

#include "nsmbw_movie_skip.h"

#include "nsmbw_button_glyphs.h"
#include "nsmbw_controls.h"
#include "nsmbw_menu_art.h"
#include "nsmbw_menu_draw.h"

#include "memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace nsmbw_movie_skip {
namespace {
namespace draw = nsmbw_menu_draw;

constexpr uint32_t kWpadMinus = 1u << 12;
constexpr uint32_t kGameStopAddr = 0x8042A228u;
constexpr uint32_t kMovieInstanceAddr = 0x8042A498u; // dScMovie_c::m_instance
constexpr uint32_t kMovieStateOffset = 0x164u, kMovieIdOffset = 0x198u;
constexpr uint32_t kGameModeAddr = 0x80315E90u + 8u;
constexpr uint32_t kReplayObjectsAddr = 0x803744D0u;
constexpr uint32_t kProfileStage = 5, kProfileMovie = 8;
constexpr uint32_t kGameModeHintMovie = 4;
constexpr double kPromptSeconds = 4.0;

enum Kind : int32_t { kNone = 0, kOpening = 1, kHintMovie = 2 };

int32_t Skippable() {
    uint32_t stop = 0;
    if (!Memory::TryRead32(kGameStopAddr, stop) || stop != 0) return kNone;
    const uint32_t profile = g_nsmbwCurrentSceneProfile;
    if (profile == kProfileMovie) {
        uint32_t movie = 0, state = 0, id = 0;
        if (!Memory::TryRead32(kMovieInstanceAddr, movie) || movie == 0) return kNone;
        if (!Memory::TryRead32(movie + kMovieStateOffset, state) || !Memory::TryRead32(movie + kMovieIdOffset, id)) {
            return kNone;
        }
        return state == 1 && id == 0 ? kOpening : kNone;
    }
    if (profile == kProfileStage) {
        uint32_t mode = 0, replay = 0;
        if (!Memory::TryRead32(kGameModeAddr, mode) || mode != kGameModeHintMovie) return kNone;
        if (!Memory::TryRead32(kReplayObjectsAddr, replay) || replay == 0) return kNone;
        return kHintMovie;
    }
    return kNone;
}

double Seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void Log(const char* what, int32_t kind) {
    static const bool log = std::getenv("NSMBW_LOG_MOVIE_SKIP") != nullptr;
    if (log) std::fprintf(stderr, "[nsmbw][skip] %s (%s, scene %u)\n", what,
                          kind == kOpening ? "opening" : kind == kHintMovie ? "hint movie" : "none",
                          g_nsmbwCurrentSceneProfile);
}

// Game thread (the KPAD override) writes; the ImGui frame reads the prompt time.
std::atomic<double> g_promptUntil{0.0};
std::atomic<int32_t> g_request{kNone};
// Per frame: what is skippable, and whether this scene was already skipped (a second request
// while the game is still fading out would run the exit twice); reset when the scene changes.
int32_t g_kind = kNone;
bool g_skipped = false;
int g_kindFrames = 0;
uint32_t g_profile = 0xFFFFFFFFu;
std::array<bool, 4> g_minusHeld{};

// NSMBW_LOG_REPLAY=1: every 30 frames, the stop word and the words of player 1's replay object
// (the first of dScStage_c::play's four at 0x803744D0) that changed since the last dump - a replay
// that is advancing changes its position words, a held one doesn't. Written for the F10 / replay
// desync (2026-11-07).
void LogReplay() {
    static const bool log = std::getenv("NSMBW_LOG_REPLAY") != nullptr;
    if (!log) return;
    static int frame = 0;
    static std::array<uint32_t, 64> last{};
    static uint32_t lastObj = 0;
    if (++frame % 30 != 0) return;
    uint32_t obj = 0, stop = 0;
    Memory::TryRead32(kReplayObjectsAddr, obj);
    Memory::TryRead32(kGameStopAddr, stop);
    std::fprintf(stderr, "[nsmbw][replay] frame=%d scene=%u stop=0x%X obj=0x%08X changed:", frame,
                 g_nsmbwCurrentSceneProfile, stop, obj);
    for (uint32_t i = 0; obj != 0 && i < last.size(); ++i) {
        uint32_t w = 0;
        Memory::TryRead32(obj + i * 4, w);
        if (obj == lastObj && w != last[i]) std::fprintf(stderr, " +0x%X=0x%X", i * 4, w);
        last[i] = w;
    }
    lastObj = obj;
    std::fprintf(stderr, "\n");
}

} // namespace

void Draw() noexcept {
    const double left = g_promptUntil.load() - Seconds();
    if (left <= 0) return;
    // The menu font and art are otherwise loaded on the options menu's first open; without them
    // only the glyph drew (2026-11-07 test shot).
    if (!nsmbw_menu_art::Load()) return;
    const float alpha = float(std::clamp(left / 0.4, 0.0, 1.0)); // fades out over the last 0.4 s
    const draw::Canvas c;
    const std::string pad = nsmbw_controls::GamepadGlyphFor(kWpadMinus);
    const std::string key = nsmbw_controls::KeyGlyphFor(kWpadMinus);
    const ImTextureID glyph = nsmbw_button_glyphs::PromptTexture(pad.c_str(), key.c_str());
    // Bottom-right corner of the 608 x 342 unit 16:9 area, laid out from the right edge.
    constexpr float kSize = 17, kGlyph = 24, kY = -150;
    const draw::TextStyle style{.align = draw::Align::Right, .alpha = alpha, .outline = true};
    const float wSkip = c.Measure("to skip", kSize), wPress = c.Measure("Press", kSize);
    const float right = 280;
    const float width = wPress + 6 + kGlyph + 6 + wSkip;
    c.Rect(right - width / 2, kY, width + 20, 30, draw::Rgb(0, 0, 0, uint8_t(110 * alpha)),
           draw::Rgb(0, 0, 0, uint8_t(110 * alpha)));
    float x = right;
    x -= c.Text("to skip", x, kY, kSize, style) + 6;
    if (glyph != ImTextureID{}) c.Image(glyph, x - kGlyph / 2, kY, kGlyph);
    else c.Text("-", x - kGlyph / 2, kY, kSize, {.alpha = alpha, .outline = true});
    x -= kGlyph + 6;
    c.Text("Press", x, kY, kSize, style);
}

} // namespace nsmbw_movie_skip

using namespace nsmbw_movie_skip;

extern "C" uint32_t NsmbwMovieSkipFilterInput(uint32_t chan, uint32_t hold) {
    if (chan == 0) { // SampleKeyboardForFrame walks the channels from 0 once per VI frame
        // A new scene starts over. Not when the scene merely stops being skippable for a while:
        // the opening leaves its playing state between its parts (2026-11-07 log), which must not
        // hide a prompt the player is about to answer.
        if (g_nsmbwCurrentSceneProfile != g_profile) {
            g_profile = g_nsmbwCurrentSceneProfile;
            g_skipped = false;
            g_promptUntil = 0.0;
            g_request = kNone;
            g_kindFrames = 0;
        }
        g_kind = Skippable();
        LogReplay();
        // NSMBW_AUTO_SKIP_TEST=<frames>: test runs hold - for 12 frames that many skippable frames
        // into a scene and again 90 frames later, as a player would (prompt, then skip).
        static const int testAt = [] {
            const char* v = std::getenv("NSMBW_AUTO_SKIP_TEST");
            return v ? std::atoi(v) : 0;
        }();
        if (g_kind != kNone) {
            ++g_kindFrames;
            if (testAt > 0 && ((g_kindFrames >= testAt && g_kindFrames < testAt + 12) ||
                               (g_kindFrames >= testAt + 90 && g_kindFrames < testAt + 102))) {
                hold |= kWpadMinus;
            }
        }
    }
    if (chan >= g_minusHeld.size()) return hold;
    const bool down = (hold & kWpadMinus) != 0;
    const bool pressed = down && !g_minusHeld[chan];
    g_minusHeld[chan] = down;
    if (g_kind == kNone) return hold;
    if (pressed && !g_skipped) {
        if (g_promptUntil.load() > Seconds()) {
            g_skipped = true;
            g_promptUntil = 0.0;
            g_request = g_kind;
            Log("skip", g_kind);
        } else {
            g_promptUntil = Seconds() + kPromptSeconds;
            Log("prompt", g_kind);
        }
    }
    return hold & ~kWpadMinus;
}

extern "C" int32_t NsmbwMovieSkipTake() { return g_request.exchange(kNone); }

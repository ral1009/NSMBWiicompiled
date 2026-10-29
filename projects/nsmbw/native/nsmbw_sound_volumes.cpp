// Music / Sound effects / Menu sounds volumes for NSMBW (the options menu's Audio tab and the F10
// bar). The shared runtime/src/music_attenuation.cpp does this for Mario Kart Wii - through MKW's
// sound-manager address (0x809C2898) and revo_kart.brsar's 11 players - and is not built for NSMBW
// (it also carries a C++/WinRT media-session monitor this build doesn't configure), so until
// 2026-10-28 these setters were no-op stubs in nsmbw_runtime_shims.cpp and only Master worked
// (Master is applied at the host audio output, not in the game).
//
// Same mechanism as MKW's: each nw4r::snd::SoundPlayer has a volume (mVolume, +0x2C) that scales
// every sound it plays. Once per audio poll, each player's volume is multiplied by its category's
// slider; a value that differs from the one last written is the game's own new volume (the game
// changes them, e.g. to fade music under the pause menu) and becomes the base that is scaled.
//
// Pointer chain: SndAudioMgr::sInstance (0x8042A768, syms.txt) -> mArcPlayer at +0x5BC
// (NSMBW-Decomp snd_audio_mgr.hpp: 0x100 + 4 + 0x4B8) -> the SoundArchivePlayer's player count
// and player array. Where those two fields sit and the SoundPlayer size differ between nw4r
// versions (MKW: array at +0x34, 0x5C bytes each; the decomp's nw4r headers, from another game's
// debug build: count at +0x3C, array +0x40, 0x54 each), so they are not assumed: the layout is
// accepted only if every player's playable-sound count (+0x24) matches the one
// wii_mj2d_sound.brsar gives it (kPlayableCounts). Nothing is written until a layout matches.
//
// Categories, by the players' names in wii_mj2d_sound.brsar:
//   Music        16-21  PLAYER_BGM_COURSE_STAR / _FANFARE / _MAIN / _SUB1 / _SUB2, BGM_CONTROL_TRACK
//   Menu sounds  0, 1, 11  PLAYER_SE_SYS_GAME_PAUSE, PLAYER_SE_SYS, PLAYER_SE_SYS_RC_CNCT
//   Effects      the rest (player, enemies, objects, ambience, boss, demo, jump, notes, instruments)
// There is no voice player: the characters' voices (SE_VOC_*) share players 2, 5, 8 and 10 with
// effects, so a voice volume can't be set per player.
#include "memory.h"
#include "music_attenuation.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MusicAttenuation {
namespace {

constexpr uint32_t kSndAudioMgrInstance = 0x8042A768u;
constexpr uint32_t kArcPlayerOffset = 0x5BCu;
constexpr uint32_t kPlayableCountOffset = 0x24u;
constexpr uint32_t kVolumeOffset = 0x2Cu;
constexpr size_t kPlayers = 22;
constexpr std::array<uint32_t, kPlayers> kPlayableCounts = {4, 3, 8, 8, 4, 8, 5, 1, 4, 4, 15,
                                                             8, 4, 4, 4, 4, 1, 1, 2, 1, 1, 2};

enum class Category { Music, Effects, Menu };
Category CategoryOf(size_t player) {
    if (player >= 16) return Category::Music;
    if (player == 0 || player == 1 || player == 11) return Category::Menu;
    return Category::Effects;
}

std::atomic<float> g_music{1.0f}, g_effects{1.0f}, g_menu{1.0f};

float Clamp(float v) { return v <= 1.0f ? (v < 0.0f ? 0.0f : v) : 1.0f; } // nw4r SoundPlayer::SetVolume's clamp

float Slider(size_t player) {
    switch (CategoryOf(player)) {
    case Category::Music: return g_music.load(std::memory_order_relaxed);
    case Category::Menu: return g_menu.load(std::memory_order_relaxed);
    case Category::Effects: return g_effects.load(std::memory_order_relaxed);
    }
    return 1.0f;
}

// Guest-thread state (TickGuest runs in the audio poll).
uint32_t g_arcPlayer = 0;   // the SoundArchivePlayer the layout below was found for
uint32_t g_players = 0;     // its SoundPlayer array; 0 = not found
uint32_t g_stride = 0;
std::array<float, kPlayers> g_base{}, g_written{};
std::array<bool, kPlayers> g_known{};

bool Read(uint32_t addr, uint32_t& v) { return Memory::TryRead32(addr, v); }
float AsFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
uint32_t AsBits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// Finds the player array: a count of 22 followed by the array pointer, and a stride under which
// every player's playable count matches the archive's.
uint32_t FindPlayers(uint32_t arcPlayer, uint32_t& stride) {
    for (uint32_t off = 0x30; off <= 0x48; off += 4) {
        uint32_t count = 0, array = 0;
        if (!Read(arcPlayer + off, count) || count != kPlayers || !Read(arcPlayer + off + 4, array) || array == 0) continue;
        for (uint32_t s = 0x50; s <= 0x100; s += 4) {
            bool ok = true;
            for (size_t i = 0; i < kPlayers && ok; ++i) {
                uint32_t playable = 0;
                ok = Read(array + uint32_t(i) * s + kPlayableCountOffset, playable) && playable == kPlayableCounts[i];
            }
            if (ok) {
                stride = s;
                std::fprintf(stderr, "[nsmbw][sound] player volumes: SoundArchivePlayer 0x%08X, %zu players at +0x%X -> 0x%08X, 0x%X bytes each\n",
                             arcPlayer, kPlayers, off + 4, array, s);
                return array;
            }
        }
    }
    return 0;
}

// Once, if the search keeps failing: what the candidate fields hold, to fix the layout from.
void DumpFailedSearch(uint32_t mgr, uint32_t arcPlayer) {
    std::fprintf(stderr, "[nsmbw][sound] player volumes NOT applied: no player layout matched. SndAudioMgr 0x%08X, "
                         "SoundArchivePlayer (+0x5BC) 0x%08X\n", mgr, arcPlayer);
    for (uint32_t off = 0x20; off <= 0x50; off += 4) {
        uint32_t v = 0;
        Read(arcPlayer + off, v);
        std::fprintf(stderr, "[nsmbw][sound]   +0x%02X = 0x%08X\n", off, v);
        if (v < 0x80000000u || v >= 0x94000000u) continue;
        for (uint32_t st : {0x54u, 0x58u, 0x5Cu, 0x60u}) {
            std::fprintf(stderr, "[nsmbw][sound]     as array, stride 0x%X: +0x24 =", st);
            for (uint32_t i = 0; i < 6; ++i) {
                uint32_t c = 0;
                Read(v + i * st + kPlayableCountOffset, c);
                std::fprintf(stderr, " %u", c);
            }
            std::fprintf(stderr, " (want 4 3 8 8 4 8)\n");
        }
    }
}

// NSMBW_LOG_SOUND_VOLUMES=1: every ~2 s, each player's game volume and the volume written.
void LogVolumes() {
    static const bool enabled = std::getenv("NSMBW_LOG_SOUND_VOLUMES") != nullptr;
    static uint32_t polls = 0;
    if (!enabled || ++polls % 2000 != 0) return;
    std::fprintf(stderr, "[nsmbw][sound] sliders music %.2f effects %.2f menu %.2f; players (base->applied):",
                 g_music.load(), g_effects.load(), g_menu.load());
    for (size_t i = 0; i < kPlayers; ++i) std::fprintf(stderr, " %zu:%.2f->%.2f", i, g_base[i], g_written[i]);
    std::fprintf(stderr, "\n");
}

} // namespace

void SetMusicVolume(float volume) noexcept { g_music.store(Clamp(volume), std::memory_order_relaxed); }
void SetSoundEffectsVolume(float volume) noexcept { g_effects.store(Clamp(volume), std::memory_order_relaxed); }
void SetUiVolume(float volume) noexcept { g_menu.store(Clamp(volume), std::memory_order_relaxed); }
void SetVoicesVolume(float) noexcept {} // no voice player in NSMBW (see above)

void TickGuest() noexcept {
    uint32_t mgr = 0, arcPlayer = 0;
    if (!Read(kSndAudioMgrInstance, mgr) || mgr == 0 || !Read(mgr + kArcPlayerOffset, arcPlayer) || arcPlayer == 0) return;
    if (arcPlayer != g_arcPlayer) {
        // Searched once per SoundArchivePlayer; it may not be set up on the first polls.
        g_players = FindPlayers(arcPlayer, g_stride);
        if (g_players == 0) {
            static uint32_t misses = 0;
            if (++misses == 20000) DumpFailedSearch(mgr, arcPlayer); // well after boot has set the players up
            return;
        }
        g_arcPlayer = arcPlayer;
        g_known.fill(false);
    }
    for (size_t i = 0; i < kPlayers; ++i) {
        const uint32_t addr = g_players + uint32_t(i) * g_stride + kVolumeOffset;
        uint32_t bits = 0;
        if (!Read(addr, bits)) continue;
        const float current = AsFloat(bits);
        if (!g_known[i] || current != g_written[i]) {
            g_base[i] = Clamp(current);
            g_known[i] = true;
        }
        const float applied = g_base[i] * Slider(i);
        if (current != applied) Memory::TryWrite32(addr, AsBits(applied));
        g_written[i] = applied;
    }
    LogVolumes();
}

// The SoundPlayer::SetVolume override this pairs with in MKW (audio.cpp, at MKW's 0x800A35E0) does
// not exist in NSMBW; TickGuest picks up the game's own volume changes instead.
void SetSoundPlayerVolume(uint32_t, float) {}

} // namespace MusicAttenuation

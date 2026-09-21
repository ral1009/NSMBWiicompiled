#pragma once

#include "logging.hpp" // IWYU pragma: keep

#include <aurora/aurora.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>
#include <vector>

using namespace std::string_view_literals;

namespace aurora {
// TEMPORARY DIAGNOSTIC: NSMBW present-source-override call-order isolation. Remove before merging.
inline std::atomic<uint64_t> g_nsmbwDiagSeq{0};
inline bool nsmbw_diag_enabled() noexcept {
  static const bool enabled = std::getenv("NSMBW_LOG_PRESENT_SEQ") != nullptr;
  return enabled;
}
// NSMBW_GPU_PEEK_SEQ (temporary): the 3 nsmbw_diag_peek_texture call sites all gated on a
// hardcoded "> 650" so they'd fire together on the same later frame - too late to see the WiiStrap
// boot screen, which finishes well under 650 present calls in. Overridable via env var so the same
// peek mechanism can be pointed at an earlier frame without moving the hardcoded threshold every
// investigation. Remove alongside the rest of this diagnostic.
inline uint64_t nsmbw_gpu_peek_seq_threshold() noexcept {
  static const uint64_t threshold = [] {
    const char* v = std::getenv("NSMBW_GPU_PEEK_SEQ");
    return v ? static_cast<uint64_t>(std::strtoull(v, nullptr, 10)) : 650u;
  }();
  return threshold;
}

// NSMBW_GPU_PEEK_SCENE (temporary): the sequence-count threshold above turned out unusable for
// reliably catching a specific scene's first frame - present-call counts to reach the same scene
// vary hugely run to run (confirmed: one run created the BOOT/WiiStrap scene around present-seq
// 606, immediately adjacent to the default 650 threshold, while other runs reach it at wildly
// different counts), because scene progression speed itself is not tied to present-call count in
// any fixed way. g_nsmbwCurrentSceneProfile (set by
// projects/nsmbw/native/nsmbw_create_next_scene_diag.cpp on every successful scene transition) is
// actual game state instead of a wall-clock-ish counter, so gating on it catches the right frame
// regardless of how fast/slow this particular run's boot happens to be. NSMBW_GPU_PEEK_SCENE is a
// decimal fProf::PROFILE_NAME_e value (0=BOOT, 5=STAGE, 10=GAME_SETUP, ...); unset keeps the old
// sequence-threshold behavior so this stays backward compatible. Remove alongside the rest of this
// diagnostic.
extern "C" uint32_t g_nsmbwCurrentSceneProfile;
// VI retrace tick, published by projects/nsmbw/native/nsmbw_tick_read_pump.cpp each frame, so
// scene-gated diagnostics can also skip the first N frames of a scene profile that is reused
// (STAGE = title screen and every level).
extern "C" uint32_t g_nsmbwCurrentViTick;
// NSMBW_GPU_PEEK_SCENE_FRAME (temporary): the very first frame after a scene transition is too
// early - confirmed directly (NSMBW_GPU_PEEK_SCENE=0 alone caught a perfectly uniform clear-color
// frame, every pixel exactly 64,64,64): the new scene's own child process is often still mid-
// creation at that point (checkChildProcessCreateState reports BLOCKED right after
// createNextScene succeeds), so nothing has actually been drawn into it yet. This counts how many
// times the target scene has been seen ready and only reports ready once that count is reached,
// so the peek lands a few frames into the scene instead of on its very first, empty one.
inline bool nsmbw_gpu_peek_ready(uint64_t seq) noexcept {
  static const char* const sceneEnv = std::getenv("NSMBW_GPU_PEEK_SCENE");
  if (sceneEnv != nullptr) {
    static const uint32_t wantScene = static_cast<uint32_t>(std::strtoul(sceneEnv, nullptr, 10));
    static const uint32_t wantFrameOffset = [] {
      const char* v = std::getenv("NSMBW_GPU_PEEK_SCENE_FRAME");
      return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : 0u;
    }();
    static uint32_t lastSeenScene = 0xFFFFFFFFu;
    static uint32_t seenCount = 0;
    if (g_nsmbwCurrentSceneProfile != wantScene) {
      return false;
    }
    if (lastSeenScene != wantScene) {
      lastSeenScene = wantScene;
      seenCount = 0;
    }
    return seenCount++ >= wantFrameOffset;
  }
  return seq > nsmbw_gpu_peek_seq_threshold();
}
inline void nsmbw_diag_log(const char* site, const char* detail = "") {
  if (!nsmbw_diag_enabled()) {
    return;
  }
  const uint64_t seq = g_nsmbwDiagSeq.fetch_add(1, std::memory_order_relaxed);
  std::ostringstream tid;
  tid << std::this_thread::get_id();
  std::fprintf(stderr, "[NSMBW_SEQ] %06llu thread=%s site=%s %s\n",
               static_cast<unsigned long long>(seq), tid.str().c_str(), site, detail);
  std::fflush(stderr);
}
} // namespace aurora

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#ifndef SBIG
#define SBIG(q)                                                                                                        \
  (((q) & 0x000000FF) << 24 | ((q) & 0x0000FF00) << 8 | ((q) & 0x00FF0000) >> 8 | ((q) & 0xFF000000) >> 24)
#endif
#else
#ifndef SBIG
#define SBIG(q) (q)
#endif
#endif

template <typename T>
  requires(sizeof(T) == sizeof(uint16_t) && std::is_arithmetic_v<T>)
constexpr T bswap(T val) noexcept {
  union {
    uint16_t u;
    T t;
  } v{.t = val};
#if __GNUC__
  v.u = __builtin_bswap16(v.u);
#elif _WIN32
  v.u = _byteswap_ushort(v.u);
#else
  v.u = (v.u << 8) | ((v.u >> 8) & 0xFF);
#endif
  return v.t;
}

template <typename T>
  requires(sizeof(T) == sizeof(uint32_t) && std::is_arithmetic_v<T>)
constexpr T bswap(T val) noexcept {
  union {
    uint32_t u;
    T t;
  } v{.t = val};
#if __GNUC__
  v.u = __builtin_bswap32(v.u);
#elif _WIN32
  v.u = _byteswap_ulong(v.u);
#else
  v.u = ((v.u & 0x0000FFFF) << 16) | ((v.u & 0xFFFF0000) >> 16) | ((v.u & 0x00FF00FF) << 8) | ((v.u & 0xFF00FF00) >> 8);
#endif
  return v.t;
}

template <typename T>
  requires(sizeof(T) == sizeof(uint64_t) && std::is_arithmetic_v<T>)
constexpr T bswap(T val) noexcept {
  union {
    uint64_t u;
    T t;
  } v{.t = val};
#if __GNUC__
  v.u = __builtin_bswap64(v.u);
#elif _WIN32
  v.u = _byteswap_uint64(v.u);
#else
  static_assert(false, "bswap 64bit not implemented on this target");
#endif
  return v.t;
}

template <typename T>
  requires(std::is_enum_v<T>)
auto underlying(T value) -> std::underlying_type_t<T> {
  return static_cast<std::underlying_type_t<T>>(value);
}

#define AURORA_ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define POINTER_ADD_TYPE(type_, ptr_, offset_) ((type_)((uintptr_t)(ptr_) + (uintptr_t)(offset_)))
#define POINTER_ADD(ptr_, offset_) POINTER_ADD_TYPE(decltype(ptr_), ptr_, offset_)

#if !defined(__has_cpp_attribute)
#define __has_cpp_attribute(name) 0
#endif
#if __has_cpp_attribute(unlikely)
#define UNLIKELY [[unlikely]]
#else
#define UNLIKELY
#endif
#if __has_cpp_attribute(likely)
#define LIKELY [[likely]]
#else
#define LIKELY
#endif
#define FATAL(msg, ...) Log.fatal(msg, ##__VA_ARGS__);
#define ASSERT(cond, msg, ...)                                                                                         \
  if (!(cond))                                                                                                         \
  UNLIKELY FATAL(msg, ##__VA_ARGS__)
#ifdef NDEBUG
#define CHECK(cond, msg, ...)
#else
#define CHECK(cond, msg, ...) ASSERT(cond, msg, ##__VA_ARGS__)
#endif
#define DEFAULT_FATAL(msg, ...) UNLIKELY default : FATAL(msg, ##__VA_ARGS__)
#define TRY(cond, msg, ...)                                                                                            \
  if (!(cond))                                                                                                         \
    UNLIKELY {                                                                                                         \
      Log.error(msg, ##__VA_ARGS__);                                                                                   \
      return false;                                                                                                    \
    }
#define TRY_WARN(cond, msg, ...)                                                                                       \
  if (!(cond))                                                                                                         \
    UNLIKELY { Log.warn(msg, ##__VA_ARGS__); }

#define UNIMPLEMENTED() FATAL("UNIMPLEMENTED: {}", __FUNCTION__)

namespace aurora {
extern AuroraConfig g_config;
extern uint32_t g_sdlCustomEventsStart;
extern char g_gameName[4];

// wait_for_frame_worker() joins the DONE phase (ImGui, surface, sealing another frame need this).
// wait_for_frame_worker_sealed() joins only SEALED, which is what the FIFO drain uses.
void wait_for_frame_worker() noexcept;
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept;
bool wait_for_frame_worker_for(std::chrono::microseconds timeout) noexcept;
std::recursive_mutex& renderer_gpu_mutex() noexcept;

template <typename T>
class ArrayRef {
public:
  using value_type = std::remove_cvref_t<T>;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = const_pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  ArrayRef() = default;
  explicit ArrayRef(const T& one) : ptr(&one), length(1) {}
  ArrayRef(const T* data, size_t length) : ptr(data), length(length) {}
  ArrayRef(const T* begin, const T* end) : ptr(begin), length(end - begin) {}
  template <size_t N>
  constexpr ArrayRef(const T (&arr)[N]) : ptr(arr), length(N) {}
  template <size_t N>
  constexpr ArrayRef(const std::array<T, N>& arr) : ptr(arr.data()), length(arr.size()) {}
  ArrayRef(const std::vector<T>& vec) : ptr(vec.data()), length(vec.size()) {}

  const T* data() const { return ptr; }
  size_t size() const { return length; }
  bool empty() const { return length == 0; }

  const T& front() const {
    assert(!empty());
    return ptr[0];
  }
  const T& back() const {
    assert(!empty());
    return ptr[length - 1];
  }
  const T& operator[](size_t i) const {
    assert(i < length && "Invalid index!");
    return ptr[i];
  }

  iterator begin() const { return ptr; }
  iterator end() const { return ptr + length; }

  reverse_iterator rbegin() const { return reverse_iterator(end()); }
  reverse_iterator rend() const { return reverse_iterator(begin()); }

  /// Disallow accidental assignment from a temporary.
  template <typename U>
  std::enable_if_t<std::is_same<U, T>::value, ArrayRef<T>>& operator=(U&& Temporary) = delete;

  /// Disallow accidental assignment from a temporary.
  template <typename U>
  std::enable_if_t<std::is_same<U, T>::value, ArrayRef<T>>& operator=(std::initializer_list<U>) = delete;

private:
  const T* ptr = nullptr;
  size_t length = 0;
};
} // namespace aurora

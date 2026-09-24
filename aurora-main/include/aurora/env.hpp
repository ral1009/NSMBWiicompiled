#pragma once

#include <cstdlib>

// Cached environment lookup for diagnostic switches.
//
// AURORA_ENV("NAME") returns the same value as std::getenv("NAME"), read once per call site on first
// use and then served from a function-local static. The MSVC/UCRT getenv takes a lock and does a
// case-insensitive linear scan of the whole environment block on every call; with dozens of
// NSMBW_LOG_* checks on the per-draw / per-FIFO-command path that scan was over half of the main
// thread's time (NSMBW_PROFILE_SAMPLER, world map, 2026-09-23: ucrtbase getenv internals 53.6 %
// inclusive, called from aurora::gx::fifo::process, HleFifoWrite, build_uniform, handle_bp, ...).
//
// Caching is only correct because nothing in this process modifies its environment after startup
// (no putenv/setenv/SetEnvironmentVariable anywhere in aurora, the runtime or projects/nsmbw); a
// switch has to be set before launch, which is how all of them are already used. The name must be a
// string literal: each expansion is its own lambda type, so each call site gets its own cache.
#define AURORA_ENV(name)                                                                                 \
  ([]() noexcept -> const char* {                                                                        \
    static const char* const aurora_env_cached_value = std::getenv(name);                                \
    return aurora_env_cached_value;                                                                      \
  }())

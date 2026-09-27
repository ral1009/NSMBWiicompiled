#pragma once

// Scaled tile cache: keeps internally-scaled EFB copies that land inside a larger texture at their
// scaled resolution, the way Dolphin's texture cache stitches partial EFB copies.
//
// NSMBW renders each animated tile (? blocks, bricks, coins) into a 32x32 EFB area every frame and
// copies it into its slot of the 1024-wide tileset atlas in main RAM (a strided copy). The RAM write-
// back is necessarily native resolution, so above 1x those tiles looked 1x next to the scaled scene.
// This cache builds an upscaled copy of such an atlas (its native RAM image, blitted up once) and
// pastes each scaled tile copy into it right after the copy's resolve; texture binding then samples
// that instead of the native atlas. The RAM write-back still happens for any CPU reader.

#include "../gfx/common.hpp"

#include <dolphin/gx/GXEnum.h>

#include <optional>

struct GXTexObj_;

namespace aurora::gx::scaled_tile_cache {

// GXCopyTex's strided path: a copy of logical size w x h into `dest`, whose row pitch is
// `strideWidth` texels, produced as the (possibly scaled) GPU texture `scaled`.
// Returns true when the copy landed in an atlas this cache has built: the GPU composite then carries
// the tile, so the caller can skip the RAM write-back (a GPU->CPU sync every frame).
bool note_tile_copy(const void* dest, u32 width, u32 height, u32 strideWidth, GXTexFmt format,
                    const gfx::TextureHandle& scaled) noexcept;

// Static texture binding: if tile copies land inside `obj`'s memory, the upscaled atlas to sample
// instead of `base` (the texture decoded from RAM). nullopt while not built yet or not applicable.
std::optional<gfx::TextureHandle> lookup(const GXTexObj_& obj, const gfx::TextureHandle& base) noexcept;

// Fast path, tried before decoding `obj` from RAM: the built composite when the atlas's RAM outside
// its tile slots is unchanged (masked hash, computed at most once per frame). The tiles' own
// write-back changes the RAM every frame, so without this the whole atlas was re-decoded, hashed
// and uploaded every frame only to be replaced by the composite. nullopt means "decode as usual".
std::optional<gfx::TextureHandle> lookup_unchanged(const GXTexObj_& obj) noexcept;


} // namespace aurora::gx::scaled_tile_cache

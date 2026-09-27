#include "scaled_tile_cache.hpp"

#include "../gfx/texture.hpp"
#include "../internal.hpp"

#include <dolphin/gx/GXTexture.h>

#include <aurora/env.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include <xxhash.h>

namespace aurora::gx::scaled_tile_cache {
namespace {

// Largest upscaled atlas we will allocate per axis. A 1024-wide tileset at 8x is 8192.
constexpr u32 kMaxAtlasDimension = 8192;
// An atlas nobody sampled for this many frames (level unloaded) is dropped.
constexpr u32 kAtlasExpiryFrames = 600;
// Only tiles copied this recently are pasted when an atlas is (re)built; older slots are left over
// from a previous level and would paste stale art.
constexpr u32 kSlotFreshFrames = 4;

struct Slot {
  gfx::TextureHandle scaled;
  u32 width = 0;
  u32 height = 0;
  u32 strideWidth = 0;
  u32 scale = 0;
  GXTexFmt format = GX_TF_RGBA8;
  u32 lastCopyFrame = 0;
};

struct Atlas {
  u32 width = 0;
  u32 height = 0;
  u32 bytes = 0;
  u32 scale = 0;
  GXTexFmt format = GX_TF_RGBA8;
  gfx::TextureHandle base;      // native image decoded from RAM, the upscale source
  gfx::TextureHandle composite; // width*scale x height*scale, what draws sample
  bool needsBuild = true;       // base must be (re)blitted and fresh slots pasted
  bool built = false;           // at least one build has been queued ahead of later draws
  // Hash of the atlas's RAM with the tile slots' bytes skipped (they change every frame through the
  // native write-back); a change means a new tileset, so the upscaled base must be rebuilt.
  u64 contentHash = 0;
  u32 lastUsedFrame = 0;
  // lookup_unchanged's once-per-frame verdict.
  u32 checkedFrame = ~0u;
  bool unchangedThisFrame = false;
};

std::map<uintptr_t, Slot> s_slots;
std::map<uintptr_t, Atlas> s_atlases;
const bool s_disabled = AURORA_ENV("AURORA_NO_SCALED_TILE_CACHE") != nullptr;
const bool s_log = AURORA_ENV("AURORA_LOG_SCALED_TILE_CACHE") != nullptr;

struct BlockInfo {
  u32 width;
  u32 height;
  u32 bytes;
};

// GX stores textures as a grid of fixed-size blocks, so a byte offset inside a texture maps to a
// block index and from there to a texel position.
std::optional<BlockInfo> block_info(GXTexFmt format) noexcept {
  switch (format) {
  case GX_TF_I4:
  case GX_TF_CMPR:
    return BlockInfo{8, 8, 32};
  case GX_TF_I8:
  case GX_TF_IA4:
    return BlockInfo{8, 4, 32};
  case GX_TF_IA8:
  case GX_TF_RGB565:
  case GX_TF_RGB5A3:
    return BlockInfo{4, 4, 32};
  case GX_TF_RGBA8:
    return BlockInfo{4, 4, 64};
  default:
    return std::nullopt;
  }
}

// Texel position of `dest` inside an atlas starting at `base`, if it is block-aligned and the
// slot fits.
std::optional<std::pair<u32, u32>> slot_position(uintptr_t base, const Atlas& atlas, uintptr_t dest,
                                                 const Slot& slot) noexcept {
  const auto info = block_info(atlas.format);
  if (!info || dest < base) {
    return std::nullopt;
  }
  const uintptr_t offset = dest - base;
  if (offset % info->bytes != 0) {
    return std::nullopt;
  }
  const u32 blocksPerRow = (atlas.width + info->width - 1) / info->width;
  const u32 blockIndex = static_cast<u32>(offset / info->bytes);
  const u32 x = (blockIndex % blocksPerRow) * info->width;
  const u32 y = (blockIndex / blocksPerRow) * info->height;
  if (x + slot.width > atlas.width || y + slot.height > atlas.height) {
    return std::nullopt;
  }
  return std::pair{x, y};
}

bool slot_belongs(const Atlas& atlas, const Slot& slot) noexcept {
  return slot.format == atlas.format && slot.scale == atlas.scale && slot.strideWidth == atlas.width &&
         slot.scaled && atlas.composite && slot.scaled->format == atlas.composite->format;
}

void queue_paste(uintptr_t base, const Atlas& atlas, uintptr_t dest, const Slot& slot) noexcept {
  const auto pos = slot_position(base, atlas, dest, slot);
  if (!pos || !slot_belongs(atlas, slot)) {
    return;
  }
  gfx::add_post_resolve_op(gfx::PostResolveOp{
      .src = slot.scaled,
      .dst = atlas.composite,
      .dstX = pos->first * atlas.scale,
      .dstY = pos->second * atlas.scale,
  });
}

// Upscale the native base into the composite, then paste every fresh tile over it. Queued behind
// the current tile copy's resolve, so it lands before the level draws that sample the atlas.
void queue_build(uintptr_t base, Atlas& atlas) noexcept {
  // tex_copy_conv UVTransform: offset, scale, copy filter off (w=0), flags {opaque, stride, minV, maxV}.
  static constexpr std::array kIdentityBlit{
      0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 64.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
  };
  if (!gfx::add_post_resolve_op(gfx::PostResolveOp{
          .src = atlas.base,
          .dst = atlas.composite,
          .blit = true,
          .uniformRange = gfx::push_uniform(kIdentityBlit),
      })) {
    return;
  }
  const u32 now = gfx::current_frame();
  u32 pasted = 0;
  for (auto it = s_slots.lower_bound(base); it != s_slots.end() && it->first < base + atlas.bytes; ++it) {
    if (now - it->second.lastCopyFrame <= kSlotFreshFrames) {
      queue_paste(base, atlas, it->first, it->second);
      ++pasted;
    }
  }
  atlas.needsBuild = false;
  atlas.built = true;
  if (s_log) {
    std::fprintf(stderr, "[scaled_tile_cache] build atlas base=%p %ux%u fmt=%u x%u -> %ux%u, %u tiles pasted\n",
                 reinterpret_cast<const void*>(base), atlas.width, atlas.height, static_cast<unsigned>(atlas.format),
                 atlas.scale, atlas.composite->size.width, atlas.composite->size.height, pasted);
  }
}

// XXH64 of the atlas bytes outside every slot's block rows.
u64 masked_hash(uintptr_t base, const Atlas& atlas, const u8* data) noexcept {
  const auto info = block_info(atlas.format);
  std::vector<std::pair<u32, u32>> skip; // [start, end) byte ranges
  if (info) {
    const u32 blocksPerRow = (atlas.width + info->width - 1) / info->width;
    for (auto it = s_slots.lower_bound(base); it != s_slots.end() && it->first < base + atlas.bytes; ++it) {
      const auto pos = slot_position(base, atlas, it->first, it->second);
      if (!pos) continue;
      const u32 rowBytes = (it->second.width / info->width) * info->bytes;
      for (u32 by = 0; by < it->second.height / info->height; ++by) {
        const u32 start = ((pos->second / info->height + by) * blocksPerRow + pos->first / info->width) * info->bytes;
        skip.emplace_back(start, start + rowBytes);
      }
    }
    std::sort(skip.begin(), skip.end());
  }
  XXH64_state_t* state = XXH64_createState();
  XXH64_reset(state, 0);
  u32 cursor = 0;
  for (const auto& [start, end] : skip) {
    if (start > cursor) XXH64_update(state, data + cursor, start - cursor);
    cursor = std::max(cursor, end);
  }
  if (cursor < atlas.bytes) XXH64_update(state, data + cursor, atlas.bytes - cursor);
  const u64 hash = XXH64_digest(state);
  XXH64_freeState(state);
  return hash;
}

} // namespace

void note_tile_copy(const void* dest, u32 width, u32 height, u32 strideWidth, GXTexFmt format,
                    const gfx::TextureHandle& scaled) noexcept {
  if (s_disabled || dest == nullptr || !scaled || width == 0 || height == 0) {
    return;
  }
  // Scale 1 included: even at native resolution, keeping the atlas on the GPU and pasting tile
  // copies into it is what lets lookup_unchanged skip the per-frame RAM re-decode.
  const u32 scale = scaled->size.width / width;
  if (s_log) {
    static std::map<uintptr_t, u32> s_seen; // one line per destination and scale
    const auto k = reinterpret_cast<uintptr_t>(dest);
    if (s_seen[k] != scaled->size.width + 1) {
      s_seen[k] = scaled->size.width + 1;
      std::fprintf(stderr, "[scaled_tile_cache] copy dest=%p %ux%u stride=%u fmt=%u gpu=%ux%u scale=%u\n", dest, width,
                   height, strideWidth, static_cast<unsigned>(format), scaled->size.width, scaled->size.height, scale);
    }
  }
  if (scale == 0 || scaled->size.width != width * scale || scaled->size.height != height * scale) {
    return;
  }
  const auto key = reinterpret_cast<uintptr_t>(dest);
  auto& slot = s_slots[key];
  slot = Slot{
      .scaled = scaled,
      .width = width,
      .height = height,
      .strideWidth = strideWidth,
      .scale = scale,
      .format = format,
      .lastCopyFrame = gfx::current_frame(),
  };

  for (auto& [base, atlas] : s_atlases) {
    if (key < base || key >= base + atlas.bytes || !atlas.composite) {
      continue;
    }
    if (atlas.needsBuild) {
      queue_build(base, atlas); // pastes this slot too
    } else {
      queue_paste(base, atlas, key, slot);
    }
    break;
  }
}

std::optional<gfx::TextureHandle> lookup(const GXTexObj_& obj, const gfx::TextureHandle& base) noexcept {
  if (s_disabled || obj.data == nullptr || !base || obj.has_mips()) {
    return std::nullopt;
  }
  const u32 width = obj.width();
  const u32 height = obj.height();
  const auto format = static_cast<GXTexFmt>(obj.raw_format());
  if (width == 0 || height == 0 || !block_info(format)) {
    return std::nullopt;
  }
  const auto key = reinterpret_cast<uintptr_t>(obj.data);
  const u32 bytes = GXGetTexBufferSize(static_cast<u16>(width), static_cast<u16>(height), format, GX_FALSE, 0);
  const auto first = s_slots.lower_bound(key);
  if (first == s_slots.end() || first->first >= key + bytes || first->second.strideWidth != width) {
    return std::nullopt;
  }
  const u32 scale = first->second.scale;
  if (width * scale > kMaxAtlasDimension || height * scale > kMaxAtlasDimension) {
    return std::nullopt;
  }

  const u32 now = gfx::current_frame();
  auto& atlas = s_atlases[key];
  if (!atlas.composite || atlas.width != width || atlas.height != height || atlas.format != format ||
      atlas.scale != scale) {
    atlas = Atlas{
        .width = width,
        .height = height,
        .bytes = bytes,
        .scale = scale,
        .format = format,
        .composite = gfx::new_render_texture(width * scale, height * scale, GX_TF_RGBA8, "Scaled tile atlas"),
    };
  }
  atlas.lastUsedFrame = now;
  // Keep the newest RAM decode as the rebuild source, but do not rebuild just because it changed:
  // the tiles' own native write-back changes it every frame, and those slots are covered by the
  // scaled pastes anyway. Rebuilds happen only when the RAM outside the slots changes (masked_hash).
  if (atlas.base != base) {
    atlas.base = base;
    const u64 hash = masked_hash(key, atlas, static_cast<const u8*>(obj.data));
    if (hash != atlas.contentHash) {
      atlas.contentHash = hash;
      atlas.needsBuild = true;
    }
  }

  for (auto it = s_atlases.begin(); it != s_atlases.end();) {
    if (now - it->second.lastUsedFrame > kAtlasExpiryFrames) {
      it = s_atlases.erase(it);
    } else {
      ++it;
    }
  }

  if (!atlas.built) {
    return std::nullopt;
  }
  return atlas.composite;
}

std::optional<gfx::TextureHandle> lookup_unchanged(const GXTexObj_& obj) noexcept {
  if (s_disabled || obj.data == nullptr || obj.has_mips()) {
    return std::nullopt;
  }
  const auto it = s_atlases.find(reinterpret_cast<uintptr_t>(obj.data));
  if (it == s_atlases.end()) {
    return std::nullopt;
  }
  auto& atlas = it->second;
  if (!atlas.built || atlas.needsBuild || !atlas.composite || atlas.width != obj.width() ||
      atlas.height != obj.height() || atlas.format != static_cast<GXTexFmt>(obj.raw_format())) {
    return std::nullopt;
  }
  const u32 now = gfx::current_frame();
  if (atlas.checkedFrame != now) {
    atlas.checkedFrame = now;
    atlas.unchangedThisFrame = masked_hash(it->first, atlas, static_cast<const u8*>(obj.data)) == atlas.contentHash;
  }
  if (!atlas.unchangedThisFrame) {
    return std::nullopt; // a new tileset: decode, and lookup() rebuilds from it
  }
  atlas.lastUsedFrame = now;
  return atlas.composite;
}

} // namespace aurora::gx::scaled_tile_cache

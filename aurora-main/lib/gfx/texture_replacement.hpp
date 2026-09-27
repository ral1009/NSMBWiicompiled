#pragma once

#include "texture.hpp"
#include <aurora/gfx.h>
#include <optional>

namespace aurora::gfx::texture_replacement {
void initialize() noexcept;
void shutdown() noexcept;
void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, uint16_t entries) noexcept;
void load_tlut(const GXTlutObj* obj, uint32_t idx) noexcept;
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj) noexcept;
std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept;
// Live on/off switch. The pack index and palette mirrors are built whenever
// g_config.allowTextureReplacements is set at init; this only decides whether find_replacement
// returns them. revision() changes on every switch so gx can drop textures it already resolved.
void set_enabled(bool enabled) noexcept;
bool enabled() noexcept;
uint32_t revision() noexcept;
// See aurora_set_texture_patches.
void set_patches(uint64_t hash, uint32_t width, uint32_t height, uint32_t format, const AuroraTexturePatch* patches,
                 size_t count) noexcept;
} // namespace aurora::gfx::texture_replacement

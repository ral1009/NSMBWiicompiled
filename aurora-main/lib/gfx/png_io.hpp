#pragma once

#include <filesystem>
#include <optional>

#include "texture_convert.hpp"

namespace aurora::gfx::png {
std::optional<ConvertedTexture> load_png_file(const std::filesystem::path& path) noexcept;
std::optional<ConvertedTexture> load_png_bytes(const uint8_t* data, size_t size) noexcept;
}

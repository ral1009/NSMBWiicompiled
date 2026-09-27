#include "texture_replacement.hpp"

#include "../internal.hpp"
#include "../gx/gx.hpp"
#include "../webgpu/gpu.hpp"
#include "dds_io.hpp"
#include "texture_convert.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include "png_io.hpp"
#include "../fs_helper.hpp"

using namespace aurora::gx;
using aurora::webgpu::g_device;

namespace aurora::gfx::texture_replacement {
Module Log("aurora::gfx::texture_replacement");

// A texture's (width, height, format): the only part of a pack key known without hashing.
constexpr uint64_t texture_shape(uint32_t width, uint32_t height, uint32_t format) noexcept {
  return (static_cast<uint64_t>(width) << 40) | (static_cast<uint64_t>(height) << 16) | format;
}
// Shapes present in the pack index. Built once at init (before any lookup), read-only afterwards.
absl::flat_hash_set<uint64_t> s_indexShapes;

// Written by the settings UI thread, read by the GX thread on every lookup.
std::atomic<bool> s_enabled{true};
std::atomic<uint32_t> s_revision{0};

struct RuntimeTextureKey {
  uint64_t textureHash = 0;
  uint64_t tlutHash = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool hasTlut = false;
  uint32_t format = 0;

  bool operator==(const RuntimeTextureKey& rhs) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const RuntimeTextureKey& key) {
    return H::combine(std::move(h), key.textureHash, key.tlutHash, key.width, key.height, key.hasTlut, key.format);
  }
};

struct TlutMetadata {
  uint32_t size = 0;
  uint32_t format = 0;
  uint16_t entries = 0;
  bool valid = false;
  // Object the palette was registered from. GXLoadTlut consumes the registration, so this is what
  // lets the same object be loaded into more than one hardware slot.
  const GXTlutObj* source = nullptr;
  ByteBuffer data;
};

struct CachedReplacement {
  gfx::TextureHandle handle;
  uint64_t bytes = 0;
  std::list<RuntimeTextureKey>::iterator lruIt;
};

struct ReplacementIndexEntry {
  std::filesystem::path path;
};

absl::flat_hash_map<RuntimeTextureKey, ReplacementIndexEntry> s_replacementIndex;
absl::flat_hash_map<RuntimeTextureKey, CachedReplacement> s_replacementCache;
absl::flat_hash_set<RuntimeTextureKey> s_failedKeys;
absl::flat_hash_set<RuntimeTextureKey> s_reportedMisses;
absl::flat_hash_map<const GXTlutObj*, TlutMetadata> s_pendingTluts;
std::array<TlutMetadata, MaxTluts> s_loadedTluts{};
std::list<RuntimeTextureKey> s_replacementLru;
std::filesystem::path s_replacementRoot;
std::filesystem::path s_dumpRoot;
uint64_t s_replacementCacheBytes = 0;
constexpr uint64_t kReplacementCacheBudgetBytes = 4294967296; // 4GB, reasonable for modern hardware?
constexpr uint64_t kReplacementWildcardTextureHash = 0xFFFFFFFFFFFFFFFFull;
constexpr uint64_t kReplacementWildcardTlutHash = 0xFFFFFFFFFFFFFFFEull;

bool iequals_ascii(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }
  return true;
}

bool is_relative_to(const std::filesystem::path& path, const std::filesystem::path& root) noexcept {
  if (root.empty()) {
    return false;
  }
  auto pathIt = path.begin();
  auto rootIt = root.begin();
  for (; rootIt != root.end(); ++rootIt, ++pathIt) {
    if (pathIt == path.end() || !iequals_ascii(fs_path_to_string(*pathIt), fs_path_to_string(*rootIt))) {
      return false;
    }
  }
  return true;
}

bool is_sidecar_mip(std::string_view stem) noexcept {
  constexpr std::string_view tag = "_mip";
  size_t i = stem.size();
  while (i > 0 && stem[i - 1] >= '0' && stem[i - 1] <= '9') {
    --i;
  }

  if (i == stem.size() || i < tag.size()) {
    return false;
  }

  return stem.substr(i - tag.size(), tag.size()) == tag;
}

std::optional<uint64_t> parse_hex(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  uint64_t value = 0;
  for (const char ch : text) {
    value <<= 4;
    if (ch >= '0' && ch <= '9') {
      value |= static_cast<uint64_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      value |= static_cast<uint64_t>(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      value |= static_cast<uint64_t>(ch - 'A' + 10);
    } else {
      return std::nullopt;
    }
  }
  return value;
}

std::optional<uint32_t> parse_u32(std::string_view text, int base = 10) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }

  uint32_t value = 0;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value, base);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::pair<uint32_t, uint32_t>> parse_dimensions(std::string_view text) noexcept {
  const size_t sep = text.find('x');
  if (sep == std::string_view::npos) {
    return std::nullopt;
  }

  const auto width = parse_u32(text.substr(0, sep));
  const auto height = parse_u32(text.substr(sep + 1));
  if (!width.has_value() || !height.has_value()) {
    return std::nullopt;
  }
  return std::pair{*width, *height};
}

uint32_t texture_base_level_size(const GXTexObj_& obj) noexcept {
  switch (obj.format()) {
  case GX_TF_R8_PC:
    return obj.width() * obj.height();
  case GX_TF_RGBA8_PC:
    return obj.width() * obj.height() * 4;
  default:
    return GXGetTexBufferSize(obj.width(), obj.height(), obj.format(), false, 0);
  }
}

std::optional<uint64_t> compute_referenced_tlut_hash(const GXTexObj_& obj) noexcept {
  if (!is_palette_format(obj.format()) || obj.tlut >= s_loadedTluts.size()) {
    return std::nullopt;
  }

  const auto& tlut = s_loadedTluts[obj.tlut];
  const uint32_t textureSize = texture_base_level_size(obj);
  const auto* textureData = static_cast<const uint8_t*>(obj.data);
  if (!tlut.valid || textureData == nullptr || textureSize == 0) {
    return std::nullopt;
  }

  uint32_t minIndex = 0xffff;
  uint32_t maxIndex = 0;
  switch (obj.format()) {
  case GX_TF_C4:
    for (uint32_t i = 0; i < textureSize; ++i) {
      const uint32_t lowNibble = textureData[i] & 0xf;
      const uint32_t highNibble = textureData[i] >> 4;
      minIndex = std::min({minIndex, lowNibble, highNibble});
      maxIndex = std::max({maxIndex, lowNibble, highNibble});
    }
    break;
  case GX_TF_C8:
    for (uint32_t i = 0; i < textureSize; ++i) {
      const uint32_t index = textureData[i];
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  case GX_TF_C14X2:
    for (uint32_t i = 0; i + sizeof(uint16_t) <= textureSize; i += sizeof(uint16_t)) {
      uint16_t value = 0;
      std::memcpy(&value, textureData + i, sizeof(value));
      const uint32_t index = bswap(value) & 0x3fff;
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  default:
    return std::nullopt;
  }

  size_t tlutSize = 2 * (static_cast<size_t>(maxIndex) + 1 - minIndex);
  const size_t tlutOffset = 2 * static_cast<size_t>(minIndex);
  if (tlutOffset + tlutSize > tlut.data.size()) {
    return std::nullopt;
  }
  return XXH64(tlut.data.data() + tlutOffset, tlutSize, 0);
}

const TlutMetadata* get_loaded_tlut(const GXTexObj_& obj) noexcept {
  if (!is_palette_format(obj.format()) || obj.tlut >= s_loadedTluts.size()) {
    return nullptr;
  }

  const auto& tlut = s_loadedTluts[obj.tlut];
  return tlut.valid ? &tlut : nullptr;
}

bool ensure_directory(const std::filesystem::path& dir) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

RuntimeTextureKey build_runtime_key(const GXTexObj_& obj) noexcept {
  RuntimeTextureKey key{
      .width = obj.width(),
      .height = obj.height(),
      .hasTlut = is_palette_format(obj.format()),
      .format = obj.format(),
  };

  const uint32_t textureSize = texture_base_level_size(obj);
  if (obj.data != nullptr && textureSize != 0) {
    key.textureHash = XXH64(obj.data, textureSize, 0);
  }
  if (key.hasTlut) {
    key.tlutHash = compute_referenced_tlut_hash(obj).value_or(0);
  }
  return key;
}

std::string format_replacement_filename(const RuntimeTextureKey& key) {
  if (key.hasTlut) {
    return fmt::format("tex1_{}x{}_{:016x}_{:016x}_{}.dds", key.width, key.height,
                       key.textureHash, key.tlutHash, key.format);
  }
  return fmt::format("tex1_{}x{}_{:016x}_{}.dds", key.width, key.height, key.textureHash, key.format);
}

std::optional<RuntimeTextureKey> parse_replacement_filename(std::string_view filename) noexcept {
  const size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos) {
    return std::nullopt;
  }

  if (!iequals_ascii(filename.substr(dot), ".dds") && !iequals_ascii(filename.substr(dot), ".png")) {
    return std::nullopt;
  }

  const std::string_view stem = filename.substr(0, dot);
  constexpr std::string_view prefix = "tex1_";
  if (!stem.starts_with(prefix)) {
    return std::nullopt;
  }

  std::array<std::string_view, 6> parts{};
  size_t partCount = 0;
  size_t offset = 0;
  bool consumedAll = false;
  while (offset <= stem.size() && partCount < parts.size()) {
    const size_t next = stem.find('_', offset);
    parts[partCount++] = stem.substr(offset, next == std::string_view::npos ? stem.size() - offset : next - offset);
    if (next == std::string_view::npos) {
      consumedAll = true;
      break;
    }
    offset = next + 1;
  }
  if (!consumedAll || partCount < 4 || partCount > 6 || parts[0] != "tex1") {
    return std::nullopt;
  }

  const auto dimensions = parse_dimensions(parts[1]);
  if (!dimensions.has_value()) {
    return std::nullopt;
  }

  size_t index = 2;
  if (parts[index] == "m") {
    ++index;
  }

  size_t remaining = partCount - index;
  if (remaining != 2 && remaining != 3) {
    return std::nullopt;
  }

  uint64_t textureHash = 0;
  if (parts[index] == "$") {
    textureHash = kReplacementWildcardTextureHash;
  } else {
    const auto parsedTex = parse_hex(parts[index]);
    if (!parsedTex.has_value()) {
      return std::nullopt;
    }
    textureHash = *parsedTex;
  }

  auto formatPart = parts[partCount - 1];
  if (formatPart == "arb") {
    formatPart = parts[partCount - 2];
    remaining -= 1;
  }
  const auto format = parse_u32(formatPart);
  if (!format.has_value()) {
    return std::nullopt;
  }

  uint64_t tlutHash = 0;
  const bool hasTlut = remaining == 3;
  if (hasTlut) {
    const std::string_view tlutPart = parts[index + 1];
    if (tlutPart == "$") {
      tlutHash = kReplacementWildcardTlutHash;
    } else {
      const auto parsedTlutHash = parse_hex(tlutPart);
      if (!parsedTlutHash.has_value()) {
        return std::nullopt;
      }
      tlutHash = *parsedTlutHash;
    }
  }

  return RuntimeTextureKey{
      .textureHash = textureHash,
      .tlutHash = tlutHash,
      .width = dimensions->first,
      .height = dimensions->second,
      .hasTlut = hasTlut,
      .format = *format,
  };
}

static std::optional<ConvertedTexture> load_texture_file(const std::filesystem::path& path) {
  if (path.extension() == ".png") {
    return png::load_png_file(path);
  } else {
    return dds::load_dds_file(path);
  }
}

constexpr bool isUnsupportedTextureFormat(const ConvertedTexture& texture) {
  switch (texture.format) {
  case wgpu::TextureFormat::BC1RGBAUnorm:
  case wgpu::TextureFormat::BC1RGBAUnormSrgb:
  case wgpu::TextureFormat::BC2RGBAUnorm:
  case wgpu::TextureFormat::BC2RGBAUnormSrgb:
  case wgpu::TextureFormat::BC3RGBAUnorm:
  case wgpu::TextureFormat::BC3RGBAUnormSrgb:
  case wgpu::TextureFormat::BC4RUnorm:
  case wgpu::TextureFormat::BC4RSnorm:
  case wgpu::TextureFormat::BC5RGUnorm:
  case wgpu::TextureFormat::BC5RGSnorm:
  case wgpu::TextureFormat::BC6HRGBUfloat:
  case wgpu::TextureFormat::BC6HRGBFloat:
  case wgpu::TextureFormat::BC7RGBAUnorm:
  case wgpu::TextureFormat::BC7RGBAUnormSrgb:
    return !webgpu::g_bcTexturesSupported;
  default:
    return false;
  }
}

std::optional<ConvertedTexture> load_replacement(const ReplacementIndexEntry& entry) noexcept {
  auto base = load_texture_file(entry.path);
  if (!base.has_value()) {
    Log.warn("texture_replacement: failed to load texture {}", fs_path_to_string(entry.path));
    return std::nullopt;
  }
  if (isUnsupportedTextureFormat(base.value())) {
    Log.warn(
      "texture_replacement: failed to load texture {} due to unsupported format: {}",
      fs_path_to_string(entry.path),
      static_cast<uint32_t>(base->format));
    return std::nullopt;
  }

  std::vector<ConvertedTexture> more;
  std::error_code ec;
  for (uint32_t mipLevel = 1;; ++mipLevel) {
    const auto mipPath = entry.path.parent_path() / fmt::format("{}_mip{}{}", fs_path_to_string(entry.path.stem()), mipLevel, fs_path_to_string(entry.path.extension()));
    if (!std::filesystem::is_regular_file(mipPath, ec)) {
      break;
    }

    auto lvl = load_texture_file(mipPath);
    const uint32_t ew = std::max(base->width >> mipLevel, 1u);
    const uint32_t eh = std::max(base->height >> mipLevel, 1u);
    const bool ok = lvl.has_value() && lvl->format == base->format && lvl->width == ew && lvl->height == eh;
    if (!ok) {
      if (!lvl.has_value()) {
        Log.warn("texture_replacement: could not load mip {}", fs_path_to_string(mipPath));
      } else {
        Log.warn("texture_replacement: expected {}x{} for mip {}, got {}x{}", ew, eh, fs_path_to_string(mipPath),
                 lvl->width, lvl->height);
      }

      break;
    }
    more.push_back(std::move(*lvl));
  }

  if (more.empty()) {
    return base;
  }

  const uint32_t mips = 1u + static_cast<uint32_t>(more.size());
  const uint64_t n = calc_texture_size(base->format, base->width, base->height, mips);
  if (n == 0) {
    return std::nullopt;
  }

  ByteBuffer blob{static_cast<size_t>(n)};
  uint8_t* const dst = blob.data();
  uint64_t o = 0;
  const auto append = [&](const ByteBuffer& d) noexcept -> bool {
    if (o + d.size() > n) {
      return false;
    }
    std::memcpy(dst + o, d.data(), d.size());
    o += d.size();
    return true;
  };
  if (!append(base->data)) {
    return std::nullopt;
  }
  for (const auto& mip : more) {
    if (!append(mip.data)) {
      return std::nullopt;
    }
  }
  if (o != n) {
    return std::nullopt;
  }

  return ConvertedTexture{
      .format = base->format,
      .width = base->width,
      .height = base->height,
      .mips = mips,
      .data = std::move(blob),
  };
}

void touch_cached_replacement(decltype(s_replacementCache)::iterator it) noexcept {
  if (it->second.lruIt != s_replacementLru.begin()) {
    s_replacementLru.splice(s_replacementLru.begin(), s_replacementLru, it->second.lruIt);
    it->second.lruIt = s_replacementLru.begin();
  }
}

void evict_replacement_cache_if_needed() noexcept {
  while (s_replacementCacheBytes > kReplacementCacheBudgetBytes && !s_replacementLru.empty()) {
    const RuntimeTextureKey key = s_replacementLru.back();
    s_replacementLru.pop_back();

    const auto it = s_replacementCache.find(key);
    if (it == s_replacementCache.end()) {
      continue;
    }

    const uint64_t entryBytes = it->second.bytes;
    s_replacementCache.erase(it);
    s_replacementCacheBytes -= std::min(s_replacementCacheBytes, entryBytes);
  }
}

void build_index() noexcept {
  if (!g_config.allowTextureReplacements) {
    return;
  }

  auto userPath = fs_path_from_string(g_config.userPath);
  auto cachePath = fs_path_from_string(g_config.cachePath);

  s_replacementRoot = userPath / "texture_replacements";
  s_dumpRoot = cachePath / "texture_dumps";

  if (!ensure_directory(s_replacementRoot)) {
    return;
  }
  if (g_config.allowTextureDumps && !ensure_directory(s_dumpRoot)) {
    return;
  }

  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(
           s_replacementRoot,
           std::filesystem::directory_options::skip_permission_denied |
               std::filesystem::directory_options::follow_directory_symlink,
           ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      break;
    }

    if (!it->is_regular_file()) {
      continue;
    }

    const auto& path = it->path();

    if (is_relative_to(path, s_dumpRoot)) {
      continue;
    }

    if (!iequals_ascii(fs_path_to_string(path.extension()), ".dds") && !iequals_ascii(fs_path_to_string(path.extension()), ".png")) {
      continue;
    }

    if (is_sidecar_mip(fs_path_to_string(path.stem()))) {
      continue;
    }

    const auto parsed = parse_replacement_filename(fs_path_to_string(path.filename()));
    if (!parsed.has_value()) {
      continue;
    }

    s_replacementIndex.try_emplace(*parsed, path);
    s_indexShapes.insert(texture_shape(parsed->width, parsed->height, parsed->format));
  }

  Log.info("Indexed {} texture replacements", s_replacementIndex.size());
}

const ReplacementIndexEntry* find_replacement_path(const RuntimeTextureKey& key) noexcept {
  if (const auto it = s_replacementIndex.find(key); it != s_replacementIndex.end()) {
    return &it->second;
  }

  if (key.hasTlut) {
    RuntimeTextureKey tlutWildcardKey = key;
    tlutWildcardKey.tlutHash = kReplacementWildcardTlutHash;
    if (const auto it = s_replacementIndex.find(tlutWildcardKey); it != s_replacementIndex.end()) {
      return &it->second;
    }
  }

  RuntimeTextureKey textureWildcardKey = key;
  textureWildcardKey.textureHash = kReplacementWildcardTextureHash;
  if (const auto it = s_replacementIndex.find(textureWildcardKey); it != s_replacementIndex.end()) {
    return &it->second;
  }

  return nullptr;
}

const gfx::TextureHandle* find_cached_replacement(const RuntimeTextureKey& key) noexcept {
  const auto cached = s_replacementCache.find(key);
  if (cached == s_replacementCache.end()) {
    return nullptr;
  }

  touch_cached_replacement(cached);
  return &cached->second.handle;
}

gfx::TextureHandle upload_converted(const std::string& label, const ConvertedTexture* replacement) noexcept {
  const wgpu::Extent3D size{
      .width = replacement->width,
      .height = replacement->height,
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDescriptor{
      .label = label.c_str(),
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = replacement->format,
      .mipLevelCount = replacement->mips,
      .sampleCount = 1,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);
  const auto viewLabel = fmt::format("{} view", label);
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = viewLabel.c_str(),
      .format = replacement->format,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = replacement->mips,
  };
  auto textureView = texture.CreateView(&textureViewDescriptor);
  auto handle = std::make_shared<gfx::TextureRef>(std::move(texture), std::move(textureView), wgpu::TextureView{}, size,
                                                  replacement->format, replacement->mips, gfx::InvalidTextureFormat);
  handle->isReplacement = true;
  gfx::write_texture(*handle, replacement->data);
  return handle;
}

gfx::TextureHandle load_replacement_texture(const RuntimeTextureKey& key, const ReplacementIndexEntry& entry) noexcept {
  const auto replacement = load_replacement(entry);
  if (!replacement.has_value()) {
    s_failedKeys.insert(key);
    return {};
  }
  return upload_converted(fmt::format("TextureReplacement {}", format_replacement_filename(key)), &*replacement);
}

void cache_replacement(const RuntimeTextureKey& key, const gfx::TextureHandle& handle) noexcept {
  const uint64_t replacementBytes =
      calc_texture_size(handle->format, handle->size.width, handle->size.height, handle->mipCount);
  s_replacementLru.push_front(key);
  s_replacementCache.emplace(
      key, CachedReplacement{.handle = handle, .bytes = replacementBytes, .lruIt = s_replacementLru.begin()});
  s_replacementCacheBytes += replacementBytes;
  evict_replacement_cache_if_needed();
}

bool dump_editable_texture_dds(const RuntimeTextureKey& key, const GXTexObj_& obj) noexcept {
  const ArrayRef<uint8_t> texData{static_cast<const uint8_t*>(obj.data), UINT32_MAX};
  const uint32_t texWidth = obj.width();
  const uint32_t texHeight = obj.height();

  ConvertedTexture pixels;
  if (is_palette_format(obj.format())) {
    const TlutMetadata* tlut = get_loaded_tlut(obj);
    if (tlut == nullptr) {
      return false;
    }
    pixels =
        convert_texture_palette(obj.format(), texWidth, texHeight, 1, texData, static_cast<GXTlutFmt>(tlut->format),
                                tlut->entries, {tlut->data.data(), tlut->data.size()});
  } else {
    pixels = convert_texture(obj.format(), texWidth, texHeight, 1, texData);
  }

  const uint64_t rgbaBytes = calc_texture_size(wgpu::TextureFormat::RGBA8Unorm, texWidth, texHeight, 1);

  if (pixels.data.empty() || pixels.format != wgpu::TextureFormat::RGBA8Unorm || pixels.data.size() != rgbaBytes) {
    return false;
  }

  const auto path = s_dumpRoot / format_replacement_filename(key);
  return dds::write_rgba8_dds(path, texWidth, texHeight, pixels.data);
}

bool report_missing_key(const RuntimeTextureKey& key, const GXTexObj_& obj) noexcept {
  if (!s_reportedMisses.insert(key).second) {
    return false;
  }

  if (g_config.allowTextureDumps) {
    dump_editable_texture_dds(key, obj);
  }
  return true;
}

void initialize() noexcept { build_index(); }

void shutdown() noexcept {
  s_replacementIndex.clear();
  s_indexShapes.clear();
  s_replacementCache.clear();
  s_failedKeys.clear();
  s_reportedMisses.clear();
  s_pendingTluts.clear();
  for (auto& tlut : s_loadedTluts) {
    tlut = {};
  }
  s_replacementLru.clear();
  s_replacementCacheBytes = 0;
  s_replacementRoot.clear();
  s_dumpRoot.clear();
}

// Mirrors of the guest palettes, kept only so find_replacement can hash and the DDS dump decode a
// CI palette. The renderer uploads from the guest pointer, so skip this work when replacement is off.
void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, uint16_t entries) noexcept {
  if (!g_config.allowTextureReplacements) {
    return;
  }
  if (obj == nullptr || data == nullptr) {
    return;
  }

  // A registration is only consumed by a matching load_tlut, and the map is keyed by a guest address
  // the game may free and reuse, so a table this large means stale entries: drop them wholesale.
  constexpr size_t kMaxPendingTluts = 4096;
  if (s_pendingTluts.size() >= kMaxPendingTluts && !s_pendingTluts.contains(obj)) {
    s_pendingTluts.clear();
  }

  const size_t sz = static_cast<size_t>(entries) * 2;
  ByteBuffer buffer{sz};
  std::memcpy(buffer.data(), static_cast<const uint8_t*>(data), sz);
  s_pendingTluts[obj] = {
      .size = static_cast<uint32_t>(entries) * 2,
      .format = static_cast<uint32_t>(format),
      .entries = entries,
      .valid = true,
      .source = obj,
      .data = std::move(buffer),
  };
}

void load_tlut(const GXTlutObj* obj, uint32_t idx) noexcept {
  if (!g_config.allowTextureReplacements) {
    return;
  }
  if (idx >= s_loadedTluts.size()) {
    return;
  }

  // Consume the pending registration. The map is keyed by a guest object address that can be freed
  // or reused at any time, and entries used to live until shutdown.
  if (const auto it = s_pendingTluts.find(obj); it != s_pendingTluts.end()) {
    s_loadedTluts[idx] = std::move(it->second);
    s_pendingTluts.erase(it);
    return;
  }

  // The same object may legally be loaded into several hardware slots; only the first load finds the
  // pending entry, so recover the palette from whichever slot already holds it.
  for (const auto& loaded : s_loadedTluts) {
    if (!loaded.valid || loaded.source != obj) {
      continue;
    }
    s_loadedTluts[idx] = {
        .size = loaded.size,
        .format = loaded.format,
        .entries = loaded.entries,
        .valid = loaded.valid,
        .source = loaded.source,
        .data = loaded.data.clone(),
    };
    return;
  }

  s_loadedTluts[idx] = {};
}

// ---- Texture patches -------------------------------------------------------------------------
// A product can paint images over rectangles of a game texture (NSMBW: controller glyphs over the
// Wii button icons of its PictureFont sheet). The base is the texture pack's file when the pack is
// on and has one, otherwise the decoded original, so patches work with or without a pack.

struct PatchImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba; // straight alpha
};

struct TexturePatch {
  uint16_t x, y, width, height; // native texels
  std::shared_ptr<const PatchImage> image;
};

struct PatchedTexture {
  uint32_t generation = 0;
  bool fromPack = false;
  TextureHandle handle;
};

// Patch sets are written by the event thread and read by the GX thread.
std::mutex s_patchMutex;
absl::flat_hash_map<RuntimeTextureKey, std::vector<TexturePatch>> s_patches;
absl::flat_hash_set<uint64_t> s_patchShapes; // under s_patchMutex, like s_patches
std::atomic<bool> s_hasPatches{false};
std::atomic<uint32_t> s_patchGeneration{0};
// Decoded images keyed by the caller's PNG buffer, which must stay alive (embedded data).
absl::flat_hash_map<const uint8_t*, std::shared_ptr<const PatchImage>> s_patchImages;
// GX thread only.
absl::flat_hash_map<RuntimeTextureKey, PatchedTexture> s_patchedCache;

RuntimeTextureKey make_patch_key(uint64_t hash, uint32_t width, uint32_t height, uint32_t format) noexcept {
  return RuntimeTextureKey{.textureHash = hash, .width = width, .height = height, .format = format};
}

std::shared_ptr<const PatchImage> decode_patch_image(const uint8_t* png, size_t size) noexcept {
  if (const auto it = s_patchImages.find(png); it != s_patchImages.end()) {
    return it->second;
  }
  auto decoded = png::load_png_bytes(png, size);
  if (!decoded.has_value() || decoded->format != wgpu::TextureFormat::RGBA8Unorm) {
    Log.warn("texture_replacement: could not decode a {}-byte patch image", size);
    return {};
  }
  auto image = std::make_shared<PatchImage>();
  image->width = decoded->width;
  image->height = decoded->height;
  image->rgba.assign(decoded->data.data(), decoded->data.data() + decoded->data.size());
  s_patchImages.emplace(png, image);
  return image;
}

void set_patches(uint64_t hash, uint32_t width, uint32_t height, uint32_t format, const AuroraTexturePatch* patches,
                 size_t count) noexcept {
  std::vector<TexturePatch> list;
  std::lock_guard lock(s_patchMutex);
  for (size_t i = 0; i < count; ++i) {
    const auto& p = patches[i];
    auto image = p.png != nullptr ? decode_patch_image(p.png, p.pngSize) : nullptr;
    list.push_back({p.x, p.y, p.width, p.height, std::move(image)});
  }
  const auto key = make_patch_key(hash, width, height, format);
  if (list.empty()) {
    s_patches.erase(key);
  } else {
    s_patches[key] = std::move(list);
  }
  s_patchShapes.clear();
  for (const auto& [k, v] : s_patches) {
    s_patchShapes.insert(texture_shape(k.width, k.height, k.format));
  }
  s_hasPatches.store(!s_patches.empty(), std::memory_order_relaxed);
  s_patchGeneration.fetch_add(1, std::memory_order_relaxed);
  // Same flush as the pack toggle: every texture gx already resolved may now be stale.
  s_revision.fetch_add(1, std::memory_order_release);
}

// Samples `image` bilinearly at (u, v) in [0,1]; colour is premultiplied by alpha.
std::array<float, 4> sample_premultiplied(const PatchImage& image, float u, float v) noexcept {
  const float fx = std::clamp(u * image.width - 0.5f, 0.0f, static_cast<float>(image.width - 1));
  const float fy = std::clamp(v * image.height - 0.5f, 0.0f, static_cast<float>(image.height - 1));
  const uint32_t x0 = static_cast<uint32_t>(fx), y0 = static_cast<uint32_t>(fy);
  const uint32_t x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
  const float tx = fx - x0, ty = fy - y0;
  std::array<float, 4> out{};
  const auto add = [&](uint32_t x, uint32_t y, float w) {
    const uint8_t* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
    const float a = p[3] / 255.0f;
    out[0] += p[0] * a * w;
    out[1] += p[1] * a * w;
    out[2] += p[2] * a * w;
    out[3] += a * w;
  };
  add(x0, y0, (1 - tx) * (1 - ty));
  add(x1, y0, tx * (1 - ty));
  add(x0, y1, (1 - tx) * ty);
  add(x1, y1, tx * ty);
  return out;
}

// Clears each patch rectangle and draws its image into it, aspect-fit and centred. The base may be
// an upscaled pack texture, so rectangles are scaled from native texels to the base's size.
// Minification (a 256 px glyph into a 32 px native cell) samples bilinearly with no mip, which is
// fine for flat-colour icons but would alias fine detail.
void apply_patches(ConvertedTexture& base, uint32_t nativeWidth, uint32_t nativeHeight,
                   const std::vector<TexturePatch>& patches) noexcept {
  const bool bgra = base.format == wgpu::TextureFormat::BGRA8Unorm;
  const float sx = static_cast<float>(base.width) / nativeWidth;
  const float sy = static_cast<float>(base.height) / nativeHeight;
  uint8_t* pixels = base.data.data();
  for (const auto& patch : patches) {
    const uint32_t rx0 = std::min(static_cast<uint32_t>(patch.x * sx), base.width);
    const uint32_t ry0 = std::min(static_cast<uint32_t>(patch.y * sy), base.height);
    const uint32_t rx1 = std::min(static_cast<uint32_t>((patch.x + patch.width) * sx), base.width);
    const uint32_t ry1 = std::min(static_cast<uint32_t>((patch.y + patch.height) * sy), base.height);
    for (uint32_t y = ry0; y < ry1; ++y) {
      std::memset(pixels + (static_cast<size_t>(y) * base.width + rx0) * 4, 0, (rx1 - rx0) * 4);
    }
    if (!patch.image || patch.image->width == 0 || patch.image->height == 0) {
      continue;
    }
    const auto& image = *patch.image;
    const float rw = static_cast<float>(rx1 - rx0), rh = static_cast<float>(ry1 - ry0);
    const float fit = std::min(rw / image.width, rh / image.height);
    const float dw = image.width * fit, dh = image.height * fit;
    const float ox = rx0 + (rw - dw) * 0.5f, oy = ry0 + (rh - dh) * 0.5f;
    for (uint32_t y = ry0; y < ry1; ++y) {
      const float v = (y + 0.5f - oy) / dh;
      if (v < 0.0f || v > 1.0f) {
        continue;
      }
      for (uint32_t x = rx0; x < rx1; ++x) {
        const float u = (x + 0.5f - ox) / dw;
        if (u < 0.0f || u > 1.0f) {
          continue;
        }
        const auto c = sample_premultiplied(image, u, v);
        uint8_t* d = pixels + (static_cast<size_t>(y) * base.width + x) * 4;
        const float a = c[3];
        const float inv = a > 0.0f ? 1.0f / a : 0.0f;
        const uint8_t r = static_cast<uint8_t>(std::clamp(c[0] * inv, 0.0f, 255.0f));
        const uint8_t g = static_cast<uint8_t>(std::clamp(c[1] * inv, 0.0f, 255.0f));
        const uint8_t b = static_cast<uint8_t>(std::clamp(c[2] * inv, 0.0f, 255.0f));
        d[0] = bgra ? b : r;
        d[1] = g;
        d[2] = bgra ? r : b;
        d[3] = static_cast<uint8_t>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
      }
    }
  }
}

std::optional<TextureHandle> find_patched(const RuntimeTextureKey& key, const GXTexObj_& obj, bool packOn) noexcept {
  std::vector<TexturePatch> patches;
  uint32_t generation = 0;
  {
    std::lock_guard lock(s_patchMutex);
    const auto it = s_patches.find(make_patch_key(key.textureHash, key.width, key.height, key.format));
    if (it == s_patches.end()) {
      return std::nullopt;
    }
    patches = it->second;
    generation = s_patchGeneration.load(std::memory_order_relaxed);
  }

  const auto* packEntry = packOn ? find_replacement_path(key) : nullptr;
  const bool fromPack = packEntry != nullptr;
  if (const auto it = s_patchedCache.find(key);
      it != s_patchedCache.end() && it->second.generation == generation && it->second.fromPack == fromPack) {
    return it->second.handle;
  }

  std::optional<ConvertedTexture> base;
  if (fromPack) {
    base = load_replacement(*packEntry);
  }
  if (!base.has_value()) {
    const u32 dataSize = GXGetTexBufferSize(static_cast<u16>(obj.width()), static_cast<u16>(obj.height()),
                                            obj.format(), false, 0);
    base = convert_texture(obj.format(), obj.width(), obj.height(), 1,
                           {static_cast<const uint8_t*>(obj.data), dataSize});
  }
  if (base->format != wgpu::TextureFormat::RGBA8Unorm && base->format != wgpu::TextureFormat::BGRA8Unorm) {
    Log.warn("texture_replacement: cannot patch {} (base format {} is not 8-bit RGBA)",
             format_replacement_filename(key), static_cast<uint32_t>(base->format));
    return std::nullopt;
  }
  if (base->mips != 1) {
    // Patching only the top level would leave the unpatched icons in the smaller mips.
    Log.warn("texture_replacement: cannot patch {} (base has {} mips)", format_replacement_filename(key), base->mips);
    return std::nullopt;
  }

  apply_patches(*base, key.width, key.height, patches);
  auto handle = upload_converted(fmt::format("PatchedTexture {}", format_replacement_filename(key)), &*base);
  s_patchedCache[key] = PatchedTexture{.generation = generation, .fromPack = fromPack, .handle = handle};
  Log.info("texture_replacement: patched {} ({} rects, base {})", format_replacement_filename(key), patches.size(),
           fromPack ? "pack" : "original");
  return handle;
}

std::optional<TextureHandle> find_replacement(const GXTexObj_& obj) noexcept {
  ZoneScoped;

  const bool packOn = g_config.allowTextureReplacements && s_enabled.load(std::memory_order_relaxed);
  const bool hasPatches = s_hasPatches.load(std::memory_order_relaxed);
  if (!packOn && !hasPatches) {
    return std::nullopt;
  }

  // Hashing the whole texture (build_runtime_key) is the expensive part, and almost no texture
  // can match: skip it unless the pack or a patch has a key of this shape. Dumping needs every
  // texture's key, so it disables the filter.
  const uint64_t shape = texture_shape(obj.width(), obj.height(), obj.format());
  const bool packCandidate = packOn && (g_config.allowTextureDumps || s_indexShapes.contains(shape));
  bool patchCandidate = false;
  if (hasPatches) {
    std::lock_guard lock(s_patchMutex);
    patchCandidate = s_patchShapes.contains(shape);
  }
  if (!packCandidate && !patchCandidate) {
    return std::nullopt;
  }

  const RuntimeTextureKey key = build_runtime_key(obj);
  if (patchCandidate) {
    if (auto patched = find_patched(key, obj, packOn); patched.has_value()) {
      return patched;
    }
  }
  if (!packCandidate) {
    return std::nullopt;
  }
  const auto* path = find_replacement_path(key);
  if (path == nullptr) {
    report_missing_key(key, obj);
    return std::nullopt;
  }

  if (const auto* cached = find_cached_replacement(key); cached != nullptr) {
    return *cached;
  }

  if (s_failedKeys.contains(key)) {
    return std::nullopt;
  }

  auto handle = load_replacement_texture(key, *path);
  if (!handle) {
    return std::nullopt;
  }

  cache_replacement(key, handle);
  // Once per load (cache hits return above), so a run shows which pack files actually matched.
  Log.info("texture_replacement: loaded {}", fs_path_to_string(path->path.filename()));
  return handle;
}

std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept {
  const RuntimeTextureKey key = build_runtime_key(obj);
  return format_replacement_filename(key);
}

void set_enabled(bool enabled) noexcept {
  if (s_enabled.exchange(enabled, std::memory_order_relaxed) != enabled) {
    s_revision.fetch_add(1, std::memory_order_release);
  }
}

bool enabled() noexcept { return g_config.allowTextureReplacements && s_enabled.load(std::memory_order_relaxed); }

uint32_t revision() noexcept { return s_revision.load(std::memory_order_acquire); }

} // namespace aurora::gfx::texture_replacement

#pragma once

#include "common.hpp"
#include "texture.hpp"

#include <dolphin/gx/GXEnum.h>

namespace aurora::gfx::efb_ram {

void schedule(void* dest, uint32_t width, uint32_t height, GXTexFmt format, TextureHandle texture) noexcept;
// A copy of a width x height rectangle into a wider image (GXSetTexCopyDst width = strideWidth, the
// SDK way to write into part of an existing texture, e.g. a tile-animation frame into a tileset
// atlas). The texture that samples it is uploaded from guest RAM, so the result must land there:
// always read back frame-latently, block rows scattered at the destination image's stride.
void schedule_strided(void* dest, uint32_t width, uint32_t height, uint32_t strideWidth, GXTexFmt format,
                      TextureHandle texture) noexcept;
bool has_pending(void* dest = nullptr) noexcept;
bool prepare_downloads(void* dest = nullptr) noexcept;
void encode_downloads(const wgpu::CommandEncoder& encoder, void* dest = nullptr) noexcept;
bool complete_downloads() noexcept;
void cancel() noexcept;

// Frame-latent readbacks for probe-sized CPU-consumed copies: they ride the frame's own encode and
// land in guest RAM a frame later. Per frame, from the worker: seal, encode, then after_submit.
void seal_async_downloads() noexcept;
void encode_async_downloads(const wgpu::CommandEncoder& encoder) noexcept;
void after_submit() noexcept;
// Drops requests sealed for a frame that will never be encoded.
void abort_async() noexcept;
void shutdown() noexcept;

} // namespace aurora::gfx::efb_ram

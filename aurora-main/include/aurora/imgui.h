#ifndef AURORA_IMGUI_H
#define AURORA_IMGUI_H

#include <imgui.h>

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stddef.h"
#include "stdint.h"
#endif

ImTextureID aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8);
// Decodes an in-memory PNG and adds it as an ImGui texture; 0 if it can't be decoded.
// width / height may be null.
ImTextureID aurora_imgui_add_texture_png(const void* png, size_t size, uint32_t* width, uint32_t* height);

#ifdef __cplusplus
}
#endif

#endif

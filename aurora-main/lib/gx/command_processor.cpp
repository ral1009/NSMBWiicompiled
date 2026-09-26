#include "command_processor.hpp"
#include <aurora/env.hpp>

#include "../gfx/common.hpp"
#include "../dolphin/gx/__gx.h"
#include "../gfx/texture_replacement.hpp"
#include "dolphin/gx/GXAurora.h"
#include "gx.hpp"
#include "gx_fmt.hpp"
#include "pipeline.hpp"
#include "shader_info.hpp"
#include "../internal.hpp"

#include <absl/container/flat_hash_map.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

// DIAGNOSTIC (temporary, NSMBW P_stripe_00 investigation - see gx_texture.cpp's
// NSMBW_TARGET_WRAP_TEV comment): NSMBW's GXSetTevOrder/ColorIn/AlphaIn calls don't go through
// the runtime's per-function HLE overrides (those are bound at MKW's own addresses and never
// fire for NSMBW's translated code - confirmed by grepping projects/mkwii/MAP.txt), so the only
// place NSMBW's real, live TEV alpha-combiner state exists is right here, decoded from the raw
// BP register writes below. These mirror that decoded state into plain extern "C" globals so
// runtime/src/hle/gx/gx_texture.cpp can read it without pulling in this library's internal
// gx.hpp (which isn't on the runtime's include path, and shouldn't need to be for a temporary
// probe). Remove once resolved, along with the matching extern declarations in gx_texture.cpp.
extern "C" {
uint32_t g_nsmbwLiveTevAlphaA[16]{};
uint32_t g_nsmbwLiveTevAlphaB[16]{};
uint32_t g_nsmbwLiveTevAlphaC[16]{};
uint32_t g_nsmbwLiveTevAlphaD[16]{};
uint32_t g_nsmbwLiveTevTexMap[16]{};
// Alpha component of TEV constant color registers 0-3 (GX_CA_APREV/A0/A1/A2's backing store) -
// P_stripe_00's alpha combiner reads as lerp(A0, A1, TEXA) (see gx_texture.cpp), so whatever A0
// and A1 actually hold determines the real on-screen opacity regardless of texture data.
float g_nsmbwLiveTevRegAlpha[4]{};
// Active stage count (genMode, BP 0x00) - stages beyond this index hold stale state from
// whatever material last touched that slot, so the trigger site must not treat them as live.
uint32_t g_nsmbwLiveNumTevStages{};
// Which color channel each TEV stage's rasterized (RASA/RASC) input actually reads, and that
// channel's config/values - stage1's alphaIn reads RASA (see gx_texture.cpp), so whether the
// final on-screen alpha (TEXA*RASA) is a faint fade or a bold constant depends entirely on
// whether this channel's alpha source is a low animated constant (matSrc==GX_SRC_REG, value in
// matColor.w) or per-vertex color data (matSrc==GX_SRC_VTX, not captured here - a vertex-level
// probe would be a separate, bigger step).
uint32_t g_nsmbwLiveTevChannelId[16]{};
uint32_t g_nsmbwLiveChanMatSrc[4]{};
float g_nsmbwLiveChanMatColorA[4]{};
float g_nsmbwLiveChanAmbColorA[4]{};
// DIAGNOSTIC (temporary, 2026-09 "every screen shows flat color instead of real texture content"
// investigation): the mirrors above only ever captured the ALPHA combiner (needed for the earlier
// P_stripe_00 fade-opacity question). Whether a pane's actual pixel COLOR comes from its bound
// texture (GX_CC_TEXC=8) or gets replaced by a constant/vertex color (GX_CC_RASC=0, GX_CC_KONST=6,
// GX_CC_ONE=15, etc.) was never captured until now - that's the more likely explanation for solid-
// color panes across every screen, not a per-texture data bug. Mirrors s.colorPass, filled at the
// same BP-decode site as the alpha mirrors above.
uint32_t g_nsmbwLiveTevColorA[16]{};
uint32_t g_nsmbwLiveTevColorB[16]{};
uint32_t g_nsmbwLiveTevColorC[16]{};
uint32_t g_nsmbwLiveTevColorD[16]{};
// RGB of TEV color registers C0-C3 (g_nsmbwLiveTevRegAlpha above only ever captured their alpha
// component). A colorIn formula of a=C0 b=C1 c=TEXC d=ZERO (seen on several NSMBW UI panes) uses
// the texture as a BLEND WEIGHT between these two constant colors, not as the displayed color
// directly - if C0 and C1 are equal (or one of them isn't being animated/set at all), the output
// is a flat color regardless of what the texture's shape/gradient actually looks like.
float g_nsmbwLiveTevRegColorR[4]{};
float g_nsmbwLiveTevRegColorG[4]{};
float g_nsmbwLiveTevRegColorB[4]{};
// DIAGNOSTIC (temporary): a direct GPU-level frame capture (NSMBW_GPU_PEEK_DUMP) proved the
// confirmed-correct draws (right texture, right TEV, right blend, right vertex/UV) never actually
// produce pixels - the final presented frame is a flat, uniform color with zero variation, exactly
// what a render target's own clear color looks like with nothing drawn on top of it. Depth
// testing is the one piece of per-draw state never checked: a Z-buffer that isn't cleared to the
// far plane each frame, or a wrong compare function, would silently discard every fragment during
// rasterization without there being anything wrong in the CPU-side TEV/blend/vertex descriptors
// already verified. Mirrors BP 0x40 (Z mode), decoded a few lines below.
bool g_nsmbwLiveDepthCompare = false;
uint32_t g_nsmbwLiveDepthFunc = 0;
bool g_nsmbwLiveDepthUpdate = false;

// FIX (2026-09, "every screen shows flat color instead of real texture content"): the
// consecutive-draw merge optimization further down this file (search "Try to merge with previous
// draw call") only starts a fresh draw batch when g_gxState.stateDirty is true, which the BP
// register decode above sets on genuine TX_SETMODE/TX_SETIMAGE0-3 changes. NSMBW's actual texture
// pixel-data upload does not go through that path at all: GXSetTevOrder/ColorIn/AlphaIn and
// friends are not bound as NSMBW HLE overrides (see projects/nsmbw/native/nsmbw_gx_overrides.cpp's
// own comment on this), so NSMBW's GXLoadTexObj HLE override
// (runtime/src/hle/gx/gx_texture.cpp) uploads new pixel data straight into aurora's host-side
// texture cache through a separate call path that never touched stateDirty. Reusing the same
// GXTexMapID slot with the same format/dimensions - extremely common for a 2D layout system's many
// same-shaped small UI icons - leaves the BP-encoded descriptor bits unchanged even though the
// actually-bound image differs, so stateDirty stays false and the following draw gets silently
// merged into whatever draw came right before it, both sharing the FIRST draw's texture bind
// group. A GPU-level frame capture (NSMBW_GPU_PEEK_DUMP) confirmed the symptom directly: the final
// rendered image was one flat, uniform color with zero variation - consistent with many
// differently-positioned UI quads all merging into one draw call bound to a single small/flat
// texture. Called from gx_texture.cpp right after a real upload (needsInit or
// textureDataUploaded), so that draw starts its own fresh batch instead of merging - the same
// effect a real BP register change already has. Inert for MKW, which never calls it.
extern "C" void NsmbwInvalidateMergeStateForTextureUpload() {
    aurora::gx::g_gxState.stateDirty = true;
}
}

namespace aurora::gx::fifo {
static Module Log("aurora::gx::fifo");

using IndexBuffer = std::vector<u16>;

static u32 prepare_idx_template(IndexBuffer& buf, GXPrimitive prim, u16 vtxCount) {
  size_t writePos = 0;
  if (prim == GX_QUADS) {
    // Retain the existing incomplete-quad behavior: every started group emits a complete six-index quad.
    buf.resize(((static_cast<u32>(vtxCount) + 3u) / 4u) * 6u);

    for (u16 v = 0; v < vtxCount; v += 4) {
      const u16 idx0 = v;
      const u16 idx1 = static_cast<u16>(v + 1);
      const u16 idx2 = static_cast<u16>(v + 2);
      const u16 idx3 = static_cast<u16>(v + 3);
      buf[writePos++] = idx0;
      buf[writePos++] = idx1;
      buf[writePos++] = idx2;
      buf[writePos++] = idx2;
      buf[writePos++] = idx3;
      buf[writePos++] = idx0;
    }
  } else if (prim == GX_TRIANGLES) {
    buf.resize(vtxCount);
    for (u16 v = 0; v < vtxCount; ++v) {
      buf[writePos++] = v;
    }
  } else if (prim == GX_TRIANGLEFAN) {
    const u32 indexCount = vtxCount <= 3 ? vtxCount : 3u + (static_cast<u32>(vtxCount) - 3u) * 3u;
    buf.resize(indexCount);
    for (u16 v = 0; v < vtxCount; ++v) {
      if (v < 3) {
        buf[writePos++] = v;
        continue;
      }
      buf[writePos++] = 0;
      buf[writePos++] = static_cast<u16>(v - 1);
      buf[writePos++] = v;
    }
  } else if (prim == GX_TRIANGLESTRIP) {
    const u32 indexCount = vtxCount <= 3 ? vtxCount : 3u + (static_cast<u32>(vtxCount) - 3u) * 3u;
    buf.resize(indexCount);
    for (u16 v = 0; v < vtxCount; ++v) {
      if (v < 3) {
        buf[writePos++] = v;
        continue;
      }
      if ((v & 1) == 0) {
        buf[writePos++] = static_cast<u16>(v - 2);
        buf[writePos++] = static_cast<u16>(v - 1);
      } else {
        buf[writePos++] = static_cast<u16>(v - 1);
        buf[writePos++] = static_cast<u16>(v - 2);
      }
      buf[writePos++] = v;
    }
  } else if (prim == GX_LINES || prim == GX_LINESTRIP || prim == GX_POINTS) {
    buf = {0, 1, 3, 3, 2, 0};
    writePos = 6;
  } else
    UNLIKELY FATAL("unsupported primitive type {}", static_cast<u32>(prim));
  CHECK(writePos == buf.size(), "index template size mismatch ({} != {})", writePos, buf.size());
  return static_cast<u32>(writePos);
}

// GX FIFO opcodes - use CP_ prefix to avoid clashing with GXCommandList.h macros
static constexpr u8 CP_CMD_NOP = GX_NOP;
static constexpr u8 CP_CMD_LOAD_CP_REG = GX_LOAD_CP_REG;
static constexpr u8 CP_CMD_LOAD_XF_REG = GX_LOAD_XF_REG;
static constexpr u8 CP_CMD_LOAD_INDX_A = GX_LOAD_INDX_A;
static constexpr u8 CP_CMD_LOAD_INDX_B = GX_LOAD_INDX_B;
static constexpr u8 CP_CMD_LOAD_INDX_C = GX_LOAD_INDX_C;
static constexpr u8 CP_CMD_LOAD_INDX_D = GX_LOAD_INDX_D;
static constexpr u8 CP_CMD_CALL_DL = GX_CMD_CALL_DL;
static constexpr u8 CP_CMD_INVAL_VTX = GX_CMD_INVL_VC;
static constexpr u8 CP_CMD_LOAD_BP_REG = GX_LOAD_BP_REG & GX_OPCODE_MASK;

// Primitive type mask
static constexpr u8 CP_OPCODE_MASK = GX_OPCODE_MASK;
static constexpr u8 CP_VAT_MASK = GX_VAT_MASK;
static constexpr u8 CP_PRIMITIVE_START = 0x80;
static constexpr u8 CP_PRIMITIVE_END = 0xBF;

// Read helpers for big/little endian
#if _MSC_VER
template<typename T>
__forceinline // Yes, this was necessary.
inline T unaligned_load(const T* ptr) {
  return *static_cast<const __unaligned T*>(ptr);
}
#else
template<typename T>
inline T unaligned_load(const T* ptr) {
  T copy;
  memcpy(&copy, ptr, sizeof(T));
  return copy;
}
#endif

static inline u16 read_u16(const u8* ptr, bool bigEndian) {
  const u16 val = unaligned_load(reinterpret_cast<const u16*>(ptr));
  if (bigEndian) {
    return bswap(val);
  }
  return val;
}

static inline u32 read_u32(const u8* ptr, bool bigEndian) {
  const u32 val = unaligned_load(reinterpret_cast<const u32*>(ptr));
  if (bigEndian) {
    return bswap(val);
  }
  return val;
}

static bool is_draw_cmd(u8 cmd) {
  return cmd >= CP_PRIMITIVE_START && cmd <= CP_PRIMITIVE_END;
}

static GXPrimitive primitive_from_draw_cmd(u8 cmd) {
  switch (cmd & CP_OPCODE_MASK) {
  case GX_DRAW_QUADS:
  case 0x88:
    return GX_QUADS;
  case GX_DRAW_TRIANGLES:
    return GX_TRIANGLES;
  case GX_DRAW_TRIANGLE_STRIP:
    return GX_TRIANGLESTRIP;
  case GX_DRAW_TRIANGLE_FAN:
    return GX_TRIANGLEFAN;
  case GX_DRAW_LINES:
    return GX_LINES;
  case GX_DRAW_LINE_STRIP:
    return GX_LINESTRIP;
  case GX_DRAW_POINTS:
    return GX_POINTS;
  default:
    UNLIKELY FATAL("unsupported primitive command 0x{:02X}", cmd);
  }
}




static u32 bp_get(u32 reg, u32 size, u32 shift);
static inline f32 read_f32(const u8* ptr, bool bigEndian);

static GXPixelFmt decode_pixel_fmt(u32 peCtrl, u32 cmode1) {
  switch (bp_get(peCtrl, 3, 0)) {
  case 0:
    return GX_PF_RGB8_Z24;
  case 1:
    return GX_PF_RGBA6_Z24;
  case 2:
    return GX_PF_RGB565_Z16;
  case 3:
    return GX_PF_Z24;
  case 4:
    switch (bp_get(cmode1, 2, 9)) {
    case 0:
      return GX_PF_Y8;
    case 1:
      return GX_PF_U8;
    case 2:
      return GX_PF_V8;
    default:
      Log.warn("command_processor: unsupported cmode1 pixel subtype {}", bp_get(cmode1, 2, 9));
      return GX_PF_Y8;
    }
  case 5:
    return GX_PF_YUV420;
  default:
    Log.warn("command_processor: unsupported PE pixel format {}", bp_get(peCtrl, 3, 0));
    return GX_PF_RGB8_Z24;
  }
}

static inline u64 read_u64(const u8* ptr, bool bigEndian) {
  u64 loaded;
  // Unaligned-safe load
  memcpy(&loaded, ptr, sizeof(u64));

  if (bigEndian) {
    return bswap(loaded);
  }

  return loaded;
}

struct TexBpRegMapping {
  u8 texMapId;
  enum class Kind : uint8_t { Mode0, Mode1, Image0, Image1, Image2, Image3, Tlut } kind;
};

static std::optional<TexBpRegMapping> decode_tex_bp_reg(u32 regId) {
  constexpr std::array mode0Ids{0x80u, 0x81u, 0x82u, 0x83u, 0xA0u, 0xA1u, 0xA2u, 0xA3u};
  constexpr std::array mode1Ids{0x84u, 0x85u, 0x86u, 0x87u, 0xA4u, 0xA5u, 0xA6u, 0xA7u};
  constexpr std::array image0Ids{0x88u, 0x89u, 0x8Au, 0x8Bu, 0xA8u, 0xA9u, 0xAAu, 0xABu};
  constexpr std::array image1Ids{0x8Cu, 0x8Du, 0x8Eu, 0x8Fu, 0xACu, 0xADu, 0xAEu, 0xAFu};
  constexpr std::array image2Ids{0x90u, 0x91u, 0x92u, 0x93u, 0xB0u, 0xB1u, 0xB2u, 0xB3u};
  constexpr std::array image3Ids{0x94u, 0x95u, 0x96u, 0x97u, 0xB4u, 0xB5u, 0xB6u, 0xB7u};
  constexpr std::array tlutIds{0x98u, 0x99u, 0x9Au, 0x9Bu, 0xB8u, 0xB9u, 0xBAu, 0xBBu};

  for (u8 i = 0; i < MaxTextures; ++i) {
    if (regId == mode0Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Mode0};
    }
    if (regId == mode1Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Mode1};
    }
    if (regId == image0Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image0};
    }
    if (regId == image1Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image1};
    }
    if (regId == image2Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image2};
    }
    if (regId == image3Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image3};
    }
    if (regId == tlutIds[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Tlut};
    }
  }
  return std::nullopt;
}

// Helper to convert packed RGBA8 to Vec4<float>
static Vec4<float> unpack_color(u32 packed) {
  return {
      static_cast<float>(packed >> 24 & 0xFF) / 255.f,
      static_cast<float>(packed >> 16 & 0xFF) / 255.f,
      static_cast<float>(packed >> 8 & 0xFF) / 255.f,
      static_cast<float>(packed & 0xFF) / 255.f,
  };
}

static inline f32 read_f32(const u8* ptr, bool bigEndian) {
  u32 bits = read_u32(ptr, bigEndian);
  f32 val;
  std::memcpy(&val, &bits, sizeof(val));
  return val;
}

// Marks draw state dirty *and* invalidates the resolved-pipeline memo.
static inline void mark_pipeline_state_dirty() noexcept {
  g_gxState.stateDirty = true;
  g_gxState.pipelineStateGeneration = next_gx_state_epoch();
}

// Rejects a malformed XF write in release builds.
#define XF_REQUIRE(cond, msg, ...)                                                                                     \
  do {                                                                                                                 \
    if (!(cond)) UNLIKELY {                                                                                            \
      CHECK(cond, msg, ##__VA_ARGS__);                                                                                  \
      Log.warn(msg, ##__VA_ARGS__);                                                                                     \
      return true;                                                                                                     \
    }                                                                                                                  \
  } while (0)

// DIAGNOSTIC (temporary): which path last wrote PNMTX0. The runtime sets g_nsmbwXfLoadSource before
// handing aurora a matrix load (1=native GXLoadPosMtxImm override, 2=indexed HLE wrapper, 3=display
// list, 4=raw guest XF packet); copy_xf_data latches it into g_nsmbwPnMtx0Source for the draw dump.
extern "C" uint32_t g_nsmbwXfLoadSource = 0;
extern "C" uint32_t g_nsmbwPnMtx0Source = 0;

static bool copy_xf_data(u32 addr, const u8* data, u32 len, bool bigEndian) {
  if (addr < 0x78) {
    // Position matrices (0x0000 - 0x0077)
    u32 mtxIdx = addr / 12;
    u32 startOffset = addr % 12;
    // We only support full writes to matrices
    XF_REQUIRE(mtxIdx < MaxPnMtx, "XF: PosMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && len == 12, "XF: PosMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    auto& mtx = g_gxState.pnMtx[mtxIdx].pos;
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      flat[i] = read_f32(data + i * 4, bigEndian);
    }
    if (mtxIdx == 0) g_nsmbwPnMtx0Source = g_nsmbwXfLoadSource;
    // DIAGNOSTIC (temporary): NSMBW_LOG_PNMTX0=<profile> - every load into PNMTX0 (any path:
    // immediate, display list, indexed) while that scene is active, sampled, so "who last loaded
    // the view matrix the level draws use, and with what" is visible. Remove once resolved.
    {
      static const long pnScene = [] {
        const char* v = AURORA_ENV("NSMBW_LOG_PNMTX0");
        return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
      }();
      if (mtxIdx == 0 && pnScene >= 0 && g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(pnScene)) {
        static uint64_t n = 0;
        static int lines = 0;
        ++n;
        if (n > 20000 && lines < 40 && (n % 97) == 0) {
          ++lines;
          std::fprintf(stderr, "[NSMBW_PNMTX0] load#%llu row0=(%.3f,%.3f,%.3f,%.1f) row1=(%.3f,%.3f,%.3f,%.1f) row2=(%.3f,%.3f,%.3f,%.1f)\n",
                       static_cast<unsigned long long>(n), flat[0], flat[1], flat[2], flat[3], flat[4], flat[5], flat[6],
                       flat[7], flat[8], flat[9], flat[10], flat[11]);
          std::fflush(stderr);
        }
      }
    }
    g_gxState.stateDirty = true;
    return true;
  } else if (addr < 0x0F0) {
    // Texture matrices (0x078-0x0EF)
    u32 texBase = addr - 0x078;
    u32 mtxIdx = texBase / 12;
    u32 startOffset = texBase % 12;
    XF_REQUIRE(mtxIdx < MaxTexMtx, "XF TexMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && (len == 8 || len == 12), "XF TexMtx sub-copy unsupported: offs={}, len={}",
               startOffset, len);

    // Determine if 2x4 or 3x4 from count
    auto& mtx = g_gxState.texMtxs[mtxIdx];
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      flat[i] = read_f32(data + i * 4, bigEndian);
    }
    g_gxState.stateDirty = true;
    return true;
  } else if (addr >= 0x400 && addr < 0x45A) {
    // Normal matrices (0x400-0x459)
    u32 nrmBase = addr - 0x400;
    u32 mtxIdx = nrmBase / 9;
    u32 startOffset = nrmBase % 9;
    // We only support full writes to matrices
    XF_REQUIRE(mtxIdx < MaxPnMtx, "XF: NrmMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && len == 9, "XF: NrmMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    auto& mtx = g_gxState.pnMtx[mtxIdx].nrm;
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      u32 xfIdx = i;
      u32 row = xfIdx / 3;
      u32 col = xfIdx % 3;
      if (row < 3) {
        flat[row * 4 + col] = read_f32(data + i * 4, bigEndian);
      }
    }
    g_gxState.stateDirty = true;
    return true;
  } else if (addr >= 0x500 && addr < 0x5F0) {
    // Post-transform texture matrices (0x500-0x5EF)
    u32 ptBase = addr - 0x500;
    u32 mtxIdx = ptBase / 12;
    u32 startOffset = ptBase % 12;
    XF_REQUIRE(mtxIdx < MaxPTTexMtx, "XF: PTTexMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && len == 12, "XF: PTTexMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    auto& mtx = g_gxState.ptTexMtxs[mtxIdx];
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      flat[startOffset + i] = read_f32(data + i * 4, bigEndian);
    }
    g_gxState.stateDirty = true;
    return true;
  } else if (addr >= 0x600 && addr < 0x680) {
    // Lights (0x600-0x67F) - 8 lights, 16 values each
    u32 lightBase = addr - 0x600;
    u32 lightIdx = lightBase / 0x10;
    u32 startOffset = lightBase % 0x10;
    XF_REQUIRE(lightIdx < GX::MaxLights, "XF: Light copy oob; lightIdx={}", lightIdx);
    XF_REQUIRE(startOffset + len <= 0x10,
               "XF: Light copy that crosses across light boundaries unsupported: offs={}, len={}", startOffset, len);
    auto& light = g_gxState.lights[lightIdx];
    for (u32 i = 0; i < len; i++) {
      u32 field = startOffset + i;
      f32 val = read_f32(data + i * 4, bigEndian);
      u32 ival = read_u32(data + i * 4, bigEndian);
      switch (field) {
      case 3: // Color (packed u32)
        light.color = unpack_color(ival);
        break;
      case 4:
        light.cosAtt[0] = val;
        break; // a0
      case 5:
        light.cosAtt[1] = val;
        break; // a1
      case 6:
        light.cosAtt[2] = val;
        break; // a2
      case 7:
        light.distAtt[0] = val;
        break; // k0
      case 8:
        light.distAtt[1] = val;
        break; // k1
      case 9:
        light.distAtt[2] = val;
        break; // k2
      case 10:
        light.pos[0] = val;
        break; // px
      case 11:
        light.pos[1] = val;
        break; // py
      case 12:
        light.pos[2] = val;
        break; // pz
      case 13:
        light.dir[0] = val;
        break; // nx
      case 14:
        light.dir[1] = val;
        break; // ny
      case 15:
        light.dir[2] = val;
        break; // nz
      default:
        break; // padding (0-2)
      }
    }
    g_gxState.preparedLightsDirty = true;
    g_gxState.stateDirty = true;
    return true;
  }
  return false;
}

static void apply_xf_viewport() {
  const auto& vp = g_gxState.xfViewport;
  const f32 sx = vp[0];
  const f32 sy = vp[1];
  const f32 sz = vp[2];
  const f32 ox = vp[3];
  const f32 oy = vp[4];
  const f32 oz = vp[5];
  const f32 width = sx * 2.0f;
  const f32 height = -sy * 2.0f;
  constexpr f32 z24Scale = 16777216.0f;

  const gfx::Viewport lv{
      .left = ox - 340.0f - width / 2.0f,
      .top = oy - 340.0f - height / 2.0f,
      .width = width,
      .height = height,
      .znear = (oz - sz) / z24Scale,
      .zfar = oz / z24Scale,
  };
  // DIAGNOSTIC (temporary): "correctly-drawn content never appears on screen" investigation.
  // Every per-draw GX state checked so far (texture, TEV routing, blend mode, vertex/UV geometry)
  // is confirmed correct - the one thing never checked is whether the XF viewport (sx/sy/ox/oy,
  // set via GXSetViewport) that maps a draw's local vertex coordinates onto the screen is sane for
  // NSMBW's actual values, or whether it puts everything off-screen/degenerate despite the source
  // geometry being correct. Remove once resolved.
  if (AURORA_ENV("NSMBW_LOG_VIEWPORT") != nullptr) {
    static int vpLogged = 0;
    if (vpLogged < 20) {
      ++vpLogged;
      std::fprintf(stderr,
          "[NSMBW_VIEWPORT] call#%d raw sx=%.2f sy=%.2f sz=%.2f ox=%.2f oy=%.2f oz=%.2f "
          "-> logical left=%.2f top=%.2f width=%.2f height=%.2f znear=%.4f zfar=%.4f\n",
          vpLogged, sx, sy, sz, ox, oy, oz, lv.left, lv.top, lv.width, lv.height, lv.znear, lv.zfar);
      std::fflush(stderr);
    }
  }
  set_logical_viewport(lv);
}

static void apply_xf_projection() {
  const auto& raw = g_gxState.xfProjection;
  auto& proj = g_gxState.proj;
  proj = {};
  proj.m0[0] = raw[0];
  proj.m1[1] = raw[2];
  proj.m2[2] = raw[4];
  proj.m2[3] = raw[5];

  if (g_gxState.projType == GX_ORTHOGRAPHIC) {
    proj.m0[3] = raw[1];
    proj.m1[3] = raw[3];
    proj.m3[3] = 1.0f;
  } else {
    proj.m0[2] = raw[1];
    proj.m1[2] = raw[3];
    proj.m3[2] = -1.0f;
  }

  g_gxState.stateDirty = true;
  // NSMBW_LOG_PROJ_SCENE (temporary): the 40-load budget below is spent during boot; this
  // re-points it at the first 40 loads seen while a given scene profile is active (decimal
  // fProf value, e.g. "5" for STAGE) so the 3D scene's own projections are visible.
  static const long projScene = [] {
    const char* v = AURORA_ENV("NSMBW_LOG_PROJ_SCENE");
    return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
  }();
  const bool sceneMatch = projScene >= 0 && g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(projScene);
  if (aurora::nsmbw_diag_enabled() || sceneMatch) {
    // TEMP: was a one-shot bool that only ever logged the very first projection load, silently
    // missing every later change (including a corrupted-looking one found around draw#8 in the
    // NSMBW garbled-screen investigation) - now logs the first 40 loads so changes are visible.
    static int logged = 0;
    static uint32_t lastScene = 0xFFFFFFFFu;
    if (sceneMatch && lastScene != g_nsmbwCurrentSceneProfile) {
      lastScene = g_nsmbwCurrentSceneProfile;
      logged = 0;
    }
    if (logged < 40) {
      ++logged;
      std::fprintf(stderr,
                   "[NSMBW_PROJ] type=%d raw=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f proj m0=%.4f,%.4f,%.4f,%.4f "
                   "m1=%.4f,%.4f,%.4f,%.4f m2=%.4f,%.4f,%.4f,%.4f m3=%.4f,%.4f,%.4f,%.4f\n",
                   static_cast<int>(g_gxState.projType), raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                   proj.m0[0], proj.m0[1], proj.m0[2], proj.m0[3], proj.m1[0], proj.m1[1], proj.m1[2], proj.m1[3],
                   proj.m2[0], proj.m2[1], proj.m2[2], proj.m2[3], proj.m3[0], proj.m3[1], proj.m3[2], proj.m3[3]);
      std::fflush(stderr);
    }
  }
}

// Forward declarations for register handlers
static void handle_bp(u32 value, bool bigEndian);
static void handle_cp(u8 addr, u32 value, bool bigEndian);
static void handle_xf(const u8* data, u32& pos, u32 size, bool bigEndian);
static bool handle_draw(u8 cmd, const u8* data, u32& pos, u32 size, bool bigEndian);
static bool handle_aurora(const u8* data, u32& pos, u32 size, bool bigEndian);

void process(const u8* data, u32 size, bool bigEndian) {
  ZoneScoped;
  // Everything decoded here mutates renderer state (GX state, the recorded command lists and the mapped staging buffers), so take the renderer GPU mutex once for the whole drain rather than once per draw command.
  std::lock_guard gpuLock(aurora::renderer_gpu_mutex());
  u32 pos = 0;

  while (pos < size) {
    u8 cmd = data[pos++];
    u8 opcode = cmd & CP_OPCODE_MASK;
    // Log.warn("Processing opcode {:02x} at pos {} (size {})", opcode, pos - 1, size);

    switch (opcode) {
    case CP_CMD_NOP:
      continue;

    case CP_CMD_LOAD_BP_REG: {
      CHECK(pos + 4 <= size, "BP reg read overrun");
      u32 value = read_u32(data + pos, bigEndian);
      pos += 4;
      handle_bp(value, bigEndian);
      break;
    }

    case CP_CMD_LOAD_CP_REG: {
      CHECK(pos + 5 <= size, "CP reg read overrun");
      u8 addr = data[pos++];
      u32 value = read_u32(data + pos, bigEndian);
      pos += 4;
      handle_cp(addr, value, bigEndian);
      break;
    }

    case CP_CMD_LOAD_XF_REG: {
      handle_xf(data, pos, size, bigEndian);
      break;
    }

    case CP_CMD_LOAD_INDX_A:
    case CP_CMD_LOAD_INDX_B:
    case CP_CMD_LOAD_INDX_C:
    case CP_CMD_LOAD_INDX_D: {
      ZoneScopedN("LOAD_INDX");
      CHECK(pos + 4 <= size, "indexed XF read overrun");
      const u32 value = read_u32(data + pos, bigEndian);
      pos += 4;

      const u32 arrayType = GX_POS_MTX_ARRAY + ((opcode - CP_CMD_LOAD_INDX_A) / 0x08);
      const u32 srcArrayIdx = value >> 16;
      const u16 len = static_cast<u16>(((value >> 12) & 0x0f) + 1);
      const u16 dstAddr = static_cast<u16>(value & 0x0fff);
      auto const& array = g_gxState.arrays[arrayType];
      const u32 byteOffset = srcArrayIdx * array.stride;
      const u32 byteCount = static_cast<u32>(len) * 4u;
      if (array.data == nullptr || array.stride == 0 || byteOffset + byteCount > array.size) {
        static u32 invalidIndexedXfLogCount = 0;
        if (invalidIndexedXfLogCount < 16) {
          Log.warn("Skipping indexed XF load with invalid source array: array={} idx={} stride={} offset={} bytes={} "
                   "size={} dst=0x{:04X}",
                   arrayType, srcArrayIdx, array.stride, byteOffset, byteCount, array.size, dstAddr);
          ++invalidIndexedXfLogCount;
        }
        break;
      }
      u8* srcData = ((u8*)array.data) + byteOffset;
      if (!copy_xf_data(dstAddr, srcData, len, bigEndian)) {
#ifndef NDEBUG
        Log.debug("Unimplemented indexed XF load (opcode 0x{:02X}, dstAddr=0x{:04X})", opcode, dstAddr);
#endif
      }
      break;
    }

    case CP_CMD_CALL_DL: {
      // Call display list: 8 bytes (address + size)
      CHECK(pos + 8 <= size, "call DL read overrun");
      {
        const u32 nestedAddr = read_u32(data + pos, bigEndian);
        const u32 nestedSize = read_u32(data + pos + 4, bigEndian);
        Log.warn("Ignoring nested GX_CMD_CALL_DL target=0x{:08X} size={} at pos {}/{}", nestedAddr,
                 nestedSize, pos - 1, size);
      }
      pos += 8;
      break;
    }

    case CP_CMD_INVAL_VTX: {
      // GXInvalidateVtxCache tells the GPU that CPU-written indexed vertex arrays must be observed by subsequent draws.
      for (int i = GX_VA_POS; i <= GX_VA_TEX7; ++i) {
        g_gxState.arrays[i].cachedRange = {};
      }
      break;
    }

    case GX_LOAD_AURORA: {
      if (!handle_aurora(data, pos, size, bigEndian)) {
        return;
      }
      break;
    }

    default:
      // Draw commands occupy the full 0x80-0xBF range.
      if (is_draw_cmd(cmd)) {
        if (!handle_draw(cmd, data, pos, size, bigEndian)) {
          return;
        }
      } else {
        static u32 unknownLogCount = 0;
        if (unknownLogCount < 16) {
          // Hex dump surrounding bytes for debugging
          u32 dumpStart = (pos > 17) ? pos - 17 : 0;
          u32 dumpEnd = (pos + 16 < size) ? pos + 16 : size;
          std::string hex;
          for (u32 i = dumpStart; i < dumpEnd; i++) {
            if (i == pos - 1)
              hex += fmt::format("[{:02x}]", data[i]);
            else
              hex += fmt::format(" {:02x}", data[i]);
          }
          Log.warn("  hex dump (pos {}-{}):{}", dumpStart, dumpEnd - 1, hex);
          Log.warn("command_processor: unknown opcode 0x{:02X} at pos {}", cmd, pos - 1);
          ++unknownLogCount;
        }
      }
      break;
    }
  }
}

// Helper to extract bit fields from a 32-bit register
inline static u32 bp_get(u32 reg, u32 size, u32 shift) { return reg >> shift & (1u << size) - 1; }

static u8 normal_frac_bits(GXCompType type) {
  switch (type) {
  case GX_U8:
    return 7;
  case GX_S8:
    return 6;
  case GX_U16:
    return 15;
  case GX_S16:
    return 14;
  default:
    return 0;
  }
}

static void refresh_copy_filter_flags() {
  bool aa = false;
  for (const auto& sample : g_gxState.copyFilterSamplePattern) {
    aa |= sample[0] != 6 || sample[1] != 6;
  }
  g_gxState.copyFilterAa = aa;

  static constexpr std::array<u8, 7> DefaultVFilter{0, 0, 21, 22, 21, 0, 0};
  bool vf = false;
  for (size_t i = 0; i < DefaultVFilter.size(); ++i) {
    vf |= g_gxState.copyFilterVFilter[i] != DefaultVFilter[i];
  }
  g_gxState.copyFilterVf = vf;
}

// BP register handler - decodes BP (RAS/pixel engine) register writes and updates g_gxState
static void handle_bp(u32 value, bool bigEndian) {
  u32 regId = (value >> 24) & 0xFF;
  // Mask off the register ID from the value for field extraction
  // (the regId is stored in bits 24-31, data is in bits 0-23)

  if (regId == 0x66) {
    // BP 0x66 is the hardware texture-cache invalidate. The SDK's GXInvalidateTexAll writes 0x66001000 then
    // 0x66001100 (one per cache half) and GXInvalidateTexRegion writes region-specific values. Aurora's own
    // GXInvalidateTexAll uses a private sub-command instead, so a title whose SDK GXInvalidateTexAll runs as
    // translated guest code only ever reaches this register write. Without this the static texture cache
    // revision never bumps and a buffer refilled with a same-shaped texture (NSMBW reloads each level's
    // tileset into the same heap address) keeps serving the previous upload. Checked before the value
    // dedup below: the write is a command, not state, so repeating it must repeat the effect.
    invalidate_static_texture_cache();
    return;
  }
  if (regId == 0xFE) {
    g_gxState.bpRegCache[regId] = value & 0x00FFFFFF;
    return;
  } else {
    const u32 ssMask = g_gxState.bpRegCache[0xFE];
    // A preceding 0xFE write is rare; the common path only has to prove the mask is already wide open.
    if (ssMask != 0x00FFFFFF) UNLIKELY {
      g_gxState.bpRegCache[0xFE] = 0x00FFFFFF;
    }
    const u32 merged = (g_gxState.bpRegCache[regId] & ~ssMask) | (value & ssMask);
    value = (regId << 24) | (merged & 0x00FFFFFF);
    if (g_gxState.bpRegCache[regId] == value) return;
    g_gxState.bpRegCache[regId] = value;
  }
  // TEV color combiner stages (0xC0, 0xC2, 0xC4, ... 0xDE)
  if (regId >= 0xC0 && regId <= 0xDE && (regId & 1) == 0) {
    u32 stage = (regId - 0xC0) / 2;
    if (stage < MaxTevStages) {
      auto& s = g_gxState.tevStages[stage];
      s.colorPass.d = static_cast<GXTevColorArg>(bp_get(value, 4, 0));
      s.colorPass.c = static_cast<GXTevColorArg>(bp_get(value, 4, 4));
      s.colorPass.b = static_cast<GXTevColorArg>(bp_get(value, 4, 8));
      s.colorPass.a = static_cast<GXTevColorArg>(bp_get(value, 4, 12));
      // DIAGNOSTIC (temporary): see the g_nsmbwLiveTevColor* comment above this namespace.
      g_nsmbwLiveTevColorA[stage] = static_cast<uint32_t>(s.colorPass.a);
      g_nsmbwLiveTevColorB[stage] = static_cast<uint32_t>(s.colorPass.b);
      g_nsmbwLiveTevColorC[stage] = static_cast<uint32_t>(s.colorPass.c);
      g_nsmbwLiveTevColorD[stage] = static_cast<uint32_t>(s.colorPass.d);
      s.colorOp.clamp = bp_get(value, 1, 19) != 0;
      s.colorOp.outReg = static_cast<GXTevRegID>(bp_get(value, 2, 22));
      if (bp_get(value, 2, 16) == 3) {
        // Bias==3 means compare mode: reconstruct GXTevOp enum (8 + 3-bit hw value)
        u32 hwOp = bp_get(value, 1, 18) | (bp_get(value, 2, 20) << 1);
        s.colorOp.op = static_cast<GXTevOp>(hwOp + 8);
        s.colorOp.bias = GX_TB_ZERO;
        s.colorOp.scale = GX_CS_SCALE_1;
      } else {
        // Normal mode: bit18 is op (0=ADD, 1=SUB), bits16-17 is bias, bits20-21 is scale
        s.colorOp.op = static_cast<GXTevOp>(bp_get(value, 1, 18));
        s.colorOp.bias = static_cast<GXTevBias>(bp_get(value, 2, 16));
        s.colorOp.scale = static_cast<GXTevScale>(bp_get(value, 2, 20));
      }
      mark_pipeline_state_dirty();
    }
    return;
  }

  // TEV alpha combiner stages (0xC1, 0xC3, 0xC5, ... 0xDF)
  if (regId >= 0xC1 && regId <= 0xDF && (regId & 1) == 1) {
    u32 stage = (regId - 0xC1) / 2;
    if (stage < MaxTevStages) {
      auto& s = g_gxState.tevStages[stage];
      s.tevSwapRas = static_cast<GXTevSwapSel>(bp_get(value, 2, 0));
      s.tevSwapTex = static_cast<GXTevSwapSel>(bp_get(value, 2, 2));
      s.alphaPass.d = static_cast<GXTevAlphaArg>(bp_get(value, 3, 4));
      s.alphaPass.c = static_cast<GXTevAlphaArg>(bp_get(value, 3, 7));
      s.alphaPass.b = static_cast<GXTevAlphaArg>(bp_get(value, 3, 10));
      s.alphaPass.a = static_cast<GXTevAlphaArg>(bp_get(value, 3, 13));
      // DIAGNOSTIC (temporary): see the g_nsmbwLiveTevAlpha* comment above this namespace.
      g_nsmbwLiveTevAlphaA[stage] = static_cast<uint32_t>(s.alphaPass.a);
      g_nsmbwLiveTevAlphaB[stage] = static_cast<uint32_t>(s.alphaPass.b);
      g_nsmbwLiveTevAlphaC[stage] = static_cast<uint32_t>(s.alphaPass.c);
      g_nsmbwLiveTevAlphaD[stage] = static_cast<uint32_t>(s.alphaPass.d);
      s.alphaOp.clamp = bp_get(value, 1, 19) != 0;
      s.alphaOp.outReg = static_cast<GXTevRegID>(bp_get(value, 2, 22));
      if (bp_get(value, 2, 16) == 3) {
        u32 hwOp = bp_get(value, 1, 18) | (bp_get(value, 2, 20) << 1);
        s.alphaOp.op = static_cast<GXTevOp>(hwOp + 8);
        s.alphaOp.bias = GX_TB_ZERO;
        s.alphaOp.scale = GX_CS_SCALE_1;
      } else {
        s.alphaOp.op = static_cast<GXTevOp>(bp_get(value, 1, 18));
        s.alphaOp.bias = static_cast<GXTevBias>(bp_get(value, 2, 16));
        s.alphaOp.scale = static_cast<GXTevScale>(bp_get(value, 2, 20));
      }
      mark_pipeline_state_dirty();
    }
    return;
  }

  switch (regId) {
  // genMode (0x00)
  case 0x00: {
    g_gxState.numTexGens = bp_get(value, 4, 0);
    g_gxState.numChans = bp_get(value, 3, 4);
    // genMode owns the same numTexGens/numChans that XF 0x3F/0x09 decode, so the XF cache can no longer vouch for those two slots.
    g_gxState.invalidateXfReg(0x3F);
    g_gxState.invalidateXfReg(0x09);
    g_gxState.numTevStages = bp_get(value, 4, 10) + 1;
    g_nsmbwLiveNumTevStages = g_gxState.numTevStages; // DIAGNOSTIC (temporary): see comment above namespace.
    u32 hwCull = bp_get(value, 2, 14);
    // Swap front/back to match GX convention
    switch (hwCull) {
    case GX_CULL_FRONT:
      g_gxState.cullMode = GX_CULL_BACK;
      break;
    case GX_CULL_BACK:
      g_gxState.cullMode = GX_CULL_FRONT;
      break;
    default:
      g_gxState.cullMode = static_cast<GXCullMode>(hwCull);
      break;
    }
    g_gxState.numIndStages = bp_get(value, 3, 16);
    mark_pipeline_state_dirty();
    break;
  }

  // Display copy sample pattern (0x01-0x04), six 4-bit samples per BP reg.
  case 0x01:
  case 0x02:
  case 0x03:
  case 0x04: {
    const size_t first = static_cast<size_t>(regId - 0x01) * 6;
    for (size_t i = 0; i < 6; ++i) {
      const size_t sample = first + i;
      g_gxState.copyFilterSamplePattern[sample / 2][sample % 2] = static_cast<u8>(bp_get(value, 4, i * 4));
    }
    refresh_copy_filter_flags();
    break;
  }

  // Indirect texture mask (0x0F).
  case 0x0F:
    g_gxState.indTexMask = static_cast<u8>(value & 0xFF);
    g_gxState.stateDirty = true;
    break;

  // TEV indirect stages (0x10-0x1F)
  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x16:
  case 0x17:
  case 0x18:
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1C:
  case 0x1D:
  case 0x1E:
  case 0x1F: {
    u32 stage = regId - 0x10;
    if (stage < MaxTevStages) {
      auto& s = g_gxState.tevStages[stage];
      s.indTexStage = static_cast<GXIndTexStageID>(bp_get(value, 2, 0));
      s.indTexFormat = static_cast<GXIndTexFormat>(bp_get(value, 2, 2));
      s.indTexBiasSel = static_cast<GXIndTexBiasSel>(bp_get(value, 3, 4));
      s.indTexAlphaSel = static_cast<GXIndTexAlphaSel>(bp_get(value, 2, 7));
      s.indTexMtxId = static_cast<GXIndTexMtxID>(bp_get(value, 4, 9));
      s.indTexWrapS = static_cast<GXIndTexWrap>(bp_get(value, 3, 13));
      s.indTexWrapT = static_cast<GXIndTexWrap>(bp_get(value, 3, 16));
      s.indTexUseOrigLOD = bp_get(value, 1, 19) != 0;
      s.indTexAddPrev = bp_get(value, 1, 20) != 0;
      mark_pipeline_state_dirty();
    }
    break;
  }

  // Scissor registers (0x20, 0x21)
  case 0x20:
  case 0x21: {
    const u32 scis0 = g_gxState.bpRegCache[0x20];
    const u32 scis1 = g_gxState.bpRegCache[0x21];
    const int32_t tp = static_cast<int32_t>(bp_get(scis0, 11, 0)) - 342;
    const int32_t lf = static_cast<int32_t>(bp_get(scis0, 11, 12)) - 342;
    const int32_t bm = static_cast<int32_t>(bp_get(scis1, 11, 0)) - 342;
    const int32_t rt = static_cast<int32_t>(bp_get(scis1, 11, 12)) - 342;
    const int32_t wd = std::max(rt - lf + 1, 0);
    const int32_t ht = std::max(bm - tp + 1, 0);
    set_logical_scissor({lf, tp, wd, ht});
    break;
  }

  // Line/point size (0x22)
  case 0x22: {
    g_gxState.lineWidth = static_cast<u8>(bp_get(value, 8, 0));
    g_gxState.pointSize = static_cast<u8>(bp_get(value, 8, 8));
    g_gxState.lineTexOffset = static_cast<GXTexOffset>(bp_get(value, 3, 16));
    g_gxState.pointTexOffset = static_cast<GXTexOffset>(bp_get(value, 3, 19));
    g_gxState.lineHalfAspect = bp_get(value, 1, 22) != 0;
    g_gxState.stateDirty = true;
    break;
  }

  // Indirect texture scale (0x25, 0x26)
  case 0x25: {
    if (MaxIndStages > 0) {
      g_gxState.indStages[0].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 0));
      g_gxState.indStages[0].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 4));
    }
    if (MaxIndStages > 1) {
      g_gxState.indStages[1].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 8));
      g_gxState.indStages[1].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 12));
    }
    mark_pipeline_state_dirty();
    break;
  }
  case 0x26: {
    if (MaxIndStages > 2) {
      g_gxState.indStages[2].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 0));
      g_gxState.indStages[2].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 4));
    }
    if (MaxIndStages > 3) {
      g_gxState.indStages[3].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 8));
      g_gxState.indStages[3].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 12));
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Indirect texture reference (0x27)
  case 0x27: {
    for (u32 i = 0; i < MaxIndStages; i++) {
      g_gxState.indStages[i].texMapId = static_cast<GXTexMapID>(bp_get(value, 3, i * 6));
      g_gxState.indStages[i].texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, i * 6 + 3));
    }
    mark_pipeline_state_dirty();
    break;
  }

  // TEV order / tref (0x28-0x2F) - 2 stages per register
  case 0x28:
  case 0x29:
  case 0x2A:
  case 0x2B:
  case 0x2C:
  case 0x2D:
  case 0x2E:
  case 0x2F: {
    u32 idx = regId - 0x28;
    u32 stage0 = idx * 2;
    u32 stage1 = idx * 2 + 1;

    // Channel ID reverse mapping from hardware to GX
    static const GXChannelID r2c[] = {GX_COLOR0A0, GX_COLOR1A1,   GX_COLOR0A0,    GX_COLOR1A1,
                                      GX_COLOR0A0, GX_ALPHA_BUMP, GX_ALPHA_BUMPN, GX_COLOR_ZERO};

    if (stage0 < MaxTevStages) {
      auto& s = g_gxState.tevStages[stage0];
      s.texMapId = static_cast<GXTexMapID>(bp_get(value, 3, 0));
      s.texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, 3));
      // bit 6 = tex enable
      if (!bp_get(value, 1, 6)) {
        s.texMapId = GX_TEXMAP_NULL;
      }
      u32 chanHw = bp_get(value, 3, 7);
      s.channelId = (chanHw < 8) ? r2c[chanHw] : GX_COLOR_NULL;
      // DIAGNOSTIC (temporary): see the g_nsmbwLiveTevAlpha* comment above this namespace.
      g_nsmbwLiveTevTexMap[stage0] = static_cast<uint32_t>(s.texMapId);
      g_nsmbwLiveTevChannelId[stage0] = static_cast<uint32_t>(s.channelId);
    }
    if (stage1 < MaxTevStages) {
      auto& s = g_gxState.tevStages[stage1];
      s.texMapId = static_cast<GXTexMapID>(bp_get(value, 3, 12));
      s.texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, 15));
      if (!bp_get(value, 1, 18)) {
        s.texMapId = GX_TEXMAP_NULL;
      }
      u32 chanHw = bp_get(value, 3, 19);
      s.channelId = (chanHw < 8) ? r2c[chanHw] : GX_COLOR_NULL;
      g_nsmbwLiveTevTexMap[stage1] = static_cast<uint32_t>(s.texMapId);
      g_nsmbwLiveTevChannelId[stage1] = static_cast<uint32_t>(s.channelId);
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Z mode (0x40)
  case 0x40: {
    g_gxState.depthCompare = bp_get(value, 1, 0) != 0;
    g_gxState.depthFunc = static_cast<GXCompare>(bp_get(value, 3, 1));
    g_gxState.depthUpdate = bp_get(value, 1, 4) != 0;
    // DIAGNOSTIC (temporary): see the g_nsmbwLiveDepth* comment above this namespace.
    g_nsmbwLiveDepthCompare = g_gxState.depthCompare;
    g_nsmbwLiveDepthFunc = static_cast<uint32_t>(g_gxState.depthFunc);
    g_nsmbwLiveDepthUpdate = g_gxState.depthUpdate;
    mark_pipeline_state_dirty();
    break;
  }

  // Blend mode / cmode0 (0x41)
  case 0x41: {
    bool blendEn = bp_get(value, 1, 0) != 0;
    bool logicEn = bp_get(value, 1, 1) != 0;
    bool dither = bp_get(value, 1, 2) != 0;
    g_gxState.colorUpdate = bp_get(value, 1, 3) != 0;
    g_gxState.alphaUpdate = bp_get(value, 1, 4) != 0;
    // DIAGNOSTIC (temporary): NSMBW_LOG_BP41 - this is the single authoritative place
    // g_gxState.colorUpdate/alphaUpdate get set, from the real BP 0x41 (cmode0) register value,
    // regardless of whether it arrived via the GXSetColorUpdate/GXSetAlphaUpdate C-API HLE (which
    // read-modify-writes the same register) or a raw FIFO/display-list BP write. The C-API always
    // logs en=1, yet every draw's pipeline config reads colorUpdate=0 - this traces every BP 0x41
    // apply (raw 24-bit value + decoded bits) to find what writes it back to 0 in between. Remove
    // once resolved.
    if (AURORA_ENV("NSMBW_LOG_BP41") != nullptr) {
      static int bp41Calls = 0;
      ++bp41Calls;
      if (bp41Calls <= 200) {
        std::fprintf(stderr,
                     "[NSMBW_BP41] call#%d rawValue=0x%06X colorUpdate=%d alphaUpdate=%d blendEn=%d\n",
                     bp41Calls, value & 0xFFFFFFu, g_gxState.colorUpdate ? 1 : 0, g_gxState.alphaUpdate ? 1 : 0,
                     blendEn ? 1 : 0);
        std::fflush(stderr);
      }
    }
    g_gxState.blendFacDst = static_cast<GXBlendFactor>(bp_get(value, 3, 5));
    g_gxState.blendFacSrc = static_cast<GXBlendFactor>(bp_get(value, 3, 8));
    bool subtract = bp_get(value, 1, 11) != 0;
    g_gxState.blendOp = static_cast<GXLogicOp>(bp_get(value, 4, 12));

    if (subtract) {
      g_gxState.blendMode = GX_BM_SUBTRACT;
    } else if (blendEn) {
      g_gxState.blendMode = GX_BM_BLEND;
    } else if (logicEn) {
      g_gxState.blendMode = GX_BM_LOGIC;
    } else {
      g_gxState.blendMode = GX_BM_NONE;
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Dst alpha / cmode1 (0x42)
  case 0x42: {
    u8 alpha = bp_get(value, 8, 0);
    bool enabled = bp_get(value, 1, 8) != 0;
    g_gxState.dstAlpha = enabled ? alpha : UINT32_MAX;
    g_gxState.pixelFmt = decode_pixel_fmt(g_gxState.bpRegCache[0x43], value);
    mark_pipeline_state_dirty();
    break;
  }

  // PE control (0x43) - pixel format, z format, zcomp location
  case 0x43: {
    g_gxState.pixelFmt = decode_pixel_fmt(value, g_gxState.bpRegCache[0x42]);
    g_gxState.zFmt = static_cast<GXZFmt16>(bp_get(value, 3, 3));
    g_gxState.zCompLocBeforeTex = bp_get(value, 1, 6) != 0;
    mark_pipeline_state_dirty();
    break;
  }
  case 0x44:
    g_gxState.fieldMask = value & 0x3u;
    break;

  // Bounding box clear/update registers (0x55, 0x56)
  case 0x55:
  case 0x56: {
    const u32 offset = (regId & 2u);
    g_gxState.boundingBox[offset] = static_cast<u16>(value & 0x3ffu);
    g_gxState.boundingBox[offset + 1] = static_cast<u16>((value >> 10) & 0x3ffu);
    break;
  }
  case 0x58:
    g_gxState.revBits = value & 0x00FFFFFFu;
    break;

  // Scissor box offset (0x59)
  case 0x59: {
    g_gxState.scissorOffsetX = static_cast<s32>(((value & 0x3ffu) << 1) - 0x156u);
    g_gxState.scissorOffsetY = static_cast<s32>(((value & 0xffc00u) >> 9) - 0x156u);
    set_logical_scissor(g_gxState.logicalScissor);
    break;
  }

  // TLUT load address / execute (0x64, 0x65)
  case 0x64:
    break;
  case 0x65: {
    const auto idx = bp_get(value, 10, 0);
    if (idx < MaxTluts) {
      auto& slot = g_gxState.loadedTluts[idx];
      slot.loadTlut0 = g_gxState.bpRegCache[0x64];
      slot.numEntries = static_cast<u16>(bp_get(value, 10, 10) + 1);
    }
    break;
  }
  case 0x68:
    g_gxState.fieldMode = value & 0x1u;
    break;

  // Alpha compare (0xF3)
  case 0xF3: {
    g_gxState.alphaCompare.ref0 = bp_get(value, 8, 0);
    g_gxState.alphaCompare.ref1 = bp_get(value, 8, 8);
    g_gxState.alphaCompare.comp0 = static_cast<GXCompare>(bp_get(value, 3, 16));
    g_gxState.alphaCompare.comp1 = static_cast<GXCompare>(bp_get(value, 3, 19));
    g_gxState.alphaCompare.op = static_cast<GXAlphaOp>(bp_get(value, 2, 22));
    // TEMPORARY DIAGNOSTIC: NSMBW flat-screen isolation. Remove before merging.
    if (nsmbw_diag_enabled()) {
      static int logged = 0;
      if (logged < 300) {
        ++logged;
        std::fprintf(stderr, "[NSMBW_ALPHACMP] ref0=%u ref1=%u comp0=%d comp1=%d op=%d\n",
                     g_gxState.alphaCompare.ref0, g_gxState.alphaCompare.ref1,
                     static_cast<int>(g_gxState.alphaCompare.comp0), static_cast<int>(g_gxState.alphaCompare.comp1),
                     static_cast<int>(g_gxState.alphaCompare.op));
        std::fflush(stderr);
      }
    }
    mark_pipeline_state_dirty();
    break;
  }

  // TEV K color/alpha select (0xF6-0xFD)
  case 0xF6:
  case 0xF7:
  case 0xF8:
  case 0xF9:
  case 0xFA:
  case 0xFB:
  case 0xFC:
  case 0xFD: {
    u32 kselIdx = regId - 0xF6;
    // Swap table entries (packed into pairs of ksel registers)
    if (kselIdx < MaxTevSwap * 2) {
      u32 swapIdx = kselIdx / 2;
      if (kselIdx & 1) {
        g_gxState.tevSwapTable[swapIdx].blue = static_cast<GXTevColorChan>(bp_get(value, 2, 0));
        g_gxState.tevSwapTable[swapIdx].alpha = static_cast<GXTevColorChan>(bp_get(value, 2, 2));
      } else {
        g_gxState.tevSwapTable[swapIdx].red = static_cast<GXTevColorChan>(bp_get(value, 2, 0));
        g_gxState.tevSwapTable[swapIdx].green = static_cast<GXTevColorChan>(bp_get(value, 2, 2));
      }
    }
    // K color/alpha selection for 2 stages per register
    u32 stage0 = kselIdx * 2;
    u32 stage1 = kselIdx * 2 + 1;
    if (stage0 < MaxTevStages) {
      g_gxState.tevStages[stage0].kcSel = static_cast<GXTevKColorSel>(bp_get(value, 5, 4));
      g_gxState.tevStages[stage0].kaSel = static_cast<GXTevKAlphaSel>(bp_get(value, 5, 9));
    }
    if (stage1 < MaxTevStages) {
      g_gxState.tevStages[stage1].kcSel = static_cast<GXTevKColorSel>(bp_get(value, 5, 14));
      g_gxState.tevStages[stage1].kaSel = static_cast<GXTevKAlphaSel>(bp_get(value, 5, 19));
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Fog A/B parameters (0xEE-0xF0)
  // FOG0 (0xEE): A parameter - sign(1)|exp(8)|mantissa(11) partial IEEE 754 float
  case 0xEE: {
    g_gxState.fog.fog0Raw = value;
    u32 a_mant = bp_get(value, 11, 0);
    u32 a_exp = bp_get(value, 8, 11);
    u32 a_sign = bp_get(value, 1, 19);
    u32 a_bits = (a_sign << 31) | (a_exp << 23) | (a_mant << 12);
    std::memcpy(&g_gxState.fog.aRaw, &a_bits, sizeof(g_gxState.fog.aRaw));
    u32 b_s = g_gxState.fog.fog2Raw & 0x1F;
    g_gxState.fog.a = std::ldexp(g_gxState.fog.aRaw, static_cast<int>(b_s));
    g_gxState.stateDirty = true;
    break;
  }
  // FOG1 (0xEF): B mantissa (24-bit)
  case 0xEF: {
    g_gxState.fog.fog1Raw = value;
    g_gxState.fog.bMagnitude = bp_get(value, 24, 0);
    u32 b_s = g_gxState.fog.fog2Raw & 0x1F;
    float B_mant = static_cast<float>(g_gxState.fog.bMagnitude) / 8388638.0f;
    g_gxState.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
    g_gxState.stateDirty = true;
    break;
  }
  // FOG2 (0xF0): B shift/exponent (5-bit)
  case 0xF0: {
    g_gxState.fog.fog2Raw = value;
    u32 b_s = bp_get(value, 5, 0);
    g_gxState.fog.bShift = b_s;
    u32 a_mant = bp_get(g_gxState.fog.fog0Raw, 11, 0);
    u32 a_exp = bp_get(g_gxState.fog.fog0Raw, 8, 11);
    u32 a_sign = bp_get(g_gxState.fog.fog0Raw, 1, 19);
    u32 a_bits = (a_sign << 31) | (a_exp << 23) | (a_mant << 12);
    std::memcpy(&g_gxState.fog.aRaw, &a_bits, sizeof(g_gxState.fog.aRaw));
    g_gxState.fog.a = std::ldexp(g_gxState.fog.aRaw, static_cast<int>(b_s));
    g_gxState.fog.bMagnitude = bp_get(g_gxState.fog.fog1Raw, 24, 0);
    float B_mant = static_cast<float>(g_gxState.fog.bMagnitude) / 8388638.0f;
    g_gxState.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
    g_gxState.stateDirty = true;
    break;
  }

  // Fog type + C parameter from FOG3 (0xF1)
  case 0xF1: {
    const u32 fogFunc = bp_get(value, 3, 21);
    const u32 fogProj = bp_get(value, 1, 20);
    GXFogType fogType = static_cast<GXFogType>(fogFunc | (fogProj << 3));
    g_gxState.fog.type = fogType;
    // DEBUG (NSMBW_DEBUG_NO_FOG): force fog off, to test whether fog is what blacks out a scene.
    if (AURORA_ENV("NSMBW_DEBUG_NO_FOG") != nullptr) g_gxState.fog.type = GX_FOG_NONE;
    // Decode C parameter (same partial float encoding as A)
    u32 c_mant = bp_get(value, 11, 0);
    u32 c_exp = bp_get(value, 8, 11);
    u32 c_sign = bp_get(value, 1, 19);
    u32 c_bits = (c_sign << 31) | (c_exp << 23) | (c_mant << 12);
    std::memcpy(&g_gxState.fog.c, &c_bits, sizeof(g_gxState.fog.c));
    mark_pipeline_state_dirty();
    break;
  }

  // Fog color from FOGCLR (0xF2)
  case 0xF2: {
    u8 b = bp_get(value, 8, 0);
    u8 g = bp_get(value, 8, 8);
    u8 r = bp_get(value, 8, 16);
    g_gxState.fog.color = {
        static_cast<float>(r) / 255.f,
        static_cast<float>(g) / 255.f,
        static_cast<float>(b) / 255.f,
        1.f,
    };
    g_gxState.stateDirty = true;
    break;
  }

  // TEV and K color registers (0xE0-0xE7): even are RA, odd are BG.
  // Bit 23 selects a K color register over a TEV color register.
  case 0xE0:
  case 0xE1:
  case 0xE2:
  case 0xE3:
  case 0xE4:
  case 0xE5:
  case 0xE6:
  case 0xE7: {
    u32 idx = (regId - 0xE0) / 2;
    bool isRA = (regId & 1) == 0;
    bool isKColor = bp_get(value, 1, 23) != 0;

    if (isKColor) {
      // K color register (8-bit components)
      if (idx < GX_MAX_KCOLOR) {
        auto& kc = g_gxState.kcolors[idx];
        if (isRA) {
          kc[0] = static_cast<float>(bp_get(value, 8, 0)) / 255.f;  // R
          kc[3] = static_cast<float>(bp_get(value, 8, 12)) / 255.f; // A
        } else {
          kc[2] = static_cast<float>(bp_get(value, 8, 0)) / 255.f;  // B
          kc[1] = static_cast<float>(bp_get(value, 8, 12)) / 255.f; // G
        }
        g_gxState.stateDirty = true;
      }
    } else {
      // TEV color register (11-bit signed components)
      if (idx < MaxTevRegs) {
        auto& cr = g_gxState.colorRegs[idx];
        if (isRA) {
          // 11-bit signed: sign-extend from 11 bits
          s32 r = bp_get(value, 11, 0);
          if (r & 0x400)
            r |= ~0x7FF; // sign extend
          s32 a = bp_get(value, 11, 12);
          if (a & 0x400)
            a |= ~0x7FF;
          cr[0] = static_cast<float>(r) / 255.f;
          cr[3] = static_cast<float>(a) / 255.f;
          // DIAGNOSTIC (temporary): see the g_nsmbwLiveTevRegAlpha/g_nsmbwLiveTevRegColor* comments
          // above this namespace.
          if (idx < 4) {
            g_nsmbwLiveTevRegAlpha[idx] = cr[3];
            g_nsmbwLiveTevRegColorR[idx] = cr[0];
          }
        } else {
          s32 b = bp_get(value, 11, 0);
          if (b & 0x400)
            b |= ~0x7FF;
          s32 g = bp_get(value, 11, 12);
          if (g & 0x400)
            g |= ~0x7FF;
          cr[2] = static_cast<float>(b) / 255.f;
          cr[1] = static_cast<float>(g) / 255.f;
          if (idx < 4) {
            g_nsmbwLiveTevRegColorG[idx] = cr[1];
            g_nsmbwLiveTevRegColorB[idx] = cr[2];
          }
        }
        g_gxState.stateDirty = true;
      }
    }
    break;
  }

  // Indirect texture matrices (0x06-0x0E), three consecutive registers per 3x2 matrix:
  // matrix 0 at 0x06, matrix 1 at 0x09, matrix 2 at 0x0C.
  case 0x06:
  case 0x07:
  case 0x08:
  case 0x09:
  case 0x0A:
  case 0x0B:
  case 0x0C:
  case 0x0D:
  case 0x0E: {
    u32 idx = (regId - 0x06) / 3;    // matrix index (0-2)
    u32 column = (regId - 0x06) % 3; // column index (0-2)
    auto& info = g_gxState.indTexMtxs[idx];

    // Decode one packed matrix column: [m[0][column], m[1][column]].
    s32 col0 = bp_get(value, 11, 0);
    if (col0 & 0x400)
      col0 |= ~0x7FF; // sign-extend from 11 bits
    s32 col1 = bp_get(value, 11, 11);
    if (col1 & 0x400)
      col1 |= ~0x7FF;

    auto& packedColumn = column == 0 ? info.mtx.m0 : (column == 1 ? info.mtx.m1 : info.mtx.m2);
    packedColumn.x = static_cast<float>(col0) / 1024.0f;
    packedColumn.y = static_cast<float>(col1) / 1024.0f;

    // Accumulate the indirect matrix scale exponent. The SDK writes two bits per column, but the
    // hardware ignores the third column's top bit, leaving 5 bits for adjScale = scaleExp + 17.
    u32 scaleBits = bp_get(value, 2, 22);
    u32 shift = column * 2;
    if (column == 2) {
      info.adjScaleRaw = (info.adjScaleRaw & ~(1u << shift)) | ((scaleBits & 1u) << shift);
    } else {
      info.adjScaleRaw = (info.adjScaleRaw & ~(3u << shift)) | (scaleBits << shift);
    }
    info.scaleExp = static_cast<s8>(info.adjScaleRaw) - 17;

    g_gxState.stateDirty = true;
    break;
  }

  // SU texture coordinate scale registers (0x30-0x3F): even (suTs0) carry S-axis scale, bias, cyl
  // wrap and line/point offset; odd (suTs1) carry the T-axis equivalents.
  case 0x30:
  case 0x31:
  case 0x32:
  case 0x33:
  case 0x34:
  case 0x35:
  case 0x36:
  case 0x37:
  case 0x38:
  case 0x39:
  case 0x3A:
  case 0x3B:
  case 0x3C:
  case 0x3D:
  case 0x3E:
  case 0x3F: {
    u32 coordIdx = (regId - 0x30) / 2;
    bool isT = (regId & 1) != 0;
    auto& tcs = g_gxState.texCoordScales[coordIdx];
    if (isT) {
      tcs.scaleT = static_cast<u16>(bp_get(value, 16, 0));
      tcs.biasT = bp_get(value, 1, 16) != 0;
      tcs.cylWrapT = bp_get(value, 1, 17) != 0;
    } else {
      tcs.scaleS = static_cast<u16>(bp_get(value, 16, 0));
      tcs.biasS = bp_get(value, 1, 16) != 0;
      tcs.cylWrapS = bp_get(value, 1, 17) != 0;
      tcs.lineOffset = bp_get(value, 1, 18) != 0;
      tcs.pointOffset = bp_get(value, 1, 19) != 0;
    }
    g_gxState.stateDirty = true;
    break;
  }

  // Copy clear color (0x4F-0x50) and depth (0x51)
  case 0x49: {
    g_gxState.dispCopySrc.x = static_cast<int32_t>(bp_get(value, 10, 0));
    g_gxState.dispCopySrc.y = static_cast<int32_t>(bp_get(value, 10, 10));
    break;
  }
  case 0x4A: {
    g_gxState.dispCopySrc.width = static_cast<int32_t>(bp_get(value, 10, 0) + 1u);
    g_gxState.dispCopySrc.height = static_cast<int32_t>(bp_get(value, 10, 10) + 1u);
    break;
  }
  case 0x4D: {
    g_gxState.dispCopyDstWidth = static_cast<u16>(((bp_get(value, 10, 0) << 5) + 31u) >> 1);
    break;
  }
  case 0x4E: {
    const u32 iScale = bp_get(value, 9, 0);
    if (iScale != 0) {
      g_gxState.dispCopyYScale = 256.f / static_cast<float>(iScale);
    }
    break;
  }
  case 0x4F: {
    u8 r = bp_get(value, 8, 0);
    u8 a = bp_get(value, 8, 8);
    g_gxState.clearColor[0] = static_cast<float>(r) / 255.f;
    g_gxState.clearColor[3] = static_cast<float>(a) / 255.f;
    g_gxState.stateDirty = true;
    break;
  }
  case 0x50: {
    u8 b = bp_get(value, 8, 0);
    u8 g = bp_get(value, 8, 8);
    g_gxState.clearColor[2] = static_cast<float>(b) / 255.f;
    g_gxState.clearColor[1] = static_cast<float>(g) / 255.f;
    g_gxState.stateDirty = true;
    break;
  }
  case 0x51: {
    g_gxState.clearDepth = bp_get(value, 24, 0);
    g_gxState.stateDirty = true;
    break;
  }
  case 0xF4: {
    g_gxState.zTextureBias = value & 0x00FFFFFFu;
    mark_pipeline_state_dirty();
    break;
  }
  case 0xF5: {
    g_gxState.zTextureFmt = static_cast<u8>(bp_get(value, 2, 0));
    g_gxState.zTextureOp = static_cast<GXZTexOp>(bp_get(value, 2, 2));
    mark_pipeline_state_dirty();
    break;
  }
  case 0x52: {
    g_gxState.copyClamp = static_cast<GXFBClamp>(bp_get(value, 2, 0));
    g_gxState.texCopyFmt = static_cast<GXTexFmt>(bp_get(value, 4, 3));
    g_gxState.dispCopyGamma = static_cast<GXGamma>(bp_get(value, 2, 7));
    g_gxState.texCopyHalfScale = bp_get(value, 1, 9) != 0;
    g_gxState.dispCopyFrame2Field = bp_get(value, 2, 12);
    break;
  }
  case 0x53: {
    g_gxState.copyFilterVFilter[0] = static_cast<u8>(bp_get(value, 6, 0));
    g_gxState.copyFilterVFilter[1] = static_cast<u8>(bp_get(value, 6, 6));
    g_gxState.copyFilterVFilter[2] = static_cast<u8>(bp_get(value, 6, 12));
    g_gxState.copyFilterVFilter[3] = static_cast<u8>(bp_get(value, 6, 18));
    refresh_copy_filter_flags();
    break;
  }
  case 0x54: {
    g_gxState.copyFilterVFilter[4] = static_cast<u8>(bp_get(value, 6, 0));
    g_gxState.copyFilterVFilter[5] = static_cast<u8>(bp_get(value, 6, 6));
    g_gxState.copyFilterVFilter[6] = static_cast<u8>(bp_get(value, 6, 12));
    refresh_copy_filter_flags();
    break;
  }
  case 0xE8:
  case 0xE9:
  case 0xEA:
  case 0xEB:
  case 0xEC:
  case 0xED:
    g_gxState.fogRange[regId - 0xE8] = value & 0x00FFFFFFu;
    mark_pipeline_state_dirty();
    break;

  default:
    if (const auto mapping = decode_tex_bp_reg(regId); mapping.has_value()) {
      auto& slot = g_gxState.loadedTextures[mapping->texMapId];
      bool changed = false;
      switch (mapping->kind) {
      case TexBpRegMapping::Kind::Mode0:
        changed = slot.mode0 != value;
        if (changed) {
          slot.mode0 = value;
        }
        break;
      case TexBpRegMapping::Kind::Mode1:
        changed = slot.mode1 != value;
        if (changed) {
          slot.mode1 = value;
        }
        break;
      case TexBpRegMapping::Kind::Image0:
        changed = slot.image0 != value;
        if (changed) {
          slot.image0 = value;
          slot.mWidth = 0;
          slot.mHeight = 0;
          slot.mFormat = gfx::InvalidTextureFormat;
        }
        break;
      case TexBpRegMapping::Kind::Image3:
        changed = slot.image3 != value;
        if (changed) {
          slot.image3 = value;
        }
        break;
      case TexBpRegMapping::Kind::Tlut:
        // TLUT region's TMEM offset
        break;
      case TexBpRegMapping::Kind::Image1:
      case TexBpRegMapping::Kind::Image2:
        // GXTexRegion regs
        break;
      }
      if (changed) {
        g_gxState.stateDirty = true;
      }
    } else {
#ifndef NDEBUG
      Log.debug("Unhandled BP register 0x{:02X} (value 0x{:06X})", regId, value & 0xFFFFFF);
#endif
    }
    break;
  }
}

extern "C" void GXApplyBPReg(u8 reg, u32 value) {
  handle_bp((static_cast<u32>(reg) << 24) | (value & 0x00FFFFFFu), true);
}

static bool cacheable_cp_register(u8 addr) {
  return addr == 0x30 || addr == 0x40 || addr == 0x50 || addr == 0x60 || (addr >= 0x70 && addr <= 0x97);
}

static std::array<u32, 0x100> s_cpRegisterCache{};
static std::array<bool, 0x100> s_cpRegisterCacheValid{};

void reset_cp_register_cache() {
  s_cpRegisterCache.fill(0);
  s_cpRegisterCacheValid.fill(false);
}

static bool cp_register_write_unchanged(u8 addr, u32 value) {
  if (!cacheable_cp_register(addr)) return false;

  if (s_cpRegisterCacheValid[addr] && s_cpRegisterCache[addr] == value) {
    return true;
  }
  s_cpRegisterCacheValid[addr] = true;
  s_cpRegisterCache[addr] = value;
  return false;
}

// CP register handler - decodes CP register writes and updates g_gxState
static void handle_cp(u8 addr, u32 value, bool bigEndian) {
  if (cp_register_write_unchanged(addr, value)) return;

  switch (addr) {
  // VCD low (0x50)
  case 0x50: {
    auto& vd = g_gxState.vtxDesc;
    auto& svd = g_gxState.sourceVtxDesc;
    vd[GX_VA_PNMTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 0));
    vd[GX_VA_TEX0MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 1));
    vd[GX_VA_TEX1MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 2));
    vd[GX_VA_TEX2MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 3));
    vd[GX_VA_TEX3MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 4));
    vd[GX_VA_TEX4MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 5));
    vd[GX_VA_TEX5MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 6));
    vd[GX_VA_TEX6MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 7));
    vd[GX_VA_TEX7MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 8));
    vd[GX_VA_POS] = static_cast<GXAttrType>(bp_get(value, 2, 9));
    vd[GX_VA_NRM] = static_cast<GXAttrType>(bp_get(value, 2, 11));
    vd[GX_VA_CLR0] = static_cast<GXAttrType>(bp_get(value, 2, 13));
    vd[GX_VA_CLR1] = static_cast<GXAttrType>(bp_get(value, 2, 15));
    for (int attr = GX_VA_PNMTXIDX; attr <= GX_VA_CLR1; ++attr) {
      svd[attr] = vd[attr];
    }
    mark_pipeline_state_dirty();
    g_gxState.clearVtxSizeCache();
    break;
  }

  // VCD high (0x60)
  case 0x60: {
    auto& vd = g_gxState.vtxDesc;
    auto& svd = g_gxState.sourceVtxDesc;
    vd[GX_VA_TEX0] = static_cast<GXAttrType>(bp_get(value, 2, 0));
    vd[GX_VA_TEX1] = static_cast<GXAttrType>(bp_get(value, 2, 2));
    vd[GX_VA_TEX2] = static_cast<GXAttrType>(bp_get(value, 2, 4));
    vd[GX_VA_TEX3] = static_cast<GXAttrType>(bp_get(value, 2, 6));
    vd[GX_VA_TEX4] = static_cast<GXAttrType>(bp_get(value, 2, 8));
    vd[GX_VA_TEX5] = static_cast<GXAttrType>(bp_get(value, 2, 10));
    vd[GX_VA_TEX6] = static_cast<GXAttrType>(bp_get(value, 2, 12));
    vd[GX_VA_TEX7] = static_cast<GXAttrType>(bp_get(value, 2, 14));
    for (int attr = GX_VA_TEX0; attr <= GX_VA_TEX7; ++attr) {
      svd[attr] = vd[attr];
    }
    mark_pipeline_state_dirty();
    g_gxState.clearVtxSizeCache();
    break;
  }

  // Matrix index A (0x30)
  case 0x30: {
    g_gxState.currentPnMtx = bp_get(value, 6, 0) / 3;
    for (u32 i = 0; i < 4 && i < MaxTexCoord; i++) {
      auto texMtx = static_cast<GXTexMtx>(bp_get(value, 6, 6 + i * 6));
      assert(texMtx >= 0 && texMtx <= GXTexMtx::GX_IDENTITY);
      g_gxState.tcgs[i].mtx = texMtx;
    }
    // Same matrix indices as XF 0x18, written from a different bank.
    g_gxState.invalidateXfReg(0x18);
    mark_pipeline_state_dirty();
    break;
  }

  // Matrix index B (0x40)
  case 0x40: {
    for (u32 i = 0; i < 4 && (i + 4) < MaxTexCoord; i++) {
      auto texMtx = static_cast<GXTexMtx>(bp_get(value, 6, i * 6));
      assert(texMtx >= 0 && texMtx <= GXTexMtx::GX_IDENTITY);
      g_gxState.tcgs[i + 4].mtx = texMtx;
    }
    // Same matrix indices as XF 0x19, written from a different bank.
    g_gxState.invalidateXfReg(0x19);
    mark_pipeline_state_dirty();
    break;
  }

  default:
    // VAT A registers (0x70-0x77)
    if (addr >= 0x70 && addr <= 0x77) {
      u32 fmt = addr - 0x70;
      auto& vf = g_gxState.vtxFmts[fmt];
      vf.attrs[GX_VA_POS].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 0));
      vf.attrs[GX_VA_POS].type = static_cast<GXCompType>(bp_get(value, 3, 1));
      vf.attrs[GX_VA_POS].frac = static_cast<u8>(bp_get(value, 5, 4));
      vf.attrs[GX_VA_NRM].type = static_cast<GXCompType>(bp_get(value, 3, 10));
      if (bp_get(value, 1, 31) != 0) {
        vf.attrs[GX_VA_NRM].cnt = GX_NRM_NBT3;
      } else {
        vf.attrs[GX_VA_NRM].cnt = bp_get(value, 1, 9) != 0 ? GX_NRM_NBT : GX_NRM_XYZ;
      }
      vf.attrs[GX_VA_NRM].frac = normal_frac_bits(vf.attrs[GX_VA_NRM].type);
      vf.attrs[GX_VA_CLR0].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 13));
      vf.attrs[GX_VA_CLR0].type = static_cast<GXCompType>(bp_get(value, 3, 14));
      vf.attrs[GX_VA_CLR0].frac = 0;
      vf.attrs[GX_VA_CLR1].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 17));
      vf.attrs[GX_VA_CLR1].type = static_cast<GXCompType>(bp_get(value, 3, 18));
      vf.attrs[GX_VA_CLR1].frac = 0;
      vf.attrs[GX_VA_TEX0].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 21));
      vf.attrs[GX_VA_TEX0].type = static_cast<GXCompType>(bp_get(value, 3, 22));
      vf.attrs[GX_VA_TEX0].frac = static_cast<u8>(bp_get(value, 5, 25));
      mark_pipeline_state_dirty();
      g_gxState.clearVtxSizeCache();
    }
    // VAT B registers (0x80-0x87)
    else if (addr >= 0x80 && addr <= 0x87) {
      u32 fmt = addr - 0x80;
      auto& vf = g_gxState.vtxFmts[fmt];
      vf.attrs[GX_VA_TEX1].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 0));
      vf.attrs[GX_VA_TEX1].type = static_cast<GXCompType>(bp_get(value, 3, 1));
      vf.attrs[GX_VA_TEX1].frac = static_cast<u8>(bp_get(value, 5, 4));
      vf.attrs[GX_VA_TEX2].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 9));
      vf.attrs[GX_VA_TEX2].type = static_cast<GXCompType>(bp_get(value, 3, 10));
      vf.attrs[GX_VA_TEX2].frac = static_cast<u8>(bp_get(value, 5, 13));
      vf.attrs[GX_VA_TEX3].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 18));
      vf.attrs[GX_VA_TEX3].type = static_cast<GXCompType>(bp_get(value, 3, 19));
      vf.attrs[GX_VA_TEX3].frac = static_cast<u8>(bp_get(value, 5, 22));
      vf.attrs[GX_VA_TEX4].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 27));
      vf.attrs[GX_VA_TEX4].type = static_cast<GXCompType>(bp_get(value, 3, 28));
      // TEX4 frac is in VAT C
      mark_pipeline_state_dirty();
      g_gxState.clearVtxSizeCache();
    }
    // VAT C registers (0x90-0x97)
    else if (addr >= 0x90 && addr <= 0x97) {
      u32 fmt = addr - 0x90;
      auto& vf = g_gxState.vtxFmts[fmt];
      vf.attrs[GX_VA_TEX4].frac = static_cast<u8>(bp_get(value, 5, 0));
      vf.attrs[GX_VA_TEX5].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 5));
      vf.attrs[GX_VA_TEX5].type = static_cast<GXCompType>(bp_get(value, 3, 6));
      vf.attrs[GX_VA_TEX5].frac = static_cast<u8>(bp_get(value, 5, 9));
      vf.attrs[GX_VA_TEX6].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 14));
      vf.attrs[GX_VA_TEX6].type = static_cast<GXCompType>(bp_get(value, 3, 15));
      vf.attrs[GX_VA_TEX6].frac = static_cast<u8>(bp_get(value, 5, 18));
      vf.attrs[GX_VA_TEX7].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 23));
      vf.attrs[GX_VA_TEX7].type = static_cast<GXCompType>(bp_get(value, 3, 24));
      vf.attrs[GX_VA_TEX7].frac = static_cast<u8>(bp_get(value, 5, 27));
      mark_pipeline_state_dirty();
      g_gxState.clearVtxSizeCache();
    }
    // Array base addresses (0xA0-0xAF)
    else if (addr >= 0xA0 && addr <= 0xAF) {
      Log.error("CP_REG_ARRAYBASE_ID is not supported on Aurora. Use GX_LOAD_AURORA_ARRAYBASE instead.");
    }
    // Array strides (0xB0-0xBF)
    else if (addr >= 0xB0 && addr <= 0xBF) {
      u32 attrIdx = addr - 0xB0 + GX_VA_POS;
      if (attrIdx < GX_VA_MAX_ATTR) {
        auto& array = g_gxState.arrays[attrIdx];
        const auto newStride = static_cast<u8>(value);
        if (array.stride != newStride) {
          array.stride = newStride;
          mark_pipeline_state_dirty();
        }
      }
    }
    break;
  }
}

// XF register handler - decodes XF (transform unit) register writes and updates g_gxState
static void handle_xf(const u8* data, u32& pos, u32 size, bool bigEndian) {
  // These bounds must hold in release too: CHECK() is a no-op under NDEBUG, so relying on it alone let a truncated guest display list read past `data`.
  if (pos > size || size - pos < 4) UNLIKELY {
      CHECK(false, "XF header read overrun");
      pos = size;
      return;
    }
  u32 header = read_u32(data + pos, bigEndian);
  pos += 4;

  u32 count = ((header >> 16) & 0xFFFF) + 1;
  u32 addr = header & 0xFFFF;
  u32 dataBytes = count * 4;
  // Log.warn("  xf: addr {:04x} count {} dataBytes {} pos {} -> {}", addr, count, dataBytes, pos, pos + dataBytes);
  if (size - pos < dataBytes) UNLIKELY {
      CHECK(false, "XF data read overrun: need {} bytes at pos {}", dataBytes, pos);
      pos = size;
      return;
    }

  const u8* xfData = data + pos;

  if (copy_xf_data(addr, xfData, count, bigEndian)) {
    // copy_xf_data handled everything.
  } else if (addr >= 0x1000) {
    // XF registers (0x1000+)
    u32 xfAddr = addr - 0x1000;
    bool viewportUpdated = false;
    bool projectionUpdated = false;
    for (u32 i = 0; i < count; i++) {
      u32 reg = xfAddr + i;
      u32 val = read_u32(xfData + i * 4, bigEndian);

      // Skip register writes that decode to state we already hold.
      const bool cacheable = reg < g_gxState.xfRegCache.size();
      const bool unchanged = cacheable && g_gxState.xfRegMatches(reg, val);
      if (cacheable) g_gxState.storeXfReg(reg, val);
      // Viewport (0x1A-0x1F) and projection (0x20-0x26) keep their unconditional apply below; only the banks that already had skip semantics and the TexGen bank drop out here.
      if (unchanged && (reg <= 0x19 || reg >= 0x3F)) continue;

      switch (reg) {
      case 0x00:
        g_gxState.xfError = val;
        break;
      case 0x08:
        // XF vertex specs (numColors, numNormals, numTexCoords) - informational
        break;
      case 0x09:
        // numChans
        g_gxState.numChans = val;
        g_gxState.stateDirty = true;
        break;
      case 0x0A:
        // Ambient color 0
        g_gxState.colorChannelState[GX_COLOR0].ambColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA0].ambColor = unpack_color(val);
        g_gxState.stateDirty = true;
        // DIAGNOSTIC (temporary): see g_nsmbwLiveChanMatSrc comment above this namespace.
        g_nsmbwLiveChanAmbColorA[GX_ALPHA0] = g_gxState.colorChannelState[GX_ALPHA0].ambColor.w();
        break;
      case 0x0B:
        // Ambient color 1
        g_gxState.colorChannelState[GX_COLOR1].ambColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA1].ambColor = unpack_color(val);
        g_gxState.stateDirty = true;
        g_nsmbwLiveChanAmbColorA[GX_ALPHA1] = g_gxState.colorChannelState[GX_ALPHA1].ambColor.w();
        break;
      case 0x0C:
        // Material color 0
        g_gxState.colorChannelState[GX_COLOR0].matColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA0].matColor = unpack_color(val);
        g_nsmbwLiveChanMatColorA[GX_ALPHA0] = g_gxState.colorChannelState[GX_ALPHA0].matColor.w();
        g_gxState.stateDirty = true;
        break;
      case 0x0D:
        // Material color 1
        g_gxState.colorChannelState[GX_COLOR1].matColor = unpack_color(val);
        g_gxState.colorChannelState[GX_ALPHA1].matColor = unpack_color(val);
        g_nsmbwLiveChanMatColorA[GX_ALPHA1] = g_gxState.colorChannelState[GX_ALPHA1].matColor.w();
        g_gxState.stateDirty = true;
        break;
      case 0x0E:
      case 0x0F:
      case 0x10:
      case 0x11: {
        // Channel control registers
        u32 chanId = reg - 0x0E;
        if (chanId < MaxColorChannels) {
          auto& chan = g_gxState.colorChannelConfig[chanId];
          chan.matSrc = static_cast<GXColorSrc>(bp_get(val, 1, 0));
          g_nsmbwLiveChanMatSrc[chanId] = static_cast<uint32_t>(chan.matSrc); // DIAGNOSTIC (temporary)
          chan.lightingEnabled = bp_get(val, 1, 1) != 0;
          // DEBUG (NSMBW_DEBUG_NO_LIGHTING): channels output their material colour unlit.
          if (AURORA_ENV("NSMBW_DEBUG_NO_LIGHTING") != nullptr) chan.lightingEnabled = false;
          u32 lightsLo = bp_get(val, 4, 2);
          chan.ambSrc = static_cast<GXColorSrc>(bp_get(val, 1, 6));
          chan.diffFn = static_cast<GXDiffuseFn>(bp_get(val, 2, 7));
          u32 lightsHi = bp_get(val, 4, 11);
          switch (bp_get(val, 2, 9)) {
          case 1:
            chan.attnFn = GX_AF_SPEC;
            break;
          case 3:
            chan.attnFn = GX_AF_SPOT;
            break;
          case 0:
          case 2:
          default:
            chan.attnFn = GX_AF_NONE;
            break;
          }
          u32 lightMask = lightsLo | (lightsHi << 4);
          g_gxState.colorChannelState[chanId].lightMask = GX::LightMask{lightMask};
          mark_pipeline_state_dirty();
        }
        break;
      }
      case 0x12:
        g_gxState.dualTex = val;
        mark_pipeline_state_dirty();
        break;
      case 0x18: {
        // Matrix index A: PnMtx + TexCoord0-3 matrix indices
        g_gxState.currentPnMtx = bp_get(val, 6, 0) / 3;
        for (u32 i = 0; i < 4 && i < MaxTexCoord; i++) {
          auto texMtx = static_cast<GXTexMtx>(bp_get(val, 6, 6 + i * 6));
          assert(texMtx >= 0 && texMtx <= GXTexMtx::GX_IDENTITY);
          g_gxState.tcgs[i].mtx = texMtx;
        }
        mark_pipeline_state_dirty();
        break;
      }
      case 0x19: {
        // Matrix index B: TexCoord4-7 matrix indices
        for (u32 i = 0; i < 4 && (i + 4) < MaxTexCoord; i++) {
          g_gxState.tcgs[i + 4].mtx = static_cast<GXTexMtx>(bp_get(val, 6, i * 6));
        }
        mark_pipeline_state_dirty();
        break;
      }
      case 0x1A:
      case 0x1B:
      case 0x1C:
      case 0x1D:
      case 0x1E:
      case 0x1F: {
        // Viewport: sx, sy, sz, ox, oy, oz at XF 0x101A-0x101F
        const u32 vpOff = reg - 0x1A;
        g_gxState.xfViewport[vpOff] = read_f32(xfData + i * 4, bigEndian);
        viewportUpdated = true;
        break;
      }
      case 0x20:
      case 0x21:
      case 0x22:
      case 0x23:
      case 0x24:
      case 0x25:
      case 0x26: {
        // Projection: 6 params + type at XF 0x1020-0x1026
        const u32 projOff = reg - 0x20;
        if (projOff < g_gxState.xfProjection.size()) {
          g_gxState.xfProjection[projOff] = read_f32(xfData + i * 4, bigEndian);
        } else {
          g_gxState.projType = static_cast<GXProjectionType>(val);
        }
        projectionUpdated = true;
        break;
      }
      case 0x3F:
        // numTexGens
        g_gxState.numTexGens = val;
        mark_pipeline_state_dirty();
        break;
      default:
        // TexGen config (0x40-0x4F) and post-transform (0x50-0x5F)
        if (reg >= 0x40 && reg <= 0x4F) {
          u32 tcIdx = reg - 0x40;
          if (tcIdx < MaxTexCoord) {
            auto& tcg = g_gxState.tcgs[tcIdx];
            bool proj = bp_get(val, 1, 1) != 0;
            u32 form = bp_get(val, 1, 2);
            u32 tgType = bp_get(val, 3, 4);
            u32 srcRow = bp_get(val, 5, 7);
            tcg.inputFormAB11 = form == 0;

            if (tgType == 0) {
              tcg.type = proj ? GX_TG_MTX3x4 : GX_TG_MTX2x4;
            } else if (tgType == 1) {
              // Bump mapping
              tcg.type = static_cast<GXTexGenType>(bp_get(val, 3, 15) + 2);
            } else if (tgType == 2 || tgType == 3) {
              tcg.type = GX_TG_SRTG;
            }

            // Decode source from row
            static const GXTexGenSrc rowToSrc[] = {GX_TG_POS,  GX_TG_NRM,  GX_TG_COLOR0, GX_TG_BINRM, GX_TG_TANGENT,
                                                   GX_TG_TEX0, GX_TG_TEX1, GX_TG_TEX2,   GX_TG_TEX3,  GX_TG_TEX4,
                                                   GX_TG_TEX5, GX_TG_TEX6, GX_TG_TEX7};
            if (srcRow < 13) {
              tcg.src = rowToSrc[srcRow];
            }
            mark_pipeline_state_dirty();
          }
        } else if (reg >= 0x50 && reg <= 0x5F) {
          u32 tcIdx = reg - 0x50;
          if (tcIdx < MaxTexCoord) {
            g_gxState.tcgs[tcIdx].postMtx = static_cast<GXPTTexMtx>(bp_get(val, 6, 0) + 64);
            g_gxState.tcgs[tcIdx].normalize = bp_get(val, 1, 8) != 0;
            mark_pipeline_state_dirty();
          }
        } else {
#ifndef NDEBUG
          Log.debug("Unhandled XF register 0x{:04X} (value 0x{:08X})", reg, val);
#endif
        }
        break;
      }
    }
    if (viewportUpdated) {
      apply_xf_viewport();
    }
    if (projectionUpdated) {
      apply_xf_projection();
    }
  }

  pos += dataBytes;
}

static void handle_draw_overrun(u8 cmd, u16 vtxCount, u32 vtxSize, u32 totalVtxBytes, const u8* data, const u32& pos,
                                u32 size) {
  static u32 truncatedDrawLogCount = 0;
  if (truncatedDrawLogCount >= 64) {
    if (truncatedDrawLogCount == 64) {
      Log.warn("suppressing further truncated draw diagnostics");
    }
    ++truncatedDrawLogCount;
    return;
  }
  ++truncatedDrawLogCount;

  // Hex dump around the draw command for debugging
  u32 cmdPos = pos - 2 - 1; // opcode byte position (before vtxCount and pos++)
  u32 dumpStart = (cmdPos > 16) ? cmdPos - 16 : 0;
  u32 dumpEnd = (cmdPos + 32 < size) ? cmdPos + 32 : size;
  std::string hex;
  for (u32 i = dumpStart; i < dumpEnd; i++) {
    if (i == cmdPos)
      hex += fmt::format("[{:02x}]", data[i]);
    else
      hex += fmt::format(" {:02x}", data[i]);
  }
  Log.warn("  hex dump around truncated draw cmd (pos {}-{}):{}", dumpStart, dumpEnd - 1, hex);
  const auto fmt = static_cast<GXVtxFmt>(cmd & CP_VAT_MASK);
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  Log.warn("  truncated draw cmd=0x{:02X} fmt={} vtxCount={} vtxSize={} desc pn={} pos={} nrm={} clr0={} clr1={} "
            "tex0={} tex1={} tex2={} tex3={} tex4={} tex5={} tex6={} tex7={}",
            cmd, static_cast<u32>(fmt), vtxCount, vtxSize, g_gxState.vtxDesc[GX_VA_PNMTXIDX],
            g_gxState.vtxDesc[GX_VA_POS], g_gxState.vtxDesc[GX_VA_NRM], g_gxState.vtxDesc[GX_VA_CLR0],
            g_gxState.vtxDesc[GX_VA_CLR1], g_gxState.vtxDesc[GX_VA_TEX0], g_gxState.vtxDesc[GX_VA_TEX1],
            g_gxState.vtxDesc[GX_VA_TEX2], g_gxState.vtxDesc[GX_VA_TEX3], g_gxState.vtxDesc[GX_VA_TEX4],
            g_gxState.vtxDesc[GX_VA_TEX5], g_gxState.vtxDesc[GX_VA_TEX6], g_gxState.vtxDesc[GX_VA_TEX7]);
  Log.warn("  fmt {} attrs pos({},{}) nrm({},{}) clr0({},{}) tex0({},{}) tex1({},{})",
            static_cast<u32>(fmt), static_cast<u32>(vtxFmt.attrs[GX_VA_POS].cnt),
            static_cast<u32>(vtxFmt.attrs[GX_VA_POS].type), static_cast<u32>(vtxFmt.attrs[GX_VA_NRM].cnt),
            static_cast<u32>(vtxFmt.attrs[GX_VA_NRM].type), static_cast<u32>(vtxFmt.attrs[GX_VA_CLR0].cnt),
            static_cast<u32>(vtxFmt.attrs[GX_VA_CLR0].type), static_cast<u32>(vtxFmt.attrs[GX_VA_TEX0].cnt),
            static_cast<u32>(vtxFmt.attrs[GX_VA_TEX0].type), static_cast<u32>(vtxFmt.attrs[GX_VA_TEX1].cnt),
            static_cast<u32>(vtxFmt.attrs[GX_VA_TEX1].type));
  Log.warn("stopping FIFO decode at truncated draw: need {} bytes at pos {}, have {}", totalVtxBytes, pos, size);
}

// Draw command handler - parses vertices inline and caches results
static u32 calculate_last_vtx_size(GXVtxFmt fmt) {
  u32 vtxSize = 0;
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    const auto attr = static_cast<GXAttr>(i);
    const auto& attrFmt = vtxFmt.attrs[i];
    switch (g_gxState.vtxDesc[i]) {
    case GX_NONE:
      break;
    case GX_DIRECT: {
      vtxSize += comp_type_size(attr, attrFmt.type) * comp_cnt_count(attr, attrFmt.cnt);
      break;
    }
    case GX_INDEX8:
      vtxSize += (attr == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3) ? 3 : 1;
      break;
    case GX_INDEX16:
      vtxSize += (attr == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3) ? 6 : 2;
      break;
    }
  }

  g_gxState.lastVtxFmt = fmt;
  g_gxState.lastVtxSize = vtxSize;

  return vtxSize;
}

static void handle_draw_unmerged(GXPrimitive prim, GXVtxFmt fmt, u16 vtxCount,
                                 gfx::Range vertRange, uint16_t usedPnMtxMask,
                                 HashType matrixTopologySignature,
                                 HashType geometrySignature, bool interpolationIdentityActive);

// The per-draw geometry signature, matrix-usage mask and draw-identity hashes exist purely to feed frame interpolation (build_uniform consumes them only after its `frame_interpolation_fps() == 0` early-out).
static inline bool frame_interpolation_identity_needed() noexcept {
  return frame_interpolation_active() && g_gxState.projType == GX_PERSPECTIVE;
}

static uint32_t matrix_index_prefix_size(GXVtxFmt fmt) noexcept {
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  uint32_t size = 0;
  for (int i = GX_VA_PNMTXIDX; i < GX_VA_POS; ++i) {
    const auto attr = static_cast<GXAttr>(i);
    switch (g_gxState.vtxDesc[i]) {
    case GX_NONE:
      break;
    case GX_DIRECT:
      size += comp_type_size(attr, vtxFmt.attrs[i].type) *
              comp_cnt_count(attr, vtxFmt.attrs[i].cnt);
      break;
    case GX_INDEX8:
      ++size;
      break;
    case GX_INDEX16:
      size += 2;
      break;
    }
  }
  return size;
}

static HashType draw_geometry_signature(GXVtxFmt fmt, const uint8_t* vertices,
                                        uint16_t vtxCount, uint32_t vtxStride) noexcept {
  Hasher hasher;
  hasher.update(vtxCount);
  hasher.update(vtxStride);

  // Matrix-index bytes select an instance's current XF palette slots, so they are deliberately excluded from mesh identity.
  const uint32_t matrixPrefix = std::min(matrix_index_prefix_size(fmt), vtxStride);
  if (matrixPrefix == 0) {
    // Most draws have no direct matrix-index prefix.
    hasher.update(vertices, static_cast<size_t>(vtxCount) * vtxStride);
  } else {
    for (uint16_t vertex = 0; vertex < vtxCount; ++vertex) {
      hasher.update(vertices + static_cast<size_t>(vertex) * vtxStride + matrixPrefix,
                    vtxStride - matrixPrefix);
    }
  }

  // Identical index streams can address different vertex arrays.
  for (int i = GX_VA_POS; i <= GX_VA_TEX7; ++i) {
    if (g_gxState.vtxDesc[i] != GX_INDEX8 && g_gxState.vtxDesc[i] != GX_INDEX16) {
      continue;
    }
    const auto& array = g_gxState.arrays[i];
    const auto attribute = static_cast<uint32_t>(i);
    const auto source = reinterpret_cast<uintptr_t>(array.data);
    hasher.update(attribute);
    hasher.update(source);
    hasher.update(array.stride);
  }
  return static_cast<HashType>(hasher.digest());
}

struct PnMtxUsage {
  uint16_t mask = 0;
  HashType topologySignature = 0;
};

// Slot mask only.
static uint16_t pn_mtx_mask(const uint8_t* vertices, uint16_t vtxCount,
                            uint32_t vtxStride) noexcept {
  if (g_gxState.vtxDesc[GX_VA_PNMTXIDX] != GX_DIRECT) {
    return static_cast<uint16_t>(1u << std::min<uint32_t>(g_gxState.currentPnMtx, MaxPnMtx - 1));
  }
  uint16_t mask = 0;
  for (uint16_t vertex = 0; vertex < vtxCount; ++vertex) {
    const uint32_t matrixIndex = vertices[static_cast<size_t>(vertex) * vtxStride] / 3u;
    if (matrixIndex < MaxPnMtx) {
      mask |= static_cast<uint16_t>(1u << matrixIndex);
    }
  }
  return mask;
}

static PnMtxUsage pn_mtx_usage(const uint8_t* vertices, uint16_t vtxCount,
                               uint32_t vtxStride) noexcept {
  if (g_gxState.vtxDesc[GX_VA_PNMTXIDX] != GX_DIRECT) {
    const uint32_t matrixIndex = std::min<uint32_t>(g_gxState.currentPnMtx, MaxPnMtx - 1);
    return {
        .mask = static_cast<uint16_t>(1u << matrixIndex),
        // There is no vertex matrix-index topology in this path.
        .topologySignature = 0,
    };
  }

  Hasher topologyHasher;
  topologyHasher.update(vtxCount);
  uint16_t mask = 0;
  for (uint16_t vertex = 0; vertex < vtxCount; ++vertex) {
    // GX matrix-index attributes precede every other vertex attribute and are always one byte.
    const uint8_t rawMatrixIndex = vertices[static_cast<size_t>(vertex) * vtxStride];
    topologyHasher.update(rawMatrixIndex);
    const uint32_t matrixIndex = rawMatrixIndex / 3u;
    if (matrixIndex < MaxPnMtx) {
      mask |= static_cast<uint16_t>(1u << matrixIndex);
    }
  }
  return {
      .mask = mask,
      .topologySignature = static_cast<HashType>(topologyHasher.digest()),
  };
}

// Whether the most recent unmerged draw recorded an interpolation snapshot, so a draw merged into it knows there is a snapshot to extend.
static bool s_lastDrawRecordedInterpolation = false;

struct CachedIndexTemplate {
  bool valid = false;
  GXPrimitive prim = static_cast<GXPrimitive>(0);
  u16 vtxCount = 0;
  u32 indexCount = 0;
  IndexBuffer indices{};
};

static const CachedIndexTemplate& cached_index_template(GXPrimitive prim, u16 vtxCount) {
  // Topology expansion is immutable for a primitive/count pair.
  constexpr size_t CacheSize = 256;
  static std::array<CachedIndexTemplate, CacheSize> cache{};
  const u32 key = (static_cast<u32>(underlying(prim)) << 16) | vtxCount;
  auto& entry = cache[(key ^ (key >> 9)) & (CacheSize - 1)];
  if (entry.valid && entry.prim == prim && entry.vtxCount == vtxCount) LIKELY {
    return entry;
  }

  entry.valid = true;
  entry.prim = prim;
  entry.vtxCount = vtxCount;
  entry.indexCount = prepare_idx_template(entry.indices, prim, vtxCount);
  return entry;
}

static IndexBuffer handle_draw_idx_buf;

static ArrayRef<u16> offset_index_template(const CachedIndexTemplate& indexTemplate,
                                           u16 vtxStart) {
  // Grow-only: resizing down and back up made every merge zero-fill the buffer before the transform immediately overwrote it.
  const size_t count = indexTemplate.indices.size();
  if (handle_draw_idx_buf.size() < count) {
    handle_draw_idx_buf.resize(count);
  }
  const u16* src = indexTemplate.indices.data();
  u16* dst = handle_draw_idx_buf.data();
  for (size_t i = 0; i < count; ++i) {
    dst[i] = static_cast<u16>(src[i] + vtxStart);
  }
  return {dst, count};
}

struct CachedPipelineState {
  gfx::PipelineRef ref = 0;
  HashType configHash = 0;
  // Carried here so the draw can be recorded without keeping the PipelineConfig that produced it alive; it is the only field of the config the draw itself still needs.
  u32 dstAlpha = UINT32_MAX;
  ShaderInfo shaderInfo{};
};

static const CachedPipelineState& cached_pipeline_state(const PipelineConfig& config) {
  constexpr size_t CacheSize = 1024;
  struct Entry {
    bool valid = false;
    PipelineConfig config{};
    CachedPipelineState state{};
  };
  static std::array<Entry, CacheSize> cache{};

  const HashType hash = xxh3_hash(config, static_cast<HashType>(gfx::ShaderType::GX));
  auto& entry = cache[hash & (CacheSize - 1)];
  if (entry.valid && entry.state.configHash == hash &&
      std::memcmp(&entry.config, &config, sizeof(config)) == 0) LIKELY {
    return entry.state;
  }

  entry.valid = true;
  entry.config = config;
  entry.state = {
      .ref = gfx::pipeline_ref(config),
      .configHash = hash,
      .dstAlpha = config.dstAlpha,
      .shaderInfo = build_shader_info(config.shaderConfig),
  };
  return entry.state;
}

// Resolving a pipeline the long way costs a ~2.7KB zero-init, a full populate_pipeline_config, an XXH3 over the whole config and a memcmp against the hash-indexed slot -- roughly 11KB of memory traffic for a result that is almost always identical to the previous draw's.
static const CachedPipelineState& resolve_pipeline_state(GXPrimitive prim, GXVtxFmt fmt) {
  struct Memo {
    const CachedPipelineState* state = nullptr;
    u32 generation = 0;
    u32 sampleCount = 0;
    GXPrimitive prim = static_cast<GXPrimitive>(0);
    GXVtxFmt fmt = static_cast<GXVtxFmt>(0);
  };
  static Memo memo{};

  const u32 sampleCount = gfx::get_sample_count();
  const u32 generation = g_gxState.pipelineStateGeneration;
  if (memo.state != nullptr && memo.generation == generation && memo.sampleCount == sampleCount &&
      memo.prim == prim && memo.fmt == fmt) LIKELY {
    return *memo.state;
  }

  PipelineConfig config{};
  populate_pipeline_config(config, prim, fmt);
  // cached_pipeline_state hands back a reference into a fixed direct-mapped table, so the address stays valid; the entry it points at can only be rewritten by another call to that function, and every such call goes through this miss path and replaces the memo in the same breath.
  const CachedPipelineState& state = cached_pipeline_state(config);
  memo = Memo{
      .state = &state,
      .generation = generation,
      .sampleCount = sampleCount,
      .prim = prim,
      .fmt = fmt,
  };
  return state;
}

// DIAGNOSTIC (temporary): NSMBW_LOG_DRAW_TEXGEN=<scene profile> - see body. Called from both the
// process() draw path and submit_raw_draw (the HLE's raw 2D path, which the layout screens use).
// Draws still to log because a "nsmbw-arm-drawlog" marker was processed (NSMBW_LOG_LIQUID, set from
// the runtime's GXLoadTexObj binding). Stream-ordered, unlike arming from the HLE call itself.
static int s_nsmbwArmedDraws = 0;
static int s_nsmbwArmedTotal = 0;

static void nsmbw_log_draw_texgen(GXPrimitive prim, GXVtxFmt fmt, const uint8_t* vertices, u16 vtxCount, u32 vtxSize) {
// draw their I8 gradient strips stretched across whole panes even though NSMBW_DUMP_TEXTURES
// shows the textures decode correctly, so this prints, per draw in that scene, the texgen
// config each active TEV stage samples through (type/src/mtx/postMtx), the texture matrix
// rows it references, the bound texture size, and the first vertex's raw UV. Remove once
// resolved.
{
static const long tgScene = [] {
  const char* v = AURORA_ENV("NSMBW_LOG_DRAW_TEXGEN");
  return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
}();
static const uint32_t tgMinTick = [] {
  const char* v = AURORA_ENV("NSMBW_LOG_DRAW_TEXGEN_TICK");
  return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : 0u;
}();
// NSMBW_LOG_DRAW_TEXGEN_MINVP=<px>: only log draws whose viewport is at least this tall.
// The level scene front-loads hundreds of render-to-texture draws (640x72 strips, 32x32 tile
// viewports) that exhaust the 160-draw budget before any full-screen draw is seen.
static const float tgMinVpHeight = [] {
  const char* v = AURORA_ENV("NSMBW_LOG_DRAW_TEXGEN_MINVP");
  return v ? static_cast<float>(std::strtoul(v, nullptr, 10)) : 0.f;
}();
static const int tgMax = [] {
  const char* v = AURORA_ENV("NSMBW_LOG_DRAW_TEXGEN_MAX");
  return v ? static_cast<int>(std::strtol(v, nullptr, 10)) : 160;
}();
static int tgLogged = 0;
const bool armedDraw = s_nsmbwArmedDraws > 0 && s_nsmbwArmedTotal < 400;
if (armedDraw) {
  --s_nsmbwArmedDraws;
  ++s_nsmbwArmedTotal;
  std::fprintf(stderr, "[NSMBW_TEXGEN] (armed by liquid renderer) blend type=%u src=%u dst=%u colorUpdate=%d alphaUpdate=%d "
                       "depth test=%d write=%d func=%u cull=%u\n",
               (unsigned)g_gxState.blendMode, (unsigned)g_gxState.blendFacSrc, (unsigned)g_gxState.blendFacDst,
               g_gxState.colorUpdate ? 1 : 0, g_gxState.alphaUpdate ? 1 : 0, g_gxState.depthCompare ? 1 : 0,
               g_gxState.depthUpdate ? 1 : 0, (unsigned)g_gxState.depthFunc, (unsigned)g_gxState.cullMode);
  // Geometry: position format, the current position matrix, and the raw vertices (big-endian).
  const auto& pf = g_gxState.vtxFmts[fmt].attrs[GX_VA_POS];
  const u32 mi = g_gxState.currentPnMtx / 3;
  float pm[12] = {};
  if (mi < g_gxState.pnMtx.size()) std::memcpy(pm, &g_gxState.pnMtx[mi].pos, sizeof(pm));
  { const auto& ca = g_gxState.arrays[GX_VA_CLR0];
    const auto& cf = g_gxState.vtxFmts[fmt].attrs[GX_VA_CLR0];
    std::fprintf(stderr, "[NSMBW_TEXGEN]   clr0 desc=%u fmt cnt=%u type=%u | array data=%p size=%u stride=%u le=%d first:",
                 (unsigned)g_gxState.vtxDesc[GX_VA_CLR0], (unsigned)cf.cnt, (unsigned)cf.type, ca.data, ca.size,
                 (unsigned)ca.stride, ca.le ? 1 : 0);
    if (ca.data != nullptr && ca.size >= 4) {
      const auto* b = static_cast<const uint8_t*>(ca.data);
      for (u32 k = 0; k < (ca.size < 16 ? ca.size : 16); ++k) std::fprintf(stderr, " %02X", b[k]);
    }
    std::fprintf(stderr, "%c", 10); }
  { const auto& na = g_gxState.arrays[GX_VA_NRM];
    const auto& nf = g_gxState.vtxFmts[fmt].attrs[GX_VA_NRM];
    std::fprintf(stderr, "[NSMBW_TEXGEN]   nrm desc=%u fmt cnt=%u type=%u frac=%u | array size=%u stride=%u first:",
                 (unsigned)g_gxState.vtxDesc[GX_VA_NRM], (unsigned)nf.cnt, (unsigned)nf.type, (unsigned)nf.frac, na.size, (unsigned)na.stride);
    if (na.data != nullptr) { const auto* b = static_cast<const uint8_t*>(na.data);
      for (u32 k = 0; k < (na.size < 24 ? na.size : 24); ++k) std::fprintf(stderr, " %02X", b[k]); }
    std::fprintf(stderr, "%c", 10); }
  { std::fprintf(stderr, "[NSMBW_TEXGEN]   dualTex=%u", (unsigned)g_gxState.dualTex);
    for (u32 t = 0; t < 3; ++t) { const u32 pm = g_gxState.tcgs[t].postMtx; if (pm >= GX_PTTEXMTX0 && pm < GX_PTIDENTITY) {
      const auto& m = g_gxState.ptTexMtxs[(pm - GX_PTTEXMTX0) / 3]; float f[12]; std::memcpy(f, &m, sizeof(f));
      std::fprintf(stderr, " post%u(tc%u)=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]", pm, t, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]); }
      const u32 tm = g_gxState.tcgs[t].mtx; if (tm >= 30 && tm < 60) { float g[12]; std::memcpy(g, &g_gxState.texMtxs[(tm - 30) / 3], sizeof(g));
        std::fprintf(stderr, " tex%u(tc%u)=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]", tm, t, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11]); } }
    std::fprintf(stderr, "%c", 10); }
  { const auto& tf = g_gxState.vtxFmts[fmt].attrs[GX_VA_TEX0];
    std::fprintf(stderr, "[NSMBW_TEXGEN]   tex0Fmt cnt=%u type=%u frac=%u clr0Fmt type=%u%c", (unsigned)tf.cnt, (unsigned)tf.type,
                 (unsigned)tf.frac, (unsigned)g_gxState.vtxFmts[fmt].attrs[GX_VA_CLR0].type, 10); }
  std::fprintf(stderr, "[NSMBW_TEXGEN]   posFmt cnt=%u type=%u frac=%u pnMtx[%u]=[%.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f | %.3f %.3f %.3f %.1f]\n",
               (unsigned)pf.cnt, (unsigned)pf.type, (unsigned)pf.frac, mi, pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6],
               pm[7], pm[8], pm[9], pm[10], pm[11]);
  for (u32 vi = 0; vi < vtxCount && vi < 4 && vtxSize <= 64; ++vi) {
    char hex[140] = {};
    for (u32 b = 0; b < vtxSize && b < 64; ++b) std::snprintf(hex + b * 2, 3, "%02X", vertices[vi * vtxSize + b]);
    std::fprintf(stderr, "[NSMBW_TEXGEN]   vtx%u %s\n", vi, hex);
  }
}
if (armedDraw || (tgScene >= 0 && g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(tgScene) &&
    g_nsmbwCurrentViTick >= tgMinTick && g_gxState.numTevStages > 0 && tgLogged < tgMax &&
    g_gxState.logicalViewport.height >= tgMinVpHeight)) {
  ++tgLogged;
  const auto& texFmt = g_gxState.vtxFmts[fmt].attrs[GX_VA_TEX0];
  const auto& posFmt = g_gxState.vtxFmts[fmt].attrs[GX_VA_POS];
  float u = 0.f, v = 0.f, x = 0.f, y = 0.f;
  if (posFmt.type == GX_F32 && posFmt.cnt == GX_POS_XY && vtxSize >= 8) {
    uint32_t w[5] = {};
    std::memcpy(w, vertices, vtxSize >= 20 ? 20 : 8);
    for (auto& q : w) q = __builtin_bswap32(q);
    std::memcpy(&x, &w[0], 4); std::memcpy(&y, &w[1], 4);
    if (vtxSize >= 20 && texFmt.type == GX_F32) { std::memcpy(&u, &w[3], 4); std::memcpy(&v, &w[4], 4); }
  }
  std::fprintf(stderr, "[NSMBW_TEXGEN] draw#%d tick=%u prim=%u n=%u vtxSize=%u numTexGens=%u numTev=%u v0=(%.1f,%.1f uv %.3f,%.3f)\n",
               tgLogged, g_nsmbwCurrentViTick, (unsigned)prim, vtxCount, vtxSize, g_gxState.numTexGens, g_gxState.numTevStages, x, y, u, v);
  for (uint32_t st = 0; st < g_gxState.numTevStages && st < 16; ++st) {
    const auto& s = g_gxState.tevStages[st];
    const int tc = static_cast<int>(s.texCoordId);
    const int tm = static_cast<int>(s.texMapId);
    const auto& lt = (tm >= 0 && tm < static_cast<int>(MaxTextures)) ? g_gxState.loadedTextures[tm] : g_gxState.loadedTextures[0];
    char tcgLine[200] = "tc=null";
    if (tc >= 0 && tc < static_cast<int>(MaxTexCoord)) {
      const auto& tcg = g_gxState.tcgs[tc];
      const int mi = static_cast<int>(tcg.mtx);
      char mtxLine[120] = "";
      if (mi >= 30 && mi < 30 + 3 * static_cast<int>(MaxTexMtx) && (mi - 30) % 3 == 0) {
        const auto& m = g_gxState.texMtxs[(mi - 30) / 3];
        std::snprintf(mtxLine, sizeof(mtxLine), " mtx=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]",
                      m.m0.x(), m.m0.y(), m.m0.z(), m.m0.w(), m.m1.x(), m.m1.y(), m.m1.z(), m.m1.w());
      }
      const auto& tcs = g_gxState.texCoordScales[tc];
      std::snprintf(tcgLine, sizeof(tcgLine), "tc=%d type=%d src=%d mtxIdx=%d post=%d norm=%d suScale=%u/%u%s", tc,
                    static_cast<int>(tcg.type), static_cast<int>(tcg.src), mi, static_cast<int>(tcg.postMtx),
                    tcg.normalize ? 1 : 0, tcs.scaleS + 1u, tcs.scaleT + 1u, mtxLine);
    }
    std::fprintf(stderr, "[NSMBW_TEXGEN]   stage%u texMap=%d %ux%u fmt=%d wrap=%d/%d %s\n", st, tm,
                 lt.width(), lt.height(), static_cast<int>(lt.format()),
                 static_cast<int>(lt.wrap_s()), static_cast<int>(lt.wrap_t()), tcgLine);
  }
  {
    const auto& cc0 = g_gxState.colorChannelConfig[GX_COLOR0];
    const auto& ca0 = g_gxState.colorChannelConfig[GX_ALPHA0];
    const auto& cs0 = g_gxState.colorChannelState[GX_COLOR0];
    const auto& as0 = g_gxState.colorChannelState[GX_ALPHA0];
    const auto& ac = g_gxState.alphaCompare;
    std::fprintf(stderr,
                 "[NSMBW_TEXGEN]   numChans=%u chan0 matSrc=%d ambSrc=%d light=%d matColor=(%.2f,%.2f,%.2f,%.2f) amb=(%.2f,%.2f,%.2f,%.2f)"
                 " alpha0 matSrc=%d light=%d matA=%.2f | blend mode=%d src=%d dst=%d op=%d | alphaCmp c0=%d r0=%u op=%d c1=%d r1=%u"
                 " | z cmp=%d upd=%d fn=%d | colorUpd=%d alphaUpd=%d dstAlpha=%u\n",
                 g_gxState.numChans, static_cast<int>(cc0.matSrc), static_cast<int>(cc0.ambSrc), cc0.lightingEnabled ? 1 : 0,
                 cs0.matColor.x(), cs0.matColor.y(), cs0.matColor.z(), cs0.matColor.w(),
                 cs0.ambColor.x(), cs0.ambColor.y(), cs0.ambColor.z(), cs0.ambColor.w(),
                 static_cast<int>(ca0.matSrc), ca0.lightingEnabled ? 1 : 0, as0.matColor.w(),
                 static_cast<int>(g_gxState.blendMode), static_cast<int>(g_gxState.blendFacSrc), static_cast<int>(g_gxState.blendFacDst),
                 static_cast<int>(g_gxState.blendOp), static_cast<int>(ac.comp0), ac.ref0, static_cast<int>(ac.op), static_cast<int>(ac.comp1), ac.ref1,
                 g_gxState.depthCompare ? 1 : 0, g_gxState.depthUpdate ? 1 : 0, static_cast<int>(g_gxState.depthFunc),
                 g_gxState.colorUpdate ? 1 : 0, g_gxState.alphaUpdate ? 1 : 0, g_gxState.dstAlpha);
    for (uint32_t st = 0; st < g_gxState.numTevStages && st < 16; ++st) {
      const auto& s = g_gxState.tevStages[st];
      std::fprintf(stderr,
                   "[NSMBW_TEXGEN]   stage%u chan=%d color a=%d b=%d c=%d d=%d op=%d bias=%d scale=%d out=%d clamp=%d | alpha a=%d b=%d c=%d d=%d op=%d bias=%d scale=%d out=%d | kc=%d ka=%d swapRas=%d swapTex=%d\n",
                   st, static_cast<int>(s.channelId), static_cast<int>(s.colorPass.a), static_cast<int>(s.colorPass.b),
                   static_cast<int>(s.colorPass.c), static_cast<int>(s.colorPass.d), static_cast<int>(s.colorOp.op),
                   static_cast<int>(s.colorOp.bias), static_cast<int>(s.colorOp.scale), static_cast<int>(s.colorOp.outReg), s.colorOp.clamp ? 1 : 0,
                   static_cast<int>(s.alphaPass.a), static_cast<int>(s.alphaPass.b), static_cast<int>(s.alphaPass.c), static_cast<int>(s.alphaPass.d),
                   static_cast<int>(s.alphaOp.op), static_cast<int>(s.alphaOp.bias), static_cast<int>(s.alphaOp.scale), static_cast<int>(s.alphaOp.outReg),
                   static_cast<int>(s.kcSel), static_cast<int>(s.kaSel), static_cast<int>(s.tevSwapRas), static_cast<int>(s.tevSwapTex));
    }
    std::fprintf(stderr, "[NSMBW_TEXGEN]   regs prev=(%.2f,%.2f,%.2f,%.2f) c0=(%.2f,%.2f,%.2f,%.2f) c1=(%.2f,%.2f,%.2f,%.2f) c2=(%.2f,%.2f,%.2f,%.2f) k0=(%.2f,%.2f,%.2f,%.2f)\n",
                 g_gxState.colorRegs[0].x(), g_gxState.colorRegs[0].y(), g_gxState.colorRegs[0].z(), g_gxState.colorRegs[0].w(),
                 g_gxState.colorRegs[1].x(), g_gxState.colorRegs[1].y(), g_gxState.colorRegs[1].z(), g_gxState.colorRegs[1].w(),
                 g_gxState.colorRegs[2].x(), g_gxState.colorRegs[2].y(), g_gxState.colorRegs[2].z(), g_gxState.colorRegs[2].w(),
                 g_gxState.colorRegs[3].x(), g_gxState.colorRegs[3].y(), g_gxState.colorRegs[3].z(), g_gxState.colorRegs[3].w(),
                 g_gxState.kcolors[0].x(), g_gxState.kcolors[0].y(), g_gxState.kcolors[0].z(), g_gxState.kcolors[0].w());
  }
  {
    const auto& fg = g_gxState.fog;
    std::fprintf(stderr, "[NSMBW_TEXGEN]   fog type=%d a=%.4f b=%.4f c=%.4f color=(%.2f,%.2f,%.2f,%.2f) cull=%d\n",
                 static_cast<int>(fg.type), fg.a, fg.b, fg.c, fg.color.x(), fg.color.y(), fg.color.z(), fg.color.w(),
                 static_cast<int>(g_gxState.cullMode));
  }
  {
    const auto& p = g_gxState.proj;
    const auto& vp = g_gxState.logicalViewport;
    const auto& sc = g_gxState.logicalScissor;
    std::fprintf(stderr, "[NSMBW_TEXGEN]   projType=%d proj=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f] vp=(%.0f,%.0f %.0fx%.0f) scissor=(%d,%d %dx%d)\n",
                 static_cast<int>(g_gxState.projType), p.m0.x(), p.m0.y(), p.m0.z(), p.m0.w(), p.m1.x(), p.m1.y(), p.m1.z(), p.m1.w(),
                 p.m2.x(), p.m2.y(), p.m2.z(), p.m2.w(), p.m3.x(), p.m3.y(), p.m3.z(), p.m3.w(),
                 vp.left, vp.top, vp.width, vp.height, sc.x, sc.y, sc.width, sc.height);
  }
  std::fflush(stderr);
}
}
}

bool submit_raw_draw(GXPrimitive prim, GXVtxFmt fmt, const uint8_t* vertices, uint16_t vtxCount,
                     uint32_t vertexBytes) {
  ZoneScoped;
  if (vertices == nullptr || vtxCount == 0 || vertexBytes == 0) {
    return false;
  }

  if (__gx->dirtyState != 0) UNLIKELY {
    __GXSetDirtyState();
  }

  // Raw bridge draws consume live decoded GX state that is also maintained by the HLE producer.
  drain();

  u32 vtxSize;
  if (g_gxState.lastVtxFmt == fmt) LIKELY {
    vtxSize = g_gxState.lastVtxSize;
  } else UNLIKELY {
    vtxSize = calculate_last_vtx_size(fmt);
  }

  const u32 expectedVertexBytes = static_cast<u32>(vtxCount) * vtxSize;
  if (expectedVertexBytes == 0 || expectedVertexBytes != vertexBytes) {
    static uint32_t rawVertexSizeMismatchCount = 0;
    if (rawVertexSizeMismatchCount++ < 64) {
      Log.warn("raw draw vertex-size mismatch prim={} fmt={} count={} cached_stride={} expected={} supplied={}",
               static_cast<uint32_t>(prim), static_cast<uint32_t>(fmt), vtxCount, vtxSize,
               expectedVertexBytes, vertexBytes);
    }
    return false;
  }

  nsmbw_log_draw_texgen(prim, fmt, vertices, vtxCount, vtxSize);
  // This entry point bypasses process(), so it owns the renderer lock itself.
  std::lock_guard gpuLock(aurora::renderer_gpu_mutex());
  const gfx::Range vertRange = gfx::push_verts(vertices, vertexBytes);
  const bool interpolationIdentityActive = frame_interpolation_identity_needed();
  const PnMtxUsage matrixUsage = interpolationIdentityActive
                                     ? pn_mtx_usage(vertices, vtxCount, vtxSize)
                                     : PnMtxUsage{};
  handle_draw_unmerged(prim, fmt, vtxCount, vertRange,
                       matrixUsage.mask, matrixUsage.topologySignature,
                       interpolationIdentityActive ? draw_geometry_signature(fmt, vertices, vtxCount, vtxSize) : 0,
                       interpolationIdentityActive);
  return true;
}

static bool handle_draw(u8 cmd, const u8* data, u32& pos, u32 size, bool bigEndian) {
  ZoneScoped;
  GXVtxFmt fmt = static_cast<GXVtxFmt>(cmd & CP_VAT_MASK);
  GXPrimitive prim = primitive_from_draw_cmd(cmd);

  if (pos + 2 > size) {
    return false;
  }
  u16 vtxCount = read_u16(data + pos, bigEndian);
  pos += 2;

  u32 vtxSize;
  if (g_gxState.lastVtxFmt == fmt) LIKELY {
    vtxSize = g_gxState.lastVtxSize;
  } else UNLIKELY {
    vtxSize = calculate_last_vtx_size(fmt);
  }

  u32 totalVtxBytes = vtxCount * vtxSize;
  if (pos + totalVtxBytes > size) UNLIKELY {
    handle_draw_overrun(cmd, vtxCount, vtxSize, totalVtxBytes, data, pos, size);
    return false;
  }


  // Push raw vertex data to buffer
  const uint8_t* vertices = data + pos;
  // DIAGNOSTIC (temporary): NSMBW_LOG_STATE_RUNS=<profile>. Prints one line each time the
  // (viewport, projection type, PnMtx0 zero-ness) tuple changes between draws, with the running
  // draw index - i.e. the frame's structure as sequences of draws sharing render state. Remove
  // once resolved.
  {
    static const long runsScene = [] {
      const char* v = AURORA_ENV("NSMBW_LOG_STATE_RUNS");
      return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
    }();
    if (runsScene >= 0 && g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(runsScene)) {
      static uint64_t drawIdx = 0;
      static int lines = 0;
      static float lastVp[4] = {-1.f, -1.f, -1.f, -1.f};
      static int lastProj = -1;
      static int lastMtxZero = -1;
      ++drawIdx;
      const auto& vp = g_gxState.logicalViewport;
      const auto& M0 = g_gxState.pnMtx[std::min<u32>(g_gxState.currentPnMtx, MaxPnMtx - 1)].pos;
      const int mtxZero = (M0.m0[0] == 0.f && M0.m1[1] == 0.f && M0.m2[2] == 0.f) ? 1 : 0;
      const int projT = static_cast<int>(g_gxState.projType);
      if (lines < 240 && drawIdx > 40000 &&
          (vp.left != lastVp[0] || vp.top != lastVp[1] || vp.width != lastVp[2] || vp.height != lastVp[3] ||
           projT != lastProj || mtxZero != lastMtxZero)) {
        ++lines;
        lastVp[0] = vp.left; lastVp[1] = vp.top; lastVp[2] = vp.width; lastVp[3] = vp.height;
        lastProj = projT; lastMtxZero = mtxZero;
        std::fprintf(stderr, "[NSMBW_STATE_RUN] draw#%llu vp=(%.0f,%.0f,%.0f,%.0f) proj=%s pnMtx[%u]=%s proj.m00=%.4f m11=%.4f\n",
                     static_cast<unsigned long long>(drawIdx), vp.left, vp.top, vp.width, vp.height,
                     projT == GX_ORTHOGRAPHIC ? "ortho" : "persp", g_gxState.currentPnMtx, mtxZero ? "ZERO" : "ok",
                     g_gxState.proj.m0[0], g_gxState.proj.m1[1]);
        std::fflush(stderr);
      }
    }
  }
  // DIAGNOSTIC (temporary): NSMBW_DUMP_DRAWS_SCENE=<profile> [NSMBW_DUMP_DRAWS_SKIP=<n>]. A test
  // triangle proved the host pipeline (submit -> decode -> draw -> resolve -> present) works, and
  // fixing the texture-copy register clobber made the 2D scenes render; the 3D STAGE scene still
  // comes out black even though ~277 draws/frame reach this point, and every projection it submits
  // is orthographic. This transforms each draw's first vertex through the live PnMtx and projection
  // to clip space so "lands off-screen / behind the camera" can be told apart from "lands on
  // screen but is painted black", alongside the pixel state that decides the latter. Skips the
  // first <n> draws seen in the scene (past the fade-in), then dumps 80. Remove once resolved.
  {
    static const long dumpScene = [] {
      const char* v = AURORA_ENV("NSMBW_DUMP_DRAWS_SCENE");
      return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
    }();
    if (dumpScene >= 0 && g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(dumpScene)) {
      static const uint64_t skipDraws = [] {
        const char* v = AURORA_ENV("NSMBW_DUMP_DRAWS_SKIP");
        return v ? static_cast<uint64_t>(std::strtoull(v, nullptr, 10)) : 0ull;
      }();
      static const uint64_t strideDraws = [] {
        const char* v = AURORA_ENV("NSMBW_DUMP_DRAWS_STRIDE");
        return v ? static_cast<uint64_t>(std::strtoull(v, nullptr, 10)) : 1ull;
      }();
      static uint64_t seen = 0;
      static int dumped = 0;
      ++seen;
      if (seen > skipDraws && dumped < 80 && ((seen - skipDraws) % strideDraws) == 0) {
        ++dumped;
        const auto& vf = g_gxState.vtxFmts[fmt];
        const auto& posFmt = vf.attrs[GX_VA_POS];
        const GXAttrType posDesc = g_gxState.vtxDesc[GX_VA_POS];
        // Byte offset of POS within a vertex: everything from PNMTXIDX up to TEX7MTXIDX precedes it.
        u32 posOff = 0;
        for (int a = GX_VA_PNMTXIDX; a < GX_VA_POS; ++a) {
          const auto attr = static_cast<GXAttr>(a);
          switch (g_gxState.vtxDesc[a]) {
          case GX_DIRECT: posOff += comp_type_size(attr, vf.attrs[a].type) * comp_cnt_count(attr, vf.attrs[a].cnt); break;
          case GX_INDEX8: posOff += 1; break;
          case GX_INDEX16: posOff += 2; break;
          default: break;
          }
        }
        const uint8_t* pb = nullptr;
        bool posBigEndian = true;
        u32 posIndex = 0;
        if (posDesc == GX_DIRECT) {
          pb = vertices + posOff;
        } else if (posDesc == GX_INDEX8 || posDesc == GX_INDEX16) {
          posIndex = posDesc == GX_INDEX8 ? vertices[posOff]
                                          : static_cast<u32>((vertices[posOff] << 8) | vertices[posOff + 1]);
          const auto& arr = g_gxState.arrays[GX_VA_POS];
          if (arr.data != nullptr) {
            pb = static_cast<const uint8_t*>(arr.data) + static_cast<size_t>(posIndex) * arr.stride;
            posBigEndian = !arr.le;
          }
        }
        float p[3] = {0.f, 0.f, 0.f};
        const u32 ncomp = posFmt.cnt == GX_POS_XYZ ? 3u : 2u;
        if (pb != nullptr) {
          const float scale = static_cast<float>(1u << posFmt.frac);
          for (u32 c = 0; c < ncomp; ++c) {
            switch (posFmt.type) {
            case GX_F32: { u32 w; std::memcpy(&w, pb + c * 4, 4); if (posBigEndian) w = __builtin_bswap32(w); std::memcpy(&p[c], &w, 4); break; }
            case GX_S16: { u16 w; std::memcpy(&w, pb + c * 2, 2); if (posBigEndian) w = __builtin_bswap16(w); p[c] = static_cast<int16_t>(w) / scale; break; }
            case GX_U16: { u16 w; std::memcpy(&w, pb + c * 2, 2); if (posBigEndian) w = __builtin_bswap16(w); p[c] = w / scale; break; }
            case GX_S8: p[c] = static_cast<int8_t>(pb[c]) / scale; break;
            case GX_U8: p[c] = pb[c] / scale; break;
            default: break;
            }
          }
        }
        u32 mtxIdx = std::min<u32>(g_gxState.currentPnMtx, MaxPnMtx - 1);
        if (g_gxState.vtxDesc[GX_VA_PNMTXIDX] == GX_DIRECT) {
          mtxIdx = std::min<u32>(vertices[0] / 3u, MaxPnMtx - 1);
        }
        const auto& M = g_gxState.pnMtx[mtxIdx].pos;
        const float vx = M.m0[0] * p[0] + M.m0[1] * p[1] + M.m0[2] * p[2] + M.m0[3];
        const float vy = M.m1[0] * p[0] + M.m1[1] * p[1] + M.m1[2] * p[2] + M.m1[3];
        const float vz = M.m2[0] * p[0] + M.m2[1] * p[1] + M.m2[2] * p[2] + M.m2[3];
        const auto& P = g_gxState.proj;
        const float cx = P.m0[0] * vx + P.m0[1] * vy + P.m0[2] * vz + P.m0[3];
        const float cy = P.m1[0] * vx + P.m1[1] * vy + P.m1[2] * vz + P.m1[3];
        const float cz = P.m2[0] * vx + P.m2[1] * vy + P.m2[2] * vz + P.m2[3];
        const float cw = P.m3[0] * vx + P.m3[1] * vy + P.m3[2] * vz + P.m3[3];
        const float iw = cw != 0.f ? 1.f / cw : 0.f;
        const auto& vp = g_gxState.logicalViewport;
        const auto& st0 = g_gxState.tevStages[0];
        std::fprintf(stderr,
                     "[NSMBW_DRAW_DUMP] #%d mtx0src=%u prim=%u vtx=%u posDesc=%u posType=%u cnt=%u frac=%u idx=%u "
                     "local=(%.1f,%.1f,%.1f) mtx=%u view=(%.1f,%.1f,%.1f) projType=%d clip=(%.2f,%.2f,%.2f,w=%.2f) "
                     "ndc=(%.3f,%.3f,%.3f) vp=(%.0f,%.0f,%.0f,%.0f,%.2f,%.2f) tev=%u tex0=%d colorUpd=%d "
                     "z=(%d,%d,%d) cull=%d blend=%d\n",
                     dumped, static_cast<unsigned>(g_nsmbwPnMtx0Source), static_cast<unsigned>(prim), static_cast<unsigned>(vtxCount), static_cast<unsigned>(posDesc),
                     static_cast<unsigned>(posFmt.type), static_cast<unsigned>(posFmt.cnt), static_cast<unsigned>(posFmt.frac),
                     posIndex, p[0], p[1], p[2], mtxIdx, vx, vy, vz, static_cast<int>(g_gxState.projType), cx, cy, cz, cw,
                     cx * iw, cy * iw, cz * iw, vp.left, vp.top, vp.width, vp.height, vp.znear, vp.zfar,
                     static_cast<unsigned>(g_gxState.numTevStages), static_cast<int>(st0.texMapId),
                     g_gxState.colorUpdate ? 1 : 0, g_gxState.depthCompare ? 1 : 0, static_cast<int>(g_gxState.depthFunc),
                     g_gxState.depthUpdate ? 1 : 0, static_cast<int>(g_gxState.cullMode), static_cast<int>(g_gxState.blendMode));
        std::fflush(stderr);
      }
    }
  }
  // DIAGNOSTIC (temporary): NSMBW_LOG_DRAW_POS decodes the first vertex's raw POS bytes for the
  // first several draws of this shape - used to check whether garbled/stretched on-screen content
  // (textures individually confirmed correct via NSMBW_DUMP_TEXTURES) traces to wrong vertex
  // screen coordinates rather than texture decode. Remove once resolved.
  if (AURORA_ENV("NSMBW_LOG_DRAW_POS") != nullptr &&
      (AURORA_ENV("NSMBW_LOG_DRAW_POS_SCENE") == nullptr ||
       g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(std::atoi(AURORA_ENV("NSMBW_LOG_DRAW_POS_SCENE"))))) {
    static int posLogged = 0;
    // Reset the budget on every scene change so an unrelated earlier scene (e.g. boot's own UI)
    // can't consume it before the scene actually being investigated gets a turn.
    static uint32_t lastPosLogScene = 0xFFFFFFFFu;
    if (g_nsmbwCurrentSceneProfile != lastPosLogScene) {
      lastPosLogScene = g_nsmbwCurrentSceneProfile;
      posLogged = 0;
    }
    // NSMBW_LOG_DRAW_POS_SKIP_NO_TEV (temporary): the original 15-draw budget was entirely
    // consumed by a per-frame debug overlay (the on-screen FPS counter box, redrawn every single
    // frame from frame 0 with numTevStages==0) before ever reaching real nw4r::lyt content -
    // confirmed by NSMBW_LOG_DRAW_TEV finding zero active TEV stages on any of those 15 draws.
    // This skips draws with no configured TEV stage so the budget is spent on actual textured
    // game content instead. Remove once resolved.
    const bool skipNoTev = AURORA_ENV("NSMBW_LOG_DRAW_POS_SKIP_NO_TEV") != nullptr &&
                           g_gxState.numTevStages == 0;
    if (!skipNoTev && posLogged < 40 && vtxCount > 0 && vtxCount <= 8) {
      ++posLogged;
      const auto& posFmt = g_gxState.vtxFmts[fmt].attrs[GX_VA_POS];
      const auto& texFmt = g_gxState.vtxFmts[fmt].attrs[GX_VA_TEX0];
      // Decode POS (and TEX0, if F32) for every vertex of this draw - a single vertex told us
      // nothing about the quad's actual width/height, which is what determines whether a
      // correctly-decoded texture (confirmed via NSMBW_DUMP_TEXTURES) ends up stretched/tiled.
      if (posFmt.type == GX_F32 && posFmt.cnt == GX_POS_XY) {
        char line[512];
        int off = std::snprintf(line, sizeof(line), "[NSMBW_DRAW_POS] prim=%u fmt=%u vtxCount=%u vtxSize=%u",
                                (unsigned)prim, (unsigned)fmt, vtxCount, vtxSize);
        for (u16 vi = 0; vi < vtxCount; ++vi) {
          const uint8_t* vp = vertices + vi * vtxSize;
          uint32_t xb, yb;
          std::memcpy(&xb, vp, 4);
          std::memcpy(&yb, vp + 4, 4);
          xb = __builtin_bswap32(xb);
          yb = __builtin_bswap32(yb);
          float x, y;
          std::memcpy(&x, &xb, 4);
          std::memcpy(&y, &yb, 4);
          float u = 0.f, v = 0.f;
          if (texFmt.type == GX_F32) {
            const uint8_t* tp = vp + 8; // POS(F32 XY)=8 bytes precede TEX0 for this fmt
            uint32_t ub, vb;
            std::memcpy(&ub, tp, 4);
            std::memcpy(&vb, tp + 4, 4);
            ub = __builtin_bswap32(ub);
            vb = __builtin_bswap32(vb);
            std::memcpy(&u, &ub, 4);
            std::memcpy(&v, &vb, 4);
          }
          off += std::snprintf(line + off, sizeof(line) - off, " v%u=(xy:%.1f,%.1f uv:%.3f,%.3f)", vi, x, y, u, v);
        }
        std::fprintf(stderr, "%s\n", line);
        std::fflush(stderr);
      }
      char hex[64] = {};
      int hexLen = 0;
      const uint32_t dumpBytes = vtxSize < 16u ? vtxSize : 16u;
      for (uint32_t i = 0; i < dumpBytes && hexLen + 3 < (int)sizeof(hex); ++i) {
        hexLen += std::snprintf(hex + hexLen, sizeof(hex) - hexLen, "%02x ", vertices[i]);
      }
      float x = 0.f, y = 0.f;
      if (posFmt.type == GX_F32 && posFmt.cnt == GX_POS_XY) {
        uint32_t xb, yb;
        std::memcpy(&xb, vertices, 4);
        std::memcpy(&yb, vertices + 4, 4);
        xb = __builtin_bswap32(xb);
        yb = __builtin_bswap32(yb);
        std::memcpy(&x, &xb, 4);
        std::memcpy(&y, &yb, 4);
      } else if (posFmt.type == GX_S16 && posFmt.cnt == GX_POS_XY) {
        int16_t xi, yi;
        std::memcpy(&xi, vertices, 2);
        std::memcpy(&yi, vertices + 2, 2);
        xi = static_cast<int16_t>(__builtin_bswap16(static_cast<uint16_t>(xi)));
        yi = static_cast<int16_t>(__builtin_bswap16(static_cast<uint16_t>(yi)));
        const float scale = static_cast<float>(1 << posFmt.frac);
        x = static_cast<float>(xi) / scale;
        y = static_cast<float>(yi) / scale;
      }
      std::fprintf(stderr,
                   "[NSMBW_DRAW_POS] prim=%u fmt=%u vtxCount=%u vtxSize=%u posType=%u posCnt=%u posFrac=%u "
                   "decodedXY=(%.2f,%.2f) rawFirstVtx=%s\n",
                   (unsigned)prim, (unsigned)fmt, vtxCount, vtxSize, (unsigned)posFmt.type, (unsigned)posFmt.cnt,
                   (unsigned)posFmt.frac, x, y, hex);
      std::fflush(stderr);
      // DIAGNOSTIC (temporary): the earlier NSMBW_COLOR_TEV probe (gx_texture.cpp) checked TEV
      // state at GXLoadTexObj time, which can run before the draw's own GXSetTevOrder/ColorIn
      // calls - so it may have logged stale state left over from an unrelated earlier material,
      // not what THIS draw actually uses. This checks it right here, at the real draw command,
      // for whichever stage(s) are actually active right now - the only place that's guaranteed
      // to reflect the state this specific draw will use.
      if (AURORA_ENV("NSMBW_LOG_DRAW_TEV") != nullptr) {
        static int tevAtDrawLogged = 0;
        if (tevAtDrawLogged < 40) {
          ++tevAtDrawLogged;
          for (uint32_t st = 0; st < g_gxState.numTevStages && st < 16; ++st) {
            const auto& s = g_gxState.tevStages[st];
            std::fprintf(stderr,
                "[NSMBW_DRAW_TEV] draw#%d stage=%u texMap=%u colorIn a=%d b=%d c=%d d=%d "
                "alphaIn a=%d b=%d c=%d d=%d\n",
                tevAtDrawLogged, st, static_cast<unsigned>(s.texMapId),
                static_cast<int>(s.colorPass.a), static_cast<int>(s.colorPass.b),
                static_cast<int>(s.colorPass.c), static_cast<int>(s.colorPass.d),
                static_cast<int>(s.alphaPass.a), static_cast<int>(s.alphaPass.b),
                static_cast<int>(s.alphaPass.c), static_cast<int>(s.alphaPass.d));
          }
          std::fflush(stderr);
        }
      }
    }
  }
  nsmbw_log_draw_texgen(prim, fmt, vertices, vtxCount, vtxSize);
  gfx::Range vertRange = gfx::push_verts(vertices, totalVtxBytes);
  pos += totalVtxBytes;

  // Try to merge with previous draw call
  if (!g_gxState.stateDirty) LIKELY {
    auto* lastDraw = gfx::get_last_draw_command<DrawData>();
    // Only if the previous draw call was a single instance draw (no lines/points handling)
    if (lastDraw != nullptr && prim != GX_LINES && prim != GX_LINESTRIP && prim != GX_POINTS &&
        lastDraw->instanceCount == 1) LIKELY {
      const auto& indexTemplate = cached_index_template(prim, vtxCount);
      const auto indices = offset_index_template(indexTemplate, lastDraw->vtxCount);
      const u32 numIndices = indexTemplate.indexCount;
      const gfx::Range idxRange = gfx::push_indices(indices);
      CHECK(lastDraw->vertRange.offset + lastDraw->vertRange.size == vertRange.offset,
            "Non-consecutive vertex ranges ({} < {})", lastDraw->vertRange.offset + lastDraw->vertRange.size,
            vertRange.offset);
      CHECK(lastDraw->idxRange.offset + lastDraw->idxRange.size == idxRange.offset,
            "Non-consecutive index ranges ({} < {})", lastDraw->idxRange.offset + lastDraw->idxRange.size,
            idxRange.offset);
      lastDraw->vertRange.size += vertRange.size;
      lastDraw->idxRange.size += idxRange.size;
      lastDraw->vtxCount += vtxCount;
      lastDraw->indexCount += numIndices;
      ++gfx::g_mergedDrawCallCount;
      // This primitive now renders through the draw we merged into, so its palette slots belong to that draw's interpolation snapshot as well.
      if (s_lastDrawRecordedInterpolation) UNLIKELY {
        extend_interpolation_draw(pn_mtx_mask(vertices, vtxCount, vtxSize));
      }
      return true;
    }
  }

  const bool interpolationIdentityActive = frame_interpolation_identity_needed();
  const PnMtxUsage matrixUsage = interpolationIdentityActive
                                     ? pn_mtx_usage(vertices, vtxCount, vtxSize)
                                     : PnMtxUsage{};
  handle_draw_unmerged(prim, fmt, vtxCount, vertRange,
                       matrixUsage.mask, matrixUsage.topologySignature,
                       interpolationIdentityActive ? draw_geometry_signature(fmt, vertices, vtxCount, vtxSize) : 0,
                       interpolationIdentityActive);
  return true;
}

static void handle_draw_unmerged(GXPrimitive prim, GXVtxFmt fmt, u16 vtxCount,
                                 gfx::Range vertRange, uint16_t usedPnMtxMask,
                                 HashType matrixTopologySignature,
                                 HashType geometrySignature, bool interpolationIdentityActive) {
  ZoneScoped;
  // DIAGNOSTIC: NSMBW_LOG_DRAW_FLOW - counts every call into handle_draw_unmerged, and separately
  // every one that gets dropped here by GX_CULL_ALL before it ever reaches
  // push_render_pass/push_draw_command.
  if (AURORA_ENV("NSMBW_LOG_DRAW_FLOW") != nullptr) {
    static uint64_t totalCalls = 0;
    static uint64_t cullAllDrops = 0;
    ++totalCalls;
    const bool wouldDrop =
        g_gxState.cullMode == GX_CULL_ALL && prim != GX_LINES && prim != GX_LINESTRIP && prim != GX_POINTS;
    if (wouldDrop) ++cullAllDrops;
    if (totalCalls <= 200 || totalCalls % 2000 == 0) {
      std::fprintf(stderr,
                   "[NSMBW_DRAW_FLOW] call#%llu prim=%u fmt=%u vtxCount=%u cullMode=%u wouldDrop=%d "
                   "totalCalls=%llu cullAllDrops=%llu\n",
                   (unsigned long long)totalCalls, (unsigned)prim, (unsigned)fmt, (unsigned)vtxCount,
                   (unsigned)g_gxState.cullMode, wouldDrop ? 1 : 0, (unsigned long long)totalCalls,
                   (unsigned long long)cullAllDrops);
      std::fflush(stderr);
    }
  }
  // GX_CULL_ALL rasterizes nothing on hardware - no color, no depth.
  if (g_gxState.cullMode == GX_CULL_ALL && prim != GX_LINES && prim != GX_LINESTRIP && prim != GX_POINTS)
      UNLIKELY {
    // Leave stateDirty alone: the next draw re-resolving its pipeline is the safe direction, and nothing about this draw reached the GPU.
    return;
  }
  // Callers hold the renderer GPU mutex for the whole drain (process() and submit_raw_draw); taking it again per draw only cost a recursive re-entry.
  const auto& indexTemplate = cached_index_template(prim, vtxCount);
  const u32 numIndices = indexTemplate.indexCount;
  const gfx::Range idxRange = gfx::push_indices(ArrayRef<u16>{
      indexTemplate.indices.data(), indexTemplate.indices.size()});

  // Build pipeline, bind groups, and push draw command
  BindGroupRanges ranges{};
  for (int i = GX_VA_POS; i <= GX_VA_TEX7; ++i) {
    if (g_gxState.vtxDesc[i] != GX_INDEX8 && g_gxState.vtxDesc[i] != GX_INDEX16) {
      continue;
    }
    auto& array = g_gxState.arrays[i];
    if (array.cachedRange.size > 0) {
      ranges.vaRanges[i - GX_VA_POS] = array.cachedRange;
    } else {
      const auto range = gfx::push_storage(static_cast<const uint8_t*>(array.data), array.size);
      ranges.vaRanges[i - GX_VA_POS] = range;
      array.cachedRange = range;
    }
  }

  const auto& pipelineState = resolve_pipeline_state(prim, fmt);
  const auto& info = pipelineState.shaderInfo;

  resolve_sampled_textures(info);

  // TEMPORARY DIAGNOSTIC: NSMBW black-screen isolation - real bound texture content. Remove
  // before merging.
  if (aurora::nsmbw_diag_enabled() && AURORA_ENV("NSMBW_TEX_PEEK") != nullptr &&
      (AURORA_ENV("NSMBW_TEX_PEEK_SCENE") == nullptr ||
       g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(std::atoi(AURORA_ENV("NSMBW_TEX_PEEK_SCENE"))))) {
    static int drawsLogged = 0;
    // Correlate with the vertex-decode finding: prim=GX_QUADS(0x80), fmt=0, vtxCount=4 was the
    // real full-screen background quad (X spans 0..640, decoded last pass). Log every textured
    // draw's context for the first several draws so this can be matched by hand, instead of
    // silently keeping only the first-ever use of each texture slot (which could belong to an
    // unrelated earlier draw).
    if (info.sampledTextures.any() && drawsLogged < 80) { // TEMP: raised from 12 for one clean frame's worth of pic1 draws; revert after this check
      ++drawsLogged;
      for (u32 ti = 0; ti < MaxTextures; ++ti) {
        if (!info.sampledTextures.test(ti)) {
          continue;
        }
        const auto& bind = g_gxState.textures[ti];
        if (!bind.ref || !bind.ref->texture) {
          std::fprintf(stderr,
                       "[NSMBW_TEXPEEK] draw#%d prim=%u fmt=%u vtxCount=%u slot=%u no resolved texture "
                       "(sampled but ref/texture null)\n",
                       drawsLogged, static_cast<unsigned>(prim), static_cast<unsigned>(fmt), vtxCount, ti);
          std::fflush(stderr);
          continue;
        }
        std::fprintf(stderr,
                     "[NSMBW_TEXPEEK] draw#%d prim=%u fmt=%u vtxCount=%u slot=%u guestDataPtr=%p guestW=%u "
                     "guestH=%u guestFmt=%u gpuSize=%ux%u\n",
                     drawsLogged, static_cast<unsigned>(prim), static_cast<unsigned>(fmt), vtxCount, ti,
                     bind.texObj.data, bind.texObj.width(), bind.texObj.height(),
                     static_cast<unsigned>(bind.texObj.format()), bind.ref->size.width, bind.ref->size.height);
        std::fflush(stderr);
        char stageBuf[64];
        std::snprintf(stageBuf, sizeof(stageBuf), "draw%d_slot%u", drawsLogged, ti);
        webgpu::nsmbw_diag_peek_texture_standalone(bind.ref->texture, bind.ref->size.width, bind.ref->size.height,
                                                   stageBuf);
      }
    }
  }

  const auto bindGroups = build_bind_groups(info);

  const auto pipeline = pipelineState.ref;

  // Draw-identity hashing only feeds frame interpolation, and only for perspective draws: build_uniform reads the identity exclusively past its `!perspective || frame_interpolation_fps() == 0` early-out.
  FrameInterpolationDrawIdentity drawIdentity{};
  if (interpolationIdentityActive) UNLIKELY {
    const HashType drawShape = static_cast<HashType>(vtxCount) |
                               (static_cast<HashType>(underlying(prim)) << 16) |
                               (static_cast<HashType>(underlying(fmt)) << 24);
    const HashType pipelineDrawSignature = xxh3_hash(pipelineState.configHash, drawShape);
    const HashType textureSignature = xxh3_hash(bindGroups.textureBindGroup);
    const HashType materialAndTopology =
        xxh3_hash(matrixTopologySignature,
                  xxh3_hash(bindGroups.textureBindGroup, pipelineDrawSignature));
    drawIdentity = FrameInterpolationDrawIdentity{
        .combined = xxh3_hash(geometrySignature, materialAndTopology),
        .pipeline = pipelineDrawSignature,
        .texture = textureSignature,
        .matrixTopology = matrixTopologySignature,
    };
  }
  const auto uniformRanges =
      build_uniform(info, vertRange.offset, ranges, drawIdentity, interpolationIdentityActive,
                    usedPnMtxMask);
  s_lastDrawRecordedInterpolation = interpolationIdentityActive;

  uint32_t instanceCount = 1;
  if (prim == GX_LINES) {
    instanceCount = vtxCount / 2;
  } else if (prim == GX_LINESTRIP) {
    instanceCount = vtxCount - 1;
  } else if (prim == GX_POINTS) {
    instanceCount = vtxCount;
  }
  gfx::push_draw_command(DrawData{
      .pipeline = pipeline,
      .vertRange = vertRange,
      .idxRange = idxRange,
      .uniformRange = uniformRanges.current,
      .interpolatedUniformRanges = uniformRanges.interpolated,
      .vtxCount = vtxCount,
      .indexCount = numIndices,
      .instanceCount = instanceCount,
      .bindGroups = bindGroups,
      .dstAlpha = pipelineState.dstAlpha,
  });
  g_gxState.stateDirty = false;
}

std::string read_string(const u8* data, u32& pos, u32 size, bool bigEndian) {
  CHECK(pos + 2 <= size, "Aurora string length read overrun");
  const u16 length = read_u16(data + pos, bigEndian);
  pos += 2;

  CHECK(pos + length <= size, "Aurora string read overrun");
  std::string str(reinterpret_cast<const char*>(data) + pos, length);
  pos += length;
  return str;
}

bool handle_aurora(const u8* data, u32& pos, u32 size, bool bigEndian) {
  ZoneScoped;
  if (pos + 2 > size) {
    return false;
  }
  u16 subCmd = read_u16(data + pos, bigEndian);
  pos += 2;

  // Setting of vertex array bases.
  if (subCmd == GX_LOAD_AURORA_VIEWPORT_RENDER) {
    CHECK(pos + 24 <= size, "GX_LOAD_AURORA_VIEWPORT_RENDER read overrun");
    const f32 left = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 top = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 width = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 height = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 nearZ = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 farZ = read_f32(data + pos, bigEndian);
    pos += 4;
    set_render_viewport({
        .left = left,
        .top = top,
        .width = width,
        .height = height,
        .znear = nearZ,
        .zfar = farZ,
    });
  } else if (subCmd == GX_LOAD_AURORA_SCISSOR_RENDER) {
    CHECK(pos + 16 <= size, "GX_LOAD_AURORA_SCISSOR_RENDER read overrun");
    const int32_t left = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    const int32_t top = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    const int32_t width = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    const int32_t height = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    set_render_scissor({left, top, width, height});
  } else if (subCmd >= GX_LOAD_AURORA_ARRAYBASE && subCmd <= (GX_LOAD_AURORA_ARRAYBASE | 0x0f)) {
    CHECK(pos + 13 <= size, "GX_LOAD_AURORA_ARRAYBASE read overrun");
    u32 attrIdx = subCmd - GX_LOAD_AURORA_ARRAYBASE + GX_VA_POS;

    u64 arrayAddr = read_u64(data + pos, bigEndian);
    pos += 8;
    u32 arraySize = read_u32(data + pos, bigEndian);
    pos += 4;
    bool le = data[pos] == 1;
    pos += 1;

    auto& array = g_gxState.arrays[attrIdx];
    const auto newData = reinterpret_cast<void*>(arrayAddr);
    if (array.data != newData || array.size != arraySize || array.le != le) {
      array.data = newData;
      array.size = arraySize;
      array.le = le;
      // Only drop the cached upload when the backing array actually changes.
      array.cachedRange = {};
      mark_pipeline_state_dirty();
    }
  } else if (subCmd == GX_LOAD_AURORA_TEXOBJ) {
    CHECK(pos + 34 <= size, "GX_LOAD_AURORA_TEXOBJ read overrun");
    const auto texMapId = data[pos];
    pos += 1;
    CHECK(texMapId < MaxTextures, "invalid texture map id {}", texMapId);
    auto& slot = g_gxState.loadedTextures[texMapId];
    GXTexObj_ next = slot;
    next.data = reinterpret_cast<const void*>(read_u64(data + pos, bigEndian));
    pos += 8;
    next.mWidth = read_u32(data + pos, bigEndian);
    pos += 4;
    next.mHeight = read_u32(data + pos, bigEndian);
    pos += 4;
    next.mFormat = static_cast<GXTexFmt>(read_u32(data + pos, bigEndian));
    pos += 4;
    next.tlut = static_cast<GXTlut>(read_u32(data + pos, bigEndian));
    pos += 4;
    if (data[pos] != 0) {
      next.flags |= 1u;
    } else {
      next.flags &= ~1u;
    }
    pos += 1;
    next.texObjId = read_u32(data + pos, bigEndian);
    pos += 4;
    next.texDataVersion = read_u32(data + pos, bigEndian);
    pos += 4;
    next.set_no_cache(false); // Reset no-cache flag
    const bool changed = slot.data != next.data || slot.mWidth != next.mWidth || slot.mHeight != next.mHeight ||
                         slot.mFormat != next.mFormat || slot.tlut != next.tlut || slot.flags != next.flags ||
                         slot.texObjId != next.texObjId || slot.texDataVersion != next.texDataVersion;
    slot = next;
    if (changed) {
      g_gxState.stateDirty = true;
    }
  } else if (subCmd == GX_LOAD_AURORA_TLUT) {
    CHECK(pos + 23 <= size, "GX_LOAD_AURORA_TLUT read overrun");
    const auto idx = data[pos];
    pos += 1;
    CHECK(idx < MaxTluts, "invalid tlut slot {}", idx);
    auto& slot = g_gxState.loadedTluts[idx];
    slot.data = reinterpret_cast<const void*>(read_u64(data + pos, bigEndian));
    pos += 8;
    slot.format = static_cast<GXTlutFmt>(read_u32(data + pos, bigEndian));
    pos += 4;
    slot.numEntries = read_u16(data + pos, bigEndian);
    pos += 2;
    slot.tlutObjId = read_u32(data + pos, bigEndian);
    pos += 4;
    slot.tlutDataVersion = read_u32(data + pos, bigEndian);
    pos += 4;
    slot.set_no_cache(false); // Reset no-cache flag
    g_gxState.stateDirty = true;
  } else if (subCmd == GX_LOAD_AURORA_DESTROY_TEXOBJ) {
    CHECK(pos + 4 <= size, "GX_LOAD_AURORA_DESTROY_TEXOBJ read overrun");
    evict_texture_object(read_u32(data + pos, bigEndian));
    pos += 4;
  } else if (subCmd == GX_LOAD_AURORA_DESTROY_TLUT) {
    CHECK(pos + 4 <= size, "GX_LOAD_AURORA_DESTROY_TLUT read overrun");
    evict_tlut_object(read_u32(data + pos, bigEndian));
    pos += 4;
  } else if (subCmd == GX_LOAD_AURORA_DESTROY_COPY_TEX) {
    CHECK(pos + 8 <= size, "GX_LOAD_AURORA_DESTROY_COPY_TEX read overrun");
    evict_copy_texture(reinterpret_cast<const void*>(read_u64(data + pos, bigEndian)));
    pos += 8;
  } else if (subCmd == GX_LOAD_AURORA_INVALIDATE_TEX_ALL) {
    invalidate_static_texture_cache();
  } else if (subCmd == GX_LOAD_AURORA_DEBUG_GROUP_PUSH) {
    auto label = read_string(data, pos, size, bigEndian);
    gfx::push_debug_group(std::move(label));
  } else if (subCmd == GX_LOAD_AURORA_DEBUG_GROUP_POP) {
    aurora_pop_debug_group();
  } else if (subCmd == GX_LOAD_AURORA_DEBUG_MARKER_INSERT) {
    auto label = read_string(data, pos, size, bigEndian);
    if (label == "nsmbw-arm-drawlog") {
      s_nsmbwArmedDraws = 8;
    }
    gfx::insert_debug_marker(std::move(label));
  }

  else {
    static u32 unknownAuroraLogCount = 0;
    if (unknownAuroraLogCount < 16) {
      Log.warn("Unknown Aurora subcommand: {:04X}; stopping FIFO decode", subCmd);
      ++unknownAuroraLogCount;
    }
    return false;
  }
  return true;
}

} // namespace aurora::gx::fifo

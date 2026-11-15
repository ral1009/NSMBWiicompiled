#pragma once
// Address layer for building the same native source against more than one NSMBW version
// (docs/futureplans.md section 3; the idea is MULTI_REGION_GUIDE.md's, with PAL as the base here).
//
// Every guest address in our hand-written code is written as a PAL rev 1 ("P1") address. A build
// for another version (CMake -DNSMBW_GAME_VERSION=E1) defines NSMBW_VERSION_TABLES and puts that
// version's generated tables on the include path (projects/nsmbw/tools/gen_version_tables.py):
//   nsmbw_va_ranges.inc  {P1 start, P1 end, offset} rows from NSMBW-Maps/address-map.txt
//   nsmbw_va_tokens.h    #define NSMBW_VA_TOK_<P1 hex> <version hex>, one per hook token in the tree
// For P1 every macro below is the identity, so the PAL build compiles to what it did before.
//
//   NSMBW_VA(0x8042A228u)   a P1 address, mapped at compile time. Forced to a compile-time constant
//                           (std::integral_constant), so it costs nothing at run time and works in
//                           case labels and template arguments. An address the map has no
//                           counterpart for fails the build (Unmapped() is declared, never defined).
//   NSMBW_VA_HOOK(addr)     the same, but an unmapped address stays as it is. Only for the hook
//                           macros, which are also used at Mario Kart Wii addresses in the shared
//                           runtime (dead code for NSMBW, see CLAUDE.md); those keep their behaviour.
//   NSMBW_TOKEN(801D2D60)   the bare hex token, mapped (for pasting into names)
//   NSMBW_FUNC(801D2D60)    the translated function at that P1 address, by this version's name
//   NSMBW_SDA13(off)        for code written as `ctx->gpr[13] - off` against P1's r13: the offset
//   NSMBW_SDA2(off)         that reaches the same object in this version (its small-data variables
//                           moved by a different amount than r13 itself; same for r2)

#include <cstdint>
#include <type_traits>

#define NSMBW_PP_CAT_(a, b) a##b
#define NSMBW_PP_CAT(a, b) NSMBW_PP_CAT_(a, b)

#if defined(NSMBW_VERSION_TABLES)
#include "nsmbw_va_tokens.h"

namespace nsmbw_va {
struct Range {
    uint32_t start;
    uint32_t end;  // inclusive
    int32_t offset;
};
inline constexpr Range kRanges[] = {
#include "nsmbw_va_ranges.inc"
};
// Generated next to the ranges: this version's and P1's small-data bases (r13, r2).
inline constexpr uint32_t kSda13P1 = NSMBW_VA_SDA13_P1, kSda13 = NSMBW_VA_SDA13;
inline constexpr uint32_t kSda2P1 = NSMBW_VA_SDA2_P1, kSda2 = NSMBW_VA_SDA2;

uint32_t Unmapped(uint32_t address);  // deliberately never defined

constexpr uint32_t Map(uint32_t address) {
    for (const Range& r : kRanges) {
        if (address >= r.start && address <= r.end) return address + static_cast<uint32_t>(r.offset);
    }
    return Unmapped(address);  // not a constant expression: the build stops here
}
constexpr uint32_t MapOrSelf(uint32_t address) {
    for (const Range& r : kRanges) {
        if (address >= r.start && address <= r.end) return address + static_cast<uint32_t>(r.offset);
    }
    return address;
}
}  // namespace nsmbw_va

#define NSMBW_TOKEN(tok) NSMBW_PP_CAT(NSMBW_VA_TOK_, tok)
#define NSMBW_VA(address) (::std::integral_constant<uint32_t, ::nsmbw_va::Map(static_cast<uint32_t>(address))>::value)
#define NSMBW_VA_HOOK(address) \
    (::std::integral_constant<uint32_t, ::nsmbw_va::MapOrSelf(static_cast<uint32_t>(address))>::value)
#define NSMBW_SDA13(off) \
    (static_cast<int32_t>(::nsmbw_va::kSda13 - NSMBW_VA(::nsmbw_va::kSda13P1 - static_cast<uint32_t>(off))))
#define NSMBW_SDA2(off) \
    (static_cast<int32_t>(::nsmbw_va::kSda2 - NSMBW_VA(::nsmbw_va::kSda2P1 - static_cast<uint32_t>(off))))
#else
#define NSMBW_TOKEN(tok) tok
#define NSMBW_VA(address) (address)
#define NSMBW_VA_HOOK(address) (address)
#define NSMBW_SDA13(off) (off)
#define NSMBW_SDA2(off) (off)
#endif

#define NSMBW_FUNC(tok) NSMBW_PP_CAT(func_, NSMBW_TOKEN(tok))

// A per-version fact that is not an address (a REL's size, say): NSMBW_BY_VERSION(pal, usa).
// CMake defines NSMBW_VERSION_E1 for the USA rev 1 build. Add a parameter per new version.
#if defined(NSMBW_VERSION_E1)
#define NSMBW_BY_VERSION(p1, e1) (e1)
#else
#define NSMBW_BY_VERSION(p1, e1) (p1)
#endif

// Copied translated code builds an address in two steps, as the PowerPC does (lis + a signed low
// half): `r3 = 0x80430000u; ... r3 + -23776`. Mapping the top half alone is only right when the
// map shifts both by the same amount, which is not true near a range boundary. So the codemod
// writes the top half as NSMBW_VA_PAIR(0x80430000u, -23776): the full address mapped, minus the
// same low half, which gives the right top half for that use in every version.
#define NSMBW_VA_PAIR(hi, lo) (NSMBW_VA((hi) + static_cast<uint32_t>(lo)) - static_cast<uint32_t>(lo))

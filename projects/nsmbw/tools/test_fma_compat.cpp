// Checks runtime/include/isa/ppc_isa_fma_compat.h bit for bit against hardware vfmaddps/vfmsubps.
// Needs a CPU with FMA (the reference functions use it); the helper itself is built for
// x86-64-v2, the compat exe's target.
//
//   clang++ -O2 -march=x86-64-v2 -fno-fast-math -ffp-contract=off -I runtime/include
//       projects/nsmbw/tools/test_fma_compat.cpp -o build/test_fma_compat.exe
//   build/test_fma_compat.exe [millions of cases per generator, default 20]
//
// Exit code 0 = every non-NaN result identical and every NaN result NaN, in all four MXCSR
// FTZ/DAZ combinations. It also counts how often two wrong implementations differ, to show the
// test can see the errors it is meant to catch.

#include "isa/ppc_isa_fma_compat.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

__attribute__((target("fma"), noinline)) __m128 RefFmadd(__m128 a, __m128 c, __m128 b) {
    return _mm_fmadd_ps(a, c, b);
}
__attribute__((target("fma"), noinline)) __m128 RefFmsub(__m128 a, __m128 c, __m128 b) {
    return _mm_fmsub_ps(a, c, b);
}
__attribute__((noinline)) __m128 NaiveMulAdd(__m128 a, __m128 c, __m128 b) {
    return _mm_add_ps(_mm_mul_ps(a, c), b);
}
__attribute__((noinline)) __m128 DoubleNoOdd(__m128 a, __m128 c, __m128 b) {
    return _mm_cvtpd_ps(_mm_add_pd(_mm_mul_pd(_mm_cvtps_pd(a), _mm_cvtps_pd(c)), _mm_cvtps_pd(b)));
}

uint64_t g_rng = 0x9E3779B97F4A7C15ull;
uint32_t Rand32() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return static_cast<uint32_t>(g_rng >> 16);
}
uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float Flt(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
bool IsNan(uint32_t u) { return (u & 0x7FFFFFFFu) > 0x7F800000u; }

// Any bit pattern, but with special exponents (0, 255) and small/large ones made common.
float RandomAnyFloat() {
    uint32_t u = Rand32();
    switch (Rand32() & 7) {
    case 0: u &= 0x807FFFFFu; break;                        // zero / subnormal
    case 1: u |= 0x7F800000u; break;                        // inf / NaN
    case 2: u = (u & 0x807FFFFFu) | (1u << 23); break;      // smallest normals
    case 3: u = (u & 0x81FFFFFFu) | (0x3Fu << 25); break;   // around 1.0
    default: break;
    }
    return Flt(u);
}

// a, c ordinary; b picked so a*c+b lands on, or right next to, a float rounding tie. That is
// where rounding twice goes wrong, and random inputs almost never hit it.
void NearTieCase(float& a, float& c, float& b) {
    a = Flt((Rand32() & 0x80FFFFFFu) | (static_cast<uint32_t>(100 + (Rand32() % 54)) << 23));
    c = Flt((Rand32() & 0x80FFFFFFu) | (static_cast<uint32_t>(100 + (Rand32() % 54)) << 23));
    const double p = static_cast<double>(a) * c;
    const float f = static_cast<float>(p);
    const float g = Flt(Bits(f) + ((Rand32() & 1) ? 1 : -1));  // a float neighbour of f
    const double halfway = (static_cast<double>(f) + g) * 0.5;
    b = static_cast<float>(halfway - p);
    const int nudge = static_cast<int>(Rand32() % 5) - 2;       // b's own neighbours too
    b = Flt(Bits(b) + nudge);
    if (Rand32() & 1) {
        b = static_cast<float>(-p) ;                              // heavy cancellation
        b = Flt(Bits(b) + static_cast<int>(Rand32() % 9) - 4);
    }
}

struct Counts {
    uint64_t cases = 0, mismatches = 0, nanPayloadDiffs = 0, naiveDiffs = 0, noOddDiffs = 0;
};

void Check(__m128 (*helper)(__m128, __m128, __m128), __m128 (*ref)(__m128, __m128, __m128),
           bool isMadd, float a0, float c0, float b0, float a1, float c1, float b1, Counts& n) {
    const __m128 a = _mm_setr_ps(a0, a1, 0, 0);
    const __m128 c = _mm_setr_ps(c0, c1, 0, 0);
    const __m128 b = _mm_setr_ps(b0, b1, 0, 0);
    alignas(16) uint32_t got[4], want[4], naive[4], noOdd[4];
    _mm_store_ps(reinterpret_cast<float*>(got), helper(a, c, b));
    _mm_store_ps(reinterpret_cast<float*>(want), ref(a, c, b));
    if (isMadd) {
        _mm_store_ps(reinterpret_cast<float*>(naive), NaiveMulAdd(a, c, b));
        _mm_store_ps(reinterpret_cast<float*>(noOdd), DoubleNoOdd(a, c, b));
    }
    for (int lane = 0; lane < 2; ++lane) {
        ++n.cases;
        if (IsNan(want[lane]) || IsNan(got[lane])) {
            if (IsNan(want[lane]) != IsNan(got[lane])) {
                if (n.mismatches++ < 10)
                    std::printf("  NaN-ness differs: lane %d got %08X want %08X\n", lane, got[lane], want[lane]);
            } else if (got[lane] != want[lane]) {
                // Only allowed when a and c are both NaN: which wins is operand order, and the
                // compiler picks that per call site in the v3 build too (see the header).
                const uint32_t aa = Bits(lane ? a1 : a0), cc = Bits(lane ? c1 : c0);
                if (IsNan(aa) && IsNan(cc)) {
                    ++n.nanPayloadDiffs;
                } else if (n.mismatches++ < 10) {
                    std::printf("  NaN payload differs: lane %d a=%08X c=%08X b=%08X got %08X want %08X\n",
                                lane, aa, cc, Bits(lane ? b1 : b0), got[lane], want[lane]);
                }
            }
            continue;
        }
        if (got[lane] != want[lane] && n.mismatches++ < 10) {
            const float aa = lane ? a1 : a0, cc = lane ? c1 : c0, bb = lane ? b1 : b0;
            std::printf("  MISMATCH lane %d: a=%08X c=%08X b=%08X got %08X want %08X\n", lane,
                        Bits(aa), Bits(cc), Bits(bb), got[lane], want[lane]);
        }
        if (isMadd) {
            n.naiveDiffs += naive[lane] != want[lane];
            n.noOddDiffs += noOdd[lane] != want[lane];
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const uint64_t millions = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20;
    const unsigned savedCsr = _mm_getcsr();
    bool ok = true;
    for (unsigned mode = 0; mode < 4; ++mode) {
        const bool ftz = mode & 1, daz = mode & 2;
        _mm_setcsr((savedCsr & ~0x8040u) | (ftz ? 0x8000u : 0) | (daz ? 0x0040u : 0));
        for (int op = 0; op < 2; ++op) {
            const bool isMadd = op == 0;
            for (int gen = 0; gen < 2; ++gen) {
                Counts n;
                for (uint64_t i = 0; i < millions * 500000; ++i) {
                    float a0, c0, b0, a1, c1, b1;
                    if (gen == 0) {
                        a0 = RandomAnyFloat(); c0 = RandomAnyFloat(); b0 = RandomAnyFloat();
                        a1 = RandomAnyFloat(); c1 = RandomAnyFloat(); b1 = RandomAnyFloat();
                    } else {
                        NearTieCase(a0, c0, b0);
                        NearTieCase(a1, c1, b1);
                        if (!isMadd) { b0 = -b0; b1 = -b1; }
                    }
                    Check(isMadd ? PpcFmaddPairCompatInline : PpcFmsubPairCompatInline,
                          isMadd ? RefFmadd : RefFmsub, isMadd, a0, c0, b0, a1, c1, b1, n);
                }
                std::printf("FTZ=%d DAZ=%d %s %-9s cases=%llu mismatches=%llu a-and-c-NaN-payload-diffs=%llu",
                            ftz, daz, isMadd ? "madd" : "msub", gen ? "near-tie" : "any-bits",
                            static_cast<unsigned long long>(n.cases),
                            static_cast<unsigned long long>(n.mismatches),
                            static_cast<unsigned long long>(n.nanPayloadDiffs));
                if (isMadd)
                    std::printf(" | mul+add wrong=%llu double-without-odd wrong=%llu",
                                static_cast<unsigned long long>(n.naiveDiffs),
                                static_cast<unsigned long long>(n.noOddDiffs));
                std::printf("\n");
                ok = ok && n.mismatches == 0;
            }
        }
    }
    _mm_setcsr(savedCsr);
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}

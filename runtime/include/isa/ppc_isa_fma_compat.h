#pragma once
// Exact float32 fused multiply-add for builds without hardware FMA (the x86-64-v2 "compat" exe).
//
// Gekko's ps_madd family rounds once, at the end. The v3 build gets that from vfmaddps. A
// multiply followed by an add rounds twice and can be one ULP off, which is the drift
// PpcAccuratePsMaddLaneInline warns about, and recorded-input replays (the title demo, hint
// movies) could desync on it. So the compat build cannot just use mul+add.
//
// How this stays exact with SSE2 only:
//  1. Widen the float lanes to double. A float has a 24-bit significand, so a*c needs at most
//     48 bits and is EXACT in a double (53 bits). No rounding has happened yet.
//  2. s = a*c + b in double rounds once. TwoSum (Knuth) recovers that rounding's error e exactly.
//  3. Round to odd: if e != 0, replace s by the neighbouring double, on the exact value's side,
//     whose last bit is 1. A round-to-odd result in p+2 or more bits rounds to the same p-bit
//     value as the exact sum would (Boldo & Melquiond, 2008), and double has 53 >= 24+2 bits.
//  4. Narrow to float: that single rounding is the only one that reaches the result.
//
// Range: products and sums of floats never get near double overflow or double subnormals, so
// MXCSR FTZ/DAZ (how this runtime models FPSCR[NI]) only act where they do for vfmaddps: DAZ on
// the float inputs at CVTPS2PD, FTZ on the float result at CVTPD2PS. Round-to-odd can't move a
// value across 2^-126 (its significand is even), so the underflow test sees the same magnitude.
// Inf/NaN inputs make e NaN; those lanes skip step 3 and keep the plain IEEE result.
//
// NaN payloads: with one NaN input the result is that NaN, quieted, as with vfmaddps. When a
// and c are both NaN, which one comes out depends on operand order, which the compiler picks
// per call site in BOTH builds (mul is commutative; vfmadd has 132/213/231 forms), so the v3
// exe doesn't fix this either. Gekko's own priority is different again.
//
// Verified bit for bit against vfmaddps by projects/nsmbw/tools/test_fma_compat.cpp.

#include <immintrin.h>

// Lanes 0 and 1 of a, c, b (the two paired-single lanes) -> fma rounded to float, in lanes 0-1.
// Lanes 2-3 of the result are zero; paired-single code only reads the low 64 bits.
inline __m128 PpcFmaddPairCompatInline(__m128 a, __m128 c, __m128 b)
{
    const __m128d ad = _mm_cvtps_pd(a);
    const __m128d cd = _mm_cvtps_pd(c);
    const __m128d bd = _mm_cvtps_pd(b);

    const __m128d p = _mm_mul_pd(ad, cd);  // exact
    const __m128d s = _mm_add_pd(p, bd);   // one rounding
    // TwoSum: e = (p + b) - s exactly. Six operations, no branches, valid for any p and b
    // that don't overflow.
    const __m128d bVirtual = _mm_sub_pd(s, p);
    const __m128d pVirtual = _mm_sub_pd(s, bVirtual);
    const __m128d e = _mm_add_pd(_mm_sub_pd(p, pVirtual), _mm_sub_pd(bd, bVirtual));

    // Lanes to fix: e is a nonzero number. (e < 0) | (e > 0) is false for NaN, unlike !=.
    const __m128d zero = _mm_setzero_pd();
    const __m128i inexact = _mm_castpd_si128(_mm_or_pd(_mm_cmplt_pd(e, zero), _mm_cmpgt_pd(e, zero)));

    // Odd neighbour on the exact value's side = (truncate toward zero) | 1. s was rounded away
    // from zero exactly when e's sign differs from s's, and then truncating means bits - 1.
    const __m128i sBits = _mm_castpd_si128(s);
    const __m128i signDiffers =
        _mm_srli_epi64(_mm_xor_si128(sBits, _mm_castpd_si128(e)), 63);  // 0 or 1 per lane
    const __m128i one = _mm_set_epi64x(1, 1);
    const __m128i odd = _mm_or_si128(_mm_sub_epi64(sBits, signDiffers), one);
    const __m128i fixed =
        _mm_or_si128(_mm_and_si128(inexact, odd), _mm_andnot_si128(inexact, sBits));

    return _mm_cvtpd_ps(_mm_castsi128_pd(fixed));
}

// a*c - b. Negating b is exact, so this is the same single rounding as vfmsubps. A NaN b is
// left unnegated: vfmsubps returns a NaN addend with its sign as it was (measured).
inline __m128 PpcFmsubPairCompatInline(__m128 a, __m128 c, __m128 b)
{
    const __m128 signMask = _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(0x80000000u)));
    const __m128 ordered = _mm_cmpord_ps(b, b);
    const __m128 negated = _mm_or_ps(_mm_and_ps(ordered, _mm_xor_ps(b, signMask)),
                                     _mm_andnot_ps(ordered, b));
    return PpcFmaddPairCompatInline(a, c, negated);
}

// Scalar form, for the std::fma(float, float, float) call sites.
inline float PpcFmaddFloatCompatInline(float a, float c, float b)
{
    return _mm_cvtss_f32(PpcFmaddPairCompatInline(_mm_set_ss(a), _mm_set_ss(c), _mm_set_ss(b)));
}

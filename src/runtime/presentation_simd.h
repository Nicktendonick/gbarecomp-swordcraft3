// Private arithmetic helpers for the host scaler. SSE2 is baseline on x64;
// other targets retain the scalar implementation, with no new CPU requirement.
#pragma once
#if !defined(GBARECOMP_PRESENTATION_SCALAR) && \
    (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define GBARECOMP_PRESENTATION_SSE2 1
#include <emmintrin.h>

namespace gbarecomp::runtime::presentation_simd {
inline __m128i byte_distance(__m128i a, __m128i b) noexcept {
    return _mm_or_si128(_mm_subs_epu8(a,b), _mm_subs_epu8(b,a));
}
// Each 32-bit lane is an independent four-byte tuple. A lane is accepted only
// if ALL its unsigned components satisfy the bound; no cross-lane reduction.
inline __m128i within(__m128i distance, int limit) noexcept {
    return _mm_cmpeq_epi32(_mm_subs_epu8(distance, _mm_set1_epi8(static_cast<char>(limit))),
                          _mm_setzero_si128());
}
inline __m128i choose(__m128i mask, __m128i yes, __m128i no) noexcept {
    return _mm_or_si128(_mm_and_si128(mask,yes), _mm_andnot_si128(mask,no));
}
// Four totals in [12,16]. Duplicate each reciprocal into the two 16-bit words
// of its lane. Later unpacking broadcasts it over that tuple's four channels.
inline __m128i inverse_pairs(__m128i totals) noexcept {
    auto inverse = _mm_set1_epi16(5462);
    inverse = choose(_mm_cmpeq_epi32(totals,_mm_set1_epi32(13)),_mm_set1_epi16(5042),inverse);
    inverse = choose(_mm_cmpeq_epi32(totals,_mm_set1_epi32(14)),_mm_set1_epi16(4682),inverse);
    inverse = choose(_mm_cmpeq_epi32(totals,_mm_set1_epi32(15)),_mm_set1_epi16(4370),inverse);
    return choose(_mm_cmpeq_epi32(totals,_mm_set1_epi32(16)),_mm_set1_epi16(4096),inverse);
}
inline __m128i normalize(__m128i low_sum, __m128i high_sum, __m128i totals) noexcept {
    const auto inverse = inverse_pairs(totals);
    auto half = _mm_srli_epi32(totals,1);
    half = _mm_or_si128(half,_mm_slli_epi32(half,16));
    const auto low = _mm_mulhi_epu16(_mm_add_epi16(low_sum,_mm_unpacklo_epi32(half,half)),
                                   _mm_unpacklo_epi32(inverse,inverse));
    const auto high = _mm_mulhi_epu16(_mm_add_epi16(high_sum,_mm_unpackhi_epi32(half,half)),
                                    _mm_unpackhi_epi32(inverse,inverse));
    return _mm_packus_epi16(low,high);
}

inline __m128i quadrant(__m128i center, __m128i horizontal, __m128i vertical,
                        __m128i diagonal, __m128i hc, __m128i vc) noexcept {
    const auto corner = _mm_andnot_si128(
        _mm_or_si128(within(hc,47),within(vc,47)),
        _mm_and_si128(within(byte_distance(horizontal,vertical),24),
            _mm_and_si128(within(byte_distance(horizontal,diagonal),32),
                          within(byte_distance(vertical,diagonal),32))));
    const auto hm = _mm_or_si128(corner,within(hc,63));
    const auto vm = _mm_or_si128(corner,within(vc,63));
    const auto dm = _mm_andnot_si128(corner,within(byte_distance(diagonal,center),47));
    const auto one = _mm_set1_epi32(1);
    const auto totals = _mm_add_epi32(_mm_set1_epi32(12),
        _mm_add_epi32(_mm_add_epi32(_mm_and_si128(hm,one),_mm_and_si128(vm,one)),
                     _mm_add_epi32(_mm_and_si128(dm,one),_mm_and_si128(corner,_mm_set1_epi32(2)))));
    const auto h = _mm_and_si128(horizontal,hm), v = _mm_and_si128(vertical,vm);
    const auto d = _mm_and_si128(diagonal,dm);
    const auto extra_h = _mm_and_si128(horizontal,corner), extra_v = _mm_and_si128(vertical,corner);
    const auto zero = _mm_setzero_si128();
    const auto c0 = _mm_unpacklo_epi8(center,zero), c1 = _mm_unpackhi_epi8(center,zero);
    const auto low = _mm_add_epi16(_mm_add_epi16(_mm_slli_epi16(c0,3),_mm_slli_epi16(c0,2)),
        _mm_add_epi16(_mm_add_epi16(_mm_unpacklo_epi8(h,zero),_mm_unpacklo_epi8(v,zero)),
            _mm_add_epi16(_mm_unpacklo_epi8(d,zero),
                _mm_add_epi16(_mm_unpacklo_epi8(extra_h,zero),_mm_unpacklo_epi8(extra_v,zero)))));
    const auto high = _mm_add_epi16(_mm_add_epi16(_mm_slli_epi16(c1,3),_mm_slli_epi16(c1,2)),
        _mm_add_epi16(_mm_add_epi16(_mm_unpackhi_epi8(h,zero),_mm_unpackhi_epi8(v,zero)),
            _mm_add_epi16(_mm_unpackhi_epi8(d,zero),
                _mm_add_epi16(_mm_unpackhi_epi8(extra_h,zero),_mm_unpackhi_epi8(extra_v,zero)))));
    return normalize(low,high,totals);
}
} // namespace gbarecomp::runtime::presentation_simd
#endif

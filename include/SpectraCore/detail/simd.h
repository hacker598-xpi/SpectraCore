#pragma once

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #define SPECTRACORE_X86 1
  #include <immintrin.h>
#elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64) || defined(_M_ARM)
  #define SPECTRACORE_ARM 1
  #include <arm_neon.h>
#endif

#ifdef SPECTRACORE_ENABLE_PREFETCH
  #if defined(_MSC_VER)
    #define SPECTRACORE_PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
  #else
    #define SPECTRACORE_PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
  #endif
#else
  #define SPECTRACORE_PREFETCH(addr)
#endif

namespace SpectraCore::detail {

#if SPECTRACORE_X86
inline void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) noexcept {
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    a = static_cast<uint32_t>(regs[0]);
    b = static_cast<uint32_t>(regs[1]);
    c = static_cast<uint32_t>(regs[2]);
    d = static_cast<uint32_t>(regs[3]);
#elif defined(SPECTRACORE_CPUID_ASM)
    asm volatile("cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf));
#else
    (void)leaf; (void)subleaf; a=b=c=d=0;
#endif
}

inline uint64_t xgetbv(uint32_t xcr) noexcept {
#if defined(_MSC_VER)
    return _xgetbv(static_cast<unsigned int>(xcr));
#elif defined(SPECTRACORE_CPUID_ASM)
    uint32_t eax, edx;
    asm volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr) : "memory");
    return (static_cast<uint64_t>(edx) << 32) | eax;
#else
    (void)xcr; return 0;
#endif
}

inline bool cpu_has_sse41() noexcept {
    uint32_t a,b,c,d;
    cpuid(1, 0, a,b,c,d);
    return (c & (1U<<19)) != 0;
}

inline bool cpu_has_os_xsave() noexcept {
    uint32_t a,b,c,d;
    cpuid(1, 0, a,b,c,d);
    return (c & (1U<<27)) != 0;
}

inline bool cpu_has_avx2() noexcept {
    if (!cpu_has_os_xsave()) return false;
    uint64_t xcr0 = xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) return false;
    uint32_t a,b,c,d;
    cpuid(1, 0, a,b,c,d);
    if (!(c & (1U<<28))) return false;
    cpuid(7, 0, a,b,c,d);
    return (b & (1U<<5)) != 0;
}

inline bool cpu_has_avx512() noexcept {
    if (!cpu_has_os_xsave()) return false;
    uint64_t xcr0 = xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) return false;
    if (!(xcr0 & (1U<<5)) || !(xcr0 & (1U<<6)) || !(xcr0 & (1U<<7)))
        return false;
    uint32_t a,b,c,d;
    cpuid(1, 0, a,b,c,d);
    if (!(c & (1U<<28))) return false;
    cpuid(7, 0, a,b,c,d);
    return (b & (1U<<16)) != 0;
}
#endif

#if defined(__SSE4_1__) || defined(__AVX2__) || defined(__AVX512F__)
inline __m128 cmul_sse(__m128 a, __m128 b) noexcept {
    __m128 a_re = _mm_shuffle_ps(a, a, 0xA0);
    __m128 a_im = _mm_shuffle_ps(a, a, 0xF5);
    __m128 b_re = _mm_shuffle_ps(b, b, 0xA0);
    __m128 b_im = _mm_shuffle_ps(b, b, 0xF5);
    __m128 re = _mm_sub_ps(_mm_mul_ps(a_re, b_re), _mm_mul_ps(a_im, b_im));
    __m128 im = _mm_add_ps(_mm_mul_ps(a_re, b_im), _mm_mul_ps(a_im, b_re));
    __m128 lo = _mm_unpacklo_ps(re, im);
    __m128 hi = _mm_unpackhi_ps(re, im);
    return _mm_movelh_ps(lo, hi);
}
#endif

#if defined(__AVX2__) || defined(__AVX512F__)
inline __m256 cmul_avx(__m256 a, __m256 b) noexcept {
    __m256 a_re = _mm256_shuffle_ps(a, a, 0xA0);
    __m256 a_im = _mm256_shuffle_ps(a, a, 0xF5);
    __m256 b_re = _mm256_shuffle_ps(b, b, 0xA0);
    __m256 b_im = _mm256_shuffle_ps(b, b, 0xF5);
    __m256 re = _mm256_sub_ps(_mm256_mul_ps(a_re, b_re), _mm256_mul_ps(a_im, b_im));
    __m256 im = _mm256_add_ps(_mm256_mul_ps(a_re, b_im), _mm256_mul_ps(a_im, b_re));
    __m256 lo = _mm256_unpacklo_ps(re, im);
    __m256 hi = _mm256_unpackhi_ps(re, im);
    __m128 lo_lo = _mm256_castps256_ps128(lo);
    __m128 lo_hi = _mm256_extractf128_ps(lo, 1);
    __m128 hi_lo = _mm256_castps256_ps128(hi);
    __m128 hi_hi = _mm256_extractf128_ps(hi, 1);
    __m128 part0 = _mm_movelh_ps(lo_lo, hi_lo);
    __m128 part1 = _mm_movelh_ps(lo_hi, hi_hi);
    return _mm256_set_m128(part1, part0);
}
#endif

#if defined(__AVX512F__)
inline __m512 cmul_avx512(__m512 a, __m512 b) noexcept {
    __m512 a_re = _mm512_shuffle_ps(a, a, 0xA0);
    __m512 a_im = _mm512_shuffle_ps(a, a, 0xF5);
    __m512 b_re = _mm512_shuffle_ps(b, b, 0xA0);
    __m512 b_im = _mm512_shuffle_ps(b, b, 0xF5);
    __m512 re = _mm512_sub_ps(_mm512_mul_ps(a_re, b_re), _mm512_mul_ps(a_im, b_im));
    __m512 im = _mm512_add_ps(_mm512_mul_ps(a_re, b_im), _mm512_mul_ps(a_im, b_re));
    __m512 lo = _mm512_unpacklo_ps(re, im);
    __m512 hi = _mm512_unpackhi_ps(re, im);
    __m128 parts[4];
    for (int i = 0; i < 4; ++i) {
        __m128 lo_lane = _mm512_extractf32x4_ps(lo, i);
        __m128 hi_lane = _mm512_extractf32x4_ps(hi, i);
        parts[i] = _mm_movelh_ps(lo_lane, hi_lane);
    }
    __m512 result = _mm512_castsi512_ps(_mm512_setzero_si512());
    result = _mm512_insertf32x4(result, parts[0], 0);
    result = _mm512_insertf32x4(result, parts[1], 1);
    result = _mm512_insertf32x4(result, parts[2], 2);
    result = _mm512_insertf32x4(result, parts[3], 3);
    return result;
}
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
inline float32x4_t cmul_neon(float32x4_t a, float32x4_t b) noexcept {
    float32x4x2_t a_trn = vtrnq_f32(a, a);
    float32x4_t a_re = a_trn.val[0];
    float32x4_t a_im = a_trn.val[1];
    float32x4x2_t b_trn = vtrnq_f32(b, b);
    float32x4_t b_re = b_trn.val[0];
    float32x4_t b_im = b_trn.val[1];
    float32x4_t re = vsubq_f32(vmulq_f32(a_re, b_re), vmulq_f32(a_im, b_im));
    float32x4_t im = vaddq_f32(vmulq_f32(a_re, b_im), vmulq_f32(a_im, b_re));
    float32x4x2_t res = vtrnq_f32(re, im);
    return res.val[0];
}
#endif

}

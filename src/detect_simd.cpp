#include "SpectraCore/Types.h"
#include "SpectraCore/detail/simd.h"

namespace SpectraCore {

SIMDLevel detectSIMD() noexcept {
#if SPECTRACORE_X86
    if (detail::cpu_has_avx512()) return SIMDLevel::AVX512;
    if (detail::cpu_has_avx2())   return SIMDLevel::AVX2;
    if (detail::cpu_has_sse41())  return SIMDLevel::SSE4_1;
    return SIMDLevel::Scalar;
#elif SPECTRACORE_ARM
    #if defined(__ARM_NEON) || defined(__aarch64__)
        return SIMDLevel::NEON;
    #else
        return SIMDLevel::Scalar;
    #endif
#else
    return SIMDLevel::Scalar;
#endif
}

}

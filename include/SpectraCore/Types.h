#pragma once

#include <cstdint>

namespace SpectraCore {

enum class WindowType : uint8_t { Hann, Hamming, Blackman, Kaiser };
enum class SIMDLevel : uint8_t { Scalar, SSE4_1, AVX2, AVX512, NEON };

SIMDLevel detectSIMD() noexcept;

}

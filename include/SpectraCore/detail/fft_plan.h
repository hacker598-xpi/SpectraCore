#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <utility>
#include <cmath>
#include "../Types.h"
#include "simd.h"

namespace SpectraCore::detail {

template <size_t N> constexpr bool is_power_of_two() { return (N & (N-1)) == 0; }
template <size_t N> constexpr size_t log2() { size_t r=0,v=N; while(v>>=1)++r; return r; }

constexpr size_t next_pow2(size_t n) {
    if (n <= 1) return 1;
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// --- Base escalar (referencia) ---
template <size_t N, SIMDLevel S = SIMDLevel::Scalar>
class PowerOfTwoFFTPlan;

template <size_t N>
class PowerOfTwoFFTPlan<N, SIMDLevel::Scalar> {
    static_assert(is_power_of_two<N>());
public:
    std::array<std::complex<float>, N> twiddle;
    std::array<size_t, N> bit_rev;

    PowerOfTwoFFTPlan() noexcept {
        for (size_t i = 0; i < N; ++i) {
            float theta = -kTwoPi * float(i) / float(N);
            twiddle[i] = {std::cos(theta), std::sin(theta)};
        }
        for (size_t i = 0; i < N; ++i) {
            size_t rev = 0;
            for (size_t bits = i, j = 0; j < log2<N>(); ++j, bits >>= 1)
                rev = (rev << 1) | (bits & 1);
            bit_rev[i] = rev;
        }
    }

    void execute(std::complex<float>* data) const noexcept {
        size_t n = N;
        for (size_t i = 0; i < n; ++i)
            if (i < bit_rev[i]) std::swap(data[i], data[bit_rev[i]]);

        for (size_t len = 2; len <= n; len <<= 1) {
            size_t half = len >> 1, stride = n / len;
            for (size_t i = 0; i < n; i += len)
                for (size_t j = 0; j < half; ++j) {
                    auto u = data[i+j];
                    auto v = data[i+j+half] * twiddle[j*stride];
                    data[i+j]       = u + v;
                    data[i+j+half]  = u - v;
                    SPECTRACORE_PREFETCH(&twiddle[(j+4)*stride]);
                }
        }
    }
};

// --- Especializaciones SIMD (recuperadas de V1) ---

#if defined(__SSE4_1__) || defined(__AVX2__) || defined(__AVX512F__)

template <size_t N>
struct alignas(64) PackedTwiddleSSE {
    static constexpr size_t num_stages = log2<N>();
    static constexpr auto counts = []() constexpr {
        std::array<size_t, num_stages> arr{};
        for (size_t s = 0; s < num_stages; ++s) {
            size_t half = size_t(1) << s;
            arr[s] = (half >= 2) ? (half + 1) / 2 : 0;
        }
        return arr;
    }();
    static constexpr size_t total = []() constexpr {
        size_t sum = 0;
        for (auto c : counts) sum += c;
        return sum;
    }();

    __m128 data[total];

    PackedTwiddleSSE(const std::complex<float>* twiddle) noexcept {
        size_t offset = 0;
        size_t n = N;
        for (size_t len = 2, stage = 0; len <= n; len <<= 1, ++stage) {
            size_t half = len >> 1, stride = n / len;
            if (half >= 2) {
                for (size_t j = 0; j < half; j += 2) {
                    const auto& w0 = twiddle[j*stride];
                    const auto& w1 = twiddle[(j+1)*stride];
                    data[offset++] = _mm_set_ps(w1.imag(), w1.real(), w0.imag(), w0.real());
                }
            }
        }
    }
};

template <size_t N>
class PowerOfTwoFFTPlan<N, SIMDLevel::SSE4_1> : public PowerOfTwoFFTPlan<N, SIMDLevel::Scalar> {
    using Base = PowerOfTwoFFTPlan<N, SIMDLevel::Scalar>;
    PackedTwiddleSSE<N> packed;
public:
    PowerOfTwoFFTPlan() noexcept : Base(), packed(Base::twiddle.data()) {}

    void execute(std::complex<float>* data) const noexcept {
        size_t n = N;
        for (size_t i = 0; i < n; ++i) if (i < Base::bit_rev[i]) std::swap(data[i], data[Base::bit_rev[i]]);
        size_t vecOff = 0;
        for (size_t len = 2, stage = 0; len <= n; len <<= 1, ++stage) {
            size_t half = len>>1, stride = n/len;
            if (half >= 2) {
                for (size_t i=0; i<n; i+=len) {
                    size_t localOff = vecOff;
                    for (size_t j=0; j<half; j+=2, ++localOff) {
                        __m128 u = _mm_load_ps(reinterpret_cast<float*>(&data[i+j]));
                        __m128 v = _mm_load_ps(reinterpret_cast<float*>(&data[i+j+half]));
                        __m128 wvec = packed.data[localOff];
                        v = cmul_sse(v, wvec);
                        _mm_store_ps(reinterpret_cast<float*>(&data[i+j]),        _mm_add_ps(u,v));
                        _mm_store_ps(reinterpret_cast<float*>(&data[i+j+half]), _mm_sub_ps(u,v));
                    }
                }
                vecOff += PackedTwiddleSSE<N>::counts[stage];
            } else {
                for (size_t i=0; i<n; i+=len) {
                    auto u = data[i];
                    auto v = data[i+1] * Base::twiddle[0];
                    data[i]   = u + v;
                    data[i+1] = u - v;
                }
            }
        }
    }
};
#endif

#if defined(__AVX2__) || defined(__AVX512F__)

template <size_t N>
struct alignas(64) PackedTwiddleAVX2 {
    static constexpr size_t num_stages = log2<N>();
    static constexpr auto counts = []() constexpr {
        std::array<size_t, num_stages> arr{};
        for (size_t s = 0; s < num_stages; ++s) {
            size_t half = size_t(1) << s;
            arr[s] = (half >= 4) ? (half + 3) / 4 : 0;
        }
        return arr;
    }();
    static constexpr size_t total = []() constexpr {
        size_t sum = 0;
        for (auto c : counts) sum += c;
        return sum;
    }();

    __m256 data[total];

    PackedTwiddleAVX2(const std::complex<float>* twiddle) noexcept {
        size_t offset = 0;
        size_t n = N;
        for (size_t len = 2, stage = 0; len <= n; len <<= 1, ++stage) {
            size_t half = len >> 1, stride = n / len;
            if (half >= 4) {
                for (size_t j = 0; j < half; j += 4) {
                    const auto& w0 = twiddle[j*stride];
                    const auto& w1 = twiddle[(j+1)*stride];
                    const auto& w2 = twiddle[(j+2)*stride];
                    const auto& w3 = twiddle[(j+3)*stride];
                    data[offset++] = _mm256_set_ps(w3.imag(), w3.real(), w2.imag(), w2.real(),
                                                   w1.imag(), w1.real(), w0.imag(), w0.real());
                }
            }
        }
    }
};

template <size_t N>
class PowerOfTwoFFTPlan<N, SIMDLevel::AVX2> : public PowerOfTwoFFTPlan<N, SIMDLevel::Scalar> {
    using Base = PowerOfTwoFFTPlan<N, SIMDLevel::Scalar>;
    PackedTwiddleAVX2<N> packed;
public:
    PowerOfTwoFFTPlan() noexcept : Base(), packed(Base::twiddle.data()) {}

    void execute(std::complex<float>* data) const noexcept {
        size_t n = N;
        for (size_t i=0; i<n; ++i) if (i<Base::bit_rev[i]) std::swap(data[i], data[Base::bit_rev[i]]);
        size_t vecOff = 0;
        for (size_t len=2, stage=0; len<=n; len<<=1, ++stage) {
            size_t half=len>>1, stride=n/len;
            if (half >= 4) {
                for (size_t i=0; i<n; i+=len) {
                    size_t localOff = vecOff;
                    for (size_t j=0; j<half; j+=4, ++localOff) {
                        __m256 u = _mm256_load_ps(reinterpret_cast<float*>(&data[i+j]));
                        __m256 v = _mm256_load_ps(reinterpret_cast<float*>(&data[i+j+half]));
                        __m256 wvec = packed.data[localOff];
                        v = cmul_avx(v, wvec);
                        _mm256_store_ps(reinterpret_cast<float*>(&data[i+j]),        _mm256_add_ps(u,v));
                        _mm256_store_ps(reinterpret_cast<float*>(&data[i+j+half]), _mm256_sub_ps(u,v));
                    }
                }
                vecOff += PackedTwiddleAVX2<N>::counts[stage];
            } else {
                for (size_t i=0; i<n; i+=len)
                    for (size_t j=0; j<half; ++j) {
                        auto u = data[i+j];
                        auto v = data[i+j+half] * Base::twiddle[j*stride];
                        data[i+j]       = u + v;
                        data[i+j+half] = u - v;
                    }
            }
        }
    }
};
#endif

#if defined(__AVX512F__)

template <size_t N>
struct alignas(64) PackedTwiddleAVX512 {
    static constexpr size_t num_stages = log2<N>();
    static constexpr auto counts = []() constexpr {
        std::array<size_t, num_stages> arr{};
        for (size_t s = 0; s < num_stages; ++s) {
            size_t half = size_t(1) << s;
            arr[s] = (half >= 8) ? (half + 7) / 8 : 0;
        }
        return arr;
    }();
    static constexpr size_t total = []() constexpr {
        size_t sum = 0;
        for (auto c : counts) sum += c;
        return sum;
    }();

    __m512 data[total];

    PackedTwiddleAVX512(const std::complex<float>* twiddle) noexcept {
        size_t offset = 0;
        size_t n = N;
        for (size_t len = 2, stage = 0; len <= n; len <<= 1, ++stage) {
            size_t half = len >> 1, stride = n / len;
            if (half >= 8) {
                for (size_t j = 0; j < half; j += 8) {
                    __m512 vec = _mm512_set_ps(
                        twiddle[(j+7)*stride].imag(), twiddle[(j+7)*stride].real(),
                        twiddle[(j+6)*stride].imag(), twiddle[(j+6)*stride].real(),
                        twiddle[(j+5)*stride].imag(), twiddle[(j+5)*stride].real(),
                        twiddle[(j+4)*stride].imag(), twiddle[(j+4)*stride].real(),
                        twiddle[(j+3)*stride].imag(), twiddle[(j+3)*stride].real(),
                        twiddle[(j+2)*stride].imag(), twiddle[(j+2)*stride].real(),
                        twiddle[(j+1)*stride].imag(), twiddle[(j+1)*stride].real(),
                        twiddle[j*stride].imag(),     twiddle[j*stride].real());
                    data[offset++] = vec;
                }
            }
        }
    }
};

template <size_t N>
class PowerOfTwoFFTPlan<N, SIMDLevel::AVX512> : public PowerOfTwoFFTPlan<N, SIMDLevel::Scalar> {
    using Base = PowerOfTwoFFTPlan<N, SIMDLevel::Scalar>;
    PackedTwiddleAVX512<N> packed;
public:
    PowerOfTwoFFTPlan() noexcept : Base(), packed(Base::twiddle.data()) {}

    void execute(std::complex<float>* data) const noexcept {
        size_t n = N;
        for (size_t i=0; i<n; ++i) if (i<Base::bit_rev[i]) std::swap(data[i], data[Base::bit_rev[i]]);
        size_t vecOff = 0;
        for (size_t len=2, stage=0; len<=n; len<<=1, ++stage) {
            size_t half=len>>1, stride=n/len;
            if (half >= 8) {
                for (size_t i=0; i<n; i+=len) {
                    size_t localOff = vecOff;
                    for (size_t j=0; j<half; j+=8, ++localOff) {
                        __m512 u = _mm512_load_ps(reinterpret_cast<float*>(&data[i+j]));
                        __m512 v = _mm512_load_ps(reinterpret_cast<float*>(&data[i+j+half]));
                        __m512 wvec = packed.data[localOff];
                        v = cmul_avx512(v, wvec);
                        _mm512_store_ps(reinterpret_cast<float*>(&data[i+j]),        _mm512_add_ps(u,v));
                        _mm512_store_ps(reinterpret_cast<float*>(&data[i+j+half]), _mm512_sub_ps(u,v));
                    }
                }
                vecOff += PackedTwiddleAVX512<N>::counts[stage];
            } else {
                for (size_t i=0; i<n; i+=len)
                    for (size_t j=0; j<half; ++j) {
                        auto u = data[i+j];
                        auto v = data[i+j+half] * Base::twiddle[j*stride];
                        data[i+j]       = u + v;
                        data[i+j+half] = u - v;
                    }
            }
        }
    }
};
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)

template <size_t N>
struct alignas(64) PackedTwiddleNEON {
    static constexpr size_t num_stages = log2<N>();
    static constexpr auto counts = []() constexpr {
        std::array<size_t, num_stages> arr{};
        for (size_t s = 0; s < num_stages; ++s) {
            size_t half = size_t(1) << s;
            arr[s] = (half >= 2) ? (half + 1) / 2 : 0;
        }
        return arr;
    }();
    static constexpr size_t total = []() constexpr {
        size_t sum = 0;
        for (auto c : counts) sum += c;
        return sum;
    }();

    float32x4_t data[total];

    PackedTwiddleNEON(const std::complex<float>* twiddle) noexcept {
        size_t offset = 0;
        size_t n = N;
        for (size_t len = 2, stage = 0; len <= n; len <<= 1, ++stage) {
            size_t half = len >> 1, stride = n / len;
            if (half >= 2) {
                for (size_t j = 0; j < half; j += 2) {
                    const auto& w0 = twiddle[j*stride];
                    const auto& w1 = twiddle[(j+1)*stride];
                    float32x4_t vec = {w0.real(), w0.imag(), w1.real(), w1.imag()};
                    data[offset++] = vec;
                }
            }
        }
    }
};

template <size_t N>
class PowerOfTwoFFTPlan<N, SIMDLevel::NEON> : public PowerOfTwoFFTPlan<N, SIMDLevel::Scalar> {
    using Base = PowerOfTwoFFTPlan<N, SIMDLevel::Scalar>;
    PackedTwiddleNEON<N> packed;
public:
    PowerOfTwoFFTPlan() noexcept : Base(), packed(Base::twiddle.data()) {}

    void execute(std::complex<float>* data) const noexcept {
        size_t n = N;
        for (size_t i=0; i<n; ++i) if (i<Base::bit_rev[i]) std::swap(data[i], data[Base::bit_rev[i]]);
        size_t vecOff = 0;
        for (size_t len=2, stage=0; len<=n; len<<=1, ++stage) {
            size_t half=len>>1, stride=n/len;
            if (half >= 2) {
                for (size_t i=0; i<n; i+=len) {
                    size_t localOff = vecOff;
                    for (size_t j=0; j<half; j+=2, ++localOff) {
                        float32x4_t u = vld1q_f32(reinterpret_cast<float*>(&data[i+j]));
                        float32x4_t v = vld1q_f32(reinterpret_cast<float*>(&data[i+j+half]));
                        float32x4_t wvec = packed.data[localOff];
                        v = cmul_neon(v, wvec);
                        vst1q_f32(reinterpret_cast<float*>(&data[i+j]),        vaddq_f32(u,v));
                        vst1q_f32(reinterpret_cast<float*>(&data[i+j+half]), vsubq_f32(u,v));
                    }
                }
                vecOff += PackedTwiddleNEON<N>::counts[stage];
            } else {
                for (size_t i=0; i<n; i+=len) {
                    auto u = data[i];
                    auto v = data[i+1] * Base::twiddle[0];
                    data[i]   = u + v;
                    data[i+1] = u - v;
                }
            }
        }
    }
};
#endif

// --- Bluestein y FFTPlan unificado ---

template <size_t N, SIMDLevel S>
class BluesteinEngine {
    static constexpr size_t M = next_pow2(2 * N - 1);
    std::array<std::complex<float>, N> chirp;
    std::array<std::complex<float>, M> b;
    PowerOfTwoFFTPlan<M, S> convPlan;

public:
    BluesteinEngine() noexcept {
        for (size_t k = 0; k < N; ++k) {
            float phase = kPi * float(k) * float(k) / float(N);
            chirp[k] = {std::cos(phase), std::sin(phase)};
        }
        for (size_t k = 0; k < M; ++k) {
            if (k < N) b[k] = std::conj(chirp[k]);
            else if (k <= 2*N-2) b[k] = std::conj(chirp[2*N-2 - k]);
            else b[k] = {0,0};
        }
        convPlan.execute(b.data());
    }

    void execute(std::complex<float>* data) const noexcept {
        alignas(64) std::array<std::complex<float>, M> a;
        a.fill({0,0});
        for (size_t k = 0; k < N; ++k) a[k] = data[k] * chirp[k];
        convPlan.execute(a.data());
        for (size_t i = 0; i < M; ++i) a[i] *= b[i];
        for (size_t i = 0; i < M; ++i) a[i] = std::conj(a[i]);
        convPlan.execute(a.data());
        for (size_t i = 0; i < M; ++i) a[i] = std::conj(a[i]) / float(M);
        for (size_t k = 0; k < N; ++k) data[k] = a[k] * chirp[k];
    }
};

template <size_t N, SIMDLevel S>
class FFTPlan {
    static constexpr bool Pow2 = is_power_of_two<N>();
    using Pow2Plan = PowerOfTwoFFTPlan<N, S>;
    using BluePlan = BluesteinEngine<N, S>;
    std::conditional_t<Pow2, Pow2Plan, BluePlan> impl;

public:
    FFTPlan() noexcept : impl{} {}

    void execute(std::complex<float>* data) const noexcept {
        if constexpr (Pow2) impl.execute(data);
        else impl.execute(data);
    }
};

struct PlanExecutor {
    void (*execute)(void*, std::complex<float>*);
    void* plan;
};

template <size_t N, SIMDLevel S>
PlanExecutor make_executor(FFTPlan<N, S>& plan) {
    return { [](void* p, std::complex<float>* d) {
        static_cast<FFTPlan<N, S>*>(p)->execute(d);
    }, &plan };
}

template <size_t N>
void real_fft_postprocess(const std::complex<float>* Z, std::complex<float>* out,
                          const std::complex<float>* twiddle_N) noexcept {
    constexpr size_t M = N / 2;
    out[0] = {Z[0].real() + Z[0].imag(), 0.0f};
    for (size_t k = 1; k < M; ++k) {
        auto zk = Z[k], zMk = std::conj(Z[M - k]);
        auto tmp = (zk - zMk) * std::complex<float>(0.0f, -1.0f);
        out[k] = 0.5f * (zk + zMk + tmp * twiddle_N[k]);
    }
    out[M] = {Z[0].real() - Z[0].imag(), 0.0f};
}

template <size_t N>
void real_fft(const float* input, std::complex<float>* output,
              const PlanExecutor& planExec,
              const std::complex<float>* twiddle_N) noexcept {
    static_assert(N % 2 == 0);
    constexpr size_t M = N / 2;
    alignas(64) std::complex<float> Z[M];
    for (size_t i = 0; i < M; ++i) Z[i] = {input[2*i], input[2*i+1]};
    planExec.execute(planExec.plan, Z);
    real_fft_postprocess<N>(Z, output, twiddle_N);
}

}

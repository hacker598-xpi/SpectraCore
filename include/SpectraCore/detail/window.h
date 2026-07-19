#pragma once
#include <array>
#include <cmath>
#include "../Types.h"

namespace SpectraCore::detail {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kDefaultKaiserBeta = 6.0f;

inline float besselI0(float x) noexcept {
    float sum = 1.0f, term = 1.0f, x2 = (x*x)*0.25f;
    for (int i = 1; i < 20; ++i) { term *= x2/float(i*i); sum += term; }
    return sum;
}

template <size_t N>
void fillWindow(std::array<float, N>& w, WindowType type, float kaiserBeta = kDefaultKaiserBeta) noexcept {
    for (size_t n = 0; n < N; ++n) {
        float val = 0.0f;
        switch (type) {
            case WindowType::Hann:
                val = 0.5f * (1.0f - std::cos(kTwoPi * float(n) / float(N-1))); break;
            case WindowType::Hamming:
                val = 0.54f - 0.46f * std::cos(kTwoPi * float(n) / float(N-1)); break;
            case WindowType::Blackman:
                val = 0.42f - 0.5f * std::cos(kTwoPi * float(n) / float(N-1))
                       + 0.08f * std::cos(2.f * kTwoPi * float(n) / float(N-1)); break;
            case WindowType::Kaiser: {
                float alpha = float(N-1)*0.5f;
                float beta = kPi * kaiserBeta;
                float num = besselI0(beta * std::sqrt(1.0f - std::pow(float((n-alpha)/alpha), 2.0f)));
                float den = besselI0(beta);
                val = num / den;
            } break;
        }
        w[n] = val;
    }
}

}

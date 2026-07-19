#include <SpectraCore/Analyzer.h>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

template <size_t N>
void reference_dft(const std::vector<float>& input, std::complex<float>* output) {
    for (size_t k = 0; k <= N / 2; ++k) {
        std::complex<float> sum(0, 0);
        for (size_t n = 0; n < N; ++n) {
            float angle = -2.0f * 3.14159265358979323846f * k * n / N;
            sum += std::complex<float>(input[n], 0) *
                   std::complex<float>(std::cos(angle), std::sin(angle));
        }
        output[k] = sum;
    }
}

template <size_t N>
bool test_accuracy(const char* label, float sampleRate, float testFreq) {
    std::vector<float> input(N);
    for (size_t i = 0; i < N; ++i)
        input[i] = std::sin(2.0f * 3.14159265358979323846f * testFreq * i / sampleRate);

    SpectraCore::Analyzer<N> analyzer;
    analyzer.setHopSize(N);
    analyzer.setSmoothing(1.0f);
    analyzer.pushSamples(input);
    analyzer.processReadyFrame();
    auto spectrum = analyzer.getSpectrum();

    std::vector<std::complex<float>> ref(N / 2 + 1);
    reference_dft<N>(input, ref.data());

    float maxRelError = 0.0f;
    for (size_t i = 0; i < ref.size(); ++i) {
        float refMag = std::abs(ref[i]);
        float ourMag = spectrum.magnitudeLin[i];
        if (refMag < 1e-9f && ourMag < 1e-9f) continue;
        float relErr = std::abs(ourMag - refMag) / std::max(refMag, 1e-9f);
        if (relErr > maxRelError) maxRelError = relErr;
    }

    if (maxRelError > 1e-5f) {
        std::cerr << "FAIL [" << label << "] max rel error " << maxRelError << " > 1e-5\n";
        return false;
    }
    std::cout << "PASS [" << label << "] max rel error " << maxRelError << "\n";
    return true;
}

int main() {
    bool ok = true;

    ok &= test_accuracy<64>   ("N=64 (pow2)",        8000.f, 1000.f);
    ok &= test_accuracy<96>   ("N=96 (no pow2)",     8000.f, 1000.f);
    ok &= test_accuracy<256>  ("N=256 (pow2)",      44100.f, 440.f);
    ok &= test_accuracy<100>  ("N=100 (no pow2)",   44100.f, 440.f);
    ok &= test_accuracy<2048> ("N=2048 (pow2)",     44100.f, 5000.f);

    return ok ? 0 : 1;
}

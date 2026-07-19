#include <SpectraCore/Analyzer.h>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    constexpr size_t N = 2048;
    constexpr float sampleRate = 44100.0f;
    constexpr float testFreq = 1000.0f;
    constexpr size_t expectedBin = static_cast<size_t>(testFreq * N / sampleRate + 0.5f);

    std::vector<float> input(N);
    for (size_t i = 0; i < N; ++i)
        input[i] = std::sin(2.0f * 3.14159265358979323846f * testFreq * i / sampleRate);

    SpectraCore::Analyzer<N> analyzer;
    analyzer.setHopSize(N);
    analyzer.setSmoothing(1.0f);
    analyzer.pushSamples(input);
    analyzer.processReadyFrame();
    auto spectrum = analyzer.getSpectrum();

    float maxMag = 0.0f;
    size_t maxBin = 0;
    for (size_t i = 0; i < spectrum.magnitudeLin.size(); ++i) {
        if (spectrum.magnitudeLin[i] > maxMag) {
            maxMag = spectrum.magnitudeLin[i];
            maxBin = i;
        }
    }

    if (maxBin != expectedBin) {
        std::cerr << "Failed: bin " << maxBin << " != " << expectedBin << "\n";
        return 1;
    }
    std::cout << "Test passed: peak at " << maxBin << "\n";
    return 0;
}

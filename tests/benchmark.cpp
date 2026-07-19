#include <SpectraCore/Analyzer.h>
#include <chrono>
#include <iostream>
#include <vector>
#include <random>

#ifdef HAS_FFTW
#include <fftw3.h>
#endif

int main() {
    constexpr size_t N = 2048;
    constexpr size_t iterations = 1000;

    std::vector<float> input(N);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < N; ++i) input[i] = dist(rng);

    // SpectraCore
    SpectraCore::Analyzer<N> analyzer;
    analyzer.setHopSize(N);
    analyzer.setSmoothing(1.0f);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t iter = 0; iter < iterations; ++iter) {
        analyzer.pushSamples(input);
        analyzer.processReadyFrame();
    }
    auto end = std::chrono::high_resolution_clock::now();
    double sc_us = std::chrono::duration<double, std::micro>(end - start).count() / iterations;

    std::cout << "SpectraCore FFT (N=" << N << "): " << sc_us << " µs/iter\n";

#ifdef HAS_FFTW
    // FFTW3
    fftwf_plan plan = fftwf_plan_dft_r2c_1d(N, input.data(), nullptr, FFTW_ESTIMATE);
    std::vector<std::complex<float>> out(N/2+1);

    start = std::chrono::high_resolution_clock::now();
    for (size_t iter = 0; iter < iterations; ++iter) {
        fftwf_execute_dft_r2c(plan, input.data(), reinterpret_cast<fftwf_complex*>(out.data()));
    }
    end = std::chrono::high_resolution_clock::now();
    double fftw_us = std::chrono::duration<double, std::micro>(end - start).count() / iterations;
    std::cout << "FFTW3 FFT (N=" << N << "): " << fftw_us << " µs/iter\n";
    fftwf_destroy_plan(plan);
#endif

    return 0;
}

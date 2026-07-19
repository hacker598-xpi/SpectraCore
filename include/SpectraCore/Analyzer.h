#pragma once

#include <cstddef>
#include <span>
#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <utility>
#include <variant>

#include "Types.h"
#include "detail/simd.h"
#include "detail/window.h"
#include "detail/fft_plan.h"
#include "detail/buffer.h"

namespace SpectraCore {

/**
 * @brief Analizador de espectro en tiempo real con FFT acelerada por SIMD.
 *
 * @tparam N       Tamaño de la FFT (número de muestras reales). Debe ser par.
 * @tparam MaxLogBands Número máximo de bandas logarítmicas (opcional, por defecto 128).
 *
 * Esta clase procesa flujos de audio, aplica ventana, solapamiento, FFT real,
 * suavizado exponencial y agrupación logarítmica en bandas. Está diseñada
 * para ser usada desde tres hilos: productor (pushSamples), trabajador
 * (processReadyFrame) y consumidor (getSpectrum).
 */
template <size_t N, size_t MaxLogBands = 128>
class Analyzer {
    static_assert(N >= 2 && N % 2 == 0, "El tamaño de la FFT debe ser par y como mínimo 2.");
    static constexpr size_t M = N / 2;
    static constexpr size_t SpectrumBins = M + 1;

    template <SIMDLevel L> struct TaggedPlan : detail::FFTPlan<M, L> {
        static constexpr SIMDLevel simdLevel = L;
    };

    using PlanVariant = std::variant<
        TaggedPlan<SIMDLevel::Scalar>
        #if defined(__SSE4_1__) || defined(__AVX2__) || defined(__AVX512F__)
        , TaggedPlan<SIMDLevel::SSE4_1>
        #endif
        #if defined(__AVX2__) || defined(__AVX512F__)
        , TaggedPlan<SIMDLevel::AVX2>
        #endif
        #if defined(__AVX512F__)
        , TaggedPlan<SIMDLevel::AVX512>
        #endif
        #if defined(__ARM_NEON) || defined(__aarch64__)
        , TaggedPlan<SIMDLevel::NEON>
        #endif
    >;

    struct OutputSet {
        std::array<float, SpectrumBins> magnitudeLin{};
        std::array<float, SpectrumBins> magnitudeDB{};
        std::array<float, SpectrumBins> phase{};
        std::array<float, MaxLogBands> logMagnitudes{};
    };

public:
    /**
     * @brief Construye el analizador con configuración por defecto.
     *
     * Inicializa la ventana Hann, 50% de solapamiento, sin suavizado
     * y sin bandas logarítmicas. La tasa de muestreo por defecto es 44100 Hz.
     */
    Analyzer()
        : hopSize_{M}
        , smoothingAlpha_{0.2f}
        , sampleRate_{44100.0f}
        , numLogBands_{0}
        , minLogFreq_{20.0f}
        , maxLogFreq_{20000.0f}
    {
        bestSIMD_ = detectSIMD();
        initPlan();
        setWindowType(WindowType::Hann);
        for (size_t i = 0; i < N; ++i) {
            float theta = -detail::kTwoPi * float(i) / float(N);
            twiddleN_[i] = {std::cos(theta), std::sin(theta)};
        }
        output_[0] = OutputSet{};
        output_[1] = OutputSet{};
        frontOutput_ = 0;
    }

    /**
     * @brief Selecciona la función ventana aplicada antes de la FFT.
     * @param type Tipo de ventana (Hann, Hamming, Blackman, Kaiser).
     * @param kaiserBeta Parámetro beta para la ventana Kaiser (solo tiene efecto si type == Kaiser).
     */
    void setWindowType(WindowType type, float kaiserBeta = detail::kDefaultKaiserBeta) noexcept {
        detail::fillWindow(window_, type, kaiserBeta);
    }

    /**
     * @brief Define el solapamiento como un factor del tamaño de la FFT.
     * @param factor N / factor = muestras de avance entre cuadros. Ej: 2 → 50% de solapamiento.
     */
    void setOverlapFactor(size_t factor) noexcept {
        factor = std::max(size_t(1), factor);
        hopSize_ = std::max(size_t(1), N / factor);
    }

    /**
     * @brief Define directamente el número de muestras a avanzar entre cuadros.
     * @param hop Número de muestras (1..N). Valores pequeños producen mayor solapamiento.
     */
    void setHopSize(size_t hop) noexcept {
        hopSize_ = std::clamp(hop, size_t(1), N);
    }

    /**
     * @brief Activa el suavizado exponencial de las magnitudes.
     * @param alpha Peso de la nueva muestra (0 = sin actualizar, 1 = sin suavizado).
     *              La fórmula aplicada es: smoothed = alpha * new + (1 - alpha) * old.
     */
    void setSmoothing(float alpha) noexcept {
        smoothingAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
        smoothedMagnitudes_.fill(0.0f);
    }

    /**
     * @brief Configura la agrupación logarítmica de frecuencias.
     * @param numBands Número de bandas (0 = desactivado). Máximo MaxLogBands.
     * @param minFreq  Frecuencia mínima en Hz.
     * @param maxFreq  Frecuencia máxima en Hz.
     * @param sampleRate Frecuencia de muestreo de la señal.
     */
    void setLogBands(size_t numBands, float minFreq, float maxFreq, float sampleRate) noexcept {
        numLogBands_ = std::min(numBands, MaxLogBands);
        minLogFreq_ = minFreq;
        maxLogFreq_ = maxFreq;
        sampleRate_ = sampleRate;
        if (numLogBands_ > 0) {
            const float nyquist = sampleRate_ * 0.5f;
            const float logMin = std::log10(minLogFreq_);
            const float logMax = std::log10(maxLogFreq_);
            for (size_t i = 0; i < SpectrumBins; ++i) {
                float freq = float(i) * nyquist / float(M);
                if (freq < minLogFreq_) binToBand_[i] = 0;
                else if (freq >= maxLogFreq_) binToBand_[i] = static_cast<uint16_t>(numLogBands_ - 1);
                else {
                    float idx = (std::log10(freq) - logMin) / (logMax - logMin) * float(numLogBands_);
                    binToBand_[i] = static_cast<uint16_t>(std::clamp(idx, 0.0f, float(numLogBands_ - 1)));
                }
            }
        }
    }

    /**
     * @brief Alimenta el analizador con muestras de audio (hilo productor).
     * @param samples Span con las muestras consecutivas.
     */
    void pushSamples(std::span<const float> samples) noexcept {
        for (float s : samples) {
            ringBuffer_[ringPos_] = s;
            ringPos_ = (ringPos_ + 1) % N;
            ++samplesSinceLastFrame_;
            if (samplesSinceLastFrame_ >= hopSize_) {
                samplesSinceLastFrame_ = 0;
                float* dest = frameBuffer_.getFillBuffer();
                size_t start = ringPos_;
                for (size_t i = 0; i < N; ++i) dest[i] = ringBuffer_[(start + i) % N];
                for (size_t i = 0; i < N; ++i) dest[i] *= window_[i];
                frameBuffer_.submitFill();
            }
        }
    }

    /**
     * @brief Indica si hay un nuevo cuadro listo para procesar (no consume el cuadro).
     */
    bool hasNewFrame() const noexcept { return frameBuffer_.hasReadyFrame(); }

    /**
     * @brief Espera pasivamente hasta que haya un nuevo cuadro disponible.
     */
    void waitForFrame() const noexcept { frameBuffer_.waitUntilReady(); }

    /**
     * @brief Notifica a todos los hilos que esperan en waitForFrame().
     */
    void notifyAll() noexcept { frameBuffer_.notifyAll(); }

    /**
     * @brief Procesa el cuadro actual: aplica ventana, ejecuta la FFT real y actualiza las salidas.
     *
     * Este método toma el cuadro del doble búfer, realiza la FFT real y calcula
     * magnitudes lineales, dB, fase y, si están activadas, las bandas logarítmicas.
     * Los resultados se almacenan en un búfer trasero y se intercambian atómicamente,
     * de modo que getSpectrum() siempre devuelve datos consistentes.
     */
    void processReadyFrame() noexcept {
        auto ready = frameBuffer_.getReadyFrame();
        if (ready.index < 0) return;

        alignas(64) std::array<std::complex<float>, SpectrumBins> spectrum;
        detail::real_fft<N>(ready.data, spectrum.data(), planExec_, twiddleN_.data());

        constexpr float epsilon = 1e-9f;
        int back = 1 - frontOutput_;
        for (size_t i = 0; i < SpectrumBins; ++i) {
            float re = spectrum[i].real(), im = spectrum[i].imag();
            float mag = std::sqrt(re*re + im*im);
            if (smoothingAlpha_ > 0.0f && smoothingAlpha_ < 1.0f) {
                mag = smoothingAlpha_ * mag + (1.0f - smoothingAlpha_) * smoothedMagnitudes_[i];
                smoothedMagnitudes_[i] = mag;
            }
            output_[back].magnitudeLin[i] = mag;
            output_[back].magnitudeDB[i] = std::max(20.0f * std::log10(std::max(mag, epsilon)), -140.0f);
            output_[back].phase[i] = (mag <= epsilon) ? 0.0f : std::atan2(im, re);
        }

        if (numLogBands_ > 0) {
            std::array<float, MaxLogBands> bandSum{};
            for (size_t i = 0; i < SpectrumBins; ++i)
                bandSum[binToBand_[i]] += output_[back].magnitudeLin[i] * output_[back].magnitudeLin[i];
            for (size_t b = 0; b < numLogBands_; ++b)
                output_[back].logMagnitudes[b] = std::max(10.0f * std::log10(std::max(bandSum[b], epsilon)), -140.0f);
        }

        frontOutput_ = back;
        frameBuffer_.markProcessed(ready.index);
    }

    /**
     * @brief Espectro de salida (vistas de solo lectura).
     */
    struct Spectrum {
        std::span<const float> magnitudeLin;  ///< Magnitudes lineales (tamaño N/2+1)
        std::span<const float> magnitudeDB;   ///< Magnitudes en dB (tamaño N/2+1)
        std::span<const float> phase;         ///< Fase en radianes (tamaño N/2+1)
        std::span<const float> logMagnitudes; ///< Magnitudes por bandas logarítmicas (tamaño numLogBands, vacío si no se configuraron)
    };

    /**
     * @brief Obtiene el espectro actual de forma segura para hilos.
     * @return Estructura Spectrum con vistas a los arrays internos del búfer delantero.
     */
    Spectrum getSpectrum() const noexcept {
        int front = frontOutput_;
        return {
            output_[front].magnitudeLin,
            output_[front].magnitudeDB,
            output_[front].phase,
            std::span<const float>(output_[front].logMagnitudes.data(), numLogBands_)
        };
    }

private:
    void initPlan() noexcept {
        auto init = [&]<SIMDLevel L>(detail::FFTPlan<M, L>& plan) {
            planExec_ = detail::make_executor<M, L>(plan);
        };
        switch (bestSIMD_) {
            default: { auto& p = halfPlan_.template emplace<TaggedPlan<SIMDLevel::Scalar>>(); init(p); break; }
            #if defined(__SSE4_1__) || defined(__AVX2__) || defined(__AVX512F__)
            case SIMDLevel::SSE4_1: { auto& p = halfPlan_.template emplace<TaggedPlan<SIMDLevel::SSE4_1>>(); init(p); break; }
            #endif
            #if defined(__AVX2__) || defined(__AVX512F__)
            case SIMDLevel::AVX2:   { auto& p = halfPlan_.template emplace<TaggedPlan<SIMDLevel::AVX2>>(); init(p); break; }
            #endif
            #if defined(__AVX512F__)
            case SIMDLevel::AVX512: { auto& p = halfPlan_.template emplace<TaggedPlan<SIMDLevel::AVX512>>(); init(p); break; }
            #endif
            #if defined(__ARM_NEON) || defined(__aarch64__)
            case SIMDLevel::NEON:   { auto& p = halfPlan_.template emplace<TaggedPlan<SIMDLevel::NEON>>(); init(p); break; }
            #endif
        }
    }

    SIMDLevel bestSIMD_;
    PlanVariant halfPlan_;
    detail::PlanExecutor planExec_;

    std::array<float, N>               window_;
    std::array<std::complex<float>, N> twiddleN_;
    std::array<float, N>               ringBuffer_{};
    size_t                             ringPos_ = 0;
    size_t                             samplesSinceLastFrame_ = 0;
    size_t                             hopSize_;

    detail::FrameDoubleBuffer<N>       frameBuffer_;

    std::array<float, SpectrumBins>   smoothedMagnitudes_{};
    float                             smoothingAlpha_;

    size_t                            numLogBands_;
    float                             minLogFreq_, maxLogFreq_, sampleRate_;
    std::array<uint16_t, SpectrumBins> binToBand_;

    OutputSet                          output_[2];
    std::atomic<int>                   frontOutput_{0};
};

} // namespace SpectraCore

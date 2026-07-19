# SpectraCore

**Biblioteca de análisis espectral en tiempo real**  
Versión 3.0.0 · Licencia Apache 2.0

SpectraCore es una librería C++20 de alto rendimiento para el cálculo de FFT real con aceleración SIMD (SSE4.1, AVX2, AVX-512, NEON). Está diseñada para visualizadores musicales, ecualizadores gráficos y cualquier aplicación que necesite transformar audio en espectros frecuenciales de forma ultrarrápida.

---

## Características principales

- FFT real de entrada, con tamaños potencia de dos o arbitrarios (Bluestein).
- Ventanas seleccionables: Hann, Hamming, Blackman, Kaiser.
- Solapamiento configurable (*hop size*).
- Suavizado exponencial de magnitudes.
- Agrupación logarítmica de frecuencias (energía por banda).
- Salida dual: magnitudes lineales, dB, fase y bandas logarítmicas.
- Doble búfer de entrada y salida para operación segura entre hilos.
- Procesador asíncrono opcional (`AsyncAnalyzer`) con espera pasiva.
- Detección automática de SIMD en tiempo de ejecución.
- Multiplataforma: Windows, macOS, Linux, iOS, Android.

---

## Requisitos

- Compilador con C++20 (GCC ≥ 11, Clang ≥ 13, MSVC ≥ 2022).
- CMake ≥ 3.16 y Ninja (recomendado).
- Intel Intrinsics habilitados según la CPU (`-msse4.1`, `-mavx2`, `-mavx512f`).

---

## Compilación rápida

1. Clona el repositorio.
2. Ejecuta el script de compilación:

   ```bash
   ./build.sh
   ```

3. Las bibliotecas estática (`.a`) y dinámica (`.so`) se generarán en:

   ```
   build/<SO>/BUILD-XX/lib/
   ```

---

## Uso en tu proyecto

Incluye el encabezado y enlaza con la librería:

```cpp
#include <SpectraCore/Analyzer.h>
```

Enlaza con `-lspectracore`.

---

## Ejemplo mínimo

```cpp
SpectraCore::Analyzer<2048> analizador;
analizador.setOverlapFactor(2);            // 50% solapamiento
analizador.setSmoothing(0.3f);             // suavizado exponencial
analizador.setLogBands(32, 20.f, 20000.f, 44100.f);

// Hilo de audio:
analizador.pushSamples(miBuffer);

// Hilo de proceso:
analizador.processReadyFrame();

// Hilo de UI:
auto spec = analizador.getSpectrum();
// spec.magnitudeDB, spec.phase, spec.logMagnitudes...
```

---

## Precisión

La rutina escalar sirve como referencia. Las versiones SIMD producen resultados idénticos bit‑a‑bit, verificados en tests. El test de exactitud (`tests/test_accuracy.cpp`) compara contra una DFT directa y asegura un error relativo < 1e⁻⁵.

---

## Rendimiento

Los bucles internos de las FFT potencia de dos están vectorizados manualmente con intrínsecos, usando twiddles pre‑empaquetados. En benchmarks internos (N=2048, AVX2, Ryzen 9 5900X) SpectraCore iguala o supera ligeramente a FFTW3 en modos de baja latencia.

Para ejecutar el benchmark:

```bash
cmake -DBUILD_BENCHMARKS=ON ..
make benchmark
```

(Se requiere FFTW3 instalado.)

---

## Licencia

Apache License 2.0. Véase el archivo [LICENSE-2.0.txt](LICENSE-2.0.txt).

---

## Créditos

Desarrollado por **H4ck3r598** Jesús Emanuel, 2026.

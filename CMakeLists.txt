cmake_minimum_required(VERSION 3.16)
project(SpectraCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mtune=native")
endif()

include(CheckCXXCompilerFlag)
if(NOT MSVC)
    check_cxx_compiler_flag(-msse4.1 COMPILER_SUPPORTS_SSE4_1)
    check_cxx_compiler_flag(-mavx2 COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag(-mavx512f COMPILER_SUPPORTS_AVX512F)
else()
    set(COMPILER_SUPPORTS_SSE4_1 TRUE)
    set(COMPILER_SUPPORTS_AVX2 TRUE)
endif()

add_library(spectracore_static STATIC
    src/detect_simd.cpp
    src/explicit_instantiations.cpp
)
add_library(spectracore_shared SHARED
    src/detect_simd.cpp
    src/explicit_instantiations.cpp
)

set_target_properties(spectracore_static PROPERTIES OUTPUT_NAME spectracore)
set_target_properties(spectracore_shared PROPERTIES OUTPUT_NAME spectracore)

target_include_directories(spectracore_static PUBLIC include)
target_include_directories(spectracore_shared PUBLIC include)

if(COMPILER_SUPPORTS_SSE4_1)
    target_compile_options(spectracore_static PRIVATE -msse4.1)
    target_compile_options(spectracore_shared PRIVATE -msse4.1)
endif()
if(COMPILER_SUPPORTS_AVX2)
    target_compile_options(spectracore_static PRIVATE -mavx2)
    target_compile_options(spectracore_shared PRIVATE -mavx2)
endif()
if(COMPILER_SUPPORTS_AVX512F)
    target_compile_options(spectracore_static PRIVATE -mavx512f -mavx512dq)
    target_compile_options(spectracore_shared PRIVATE -mavx512f -mavx512dq)
endif()

option(BUILD_TESTS "Construir tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_simd tests/test_simd.cpp)
    target_link_libraries(test_simd PRIVATE spectracore_static)
    add_test(NAME test_simd COMMAND test_simd)

    add_executable(test_accuracy tests/test_accuracy.cpp)
    target_link_libraries(test_accuracy PRIVATE spectracore_static)
    add_test(NAME test_accuracy COMMAND test_accuracy)
endif()

option(BUILD_BENCHMARKS "Construir benchmarks (requiere FFTW3)" OFF)
if(BUILD_BENCHMARKS)
    find_package(FFTW3 QUIET)
    if(FFTW3_FOUND)
        add_executable(benchmark tests/benchmark.cpp)
        target_compile_definitions(benchmark PRIVATE HAS_FFTW)
        target_link_libraries(benchmark PRIVATE spectracore_static FFTW3::fftw3f)
    else()
        message(WARNING "FFTW3 no encontrado, el benchmark se compilará sin comparativa.")
        add_executable(benchmark tests/benchmark.cpp)
        target_link_libraries(benchmark PRIVATE spectracore_static)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS spectracore_static spectracore_shared
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

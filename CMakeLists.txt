cmake_minimum_required(VERSION 3.24)
# Release builds take the version from the tag (CI passes -DGLIDE90_VERSION=x.y.z).
set(GLIDE90_VERSION "0.3.0" CACHE STRING "Plugin version, x.y.z")
project(Glide90 VERSION ${GLIDE90_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Static MSVC runtime: the plugin then loads on machines without the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(GLIDE90_BUILD_PLUGIN "Build the JUCE plugin (needs JUCE's system dependencies)" ON)
option(GLIDE90_BUILD_TESTS "Build the DSP tests" ON)
option(GLIDE90_LTO "Link-time optimisation of the plugin (slow links; CI uses it for releases only)" ON)

include(FetchContent)

# JUCE-free signal processing: everything that makes the sound.
add_library(glide90_dsp STATIC
    src/dsp/SincResampler.cpp
    src/dsp/ErEngine.cpp
    src/dsp/SpxEngine.cpp
    src/dsp/sway/Shape.cpp
    src/dsp/sway/PickDetector.cpp
    src/dsp/sway/BarGenerator.cpp
    src/dsp/sway/SwayProcessor.cpp
    src/dsp/hold/HoldEngine.cpp)
target_include_directories(glide90_dsp PUBLIC src/dsp src/dsp/sway src/dsp/hold)
if(MSVC)
    target_compile_options(glide90_dsp PRIVATE /W4 /fp:fast)
else()
    target_compile_options(glide90_dsp PRIVATE -Wall -Wextra -O3 -ffast-math)
endif()

if(GLIDE90_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR header-only-no-cmake) # only the header is used
    FetchContent_MakeAvailable(doctest)

    enable_testing()
    add_executable(glide90_tests
        tests/main.cpp
        tests/test_resampler.cpp
        tests/test_er.cpp
        tests/test_spx.cpp
        tests/test_sway.cpp
        tests/test_hold.cpp)
    target_link_libraries(glide90_tests PRIVATE glide90_dsp)
    target_include_directories(glide90_tests PRIVATE tests ${doctest_SOURCE_DIR})
    target_compile_definitions(glide90_tests PRIVATE GLIDE90_IR_DIR="${CMAKE_SOURCE_DIR}/ir")
    add_test(NAME dsp COMMAND glide90_tests)

    add_executable(glide90_render tools/render/render.cpp)
    target_link_libraries(glide90_render PRIVATE glide90_dsp)
    target_include_directories(glide90_render PRIVATE tests)
endif()

# After the tests: the plugin's own tests reuse doctest.
if(GLIDE90_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

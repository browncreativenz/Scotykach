cmake_minimum_required(VERSION 3.22)

# Must be set before project(). 11.0 is the first macOS with Apple Silicon.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Scotykach VERSION 0.1.0 LANGUAGES C CXX)

option(SCOTYKACH_BUILD_TESTS "Build the engine tests"         ON)
option(SCOTYKACH_BUILD_TOOLS "Build the command-line renderer" ON)
option(SCOTYKACH_SANITIZE    "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SPOTYKACH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/spotykach")
set(DAISYSP_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/external/DaisySP")
if(NOT EXISTS "${SPOTYKACH_DIR}/src/core/core.h" OR NOT EXISTS "${DAISYSP_DIR}/Source/daisysp.h")
    message(FATAL_ERROR
        "Firmware sources not found. Run:\n"
        "  git submodule update --init external/spotykach external/DaisySP")
endif()

# Floating-point contraction (FMA) is off everywhere, so builds round the same
# way on every compiler and the patched and upstream engines can be compared
# bit for bit.
set(SCOTYKACH_FP_FLAGS
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)

# The sanitizers find firmware paths that behave differently on a desktop
# than on the module: integer division by zero and out-of-range float to
# integer conversions, which the Cortex-M7 handles without trapping and
# x86 does not (see patches/portability). Any report fails the test.
if(SCOTYKACH_SANITIZE)
    add_compile_options(-fsanitize=address,undefined,float-cast-overflow
                        -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

include(cmake/Firmware.cmake)

if(SCOTYKACH_BUILD_TOOLS)
    add_executable(scotykach_render tools/render/scotykach_render.cpp)
    target_include_directories(scotykach_render PRIVATE tools tools/render)
    target_link_libraries(scotykach_render PRIVATE scotykach_fw)
    target_compile_options(scotykach_render PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
endif()

if(SCOTYKACH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

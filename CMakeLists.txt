cmake_minimum_required(VERSION 3.28...4.4)

project(avgen
    VERSION 0.1.0
    DESCRIPTION "Real-time GPU audiovisual engine"
    LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(AVGEN_BUILD_TESTS "Build the test suite" ON)
option(AVGEN_DAWN_FROM_SOURCE "Build Dawn from source instead of using the prebuilt archive" OFF)
option(AVGEN_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(AVGEN_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(AVGEN_WARNINGS_AS_ERRORS "Treat warnings as errors for engine targets" OFF)
# Transitional (ADR-023): link the test-only scene::makeSourceMesh stub from tests/support while the
# CPU side of scene/procedural.hpp lives on another branch. Off once both branches are merged.
option(AVGEN_PROC_GPU_STANDALONE "Build with the standalone procedural source-mesh stub" OFF)

if(APPLE)
    # The prebuilt Dawn archive is built against the macOS 26 SDK with a 26.0 deployment floor.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

include(Warnings)
include(Dependencies)

# Runtime asset locations for development builds. The executable also searches next to itself.
set(AVGEN_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

add_subdirectory(src)

if(AVGEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

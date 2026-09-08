# All third-party dependencies, pinned. See docs/dependencies.md for purpose and licence of each.
# Policy: SYSTEM + EXCLUDE_FROM_ALL for everything; per-package options only; no global policy
# overrides (ADR-008).

set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cache/cpm" CACHE PATH "CPM source cache")
set(CPM_USE_LOCAL_PACKAGES OFF)
include(CPM)

find_package(Threads REQUIRED)

# ---- fmt + spdlog (logging) -------------------------------------------------------------------
CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 12.2.0
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS "FMT_INSTALL OFF" "FMT_DOC OFF" "FMT_TEST OFF")

CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.17.0
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS "SPDLOG_FMT_EXTERNAL ON" "SPDLOG_INSTALL OFF" "SPDLOG_BUILD_EXAMPLE OFF" "SPDLOG_BUILD_TESTS OFF")

# ---- glm (math) -------------------------------------------------------------------------------
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.3
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS "GLM_BUILD_TESTS OFF" "GLM_BUILD_INSTALL OFF" "GLM_ENABLE_CXX_20 ON")

# ---- nlohmann/json (serialisation) -----------------------------------------------------------
CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    GIT_TAG v3.12.0
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")

# ---- miniaudio (audio decoding + device I/O), header-only, implementation in src/audio ---------
CPMAddPackage(
    NAME miniaudio
    GITHUB_REPOSITORY mackron/miniaudio
    GIT_TAG 0.11.25
    DOWNLOAD_ONLY YES)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")
add_library(miniaudio::miniaudio ALIAS miniaudio)

# ---- KissFFT (scalar reference FFT), compiled directly with float scalars ----------------------
CPMAddPackage(
    NAME kissfft
    GITHUB_REPOSITORY mborgerding/kissfft
    GIT_TAG 131.2.0
    DOWNLOAD_ONLY YES)
add_library(kissfft STATIC EXCLUDE_FROM_ALL
    "${kissfft_SOURCE_DIR}/kiss_fft.c"
    "${kissfft_SOURCE_DIR}/kiss_fftr.c")
target_include_directories(kissfft SYSTEM PUBLIC "${kissfft_SOURCE_DIR}")
target_compile_definitions(kissfft PUBLIC kiss_fft_scalar=float KISS_FFT_SHARED=0)
set_target_properties(kissfft PROPERTIES C_STANDARD 99)
add_library(kissfft::kissfft ALIAS kissfft)

# ---- fastgltf (glTF 2.0 loader) -----------------------------------------------------------------
CPMAddPackage(
    NAME fastgltf
    GITHUB_REPOSITORY spnda/fastgltf
    GIT_TAG v0.9.0
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS "FASTGLTF_COMPILE_AS_CPP20 ON" "FASTGLTF_ENABLE_TESTS OFF" "FASTGLTF_ENABLE_EXAMPLES OFF")

# ---- stb (image decode/encode), header-only, implementation in src/assets --------------------------
CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    DOWNLOAD_ONLY YES)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(stb::stb ALIAS stb)

# ---- Catch2 (tests) ---------------------------------------------------------------------------
if(AVGEN_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG v3.16.0
        SYSTEM YES EXCLUDE_FROM_ALL YES
        OPTIONS "CATCH_INSTALL_DOCS OFF" "CATCH_INSTALL_EXTRAS ON")
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()

# ---- SDL3 (windowing, input, dialogs) ---------------------------------------------------------
CPMAddPackage(
    NAME SDL3
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG release-3.4.16
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS
        "SDL_SHARED OFF" "SDL_STATIC ON" "SDL_TEST_LIBRARY OFF" "SDL_TESTS OFF" "SDL_EXAMPLES OFF"
        "SDL_INSTALL OFF" "SDL_AUDIO OFF" "SDL_GPU OFF" "SDL_RENDER OFF" "SDL_CAMERA OFF"
        "SDL_SENSOR OFF" "SDL_HAPTIC OFF")

# ---- Dawn (WebGPU implementation) -------------------------------------------------------------
# Pinned nightly release v20260907.201642 (commit c4e47b5e). Prebuilt archive: macOS arm64 Release,
# minos 26.0. Anything else builds from source.
set(AVGEN_DAWN_COMMIT "c4e47b5eddc06f271cb07c3108cfccb1bb4704ec")
set(AVGEN_DAWN_RELEASE "v20260907.201642")
if(NOT AVGEN_DAWN_FROM_SOURCE AND APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    CPMAddPackage(
        NAME dawn_prebuilt
        URL "https://github.com/google/dawn/releases/download/${AVGEN_DAWN_RELEASE}/Dawn-${AVGEN_DAWN_COMMIT}-macos-latest-Release.tar.gz"
        URL_HASH SHA256=1902997cc8589290f3a4b896e700172a36eb80c24f700702564709938a12a28d
        DOWNLOAD_ONLY YES)
    find_package(Dawn REQUIRED CONFIG PATHS "${dawn_prebuilt_SOURCE_DIR}/lib/cmake/Dawn" NO_DEFAULT_PATH)
    set(AVGEN_DAWN_KIND "prebuilt")
else()
    # From-source path (Intel, older macOS, Windows, Linux, Debug Dawn, Tint SPIR-V reader).
    # Documented in docs/build.md; requires Python 3 for DAWN_FETCH_DEPENDENCIES.
    CPMAddPackage(
        NAME dawn
        GITHUB_REPOSITORY google/dawn
        GIT_TAG ${AVGEN_DAWN_COMMIT}
        SYSTEM YES EXCLUDE_FROM_ALL YES
        OPTIONS
            "DAWN_FETCH_DEPENDENCIES ON" "DAWN_ENABLE_INSTALL OFF" "DAWN_BUILD_SAMPLES OFF"
            "DAWN_BUILD_MONOLITHIC_LIBRARY STATIC" "TINT_BUILD_TESTS OFF" "TINT_BUILD_CMD_TOOLS OFF"
            "TINT_BUILD_SPV_READER ON" "DAWN_USE_GLFW OFF" "DAWN_BUILD_TESTS OFF")
    set(AVGEN_DAWN_KIND "source")
endif()
message(STATUS "avgen: Dawn ${AVGEN_DAWN_RELEASE} (${AVGEN_DAWN_KIND})")

# ---- Dear ImGui (docking) + ImPlot ------------------------------------------------------------
CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.92.9b-docking
    DOWNLOAD_ONLY YES)
CPMAddPackage(
    NAME implot
    GITHUB_REPOSITORY epezent/implot
    GIT_TAG v1.0
    DOWNLOAD_ONLY YES)

add_library(imgui STATIC EXCLUDE_FROM_ALL
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp"
    "${implot_SOURCE_DIR}/implot.cpp"
    "${implot_SOURCE_DIR}/implot_items.cpp")
target_include_directories(imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${implot_SOURCE_DIR}")
# ImPlot v1.0 still uses ImDrawList overloads that IMGUI_DISABLE_OBSOLETE_FUNCTIONS deletes.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static dawn::webgpu_dawn)
target_compile_features(imgui PUBLIC cxx_std_20)
if(APPLE)
    # The WebGPU backend's multi-viewport surface helper includes Cocoa headers.
    set_source_files_properties("${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp" PROPERTIES LANGUAGE OBJCXX)
endif()
add_library(imgui::imgui ALIAS imgui)

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

# ---- meshoptimizer (mesh optimisation and LOD simplification) ----------------------------------
# ADR-078. Only the library target is built: the demo and gltfpack pull in extra sources and, in
# gltfpack's case, its own dependencies, and neither is used from the engine.
CPMAddPackage(
    NAME meshoptimizer
    GITHUB_REPOSITORY zeux/meshoptimizer
    GIT_TAG v1.2
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS "MESHOPT_BUILD_DEMO OFF" "MESHOPT_BUILD_GLTFPACK OFF" "MESHOPT_BUILD_SHARED_LIBS OFF"
            "MESHOPT_WERROR OFF" "MESHOPT_INSTALL OFF")

# ---- stb (image decode/encode), header-only, implementation in src/assets --------------------------
CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    DOWNLOAD_ONLY YES)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(stb::stb ALIAS stb)

# ---- tinyexr (OpenEXR read/write), header-only, implementation in src/assets/exr_impl.cpp -------
# Release archive v3.2.0 (commit 6f470c9a), SHA256-pinned; the git checkout would also pull the ZFP
# submodule. We use the classic single header `tinyexr.h` with the bundled miniz for ZIP, not the
# v3 C11 library that the same release introduces.
CPMAddPackage(
    NAME tinyexr
    URL "https://github.com/syoyo/tinyexr/archive/refs/tags/v3.2.0.tar.gz"
    URL_HASH SHA256=df2bd61124a35d8138f8b0bc22418a1d4fe33622c818e0e022f5522afc0821b0
    DOWNLOAD_ONLY YES)
add_library(tinyexr STATIC EXCLUDE_FROM_ALL "${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c")
target_include_directories(tinyexr SYSTEM PUBLIC "${tinyexr_SOURCE_DIR}" "${tinyexr_SOURCE_DIR}/deps/miniz")
target_compile_definitions(tinyexr PUBLIC
    TINYEXR_USE_MINIZ=1 TINYEXR_USE_STB_ZLIB=0 TINYEXR_USE_NANOZLIB=0 TINYEXR_USE_THREAD=0 TINYEXR_USE_OPENMP=0
    MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO)
set_target_properties(tinyexr PROPERTIES C_STANDARD 99)
if(NOT MSVC)
    target_compile_options(tinyexr PRIVATE -w) # third-party code: no engine warning flags
endif()
add_library(tinyexr::tinyexr ALIAS tinyexr)

# ---- Embree (ray/geometry intersection and BVH for the path tracer) ---------------------------
# ADR-348. Embree owns intersection and acceleration only; the integrator, materials, sampling and
# output are AV Gen's (spec section 74). Apache-2.0; it vendors sse2neon.h (MIT) for the NEON path.
#
# EMBREE_TASKING_SYSTEM=INTERNAL keeps oneTBB out of the build. The path tracer additionally
# configures the device with an explicit thread count and commits with `rtcJoinCommitScene` from
# AV Gen-owned threads, so Embree starts no pool of its own (spec section 36).
#
# The CMAKE_CXX_STANDARD dance is not optional and not cosmetic. Embree appends `-std=c++11` to
# CMAKE_CXX_FLAGS, but a globally-set CMAKE_CXX_STANDARD (CMakeLists.txt sets 23) emits its own
# `-std=` flag LATER on the same command line and wins. Embree 4.4.0 does not compile as C++23
# under Apple clang 21: twenty errors in kernels/builders/priminfo_mb.h about PrimInfoMB "not a
# direct or virtual base of embree::SetMB". Engine targets carry the standard themselves.
set(_avgen_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
unset(CMAKE_CXX_STANDARD)
unset(CMAKE_CXX_STANDARD CACHE)
CPMAddPackage(
    NAME embree
    GITHUB_REPOSITORY RenderKit/embree
    GIT_TAG v4.4.0
    SYSTEM YES EXCLUDE_FROM_ALL YES
    OPTIONS "EMBREE_TASKING_SYSTEM INTERNAL" "EMBREE_ISPC_SUPPORT OFF" "EMBREE_TUTORIALS OFF"
            "EMBREE_STATIC_LIB ON" "EMBREE_MAX_ISA NEON"
            "EMBREE_GEOMETRY_QUAD OFF" "EMBREE_GEOMETRY_CURVE OFF" "EMBREE_GEOMETRY_SUBDIVISION OFF"
            "EMBREE_GEOMETRY_POINT OFF" "EMBREE_GEOMETRY_GRID OFF")
set(CMAKE_CXX_STANDARD "${_avgen_saved_cxx_standard}")
unset(_avgen_saved_cxx_standard)

# ---- OpenImageDenoise (path-tracer denoise) ----------------------------------------------------
# ADR-350. Prebuilt macOS arm64 archive, SHA256-pinned, exactly as Dawn is -- and for a sharper
# reason than Dawn's. OIDN's CPU device cannot be built from source without TWO things this project
# does not have and should not acquire for one feature:
#
#   * oneTBB, which `devices/cpu/CMakeLists.txt` marks REQUIRED with no alternative tasking backend;
#   * ISPC >= 1.21, a *binary compiler toolchain* (not a library) that OIDN's CMake refuses to
#     proceed without. Nothing else in this project needs a third-party compiler to build.
#
# The prebuilt archive carries both -- it ships its own `libtbb` -- and also carries the trained
# weights, which are the 49 MB in `libOpenImageDenoise_core`. On Apple silicon OIDN runs its network
# through BNNS (Accelerate), so no oneDNN is involved.
#
# OFF by default: it is a 51 MB download for a feature the renderer works without, and a build that
# has not opted in must not pay for it. `AVGEN_PATHTRACE_DENOISE` gates the code as well as the
# fetch, and the tracer reports denoising as unavailable rather than failing (spec section 54).
option(AVGEN_PATHTRACE_DENOISE "Fetch Open Image Denoise and enable path-tracer denoising" OFF)
if(AVGEN_PATHTRACE_DENOISE)
    if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        CPMAddPackage(
            NAME oidn_prebuilt
            URL "https://github.com/RenderKit/oidn/releases/download/v2.3.3/oidn-2.3.3.arm64.macos.tar.gz"
            URL_HASH SHA256=b3c005ed437547fca5460ae43c8631ff46bc4ec4f9d5f219940ef941601a9d81
            DOWNLOAD_ONLY YES)
        add_library(oidn INTERFACE)
        target_include_directories(oidn SYSTEM INTERFACE "${oidn_prebuilt_SOURCE_DIR}/include")
        target_link_libraries(oidn INTERFACE
            "${oidn_prebuilt_SOURCE_DIR}/lib/libOpenImageDenoise.dylib")
        # The dylibs resolve each other by @rpath at load time, so the directory has to be on it.
        target_link_options(oidn INTERFACE "-Wl,-rpath,${oidn_prebuilt_SOURCE_DIR}/lib")
        add_library(oidn::oidn ALIAS oidn)
        set(AVGEN_HAVE_OIDN ON)
        message(STATUS "avgen: Open Image Denoise 2.3.3 (prebuilt arm64)")
    else()
        message(WARNING "avgen: AVGEN_PATHTRACE_DENOISE is ON but no prebuilt OIDN exists for this "
                        "platform; denoising will report itself unavailable")
    endif()
endif()

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

# ---- Syphon-Framework (macOS frame sharing), compiled from source as a static library -----------
# Pinned to the 2025-10-06 master commit (the last tagged release, "5", is from 2019 and predates
# the Metal server). Only the Metal server/client, messaging and directory sources are compiled;
# the OpenGL server/client and their CGL helpers are left out so the deprecated OpenGL framework
# is never linked. The renderer loads its shaders from the framework bundle, which a static
# library does not have: the patch adds a compile-from-source fallback (see cmake/patches/).
if(APPLE)
    CPMAddPackage(
        NAME syphon
        GITHUB_REPOSITORY Syphon/Syphon-Framework
        GIT_TAG 71351d4b484cd2d1917867f7846a5cdca724552d
        DOWNLOAD_ONLY YES
        PATCHES "${CMAKE_CURRENT_LIST_DIR}/patches/syphon-metal-library-from-source.patch")
    # CPM's source-cache key includes the patch path but not its content: check it was applied.
    file(STRINGS "${syphon_SOURCE_DIR}/SyphonServerRendererMetal.m" AVGEN_SYPHON_PATCHED
        REGEX "newLibraryWithSource")
    if(NOT AVGEN_SYPHON_PATCHED)
        message(FATAL_ERROR "avgen: Syphon sources in ${syphon_SOURCE_DIR} are unpatched; delete that "
                            "directory and re-run CMake so the patch in cmake/patches is applied")
    endif()
    # The sources import their own headers as <Syphon/...>: mirror them into a Syphon/ directory.
    set(AVGEN_SYPHON_INCLUDE "${CMAKE_BINARY_DIR}/syphon-include")
    file(GLOB AVGEN_SYPHON_HEADERS "${syphon_SOURCE_DIR}/*.h")
    file(MAKE_DIRECTORY "${AVGEN_SYPHON_INCLUDE}/Syphon")
    file(COPY ${AVGEN_SYPHON_HEADERS} DESTINATION "${AVGEN_SYPHON_INCLUDE}/Syphon")
    add_library(syphon STATIC EXCLUDE_FROM_ALL
        "${syphon_SOURCE_DIR}/SyphonCFMessageReceiver.m"
        "${syphon_SOURCE_DIR}/SyphonCFMessageSender.m"
        "${syphon_SOURCE_DIR}/SyphonClientBase.m"
        "${syphon_SOURCE_DIR}/SyphonClientConnectionManager.m"
        "${syphon_SOURCE_DIR}/SyphonDispatch.c"
        "${syphon_SOURCE_DIR}/SyphonMessageQueue.m"
        "${syphon_SOURCE_DIR}/SyphonMessageReceiver.m"
        "${syphon_SOURCE_DIR}/SyphonMessageSender.m"
        "${syphon_SOURCE_DIR}/SyphonMessaging.m"
        "${syphon_SOURCE_DIR}/SyphonMetalClient.m"
        "${syphon_SOURCE_DIR}/SyphonMetalServer.m"
        "${syphon_SOURCE_DIR}/SyphonPrivate.m"
        "${syphon_SOURCE_DIR}/SyphonServerBase.m"
        "${syphon_SOURCE_DIR}/SyphonServerConnectionManager.m"
        "${syphon_SOURCE_DIR}/SyphonServerDirectory.m"
        "${syphon_SOURCE_DIR}/SyphonServerRendererMetal.m")
    target_include_directories(syphon SYSTEM PUBLIC "${AVGEN_SYPHON_INCLUDE}" "${syphon_SOURCE_DIR}")
    # The Xcode project compiles everything with ARC and the Syphon_Prefix.pch prefix header
    # (Cocoa import + SYPHONLOG). Third-party code: its warnings are not ours to fix.
    target_compile_options(syphon PRIVATE -w "-include${syphon_SOURCE_DIR}/Syphon_Prefix.pch"
        $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
    target_link_libraries(syphon PUBLIC
        "-framework Metal" "-framework IOSurface" "-framework CoreVideo" "-framework Cocoa"
        "-framework Foundation")
    add_library(syphon::syphon ALIAS syphon)
endif()

# ---- avgen_core: GPU-free engine logic (core, audio, analysis, signals, params, scene, entity, assets)
file(GLOB_RECURSE AVGEN_CORE_SOURCES CONFIGURE_DEPENDS
    core/*.cpp audio/*.cpp analysis/*.cpp signals/*.cpp params/*.cpp scene/*.cpp spatial/*.cpp world/*.cpp graph/*.cpp shaders/*.cpp assets/*.cpp ai/*.cpp entity/*.cpp
    control/*.cpp comp/*.cpp seq/*.cpp help/*.cpp)
# The cinematic shot vocabulary (ADR-062) is pure geometry and JSON, and the sequencer (ADR-089)
# holds an app::Shot by value in every shot it bakes. It lives in app/ for history's sake; it
# belongs to the library, or seq/ could not be tested without linking the editor.
list(APPEND AVGEN_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app/cinematic.cpp")
if(APPLE)
    # Native video export (AVFoundation) is Objective-C++; the portable stub is for other platforms.
    # The AI control plane's HTTP transport (ADR-094) is the same shape: NSURLSession has no C
    # interface, so the system client is Objective-C++ and the portable half reports "unavailable".
    file(GLOB_RECURSE AVGEN_CORE_OBJCXX_SOURCES CONFIGURE_DEPENDS assets/*.mm comp/*.mm ai/*.mm)
    list(APPEND AVGEN_CORE_SOURCES ${AVGEN_CORE_OBJCXX_SOURCES})
    list(REMOVE_ITEM AVGEN_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/assets/video_writer_native_stub.cpp")
    # Fonts: CoreText on macOS (ADR-083); the stub is for platforms with no type engine.
    list(REMOVE_ITEM AVGEN_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/comp/font_stub.cpp")
    # MIDI: CoreMIDI on macOS; the stub is for other platforms.
    list(REMOVE_ITEM AVGEN_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/control/midi_stub.cpp")
    # HTTP: NSURLSession on macOS (ADR-094).
    list(REMOVE_ITEM AVGEN_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ai/http_portable.cpp")
else()
    list(REMOVE_ITEM AVGEN_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/control/midi_coremidi.cpp")
    list(REMOVE_ITEM AVGEN_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/comp/font_coretext.mm")
endif()
add_library(avgen_core STATIC ${AVGEN_CORE_SOURCES})
target_include_directories(avgen_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(avgen_core
    PUBLIC fmt::fmt spdlog::spdlog glm::glm nlohmann_json::nlohmann_json Threads::Threads
    PRIVATE miniaudio::miniaudio kissfft::kissfft fastgltf::fastgltf stb::stb tinyexr::tinyexr meshoptimizer)
if(NOT MSVC)
    # The tinyexr implementation is third-party code compiled inside avgen_core: no warnings for it.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/assets/exr_impl.cpp" PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(APPLE)
    set_source_files_properties(${AVGEN_CORE_OBJCXX_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    # Security is the Keychain (ADR-094). CoreFoundation is listed explicitly rather than relied on
    # through Foundation: Security does not re-export CF's symbols, and linking one without the
    # other fails at link time on _CFRelease and friends.
    target_link_libraries(avgen_core PUBLIC
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation"
        "-framework CoreMIDI" "-framework CoreText" "-framework CoreGraphics"
        "-framework Security" "-framework CoreFoundation")
endif()
target_compile_definitions(avgen_core PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
avgen_set_warnings(avgen_core)
target_compile_definitions(avgen_core PUBLIC AVGEN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(AVGEN_ALLOC_COUNTERS)
    target_compile_definitions(avgen_core PUBLIC AVGEN_ALLOC_COUNTERS)
endif()

# ---- avgen_gpu: WebGPU facade and scene renderer (needs Dawn) ---------------------------------
file(GLOB_RECURSE AVGEN_GPU_SOURCES CONFIGURE_DEPENDS gpu/*.cpp rendering/*.cpp share/*.cpp)
if(APPLE)
    # Texture sharing (milestone 1.2): Syphon is Objective-C++; the stub is for other platforms.
    file(GLOB_RECURSE AVGEN_GPU_OBJCXX_SOURCES CONFIGURE_DEPENDS share/*.mm)
    list(APPEND AVGEN_GPU_SOURCES ${AVGEN_GPU_OBJCXX_SOURCES})
    list(REMOVE_ITEM AVGEN_GPU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/share/syphon_share_stub.cpp")
endif()
if(AVGEN_GPU_SOURCES)
    add_library(avgen_gpu STATIC ${AVGEN_GPU_SOURCES})
    target_link_libraries(avgen_gpu PUBLIC avgen_core dawn::webgpu_dawn)
    if(APPLE)
        set_source_files_properties(${AVGEN_GPU_OBJCXX_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(avgen_gpu PRIVATE syphon::syphon
            "-framework Metal" "-framework IOSurface" "-framework CoreVideo" "-framework Foundation")
    endif()
    target_compile_definitions(avgen_gpu PUBLIC AVGEN_SHADER_SOURCE_DIR="${AVGEN_SHADER_SOURCE_DIR}")
    avgen_set_warnings(avgen_gpu)
endif()

# ---- avgen_platform: SDL3 window + Dear ImGui layer -------------------------------------------
file(GLOB_RECURSE AVGEN_PLATFORM_SOURCES CONFIGURE_DEPENDS platform/*.cpp platform/*.mm ui/*.cpp)
if(AVGEN_PLATFORM_SOURCES AND TARGET avgen_gpu)
    add_library(avgen_platform STATIC ${AVGEN_PLATFORM_SOURCES})
    target_link_libraries(avgen_platform PUBLIC avgen_gpu SDL3::SDL3-static imgui::imgui)
    avgen_set_warnings(avgen_platform)
endif()

# ---- avgen: the application -------------------------------------------------------------------
file(GLOB_RECURSE AVGEN_APP_SOURCES CONFIGURE_DEPENDS app/*.cpp)
# cinematic.cpp is compiled into avgen_core (above); leaving it here too would define it twice.
list(REMOVE_ITEM AVGEN_APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app/cinematic.cpp")
# The engine revision, regenerated on every build so a benchmark record's conditions cannot be
# stale (ADR-113). Script mode rather than configure time: a revision captured at configure time is
# whatever it was the last time somebody ran cmake, which is exactly the kind of number that makes
# two measurements look comparable when they are not.
set(AVGEN_BUILD_INFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(avgen_build_info ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEMPLATE=${CMAKE_SOURCE_DIR}/cmake/build_info.hpp.in
        -DOUT=${AVGEN_BUILD_INFO_DIR}/avgen_build_info.hpp
        -DAVGEN_BUILD_TYPE=$<CONFIG>
        -P ${CMAKE_SOURCE_DIR}/cmake/GitRevision.cmake
    BYPRODUCTS "${AVGEN_BUILD_INFO_DIR}/avgen_build_info.hpp"
    COMMENT "Recording the engine revision for benchmark records"
    VERBATIM)

add_executable(avgen ${AVGEN_APP_SOURCES})
add_dependencies(avgen avgen_build_info)
target_include_directories(avgen PRIVATE "${AVGEN_BUILD_INFO_DIR}")
target_link_libraries(avgen PRIVATE avgen_core)
if(TARGET avgen_platform)
    target_link_libraries(avgen PRIVATE avgen_platform)
endif()
avgen_set_warnings(avgen)

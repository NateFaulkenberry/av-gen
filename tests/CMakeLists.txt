include(Catch)

file(GLOB_RECURSE AVGEN_UNIT_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp integration/*.cpp support/*.cpp)
add_executable(avgen_tests ${AVGEN_UNIT_TEST_SOURCES})
target_include_directories(avgen_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Integration tests drive app::Engine, which lives in the app sources; compile the GPU-free part in.
target_sources(avgen_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app/engine.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/control_hub.cpp"
    # Output mapping maths/JSON and the output set are GPU-free (milestone 1.2).
    "${CMAKE_SOURCE_DIR}/src/rendering/output_mapping.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/output_manager.cpp")
target_link_libraries(avgen_tests PRIVATE avgen_core Catch2::Catch2WithMain)
avgen_set_warnings(avgen_tests)
catch_discover_tests(avgen_tests
    TEST_PREFIX "unit."
    PROPERTIES LABELS "unit"
    DISCOVERY_MODE PRE_TEST)

file(GLOB_RECURSE AVGEN_RENDER_TEST_SOURCES CONFIGURE_DEPENDS rendering/*.cpp support/*.cpp)
if(APPLE)
    # In-process Syphon client for the texture-sharing tests (Objective-C++, ARC).
    file(GLOB_RECURSE AVGEN_RENDER_TEST_OBJCXX_SOURCES CONFIGURE_DEPENDS rendering/*.mm support/*.mm)
    list(APPEND AVGEN_RENDER_TEST_SOURCES ${AVGEN_RENDER_TEST_OBJCXX_SOURCES})
    set_source_files_properties(${AVGEN_RENDER_TEST_OBJCXX_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
if(TARGET avgen_gpu AND AVGEN_RENDER_TEST_SOURCES)
    add_executable(avgen_render_tests ${AVGEN_RENDER_TEST_SOURCES}
        "${CMAKE_SOURCE_DIR}/src/app/engine.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/render_job.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/control_hub.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/output_manager.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/output_manager_gpu.cpp")
    target_include_directories(avgen_render_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    # Output windows need the platform layer (SDL); the window smoke test enumerates displays.
    target_link_libraries(avgen_render_tests PRIVATE avgen_gpu avgen_platform Catch2::Catch2WithMain)
    if(APPLE)
        target_link_libraries(avgen_render_tests PRIVATE syphon::syphon "-framework Metal")
    endif()
    avgen_set_warnings(avgen_render_tests)
    catch_discover_tests(avgen_render_tests
        TEST_PREFIX "gpu."
        PROPERTIES LABELS "gpu"
        DISCOVERY_MODE PRE_TEST)
endif()

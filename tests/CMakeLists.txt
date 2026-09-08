include(Catch)

file(GLOB_RECURSE AVGEN_UNIT_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp integration/*.cpp support/*.cpp)
add_executable(avgen_tests ${AVGEN_UNIT_TEST_SOURCES})
target_include_directories(avgen_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Integration tests drive app::Engine, which lives in the app sources; compile the GPU-free part in.
target_sources(avgen_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/app/engine.cpp")
target_link_libraries(avgen_tests PRIVATE avgen_core Catch2::Catch2WithMain)
avgen_set_warnings(avgen_tests)
catch_discover_tests(avgen_tests
    TEST_PREFIX "unit."
    PROPERTIES LABELS "unit"
    DISCOVERY_MODE PRE_TEST)

file(GLOB_RECURSE AVGEN_RENDER_TEST_SOURCES CONFIGURE_DEPENDS rendering/*.cpp support/*.cpp)
if(TARGET avgen_gpu AND AVGEN_RENDER_TEST_SOURCES)
    add_executable(avgen_render_tests ${AVGEN_RENDER_TEST_SOURCES})
    target_include_directories(avgen_render_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(avgen_render_tests PRIVATE avgen_gpu Catch2::Catch2WithMain)
    avgen_set_warnings(avgen_render_tests)
    catch_discover_tests(avgen_render_tests
        TEST_PREFIX "gpu."
        PROPERTIES LABELS "gpu"
        DISCOVERY_MODE PRE_TEST)
endif()

include(Catch)

file(GLOB_RECURSE AVGEN_UNIT_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp integration/*.cpp support/*.cpp)
add_executable(avgen_tests ${AVGEN_UNIT_TEST_SOURCES})
target_include_directories(avgen_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Integration tests drive app::Engine, which lives in the app sources; compile the GPU-free part in.
target_sources(avgen_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app/settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/engine.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/control_hub.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/scene_states.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/profile_capture.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/asset_browser.cpp"
    # The example index is JSON and paths. It is here so the menu the application builds is the one
    # the test reads: checking the raw JSON instead passed on an index the application then refused.
    "${CMAKE_SOURCE_DIR}/src/app/examples.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/world_director.cpp"
    # The job system is threads and state; no device needed (ADR-064).
    "${CMAKE_SOURCE_DIR}/src/app/job_system.cpp"
    # Generate World: composition on a worker, install on the main thread (ADR-066).
    "${CMAKE_SOURCE_DIR}/src/app/world_builder.cpp"
    # Viewport camera gestures are vector arithmetic over an eye and a target (ADR-068).
    "${CMAKE_SOURCE_DIR}/src/app/viewport_camera.cpp"
    # The editor shell's panel registry and layout file (ADR-076). Deliberately ImGui-free so it
    # can be checked here; the dock builder that reads it lives in avgen_platform.
    "${CMAKE_SOURCE_DIR}/src/ui/editor_layout.cpp"
    # The world editor (ADR-092). Every decision it makes -- what the ghost says, which handle the
    # pointer is over, what a drag means, what undo restores -- is ImGui-free for exactly this
    # reason: an editor whose behaviour can only be checked by looking at it is an editor nobody
    # checks. The drawing half (viewport_overlay.cpp, world_edit_panel.cpp) is not on this list.
    "${CMAKE_SOURCE_DIR}/src/ui/edit_history.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/world_edit.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/world_probe.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/brush.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/gizmo.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/world_editor.cpp"
    # The camera director's join: heroes to a brief, a sequence onto the timeline (ADR-075).
    "${CMAKE_SOURCE_DIR}/src/app/camera_director.cpp"
    # Musical events published as signals (ADR-073). engine.cpp holds a MusicRuntime, so this has to
    # be on the list for the test binary to link one -- without it the implementation is forced into
    # the header, which is a build constraint masquerading as a design decision.
    "${CMAKE_SOURCE_DIR}/src/app/music_runtime.cpp"
    # Placement layout is geometry over a click and a surface (ADR-069).
    "${CMAKE_SOURCE_DIR}/src/app/placement.cpp"
    # Output mapping maths/JSON and the output set are GPU-free (milestone 1.2).
    "${CMAKE_SOURCE_DIR}/src/rendering/output_mapping.cpp"
    # Light packing, the froxel grid, the LTC fit and the cascade maths are GPU-free (ADR-033/034).
    "${CMAKE_SOURCE_DIR}/src/rendering/light_data.cpp"
    # The frame timeline's interval arithmetic is GPU-free so it can be pinned without a device.
    "${CMAKE_SOURCE_DIR}/src/gpu/timeline_math.cpp"
    # The frame statistics' arithmetic is GPU-free for the same reason (ADR-077): submitted-triangle
    # widening, the per-label medians and the removal attribution are where these can be wrong.
    "${CMAKE_SOURCE_DIR}/src/rendering/render_stats.cpp"
    "${CMAKE_SOURCE_DIR}/src/rendering/shadow_math.cpp"
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
        "${CMAKE_SOURCE_DIR}/src/app/scene_states.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/profile_capture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/asset_browser.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/world_director.cpp"
        # The job system is threads and state; no device needed (ADR-064).
        "${CMAKE_SOURCE_DIR}/src/app/job_system.cpp"
        # Generate World: composition on a worker, install on the main thread (ADR-066).
        "${CMAKE_SOURCE_DIR}/src/app/world_builder.cpp"
        # Picking reads the identifier and linear-depth targets, so it lives with the device tests
        # even though its reconstruction maths is pure (ADR-068).
        "${CMAKE_SOURCE_DIR}/src/app/viewport_camera.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/viewport_pick.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/camera_director.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/music_runtime.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/placement.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/output_manager.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/output_manager_gpu.cpp")
    target_include_directories(avgen_render_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    # Output windows need the platform layer (SDL); the window smoke test enumerates displays.
    target_link_libraries(avgen_render_tests PRIVATE avgen_gpu avgen_platform Catch2::Catch2WithMain)
    if(APPLE)
        target_link_libraries(avgen_render_tests PRIVATE syphon::syphon "-framework Metal")
    endif()
    avgen_set_warnings(avgen_render_tests)
    # RESOURCE_LOCK, because there is one GPU. Running four GPU tests at once on a single device
    # does not make them finish sooner -- they contend for the same hardware -- and it makes the two
    # that measure *time* rather than pixels (the frame timeline's per-pass attribution, and the
    # non-stalling readback ring) fail intermittently while passing alone. That reads as flakiness
    # in the engine when it is contention in the harness. Unit tests still run in parallel alongside.
    catch_discover_tests(avgen_render_tests
        TEST_PREFIX "gpu."
        PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
        DISCOVERY_MODE PRE_TEST)
endif()

# The binary, launched the way a batch render launches it.
#
# Everything above tests libraries. Nothing tested the application itself, and that gap had a cost:
# a theme call in the headless branch dereferenced a null `imgui_` -- which is only constructed
# alongside the window -- and took down every `--headless` render, the `--render` batch path and
# tools/review_frames.py, while the whole suite stayed green. No unit test can catch that, because
# the fault is in how `Application` wires itself together when there is no window.
#
# So this runs the real binary and asks it for one frame. It needs a GPU, like the gpu-labelled
# tests, and it takes the same lock for the same reason.
if(TARGET avgen)
    add_test(NAME gpu.the-binary-renders-a-frame-headless
             COMMAND avgen --headless --example Hyperspace --frames 1 --fps 30 --size 320x180
                     --capture "${CMAKE_CURRENT_BINARY_DIR}/headless-smoke.png")
    set_tests_properties(gpu.the-binary-renders-a-frame-headless PROPERTIES
        LABELS "gpu" RESOURCE_LOCK gpu TIMEOUT 180)
endif()

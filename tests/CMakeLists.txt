include(Catch)

file(GLOB_RECURSE AVGEN_UNIT_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp integration/*.cpp support/*.cpp)
add_executable(avgen_tests ${AVGEN_UNIT_TEST_SOURCES})
target_include_directories(avgen_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The Quality Lab's metric engine, so its distortion ladder (docs/quality-lab/metrics.md 4.1) runs
# in the ordinary unit suite rather than in a harness of its own.
target_link_libraries(avgen_tests PRIVATE tinyexr::tinyexr PRIVATE avgen_quality_metrics)
# Integration tests drive app::Engine, which lives in the app sources; compile the GPU-free part in.
target_sources(avgen_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/app/settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/engine.cpp"
    # The transport (ADR-102): the playback state machine and the frame arithmetic. Deliberately
    # engine-free and clock-free so the state machine can be checked without a device or a window.
    "${CMAKE_SOURCE_DIR}/src/app/transport.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
    # The frame-range driver (ADR-383): the range arithmetic, the frame loop, the hash chain and
    # the bounded writer pool, with no GPU and no renderer in any of it. On this list for the
    # reason `output_preview.cpp` is -- a sequence that is a frame short, or hashes out of order,
    # is the kind of wrong that a device is not needed to find and a device makes harder to find.
    "${CMAKE_SOURCE_DIR}/src/app/frame_range.cpp"
    # A path-traced sequence (ADR-383). It needs no GPU -- which is the whole point of it -- so it
    # belongs in the CPU suite where it can actually be run.
    "${CMAKE_SOURCE_DIR}/src/app/trace_sequence.cpp"
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
    "${CMAKE_SOURCE_DIR}/src/app/ai_edit_sink.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/edit_system.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/viewport_camera.cpp"
    # The editor shell's panel registry and layout file (ADR-076). Deliberately ImGui-free so it
    # can be checked here; the dock builder that reads it lives in avgen_platform.
    "${CMAKE_SOURCE_DIR}/src/ui/editor_layout.cpp"
    # The output preview's arithmetic (ADR-246). Here for the same reason `editor_layout.cpp` is:
    # the frame's geometry, the zoom, the safe areas and every coordinate conversion a click goes
    # through are ImGui-free, and a viewport rule that needs a window and a device to check is a
    # rule nobody checks. The drawing half lives in control_panel.cpp and is not on this list.
    "${CMAKE_SOURCE_DIR}/src/ui/output_preview.cpp"
    # The Help sidebar's navigation (ADR-361). Same reason again: the sidebar drew every recently
    # viewed topic a second time under its category, with the same id, and Dear ImGui said so at
    # runtime and nowhere else. The grouping is now data, it is ImGui-free, and a test asserts the
    # property that was violated. `help_panel.cpp` -- the drawing half -- is not on this list.
    "${CMAKE_SOURCE_DIR}/src/ui/help_sidebar.cpp"
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
    # The Lights panel's decisions, which are ImGui-free precisely so they can be linked here.
    "${CMAKE_SOURCE_DIR}/src/ui/lights_panel_logic.cpp"
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
    # Phase C's two selectors (ADR-122/123). Screen-space importance and the representation choice
    # are pure arithmetic over a scene and a camera -- no device, and deliberately so: the flicker
    # risk lives in the hysteresis and it has to be checkable without rendering anything.
    "${CMAKE_SOURCE_DIR}/src/rendering/importance.cpp"
    "${CMAKE_SOURCE_DIR}/src/rendering/representation.cpp"
    # The culling decision and its reason codes (Visibility / Culling Lab). Same shape and the same
    # reason as the three selectors above: whether an instance is drawn is arithmetic over a camera,
    # a bounding sphere and a set of limits, and the bug that put this file here -- a cull sphere
    # that did not contain the tree it was culling -- is provable from the asset's own bounds with
    # no device in the room. It is also compiled into avgen_gpu, where the renderer calls it.
    "${CMAKE_SOURCE_DIR}/src/rendering/visibility.cpp"
    # Phase D's third selector (ADR-133). Same shape and the same reason: the tier a drawable shades
    # at is pure arithmetic over an importance record and a policy.
    "${CMAKE_SOURCE_DIR}/src/rendering/material_tier.cpp"
    # The selected object's transform history (forensics 4.3/9.3): it reads the renderer's published
    # diagnosis, which is names and numbers, so it belongs where it can be checked without a device.
    "${CMAKE_SOURCE_DIR}/src/rendering/transform_history.cpp"
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
        "${CMAKE_SOURCE_DIR}/src/app/transport.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
        # ADR-383 split the range out of RenderSettings; the unit-test list gained it and this one
        # did not, so `avgen_render_tests` stopped linking. Not caught by a build of the default
        # target, which is why it is worth a line: this executable is only built on request, and
        # this is the second time a merge has dropped it. ADR-393 hit the third: a suite that cannot
        # start cannot report that it did not start, so build all three targets and check the
        # binaries' mtimes against the sources before trusting a green run (docs/testing.md).
        "${CMAKE_SOURCE_DIR}/src/app/frame_range.cpp"
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
    # The Quality Lab's metric engine, so the render -> capture -> analyse path can be checked
    # against frames the engine actually wrote rather than against synthetic ones. The unit ladder
    # proves the metrics respond to a distortion; only this proves they can read a real AOV.
    target_link_libraries(avgen_render_tests PRIVATE avgen_quality_metrics)
    if(APPLE)
        target_link_libraries(avgen_render_tests PRIVATE syphon::syphon "-framework Metal")
    endif()
    avgen_set_warnings(avgen_render_tests)
    # RESOURCE_LOCK, because there is one GPU. Running four GPU tests at once on a single device
    # does not make them finish sooner -- they contend for the same hardware -- and it makes the two
    # that measure *time* rather than pixels (the frame timeline's per-pass attribution, and the
    # non-stalling readback ring) fail intermittently while passing alone. That reads as flakiness
    # in the engine when it is contention in the harness. Unit tests still run in parallel alongside.
    #
    # What RESOURCE_LOCK does *not* do: it is scoped to this one ctest invocation's own scheduler, so
    # it only keeps this project's GPU tests from overlapping *each other*. It has no way to see a
    # second process on the same GPU that ctest did not launch -- another `ctest` run, a render, a
    # second agent's work in a sibling worktree (tools/gpu-lock.sh exists for exactly that case, and
    # a plain `ctest -j4` does not go through it). A test whose assertion is a wall-clock magnitude
    # or ratio is exposed to that no matter how tightly this file locks its own tests against each
    # other, so those tests are tagged `[.perf]` (Catch2's hidden-tag convention) rather than relied
    # on to be robust under RESOURCE_LOCK alone -- `[.perf]` tests never reach catch_discover_tests'
    # `--list-test-names-only` enumeration, so they are absent from `ctest` entirely rather than
    # present-and-flaky; see test_frame_profiler_gpu.cpp's header for which assertions qualify and
    # which don't. Run them deliberately and alone: `avgen_render_tests "[.perf]"`.
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

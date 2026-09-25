# Command-line tools that link the GPU-free core. They are build targets rather than scripts so a
# world can be inspected with exactly the code the engine samples, not a reimplementation of it.
add_executable(avgen_world_preview world_preview.cpp)
target_link_libraries(avgen_world_preview PRIVATE avgen_core)

# The Help documentation validator (help-system spec 35). A build target rather than a script
# because it reads the same content model the application does: a lint that reimplemented the
# parser would eventually disagree with it, and the disagreement would be silent.
add_executable(avgen_help_lint help_lint.cpp)
target_link_libraries(avgen_help_lint PRIVATE avgen_core)


# The viewport overlay, rasterised into a PNG with no window and no GPU (ADR-194).
#
# This repository states in two places that it cannot screenshot an ImGui frame, and the world
# editor is built around that -- every decision it makes is ImGui-free so it can be asserted rather
# than looked at. That is right for the decisions and useless for the drawing: "is the label on the
# character or off the side of the canvas" and "is a grid cell at alpha 46 visible at all" are not
# assertions. This runs the real `drawViewportOverlay` against a real scene and rasterises the draw
# list in software, so what comes out is the overlay itself and not a second implementation of it.
#
# It needs avgen_platform for the overlay and Dear ImGui, and engine.cpp for app::Engine -- which
# lives in the application's sources rather than in a library, the same reason avgen_tests compiles
# it in. No device is touched at runtime.
if(TARGET avgen_platform)
    add_executable(avgen_overlay_shot overlay_shot.cpp
        "${CMAKE_SOURCE_DIR}/src/app/engine.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/transport.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/settings.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
        # ADR-383 split the frame range out of RenderSettings, and `render_settings.cpp` above calls
        # three of its symbols. A target that lists one and not the other links fine until somebody
        # builds it -- which for these tools is rarely, and for `avgen_render_tests` was a day.
        # Third target to need this line; the other two are `avgen_render_tests` and
        # `avgen_metal_rt_probe`. They go together.
        "${CMAKE_SOURCE_DIR}/src/app/frame_range.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/control_hub.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/scene_states.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/world_director.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/music_runtime.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/placement.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/edit_system.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/camera_director.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/asset_browser.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/profile_capture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/job_system.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/world_builder.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ai_edit_sink.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/edit_capture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/viewport_camera.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/examples.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/output_manager.cpp")
    target_link_libraries(avgen_overlay_shot PRIVATE avgen_platform)
    avgen_set_warnings(avgen_overlay_shot)
endif()

# The farm-animal placer (ADR-209, Part 1). A build target rather than a script for the reason the
# Tree of Life emitter gives: it asks the engine's own questions of the world -- walkable, wet,
# steep, under canopy, inside a recorded solid -- against the real WorldMap, the real ecology
# scatter and the real per-instance obstacle field the scene builds at load. A Python placer would
# hold a second copy of the terrain noise and the two would drift apart silently.
add_executable(avgen_place_farm_animals place_farm_animals.cpp)
target_link_libraries(avgen_place_farm_animals PRIVATE avgen_core)

# The atmospheric preset dumper (ADR-230, §10). A build target rather than a table in the scene
# emitter for the reason the Tree of Life emitter gives: a Python copy of every tuned radiance would
# drift from applyCometStyle/applyAuroraStyle silently, and the first symptom would be a shipped
# example that no longer matches the preset the UI offers under the same name.
add_executable(avgen_atmos_presets atmos_presets.cpp)
target_link_libraries(avgen_atmos_presets PRIVATE avgen_core)

# TEMPORARY (docs/investigations/ui-responsiveness.md). Delete with the file.
add_executable(avgen_bench_flatten bench_flatten.cpp)
target_link_libraries(avgen_bench_flatten PRIVATE avgen_core)

# The Quality Lab (ADR-250): a measurement instrument for the renderer, kept out of the engine.
add_subdirectory(quality-lab)

# TEMPORARY (docs/character-ai-research.md, Phase 0). Delete with the file.
add_executable(avgen_charai_probe charai_probe.cpp)
target_link_libraries(avgen_charai_probe PRIVATE avgen_core)

# The offline motion pipeline (Phase A step 12). A build target rather than a script because it
# uses exactly the importer, analyser, retargeter and pack writer the engine uses: a
# reimplementation would eventually disagree with them, and the disagreement would be silent.
add_executable(avgen_motion motion_build.cpp)
target_link_libraries(avgen_motion PRIVATE avgen_core)

# TEMPORARY (docs/design/autonomous-character-animation.md, Phase A). The alien-leg IK probe:
# can the analytic solver reach and hold a target on a chain whose joints are not each other's
# ancestors? Delete it with the architecture it informs.
add_executable(avgen_motion_probe motion_probe.cpp)
target_link_libraries(avgen_motion_probe PRIVATE avgen_core)

# TEMPORARY (ADR-273). Delete with the phase it priced.
add_executable(avgen_seek_probe seek_probe.cpp)
target_link_libraries(avgen_seek_probe PRIVATE avgen_core)

# TEMPORARY (ADR-383, the audit's W6). The Metal ray-tracing DECISION, not a backend: does Metal
# beat Embree on an M2, where there is no hardware RT unit? Delete it with the decision it informs.
if(APPLE)
    # The same app/ sources `avgen_overlay_shot` lists, and for the same reason: `Engine` lives in
    # the executable's glob rather than in `avgen_core`, so anything outside the app that wants to
    # load a project has to name them.
    add_executable(avgen_metal_rt_probe metal_rt_probe.mm
        "${CMAKE_SOURCE_DIR}/src/app/engine.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/transport.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/settings.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/frame_range.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/control_hub.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/scene_states.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/world_director.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/music_runtime.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/placement.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/edit_system.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/camera_director.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/asset_browser.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/profile_capture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/job_system.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/world_builder.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/ai_edit_sink.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/edit_capture.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/viewport_camera.cpp"
        "${CMAKE_SOURCE_DIR}/src/app/examples.cpp"
        # `edit_system.cpp` and `ai_edit_sink.cpp` reach into the editor's undo stack, which lives
        # in `src/ui/`. Three files rather than linking `avgen_platform`, because that would drag
        # Dear ImGui and a window into a probe that wants neither.
        "${CMAKE_SOURCE_DIR}/src/ui/edit_history.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/world_edit.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/world_editor.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/world_probe.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/brush.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/gizmo.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/output_preview.cpp")
    set_source_files_properties(metal_rt_probe.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(avgen_metal_rt_probe PRIVATE avgen_core
        "-framework Metal" "-framework Foundation")
endif()

add_executable(avgen_slope_probe _slope_probe.cpp)
target_link_libraries(avgen_slope_probe PRIVATE avgen_core)


# Phase D §58/§64: simulate a scene headless and print one line per decision any character makes,
# plus the §41 "why" report on request. A permanent tool, not a probe: it is the offline half of
# "why is this character doing that", and its --repeat and --seek modes are the determinism checks.
add_executable(avgen_behavior_trace behavior_trace.cpp)
target_link_libraries(avgen_behavior_trace PRIVATE avgen_core)

# ADR-826 (Glowmere review build, item 9): simulate a scene or a project headless and report each
# character's motion and behaviour quality -- root pops, foot slip, stuck time, activity churn --
# as individual metrics in JSON. It drives the real `app::Engine`, so a project's Director and
# staging move the cast exactly as they do in the film; that needs the same app/ sources
# `avgen_metal_rt_probe` lists, for the same reason (`Engine` lives in the executable's glob), and
# the ui/ files that keep it free of Dear ImGui and a window.
add_executable(avgen_character_quality character_quality.cpp
    "${CMAKE_SOURCE_DIR}/src/app/engine.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/transport.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/recent_files.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/render_settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/frame_range.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/control_hub.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/scene_states.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/world_director.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/music_runtime.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/placement.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/edit_system.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/camera_director.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/asset_browser.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/profile_capture.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/job_system.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/world_builder.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/ai_edit_sink.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/edit_capture.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/viewport_camera.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/examples.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/edit_history.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/world_edit.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/world_editor.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/world_probe.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/brush.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/gizmo.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/output_preview.cpp")
target_link_libraries(avgen_character_quality PRIVATE avgen_core)

# Phase D §58: validate a scene's autonomous characters before runtime.
add_executable(avgen_character_validate character_validate.cpp)
target_link_libraries(avgen_character_validate PRIVATE avgen_core)

# ---- sanitizer runtimes ------------------------------------------------------------------------
# Every executable above links `avgen_core`, which is instrumented under `--preset asan` and
# `--preset tsan`. A tool that does not also link the sanitizer runtime fails with undefined
# `___asan_*` symbols -- and because ninja stops at the first failing target and these are ordered
# *before* `avgen_tests`, that broke the whole documented sanitizer workflow: `docs/testing.md`
# tells you to run `cmake --preset asan && cmake --build --preset asan && ctest --preset asan`, and
# the suite binary was never reached. Found by the QA pass of 2026-09-22, which went looking for an
# ownership audit and found it could not build one.
#
# Applied to every target in this directory rather than named one at a time, because the defect was
# exactly that thirteen of fourteen tools had been added without the call and a fourteenth would
# have been added the same way. `avgen_overlay_shot` already gets these through
# `avgen_set_warnings`; repeating a `-fsanitize` flag is idempotent.
get_property(avgen_tool_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(avgen_tool IN LISTS avgen_tool_targets)
    get_target_property(avgen_tool_type ${avgen_tool} TYPE)
    if(avgen_tool_type STREQUAL "EXECUTABLE")
        avgen_set_sanitizers(${avgen_tool})
    endif()
endforeach()

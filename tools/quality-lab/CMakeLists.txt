# The Quality Lab's metric engine (ADR-250). A `tools/` target, not a `src/` module: the Lab measures
# the engine and is not part of it, which is §49's principles 7 and 8 satisfied by construction
# rather than by discipline.
#
# Links `avgen_core` because that is where image I/O already lives (tinyexr, stb, `ImageF`/`Image8`).
# It adds no dependency to the engine and nothing in `src/` includes anything here.
add_library(avgen_quality_metrics STATIC
    metrics/spatial.cpp
    metrics/temporal.cpp
    capture/sequence.cpp
    artifacts/masks.cpp
    report/vector.cpp
    report/diagnostics.cpp
    external/vmaf.cpp
    validation/ladder.cpp)
target_include_directories(avgen_quality_metrics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(avgen_quality_metrics PUBLIC avgen_core)
avgen_set_warnings(avgen_quality_metrics)

# The CLI (docs/quality-lab/architecture.md §4). `render` and `experiment` are deliberately NOT
# subcommands: rendering is `avgen --project ... --render ...` and batching is `--queue`, both of
# which exist, are tested, and already write the determinism proof the Lab depends on.
add_executable(avgen_quality main.cpp)
target_link_libraries(avgen_quality PRIVATE avgen_quality_metrics)
avgen_set_warnings(avgen_quality)

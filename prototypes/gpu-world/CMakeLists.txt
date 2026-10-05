# GPU World research spike (docs/research/gpu-world-architecture-spike.md). Disposable; OFF by default.
add_executable(avgen_gpu_world_bench gpu_world_bench.cpp)
target_link_libraries(avgen_gpu_world_bench PRIVATE avgen_gpu)

# Phase 3 (art): three experimental visual systems on real audio. Same option, same rules.
add_executable(avgen_gpu_world_art gpu_world_art.cpp art_audio.cpp)
target_link_libraries(avgen_gpu_world_art PRIVATE avgen_gpu)

# Phase 4: one narrow procedural world held compact (generated per frame) vs expanded (records at load).
add_executable(avgen_gpu_world_rep gpu_world_rep.cpp)
target_link_libraries(avgen_gpu_world_rep PRIVATE avgen_gpu)
# Phase 4 control: the conventional meadow through production's real flatten (CPU only).
add_executable(avgen_gpu_world_flatten_probe phase4-conventional/flatten_probe.cpp)
target_link_libraries(avgen_gpu_world_flatten_probe PRIVATE avgen_core)

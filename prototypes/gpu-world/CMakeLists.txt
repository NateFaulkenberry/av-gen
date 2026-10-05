# GPU World research spike (docs/research/gpu-world-architecture-spike.md). Disposable; OFF by default.
add_executable(avgen_gpu_world_bench gpu_world_bench.cpp)
target_link_libraries(avgen_gpu_world_bench PRIVATE avgen_gpu)

# Phase 3 (art): three experimental visual systems on real audio. Same option, same rules.
add_executable(avgen_gpu_world_art gpu_world_art.cpp art_audio.cpp)
target_link_libraries(avgen_gpu_world_art PRIVATE avgen_gpu)

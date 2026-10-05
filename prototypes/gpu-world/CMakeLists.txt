# GPU World research spike (docs/research/gpu-world-architecture-spike.md). Disposable; OFF by default.
add_executable(avgen_gpu_world_bench gpu_world_bench.cpp)
target_link_libraries(avgen_gpu_world_bench PRIVATE avgen_gpu)

# Command-line tools that link the GPU-free core. They are build targets rather than scripts so a
# world can be inspected with exactly the code the engine samples, not a reimplementation of it.
add_executable(avgen_world_preview world_preview.cpp)
target_link_libraries(avgen_world_preview PRIVATE avgen_core)

# THE ASTRAL FORGE scene experiment (docs/prototypes/astral-forge/). Disposable prototype, OFF by default.
# Shaders are read at run time from the source tree, so they can be edited without a rebuild.
add_executable(avgen_astral_forge astral_forge.cpp astral_audio.cpp)
target_link_libraries(avgen_astral_forge PRIVATE avgen_gpu)
target_compile_definitions(avgen_astral_forge PRIVATE ASTRAL_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")

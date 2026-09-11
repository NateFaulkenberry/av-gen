# Command-line tools that link the GPU-free core. They are build targets rather than scripts so a
# world can be inspected with exactly the code the engine samples, not a reimplementation of it.
add_executable(avgen_world_preview world_preview.cpp)
target_link_libraries(avgen_world_preview PRIVATE avgen_core)

# The Help documentation validator (help-system spec 35). A build target rather than a script
# because it reads the same content model the application does: a lint that reimplemented the
# parser would eventually disagree with it, and the disagreement would be silent.
add_executable(avgen_help_lint help_lint.cpp)
target_link_libraries(avgen_help_lint PRIVATE avgen_core)

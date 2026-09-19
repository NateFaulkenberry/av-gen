cmake_minimum_required(VERSION 3.24)
project(embree_spike CXX)
# NOTE: CMAKE_CXX_STANDARD is deliberately NOT set globally. It leaks into Embree's
# targets and overrides Embree's own -std=c++11, which does not compile under clang 21.

include(FetchContent)
# Mirrors the pinned-tag CPM convention used by av-gen's cmake/Dependencies.cmake.
FetchContent_Declare(embree
  GIT_REPOSITORY https://github.com/RenderKit/embree.git
  GIT_TAG v4.4.0
  GIT_SHALLOW TRUE)
set(EMBREE_TASKING_SYSTEM INTERNAL CACHE STRING "" FORCE)
set(EMBREE_ISPC_SUPPORT OFF CACHE BOOL "" FORCE)
set(EMBREE_TUTORIALS OFF CACHE BOOL "" FORCE)
set(EMBREE_STATIC_LIB ON CACHE BOOL "" FORCE)
set(EMBREE_MAX_ISA NEON CACHE STRING "" FORCE)
set(EMBREE_GEOMETRY_QUAD OFF CACHE BOOL "" FORCE)
set(EMBREE_GEOMETRY_CURVE OFF CACHE BOOL "" FORCE)
set(EMBREE_GEOMETRY_SUBDIVISION OFF CACHE BOOL "" FORCE)
set(EMBREE_GEOMETRY_POINT OFF CACHE BOOL "" FORCE)
set(EMBREE_GEOMETRY_GRID OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(embree)

add_executable(spike spike.cpp)
target_link_libraries(spike PRIVATE embree)
target_compile_features(spike PRIVATE cxx_std_23)
add_executable(threads threads.cpp)
target_link_libraries(threads PRIVATE embree)
target_compile_features(threads PRIVATE cxx_std_23)

// Single translation unit that compiles the tinyexr implementation (OpenEXR read/write; ADR-020
// follow-up). Third-party code: src/CMakeLists.txt compiles this file with warnings off, so keep
// it free of engine code.

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#pragma once

// ADR-1493: material programs compiled to straight-line WGSL.
//
// The interpreter in shaders/material.wgsl runs a program as a loop over 48 op records: per op it reads the record from
// the storage buffer, tests the op's kind against every kind in turn, reads three registers through a select tree and
// writes one back through another. Its loop body carries every op's code at once, which also sets the shader's
// register allocation. On the Resonator scenes the programs were the largest share of the frame (Cymatic World's 41-op
// ground: 34 of 48 ms at 1080p, measured with the `noprograms` arm).
//
// A compiled program is the same evaluation with the structure written out: each op becomes one call of its kind's
// function (`matK<Kind>`, the code the interpreter runs for it) on named registers. What a route can move -- the
// constants, the scalar, the seed, the layer's intensity and blend range -- is still read from the buffer, so a
// compiled program follows every parameter without a recompile; only a change of structure (an op's kind, input or
// registers, the outputs, the layer ranges, the program count) needs a new variant. GPU-free, so the CPU suite checks
// the text it writes.

#include "scene/material_program.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace avgen::scene {

// What the compiled code depends on: every structural field of the packed programs (`count` of them). Equal keys,
// interchangeable variants. Never 0.
[[nodiscard]] std::uint64_t materialStructureKey(std::span<const scene::MaterialProgramGpu> programs);

// One program as a WGSL function `matCompiled<slot>(ctx, base) -> MaterialResult`, equal to
// `evaluateMaterialProgram(slot, ctx, base)` in shaders/material.wgsl.
[[nodiscard]] std::string compileMaterialProgramWgsl(const scene::MaterialProgramGpu& program, int slot);

// The text that replaces the `@@material-compiled-begin` .. `@@material-compiled-end` block of material.wgsl: every
// program's function and `evaluateMaterialProgramFast`, a switch on the slot with the interpreter as its default.
[[nodiscard]] std::string compiledMaterialLibraryWgsl(std::span<const scene::MaterialProgramGpu> programs);

// `source` (a resolved shader that includes material.wgsl) with its marker block replaced by `library`. Empty when the
// markers are missing.
[[nodiscard]] std::string spliceCompiledMaterials(const std::string& source, const std::string& library);

} // namespace avgen::scene

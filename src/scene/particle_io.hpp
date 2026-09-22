#pragma once

// A particle system's file representation, and nothing else.
//
// This is a pure translation between `ParticleSystem` and the `particles` object a scene file
// carries: no scene graph, no parameters, no registry. It lived in `composition.cpp`'s anonymous
// namespace until the QA pass of 2026-09-22, which is why it had no direct test -- an anonymous
// namespace is unreachable from a test binary, so the only coverage a ~450-line, 85-field
// serialiser had was whatever a whole-Composition round trip happened to exercise, and that turned
// out to be nothing. The extraction exists to make `tests/unit/test_particle_io.cpp` possible;
// the behaviour is unchanged.

#include "core/error.hpp"
#include "scene/particles.hpp"

#include <nlohmann/json.hpp>

namespace avgen::scene {

// The `particles` object a scene file stores for this system.
//
// **Many fields are elided at their default** (`softness`, `windInfluence`, the whole trail,
// collision, cluster, pulse and scatter groups, ...), which keeps an authored file to the lines a
// person actually set. That elision is only lossless while each writer's "is it default?" test and
// the reader's fallback for the same key name the same number, and nothing but a round trip checks
// that they still agree -- so a change to a default in `particles.hpp` has to be made in three
// places at once, and `tests/unit/test_particle_io.cpp` is what notices when it is not.
[[nodiscard]] nlohmann::json particlesToJson(const ParticleSystem& s);

// The inverse. Every field is optional and falls back to `ParticleSystem`'s own default, so a file
// that predates a field reads as that field's default rather than as a failure. A field that is
// present but of the wrong JSON type, or an enum spelling that is not one of the known names, is
// an error naming the key -- never a silent fallback to the default.
[[nodiscard]] Result<ParticleSystem> particlesFromJson(const nlohmann::json& j);

} // namespace avgen::scene

#pragma once

// Parameters for a scene field (ADR-025), the same rest/finals pattern as particles and
// procedural objects: registerFieldParameters() registers "<prefix><name>" parameters for every
// modulatable FieldSpec member, applyFieldParameters() copies finals into a live copy each frame.
// Kind, space, wave geometry/shape, combine mode, children and the reference are structural and
// come from the file. Group = prefix without the trailing '/'.
//
// Paths (relative to the prefix, e.g. "field/pulse/"): enabled, position, rotation, scale,
// strength, invert, speed, phase, axis, point, radius, length, size, softness, frequency,
// spiralBias, amplitude, wavelength, waveSpeed, waveWidth, waveOrigin, colorA, colorB, mix,
// falloff/inner, falloff/outer, falloff/exponent, falloff/noiseAmount, falloff/noiseScale.

#include "params/parameter_set.hpp"
#include "spatial/field.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

struct FieldParameters {
    std::string prefix;
    std::vector<params::IParameter*> all;
    params::Parameter<bool>* enabled = nullptr;
    params::Parameter<glm::vec3>* position = nullptr;
    params::Parameter<glm::vec3>* rotation = nullptr; // Euler degrees
    params::Parameter<glm::vec3>* scale = nullptr;
    params::Parameter<float>* strength = nullptr;
    params::Parameter<float>* speed = nullptr;
    params::Parameter<float>* radius = nullptr;
    params::Parameter<float>* frequency = nullptr;
    params::Parameter<float>* amplitude = nullptr;
    params::Parameter<float>* falloffOuter = nullptr;
    params::Parameter<glm::vec4>* colorA = nullptr;
    params::Parameter<glm::vec4>* colorB = nullptr;
};

[[nodiscard]] FieldParameters registerFieldParameters(params::ParameterSet& params, const spatial::FieldSpec& rest,
                                                      const std::string& prefix);
// Copies finals into `live` (name/kind/space/children/reference come from `rest`).
void applyFieldParameters(const FieldParameters& p, const spatial::FieldSpec& rest, spatial::FieldSpec& live);
void unregisterFieldParameters(params::ParameterSet& params, const FieldParameters& p);

} // namespace avgen::scene

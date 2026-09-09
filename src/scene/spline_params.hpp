#pragma once

// Parameters for a scene spline node (ADR-026), rest/finals pattern. Structural members (kind,
// generator, closed, explicit points, count) come from the file; the generator's continuous
// members are parameters: "<prefix>radius|radiusGrowth|turns|height|startAngle|noiseAmount|
// noiseScale|start|end|center|axis|up|p0..p3|tension".

#include "params/parameter_set.hpp"
#include "spatial/spline.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

struct SplineParameters {
    std::string prefix;
    std::vector<params::IParameter*> all;
    params::Parameter<float>* radius = nullptr;
    params::Parameter<float>* turns = nullptr;
    params::Parameter<float>* height = nullptr;
    params::Parameter<float>* noiseAmount = nullptr;
    params::Parameter<int>* count = nullptr; // structural
};
[[nodiscard]] SplineParameters registerSplineParameters(params::ParameterSet& params, const spatial::Spline& rest,
                                                        const std::string& prefix);
void applySplineParameters(const SplineParameters& p, const spatial::Spline& rest, spatial::Spline& live);
void unregisterSplineParameters(params::ParameterSet& params, const SplineParameters& p);

} // namespace avgen::scene

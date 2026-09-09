#pragma once

// Parameters for a scene material program (ADR-030), rest/finals pattern: "<prefix>emissionIntensity"
// and "<prefix>op/<i>/value|constant|constant2|constant3|constant4|enabled" (i 1-based, every op;
// kinds, registers, inputs and field names are structural and come from the file).

#include "params/parameter_set.hpp"
#include "scene/material_program.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

struct MaterialProgramParameters {
    std::string prefix;
    std::vector<params::IParameter*> all;
    params::Parameter<float>* emissionIntensity = nullptr;
    std::vector<params::Parameter<float>*> opValue;
};
[[nodiscard]] MaterialProgramParameters registerMaterialProgramParameters(params::ParameterSet& params,
                                                                          const MaterialProgram& rest,
                                                                          const std::string& prefix);
void applyMaterialProgramParameters(const MaterialProgramParameters& p, const MaterialProgram& rest,
                                    MaterialProgram& live);
void unregisterMaterialProgramParameters(params::ParameterSet& params, const MaterialProgramParameters& p);

} // namespace avgen::scene

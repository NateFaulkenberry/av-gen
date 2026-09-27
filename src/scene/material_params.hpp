#pragma once

// Parameters for a scene material program (ADR-030), rest/finals pattern: "<prefix>emissionIntensity"
// (the base's emission; registered only when the base writes one, ADR-905),
// "<prefix>layer/<i>/<name>/emissionIntensity" (each emitting layer's, ADR-905) and
// "<prefix>op/<i>/<kind>/value|constant|constant2|constant3|constant4|enabled" (i 1-based,
// every base op; kinds, registers, inputs and field names are structural and come from the file).
//
// ADR-232: the op segment carries the op's *kind* as well as its index. A material program is a
// file and a project is a table of values for it, and the two are edited apart -- inserting an op
// into the file shifts every op after it, and a purely positional path let a project saved before
// that edit write its old values silently onto the wrong ops. With the kind in the path a shifted
// value lands on a path nobody registered, so the project loader drops it with a warning and the
// file's own value stands, which is the right answer when the two disagree about the structure.

#include "params/parameter_set.hpp"
#include "scene/material_program.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

struct MaterialProgramParameters {
    std::string prefix;
    std::vector<params::IParameter*> all;
    params::Parameter<float>* emissionIntensity = nullptr;
    // ADR-905: "<prefix>layer/<i>/<name>/emissionIntensity", one per layer (i 1-based, the name
    // in the path for ADR-232's reason).
    std::vector<params::Parameter<float>*> layerEmissionIntensity;
    std::vector<params::Parameter<float>*> opValue;
};
[[nodiscard]] MaterialProgramParameters registerMaterialProgramParameters(params::ParameterSet& params,
                                                                          const MaterialProgram& rest,
                                                                          const std::string& prefix);
void applyMaterialProgramParameters(const MaterialProgramParameters& p, const MaterialProgram& rest,
                                    MaterialProgram& live);
void unregisterMaterialProgramParameters(params::ParameterSet& params, const MaterialProgramParameters& p);

} // namespace avgen::scene

#pragma once

// ADR-1071: a material's cel ("toon") lighting -- its scene-file block and its parameters.
//
// The block is the material's `"toon"` object, wherever a material is authored: a procedural node's
// `material`, an SDF object's `material`. Every key is optional and defaults to ToonShading's value:
//
//   "toon": {"bands": 3, "softness": 0.02, "terminator": 0.0,
//            "shadowColor": [0.45, 0.4, 0.7], "ambient": 0.3,
//            "rimWidth": 0.25, "rimColor": [1, 1, 1], "rimIntensity": 1.5,
//            "specular": 1.0, "specularSize": 0.08}
//
// The parameters sit under the owner's prefix, `<prefix>toon/<key>` -- "procedural/ground/toon/bands"
// -- so a route or a timeline lane can move any of them, and they are registered whether or not the
// file authored a block, so a person can turn cel lighting on from the Parameters panel (the owner's
// rule: anything visible is controllable, and that includes switching the look on).

#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::params {
class IParameter;
class ParameterSet;
template <typename T>
class Parameter;
} // namespace avgen::params

namespace avgen::scene {

// Reads a `"toon"` object into `out` (keys it does not name keep their value). An unknown key, a
// wrong type or a non-finite number is an error naming the key.
[[nodiscard]] Result<void> readToonShading(const nlohmann::json& j, ToonShading& out);
// Every field, so a written block reads back exactly.
[[nodiscard]] nlohmann::json toonShadingToJson(const ToonShading& t);
// True when `t` is ToonShading{} field for field: such a material writes no block.
[[nodiscard]] bool toonShadingIsDefault(const ToonShading& t);

struct ToonParameters {
    params::Parameter<float>* bands = nullptr;
    params::Parameter<float>* softness = nullptr;
    params::Parameter<float>* terminator = nullptr;
    params::Parameter<glm::vec3>* shadowColor = nullptr;
    params::Parameter<float>* ambient = nullptr;
    params::Parameter<float>* rimWidth = nullptr;
    params::Parameter<glm::vec3>* rimColor = nullptr;
    params::Parameter<float>* rimIntensity = nullptr;
    params::Parameter<float>* specular = nullptr;
    params::Parameter<float>* specularSize = nullptr;
};

// Registers `<prefix>toon/<key>` for every field, with `rest` as the defaults, in `group`. Each
// parameter is also appended to `all` when it is not null (the owner's unregister list). The label
// is "toon/<key>", so the Parameters panel draws them as one "toon" section of the owner.
[[nodiscard]] ToonParameters registerToonParameters(params::ParameterSet& params, const std::string& prefix,
                                                    const std::string& group, const ToonShading& rest,
                                                    std::vector<params::IParameter*>* all);
// Copies the parameters' final values into `live`. Null handles leave their field alone.
void applyToonParameters(const ToonParameters& p, ToonShading& live);

} // namespace avgen::scene

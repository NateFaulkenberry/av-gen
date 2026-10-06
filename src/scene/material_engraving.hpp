#pragma once

// ADR-1152: a material's guilloche engraving -- its scene-file block and its parameters. The struct and
// what each field means are in scene/scene_types.hpp (`Engraving`, `EngravingLayer`).
//
//   "material": { ..., "engraving": {
//       "depth": 0.3, "crawl": 0.03, "grating": 1.0, "spacing": 1600, "panels": 0.55,
//       "layers": [
//         {"family": "rosette", "center": [-0.5, 0.38, 0.5], "petals": 12, "frequency": 14,
//          "weight": 1, "inner": 0.12, "outer": 0.55, "depth": 0.35},
//         {"family": "contour", "center": [0, 0.2, -3], "petals": 7, "frequency": 7},
//         {"family": "engine", "axis": [0.94, 0.30, 0.17], "frequency": 9, "weight": 0.25} ] } }
//
// An SDF object's material may carry it (the raymarch draws it with its engraved pipeline variant);
// any other owner refuses the block by name, because nothing else would draw it. Absent is the default
// and the default is off: no parameter, no variant, the object's module exactly as it was.
//
// Parameters (registered only when the block is present, all uniforms -- no rebuild):
//   <prefix>material/engraving/{depth, crawl, grating, spacing, panels}
//   <prefix>material/engraving/<k>/{frequency, weight}

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

// Reads an `"engraving"` object. Unknown keys, wrong types, non-finite numbers, an unknown family,
// more than kMaxEngravingLayers layers, a zero axis or a frequency <= 0 are refused by name.
[[nodiscard]] Result<Engraving> readEngraving(const nlohmann::json& j);
// Every field of every layer, so a written block reads back exactly. Only for an enabled engraving.
[[nodiscard]] nlohmann::json engravingToJson(const Engraving& e);
[[nodiscard]] const char* engravingFamilyName(EngravingFamily f);

struct EngravingParameters {
    params::Parameter<float>* depth = nullptr;
    params::Parameter<float>* crawl = nullptr;
    params::Parameter<float>* grating = nullptr;
    params::Parameter<float>* spacing = nullptr;
    params::Parameter<float>* panels = nullptr;
    struct Layer {
        params::Parameter<float>* frequency = nullptr;
        params::Parameter<float>* weight = nullptr;
    };
    std::vector<Layer> layers;
};

[[nodiscard]] EngravingParameters registerEngravingParameters(params::ParameterSet& params, const std::string& prefix,
                                                              const std::string& group, const Engraving& rest,
                                                              std::vector<params::IParameter*>* all);
void applyEngravingParameters(const EngravingParameters& p, Engraving& live);

// ---- the line field on the CPU (tests) -----------------------------------------------------------------
//
// The surface coordinates (u, v) of layer `layer` at object-space point `q` at time `time`, as the shader's
// `engraveUv` computes them; `amp` is the wave's height (it fades to 0 at a rosette's centre).
struct EngravingUv {
    float u = 0.0f;
    float v = 0.0f;
    float petals = 1.0f;
    float amp = 1.0f;
    bool angular = false;
};
[[nodiscard]] EngravingUv engravingUv(const EngravingLayer& layer, glm::vec3 q, float crawlPhase);

// ---- ADR-1149: per-region temper and polish --------------------------------------------------------------
//
//   "material": { ..., "regions": {"film": 120, "filmNoise": 60, "noiseScale": 0.9, "polish": 0.7,
//       "points": [{"center": [-0.76, 0.72, 0.6], "sharpness": 4, "weight": 1},
//                  {"center": [0, -1.45, 0.62], "scale": [0.8, 2, 1], "sharpness": 3, "weight": 0.6}]} }
//
// SDF objects only (refused by name elsewhere). Unknown keys, wrong types, more than kMaxSurfaceRegions points,
// negative film, polish outside 0..1, a weight outside 0..1 or a sharpness <= 0 are refused by name.
// Parameters (only when present): <prefix>material/regions/{film, filmNoise, polish}.
[[nodiscard]] Result<SurfaceRegions> readSurfaceRegions(const nlohmann::json& j);
[[nodiscard]] nlohmann::json surfaceRegionsToJson(const SurfaceRegions& r);
// The weight fw at the domain point q (the shader's sdfEngrave, for tests).
[[nodiscard]] float surfaceRegionWeight(const SurfaceRegions& r, glm::vec3 q);

struct SurfaceRegionParameters {
    params::Parameter<float>* film = nullptr;
    params::Parameter<float>* filmNoise = nullptr;
    params::Parameter<float>* polish = nullptr;
};
[[nodiscard]] SurfaceRegionParameters registerSurfaceRegionParameters(params::ParameterSet& params,
                                                                      const std::string& prefix,
                                                                      const std::string& group,
                                                                      const SurfaceRegions& rest,
                                                                      std::vector<params::IParameter*>* all);
void applySurfaceRegionParameters(const SurfaceRegionParameters& p, SurfaceRegions& live);

} // namespace avgen::scene

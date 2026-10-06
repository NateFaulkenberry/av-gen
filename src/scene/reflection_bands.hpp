#pragma once

// ADR-1151: reflection-only light bands -- an environment term made of strip and ring lights in
// DIRECTION space, after THE ASTRAL FORGE prototype's `env()` (prototypes/astral-forge/shaders/
// lighting.wgsl). A band is seen only where a surface reflects it: it lights no diffuse term, it is
// not drawn behind the world, and a black background stays black. It is the light of product
// photography of black chrome -- a few crisp strips and one broad, dim soft box -- and it is what makes
// a dark conductor read as metal rather than as a hole.
//
//   "environment": { "bands": {
//       "phase": 0.0, "rotation": 0.0, "gain": 1.0,
//       "softbox": {"intensity": 0.55, "azimuth": 0.8, "elevation": 0.5, "falloff": 2.6, "skyFill": 0.08},
//       "strips": [ {"axis": [0, 1, 0], "offset": 0.35, "width": 0.035, "intensity": 2.4,
//                    "segments": 0, "warmth": 0.8, "rate": 0.07}, ... up to 4 ] } }
//
// A strip is the set of directions `dir` with dot(dir, axis) = offset: a great circle at offset 0, a
// latitude ring otherwise. Its profile across that line is a Gaussian of angular width `width`,
// convolved with the surface's roughness (`alpha`) so a rough surface sees a wider, dimmer band with
// the same energy. `segments` > 0 cuts the ring into that many dashes, which turn about the axis as
// `phase` advances (at `rate` turns per unit of phase), so a route on `phase` sweeps highlights over a
// still form. `rotation` turns the whole rig about world +Y. `warmth` 0 is a cool (bluish) strip, 1 a
// warm (amber) one. The soft box is a broad lobe toward (azimuth, elevation) plus a faint upward fill.
//
// Absent is the default, and the default is off: no parameter is registered, the frame lanes are zero
// and the lit shader's gate is never taken, so every existing scene renders exactly as it did.

#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <string>
#include <vector>

namespace avgen::params {
class IParameter;
class ParameterSet;
template <typename T>
class Parameter;
} // namespace avgen::params

namespace avgen::scene {

// The structs (ReflectionBand, ReflectionSoftbox, ReflectionBands) are in scene/scene_types.hpp.

// Reads an `environment.bands` object. Unknown keys, wrong types, non-finite numbers, more than
// kMaxReflectionBands strips, a zero axis, a width <= 0 or a negative intensity are refused by name.
[[nodiscard]] Result<ReflectionBands> readReflectionBands(const nlohmann::json& j);
// Every field, so a written block reads back exactly. Called only for an enabled block.
[[nodiscard]] nlohmann::json reflectionBandsToJson(const ReflectionBands& b);

struct ReflectionBandParameters {
    params::Parameter<float>* phase = nullptr;
    params::Parameter<float>* rotation = nullptr;
    params::Parameter<float>* gain = nullptr;
    params::Parameter<float>* softboxIntensity = nullptr;
    params::Parameter<float>* softboxAzimuth = nullptr;
    params::Parameter<float>* softboxElevation = nullptr;
    struct Strip {
        params::Parameter<float>* intensity = nullptr;
        params::Parameter<float>* offset = nullptr;
        params::Parameter<float>* width = nullptr;
        params::Parameter<float>* warmth = nullptr;
    };
    std::vector<Strip> strips;
};

// Registers `<prefix>bands/{phase,rotation,gain}`, `<prefix>bands/softbox/{intensity,azimuth,elevation}`
// and `<prefix>bands/<k>/{intensity,offset,width,warmth}` with `rest` as the defaults. Nothing is
// registered for a block that is not enabled. Every parameter is a uniform: no rebuild.
[[nodiscard]] ReflectionBandParameters registerReflectionBandParameters(params::ParameterSet& params,
                                                                        const std::string& prefix,
                                                                        const ReflectionBands& rest,
                                                                        std::vector<params::IParameter*>* all);
// Copies the parameters' values into `live` (null handles leave their field alone).
void applyReflectionBandParameters(const ReflectionBandParameters& p, ReflectionBands& live);

// ---- the packed frame lanes, and the CPU twin of the shader -----------------------------------------
//
// info:  x = strips, y = phase, z = gain, w = rotation (radians)
// soft:  xyz = the soft box's key direction, w = its intensity
// soft2: x = falloff, y = sky fill, z = 0, w = 1 when the block is on
// rate:  the strips' dash rates
// a[k]:  axis xyz (unit), offset;  b[k]: width, intensity, segments, warmth
struct ReflectionBandLanes {
    glm::vec4 info{0.0f};
    glm::vec4 soft{0.0f};
    glm::vec4 soft2{0.0f};
    glm::vec4 rate{0.0f};
    std::array<glm::vec4, 2 * kMaxReflectionBands> bands{};
};
// All zero for a block that is not enabled.
[[nodiscard]] ReflectionBandLanes packReflectionBands(const ReflectionBands& b);

// The radiance the bands put in direction `dir` (unit, world) for a lobe of angular width `alpha`
// (the GGX alpha of the surface reflecting it). `softbox` false evaluates the strips alone: the
// flakes (ADR-1153) use that, so a small plate is dark unless it catches a strip. The WGSL is
// `reflectionBands` in shaders/reflection_bands.wgsl, line for line.
[[nodiscard]] glm::vec3 reflectionBandRadiance(const ReflectionBandLanes& lanes, glm::vec3 dir, float alpha,
                                               bool softbox);

} // namespace avgen::scene

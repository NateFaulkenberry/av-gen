#pragma once

// ADR-1073: a surface's edges drawn as lines (the Neon Vector look) -- the settings a material carries.
//
// `mode` 0 (the default) is off. 1 draws the FEATURE edges: every boundary edge, and every edge whose two
// faces meet at more than `crease` degrees (a cube's twelve, a low-poly sphere's facets past the angle).
// 2 draws every triangle edge. The lines are screen-space quads `width` pixels wide at 1080 lines, coloured
// `color` x `intensity` in scene-linear light (above 1 they glow and bloom; they also write the emission
// target), blended over the frame at `opacity`. `occlude` 1 hides a line behind any surface, 0 draws it
// through everything. `fill` 0 stops the surface itself from drawing (in every pass: no depth, no shadow),
// leaving only its lines.
//
// The edge list is extracted from the source mesh on the CPU (scene/wire_edges.hpp) and drawn by a vertex
// entry in the surface's own shader module, so a line runs exactly the deformer chain, instancing, wind and
// effect displacement the surface it sits on runs.

#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>
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

// WireLines itself is in scene/scene_types.hpp, beside Material.

// ---- the file block and the parameters (wire_lines.cpp) ----------------------------------------------
//
//   "wire": {"mode": 1, "crease": 30, "color": [0.2, 1, 0.9], "intensity": 2, "opacity": 1, "width": 1.5,
//            "fill": 1, "occlude": 1}
//
// inside a procedural node's `material`. Parameters `<prefix>wire/<key>`, registered for every procedural
// whether or not its file has a block, so the look can be switched on from the panel.

[[nodiscard]] Result<void> readWireLines(const nlohmann::json& j, WireLines& out);
[[nodiscard]] nlohmann::json wireLinesToJson(const WireLines& w);
[[nodiscard]] bool wireLinesIsDefault(const WireLines& w);

struct WireParameters {
    params::Parameter<float>* mode = nullptr;
    params::Parameter<float>* crease = nullptr;
    params::Parameter<glm::vec3>* color = nullptr;
    params::Parameter<float>* intensity = nullptr;
    params::Parameter<float>* opacity = nullptr;
    params::Parameter<float>* width = nullptr;
    params::Parameter<float>* fill = nullptr;
    params::Parameter<float>* occlude = nullptr;
};

[[nodiscard]] WireParameters registerWireParameters(params::ParameterSet& params, const std::string& prefix,
                                                    const std::string& group, const WireLines& rest,
                                                    std::vector<params::IParameter*>* all);
void applyWireParameters(const WireParameters& p, WireLines& live);

} // namespace avgen::scene

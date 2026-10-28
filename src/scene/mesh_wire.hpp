#pragma once

// ADR-1862: a mesh entity's own triangle edges, revealed as lines -- the generic half of the Strange Attractor's
// "hidden mathematics" (canyon brief §5), and the mesh-entity counterpart ADR-1073 left open ("mesh entities do not
// get lines yet").
//
// The lines are the mesh's real topology, read on the GPU straight from its vertex and index buffers: no edge list
// is extracted or stored, so a 26 M-triangle terrain costs nothing until the effect is on. Each triangle draws the
// three edges it owns (an interior edge is drawn by exactly one of its two triangles, chosen by its endpoints'
// order, so nothing doubles -- across a chunk seam too).
//
// What a line shows is restrained by construction (brief §5.3):
//   * a STRUCTURE weight per edge, from how far the face turns away from the smooth surface at that edge (the face
//     normal against the edge's vertex normals): ridges and folds weigh 1, a flat diagonal 0. `reveal` 0 shows only
//     the structure; 1 shows every triangle.
//   * a distance fade (`fadeNear`..`fadeFar`) and a DENSITY fade: an edge shorter on screen than `minPixels` fades
//     out, so distant dense geometry never piles into a solid luminous mass.
//   * a travelling WAVE: `wave` is an envelope (1 at a musical event, decaying to 0) and the front sits at
//     (1 - wave) x `waveRadius` from a point `waveAhead` metres in front of the camera; every edge it crosses lights
//     up, structure or not, then the rock returns. That is the localised dissolution (Mode C) without touching the
//     geometry.
//   * lines are added light, depth-tested against the surface they lie on (pulled a fraction toward the eye), never
//     written to depth, fogged like the ground behind them.
//
// File block (top level of a scene file):
//
//   "meshWire": {"targets": ["attractor.terrain"], "color": [1, 0.6, 0.3], "intensity": 0.4, "width": 1.0,
//                "creaseLow": 4, "creaseHigh": 30, "reveal": 0, "fadeNear": 150, "fadeFar": 1500, "minPixels": 10,
//                "wave": 0, "waveRadius": 900, "waveWidth": 50, "waveGain": 4, "waveAhead": 300,
//                "clipBelow": -0.5}
//
// `targets` are entity-name prefixes. Every other key is a parameter `meshWire/<key>` (routes, timeline, panel).

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::params {
class IParameter;
class ParameterSet;
} // namespace avgen::params

namespace avgen::scene {

struct MeshWire {
    std::vector<std::string> targets; // entity-name prefixes; empty = none
    glm::vec3 color{1.0f, 0.62f, 0.32f};
    float intensity = 0.0f;   // the structure lines' radiance multiplier (scene-linear); 0 = only the wave draws
    float opacity = 1.0f;     // multiplies everything
    float width = 1.0f;       // pixels at 1080 lines
    float creaseLow = 4.0f;   // degrees of face turn at which an edge starts to count as structure
    float creaseHigh = 30.0f; // ... and fully counts
    float reveal = 0.0f;      // 0 structure only .. 1 every triangle edge
    float fadeNear = 150.0f;  // metres: full strength inside
    float fadeFar = 1500.0f;  // metres: gone beyond (and nothing past it is drawn at all)
    float minPixels = 10.0f;  // an edge shorter on screen than this fades out (density control)
    float wave = 0.0f;        // the travelling front's envelope (1 just triggered .. 0 spent)
    float waveRadius = 900.0f;
    float waveWidth = 50.0f;
    float waveGain = 4.0f;    // the front's radiance multiplier
    float waveAhead = 300.0f; // metres in front of the camera, horizontally, where the front starts
    float clipBelow = -1.0e9f; // world height: an edge with an end below it is not drawn (a mesh under water); lines
                               // fade in over the 3 m above it

    // Whether anything can be drawn this frame: the renderer's gate (nothing bound, nothing drawn when false).
    [[nodiscard]] bool active() const {
        return !targets.empty() && opacity > 0.0f && (intensity > 0.0f || (wave > 0.0f && waveGain > 0.0f));
    }
};

// True when `name` starts with one of `w.targets`.
[[nodiscard]] bool meshWireTargets(const MeshWire& w, const std::string& name);

[[nodiscard]] Result<MeshWire> meshWireFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json meshWireToJson(const MeshWire& w);

struct MeshWireParameters {
    std::vector<params::IParameter*> all;
};
[[nodiscard]] MeshWireParameters registerMeshWireParameters(params::ParameterSet& params, const MeshWire& rest,
                                                            const std::string& prefix);
void applyMeshWireParameters(const MeshWireParameters& p, const MeshWire& rest, MeshWire& live);

} // namespace avgen::scene

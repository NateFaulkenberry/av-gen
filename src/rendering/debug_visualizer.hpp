#pragma once

// Builds debug geometry from a Scene according to the view options (ADR-031). Pure: it reads the
// scene and writes lines and points into a DebugDraw, so it is testable without a GPU.

#include "rendering/debug_draw.hpp"
#include "rendering/transform_history.hpp"
#include "scene/scene.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace avgen::rendering {

// What `options.lod` colours by: the rung the cull pass assigned every record of a procedural
// object, keyed by the object's name. -1 is a record the pass rejected.
//
// A parameter for the same reason `history` is one. The rung lives in a GPU buffer, reading it is a
// blocking readback, and the decision about when that is worth doing belongs to whoever owns the
// frame. It also lets the overlay be honest about not knowing: an object absent from the map is
// drawn in the "no decision available" colour rather than in rung 0's, which is what an overlay
// that silently defaulted would do -- and rung 0 is the most reassuring answer it could give.
using ProceduralLodLevels = std::unordered_map<std::string, std::vector<int>>;

// Appends the enabled visualisations. `cameraPosition` scales screen-relative sizes.
//
// `history` is the recorded path behind `options.transformTrail` (Phase 4.3). It is a parameter
// rather than a member because the builder stays pure: a trail is state accumulated across frames,
// and the thing that owns the frames is the thing that should own it.
void buildDebugGeometry(DebugDraw& draw, const scene::Scene& scene, const DebugViewOptions& options, double time,
                        const TransformHistory* history = nullptr,
                        const ProceduralLodLevels* lodLevels = nullptr);

} // namespace avgen::rendering

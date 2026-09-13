#pragma once

// Builds debug geometry from a Scene according to the view options (ADR-031). Pure: it reads the
// scene and writes lines and points into a DebugDraw, so it is testable without a GPU.

#include "rendering/debug_draw.hpp"
#include "rendering/transform_history.hpp"
#include "scene/scene.hpp"

namespace avgen::rendering {

// Appends the enabled visualisations. `cameraPosition` scales screen-relative sizes.
//
// `history` is the recorded path behind `options.transformTrail` (Phase 4.3). It is a parameter
// rather than a member because the builder stays pure: a trail is state accumulated across frames,
// and the thing that owns the frames is the thing that should own it.
void buildDebugGeometry(DebugDraw& draw, const scene::Scene& scene, const DebugViewOptions& options, double time,
                        const TransformHistory* history = nullptr);

} // namespace avgen::rendering

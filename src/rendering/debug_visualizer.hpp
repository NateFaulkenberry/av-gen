#pragma once

// Builds debug geometry from a Scene according to the view options (ADR-031). Pure: it reads the
// scene and writes lines and points into a DebugDraw, so it is testable without a GPU.

#include "rendering/debug_draw.hpp"
#include "scene/scene.hpp"

namespace avgen::rendering {

// Appends the enabled visualisations. `cameraPosition` scales screen-relative sizes.
void buildDebugGeometry(DebugDraw& draw, const scene::Scene& scene, const DebugViewOptions& options, double time);

} // namespace avgen::rendering

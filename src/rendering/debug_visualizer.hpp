#pragma once

// Builds debug geometry from a Scene according to the view options (ADR-031). Pure: it reads the
// scene and writes lines and points into a DebugDraw, so it is testable without a GPU.

#include "rendering/debug_draw.hpp"
#include "rendering/shadow_math.hpp"
#include "rendering/transform_history.hpp"
#include "scene/scene.hpp"

#include <span>

namespace avgen::rendering {

// Appends the enabled visualisations. `cameraPosition` scales screen-relative sizes.
//
// `history` is the recorded path behind `options.transformTrail` (Phase 4.3). It is a parameter
// rather than a member because the builder stays pure: a trail is state accumulated across frames,
// and the thing that owns the frames is the thing that should own it.
// `shadowViews` is the list `ShadowRenderer::views()` returned for the frame being drawn over, and
// it is a parameter for the reason `history` is: the cascades are state the renderer produced, and
// the thing that owns them should hand them over rather than have the builder fit its own. An
// empty span turns the two shadow overlays into nothing, which is the honest picture of a frame
// that has no shadow views.
void buildDebugGeometry(DebugDraw& draw, const scene::Scene& scene, const DebugViewOptions& options, double time,
                        const TransformHistory* history = nullptr,
                        std::span<const ShadowView> shadowViews = {});

} // namespace avgen::rendering

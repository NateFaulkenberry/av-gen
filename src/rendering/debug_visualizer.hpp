#pragma once

// Builds debug geometry from a Scene according to the view options (ADR-031). Pure: it reads the
// scene and writes lines and points into a DebugDraw, so it is testable without a GPU.

#include "rendering/debug_draw.hpp"
#include "rendering/shadow_math.hpp"
#include "rendering/transform_history.hpp"
#include "scene/scene.hpp"

#include <span>
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
class ProceduralRenderer;

using ProceduralLodLevels = std::unordered_map<std::string, std::vector<int>>;

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
                        const ProceduralLodLevels* lodLevels = nullptr,
                        std::span<const ShadowView> shadowViews = {});

// Reads each visible procedural's rung buffer back, for `options.lod`. Empty when the overlay is
// off, which is what makes calling it unconditionally cheap.
//
// Free rather than a member of whoever owns the frame, because there are two such owners -- the
// live window and `RenderJob` -- and while this lived as a private method of the first, the second
// called `buildDebugGeometry` with four arguments and got no rung overlay at all. `--debug-draw`
// is reached from the command line, which is the offline path by definition, so the overlay that
// most needed to be reachable from a file was the one that was not.
[[nodiscard]] ProceduralLodLevels readProceduralLodLevels(ProceduralRenderer& procedurals,
                                                          const scene::Scene& scene,
                                                          const DebugViewOptions& options);

} // namespace avgen::rendering

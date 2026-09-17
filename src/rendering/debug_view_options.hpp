#pragma once

// Which visualisations to build from the scene each frame (ADR-031).
//
// Split out of `debug_draw.hpp` for the reason `renderer_diagnostics.hpp` was split out of
// `scene_renderer.hpp`: nothing here needs a device. These are bools, floats and a string, and
// keeping them behind an include of `gpu/context.hpp` meant that anything wanting to *choose* a set
// of overlays -- `labs/overlays.hpp`, which has no business linking Dawn -- had to link the
// renderer to name them. `debug_draw.hpp` includes this, so every existing include still compiles.

#include <string>

namespace avgen::rendering {

// Which visualisations to build from the scene each frame.
struct DebugViewOptions {
    bool points = false;          // instance origins of procedural objects
    bool bounds = false;          // per-object bounds and SDF bounds
    bool density = false;         // colour points by density
    std::string attribute;        // colour points by this attribute (min..max → blue..red)
    bool fields = false;          // field frames, falloff radii
    bool fieldVectors = false;    // sampled vector arrows on a grid (fieldGrid^3 over the field bounds)
    int fieldGrid = 8;
    bool normals = false;         // instance frames (y axis) as short lines
    bool splines = false;         // spline polylines + frames
    bool sdfSlice = false;        // SDF distance iso-lines on a horizontal slice at sliceHeight
    float sliceHeight = 0.0f;
    bool instanceIds = false;     // colour points by id hash
    bool entityBounds = false;    // world-space bounds of ordinary mesh entities
    bool entityOrigins = false;   // world-space origins and axes of ordinary mesh entities
    std::string selectedEntity;   // restrict entity diagnostics when non-empty
    bool lod = false;             // colour by LOD level (ADR-029)
    bool culling = false;         // draw culled instances in red
    // Renderer forensics, Phase 4.3. Each of these isolates or shows one thing, and each is checked
    // in `[debug]` to draw only for the case it names -- the plan's rule is that a diagnostic which
    // shows the same picture whatever the state is worse than none, because somebody trusts it.
    bool worldAxes = false;       // the world origin and its three axes, so "where is zero" is answerable
    bool entityIds = false;       // colour entity bounds by the pick id the identifier target writes
    bool submittedOnly = false;   // restrict entity diagnostics to what the frame actually submits
    // The camera frustum and basis. Drawn from `scene.camera` at `frustumAspect`, which is a
    // separate number because the scene's camera does not carry one -- the viewport supplies it, and
    // an overlay drawn at the wrong aspect is a box that does not match the screen it is over. On a
    // live camera this is exactly the screen edge and tells you nothing; it is worth drawing when
    // the camera is frozen (Phase 4.2's arm), because then it is the volume the cull used.
    bool frustum = false;
    float frustumAspect = 16.0f / 9.0f;
    bool transformTrail = false;  // the recorded world path of the selected object (transform_history.hpp)
    // Renderer forensics, Phase 4.5. Every skinned entity's joints, in world space: a point per
    // joint and a line to its parent. Drawn from the rig's *model-space* matrices rather than from
    // the GPU palette, because the palette is `model * inverseBind` and its translation is not
    // where the joint is -- reading a bone position out of it is the kind of plausible-looking
    // mistake a skeleton overlay exists to catch, not to make.
    bool skeletons = false;
    // ---- Shadow Lab (§15) --------------------------------------------------------------------
    //
    // The cascades had no overlay at all: `rendering::ShadowView` carried four matrices and nothing
    // drew them, so "which cascade is this pixel in" and "what volume does that cascade cover" were
    // questions with no answer short of reading a uniform buffer in a debugger.
    //
    // Both switches draw from the views the renderer **uploaded this frame**, handed to
    // `buildDebugGeometry` as a span. Neither re-fits anything: a cascade overlay that fitted its
    // own cascades would agree with the renderer exactly until the day it mattered.
    //
    // The light-space volume: the orthographic box each cascade rasterises into, through the
    // inverse of its own view-projection. This is where the shadow map *is*.
    bool shadowCascades = false;
    // The camera-space slice: the part of the camera frustum between a cascade's near and far view
    // depths, in the same colour. This is which pixels *select* that cascade, and it is the half
    // that answers "why did the shadow change when I dollied". Drawn from `scene.camera` at
    // `frustumAspect`, like `frustum`, so on a live camera it is the screen edge and tells you
    // nothing -- it is worth drawing frozen, or from a second view.
    bool shadowCascadeSlices = false;
    // Restrict both of the above to one view index; -1 draws every one. Four overlapping boxes is
    // four boxes, and the question is usually about one of them.
    int shadowCascade = -1;
    // Entity bounds coloured by `rendering::casterState`: what the shadow passes did with this
    // object, and why not when they did nothing. Green casts, amber casts from off screen, red is
    // one of the four reasons it does not.
    bool shadowCasters = false;
    bool depthTest = true;
    float pointSize = 3.0f;
    int maxPoints = 200000;       // safety cap per frame
};

} // namespace avgen::rendering

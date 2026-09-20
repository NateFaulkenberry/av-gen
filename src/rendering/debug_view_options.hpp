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
    // ADR-382, the brief's §19. Developer views, not artist controls -- three of the real bugs in
    // this branch were geometry errors that a picture would have shown in a second and that a
    // metric did not: the vortex funnel extending UPWARD as a full-radius cylinder, the camera
    // sitting inside the funnel's mouth, and a per-metre conversion missing so a term was two
    // orders of magnitude out. None of those is visible in a luminance number.
    bool wind = false;            // the wind field: direction and strength on a grid, and each
                                  // declared wind body's origin, height and radius
    bool vortex = false;          // the vortex's mouth, throat, depth and swirl direction
    bool frustum = false;
    float frustumAspect = 16.0f / 9.0f;
    bool transformTrail = false;  // the recorded world path of the selected object (transform_history.hpp)
    // Renderer forensics, Phase 4.5. Every skinned entity's joints, in world space: a point per
    // joint and a line to its parent. Drawn from the rig's *model-space* matrices rather than from
    // the GPU palette, because the palette is `model * inverseBind` and its translation is not
    // where the joint is -- reading a bone position out of it is the kind of plausible-looking
    // mistake a skeleton overlay exists to catch, not to make.
    bool skeletons = false;
    // ADR-262. Every particle system's **emitter disc and centreline**, drawn from the flattened
    // scene: a ring of `extent.x` at the emitter's world point, the vertical axis the column
    // actually fires along (`direction` is not transformed by `applyParameters`, so it is world
    // down however the emitter's node is rotated), and a second ring where the column *ends* --
    // `speedMin x lifetimeMin`, integrated with the system's own gravity and drag.
    //
    // The bottom ring is the point of this. Every diagnostic ever pointed at the tractor beam
    // treated it as an axis, which is a line of infinite length, and asked only how far the animal
    // was from it sideways; the beam that is drawn is a column with a bottom, and for the whole
    // first half of every lift the animal was underneath it. A centreline with an end on it is a
    // picture of that; the beam volume is not.
    //
    // Pair it with `entityBounds` + `entityOrigins`: the origin marker is the point the director
    // aims, the box is what a viewer sees, and the gap between them is the other half of ADR-262.
    bool beams = false;
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
    // ---- Lighting Lab (§7) --------------------------------------------------------------------
    //
    // Two things nothing drew before, and they are the two halves of "which lights reached this
    // pixel, and with how much": where each light *is and reaches*, and which froxels were told
    // about it.
    //
    // `lights` draws every enabled light in `scene.lights` in its own colour: the position, the
    // direction it travels, the **emitter** (a spot's cone, an area light's quad or tube, a
    // sphere's ball -- the shape the shading pass integrates, not a generic marker), and the
    // sphere of influence `rendering::lightInfluenceRadius` gave it. That last ring is the one
    // worth having: it is a *hard* edge, because past it the froxel pass does not assign the light
    // at all, and until it was drawn the only way to see where a light stopped was to find the
    // line in the image and wonder what made it.
    bool lights = false;
    // `lightClusters` draws the froxel grid a fragment reads, coloured by how many lights reach
    // each froxel: the CPU reference `assignClusters` run over the frame's own camera and lights.
    // Only occupied froxels are drawn, and only their near face -- 16 x 8 x 24 boxes is 36,864
    // lines and a picture of nothing.
    bool lightClusters = false;
    // Restrict `lightClusters` to one depth slice; -1 draws every occupied one. Twenty-four nested
    // shells is twenty-four shells, and the question is usually about the depth the subject is at.
    int lightClusterSlice = -1;
    bool depthTest = true;
    float pointSize = 3.0f;
    int maxPoints = 200000;       // safety cap per frame
};

} // namespace avgen::rendering

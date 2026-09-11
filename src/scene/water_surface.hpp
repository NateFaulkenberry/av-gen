#pragma once

// How a water surface looks (ADR-091). Its own header, and in `scene` rather than in `world`,
// because two things need it that are on opposite sides of that line: a `world::TerrainSettings`
// authors it, and a `scene::Scene` carries it to the renderer. `world::WaterSettings` is an alias
// of this, so terrain still reads the way it always did.
//
// The map decides where water *is* (world/world_map.hpp) and `world::WaterFlowSettings` decides
// which way it runs (world/water.hpp); this decides what it looks like.
//
// Only the first block existed before ADR-091, when water was a depth-coloured opaque sheet drawn
// through the general material interpreter. Everything below it is what a *surface* needs and a
// material program cannot reach: the scene's own depth (for thickness, and so for a shoreline that
// is not the mesh's silhouette), the environment cube (for a reflection), and a normal that moves.

#include "core/error.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

struct WaterSettings {
    bool enabled = true;
    float shallow = 2.2f;                       // metres of depth over which the colour reaches deep
    glm::vec3 shallowColor{0.045f, 0.16f, 0.15f}; // linear; the edge, where the bed shows through
    glm::vec3 deepColor{0.004f, 0.020f, 0.043f};  // linear; the channel
    float roughness = 0.06f;                    // low: water is a mirror before it is a colour
    float emissiveIntensity = 0.0f;             // for a world whose water carries light
    glm::vec3 emissiveColor{0.0f};

    // ---- transparency -------------------------------------------------------------------------
    // Metres of water the bed stays visible through. This is what makes the surface a volume: a
    // fragment's opacity comes from how much water the view ray crosses before it reaches the bed,
    // so the same sheet is nearly clear in the shallows and solid in the channel *without* the mesh
    // knowing where its own edge is. Beer-Lambert: alpha = 1 - exp(-thickness / clarity).
    float clarity = 1.35f;
    float maxOpacity = 0.94f;   // the deepest water still lets a little through; 1 reads as paint
    // Metres of *vertical* depth the surface additionally fades out over as the water gets thin.
    // This is the shoreline: a hard waterline is the loudest tell that water was laid over the
    // world, and it is hard because the mesh ends, not because the water does.
    float edgeFade = 0.75f;

    // ---- surface ------------------------------------------------------------------------------
    // Reflectance at normal incidence, 0..1. Water's real value is 0.02 and this is deliberately
    // not it: see shaders/water.wgsl. 0 gives physical water, which at the angle a camera looks at
    // a river from is a black bed with a five-per-cent sky on it.
    float fresnel = 0.20f;
    glm::vec3 reflectionTint{0.55f, 0.72f, 0.95f}; // the sky's colour as this water returns it
    float reflection = 1.6f;     // multiplies the environment reflection
    float specular = 1.6f;       // the moon's own glint, over the reflection
    // Ripples (§11): three layers, not one scrolling texture. `rippleScale` is the coarsest layer's
    // frequency in cycles per metre; each layer above it is finer, shorter-lived and travels at its
    // own speed, so the pattern never repeats visibly and never reads as one sheet being dragged.
    // `rippleSpeed` multiplies the body's own flow speed.
    float ripple = 0.95f;        // normal amplitude, 0 = glass
    float rippleScale = 0.42f;
    float rippleSpeed = 1.0f;
    float chop = 0.35f;          // how much the layers travel *across* the flow as well as along it

    // ---- shoreline (§10) -----------------------------------------------------------------------
    float foam = 0.55f;          // how bright the waterline band is, 0 = none
    float foamWidth = 0.42f;     // metres of water depth the band covers
    glm::vec3 foamColor{0.70f, 0.88f, 0.92f};
    // Metres of world-space distortion applied to the point the depth behind the water is read at.
    // It breaks the waterline up without a second geometry pass, and it is the only refraction
    // here: the colour behind the water arrives through the blend, not through a copy of the frame.
    float refraction = 0.22f;

    // ---- life (§16) ----------------------------------------------------------------------------
    // Bioluminescence *under* the surface: sparse patches that drift with the current and show only
    // through water deep enough to hold them. Sparse on purpose -- the brief is "the viewer should
    // occasionally notice there is something glowing underneath", not a lit river.
    float glow = 0.0f;
    glm::vec3 glowColor{0.20f, 1.0f, 0.72f};
    float glowScale = 0.16f;     // cycles per metre of the patch field
    float glowCoverage = 0.30f;  // how much of the channel lights up, 0..1
    float glowDepth = 0.55f;     // metres of water below which the patches start to show
    // Surface sparkle: a high-frequency glint riding the ripples, for the top of the mix.
    float sparkle = 0.0f;
    glm::vec3 sparkleColor{0.75f, 0.95f, 1.0f};
    // A whole-surface swell, in metres of vertical displacement, for a transition event. Zero
    // unless something is driving it; it is a modulation target, not a look.
    float swell = 0.0f;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// One water surface in a scene, as the renderer receives it. `program` is the material-program name
// the water entities of this surface carry, which is how a draw finds its settings without an index
// on every entity in the world.
struct WaterSurface {
    std::string program;
    WaterSettings settings;
    // Metres per second of the quickest body in the world this surface belongs to. The vertex's
    // speed lane is a fraction of it, so the shader needs it to get back to metres per second.
    float fastestFlow = 0.55f;
};

} // namespace avgen::scene

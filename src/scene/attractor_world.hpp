#pragma once

// THE STRANGE ATTRACTOR (ADR-1850): the fourth Environment (ADR-1200's seam). A scene-level block, `"attractorWorld"`,
// that holds a procedural surrealist landscape: a baked height field of a few art-directed geological masses, folded
// strata, a valley, and the composed camera that frames them. `rendering::AttractorRenderer` ray-marches it.
//
// Two halves, kept apart on purpose (brief §3.1):
//   * GENERATION is a pure function of `AttractorGen` (the seed and the structural knobs). `generateAttractorWorld`
//     builds the height field, the material masks, the layout and the camera on the CPU, deterministically. The
//     Composition regenerates only when the structural hash changes -- never because of audio.
//   * LOOK is continuous: palette, light, atmosphere, exposure. Those are parameters the renderer reads every frame.
//
// docs/prototypes/strange-attractor/ holds the research, the plan and the progress log.

#include "core/error.hpp"
#include "params/parameter_set.hpp"
#include "scene/attractor_formations.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::scene {

// The structural knobs. Any change regenerates the world (a second or so on the CPU).
struct AttractorGen {
    std::uint32_t seed = 1;
    int resolution = 2048;       // height-field texels per side (256..4096)
    float extent = 16000.0f;     // metres across the baked square
    float terrainScale = 1.0f;   // scales the masses' footprint and height (0.5..1.6)
    float roughness = 0.5f;      // secondary detail (0..1)
    float ridgeStrength = 0.6f;  // ridged multifractal on the masses (0..1)
    float domainWarp = 0.5f;     // spatial twisting of the masses and the strata (0..1)
    float terraces = 0.6f;       // the strata's steps cut into the masses (0..1)
    float erosion = 0.5f;        // gullies on the steep faces (0..1)
    float strataFold = 0.7f;     // an impossible anticline wave through the strata and their terraces (0..1)
    // Formations (ADR-1852)
    int recursiveDepth = 3;      // landmark recursion levels after the first (0..kAttractorMaxRecursion)
    float formationScale = 1.0f; // size of the landmarks and the attractor (0.5..1.5)
    float attractorInfluence = 1.0f; // 0 none .. 1 the full ribbon (its size and presence)
    int attractorSystem = 0;     // 0 Rossler, 1 Thomas
    float branchingDensity = 0.6f; // tributary gorges (0 none .. 1 many)
    int landmarks = 3;           // bitmask: 1 the butte tower, 2 the nested-arch fin

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t hash() const;
};

// The continuous look. Every member is an `attractorWorld/<leaf>` parameter.
struct AttractorLook {
    float palette = 0.0f;         // 0 Dusk Mineral, 1 Nocturne, 2 Chromatic (fractions blend)
    float sunAzimuth = 70.0f;     // degrees from the composed view, toward the side that lights the escarpment
    float sunElevation = 9.0f;    // degrees above the horizon
    float sunIntensity = 1.0f;
    float ambient = 1.0f;
    float atmosphere = 1.0f;      // aerial-perspective density multiplier
    float strata = 1.0f;          // visibility of the strata bands
    float exposure = 1.0f;
    float detail = 1.0f;          // near-field displacement detail
    float drift = 0.5f;           // the composed camera's slow drift (0 still .. 1 the widest, slowest arc)
};

struct AttractorMass {
    glm::vec2 centre{0.0f};
    glm::vec2 axes{500.0f, 300.0f}; // half extents (along, across)
    float angle = 0.0f;             // radians, the along axis from +X toward +Z
    float height = 300.0f;
    int kind = 0;                   // 0 mesa, 1 fin, 2 spire cluster, 3 range
};

// What the generator builds. Immutable once made; the scene and the renderer share it by pointer.
struct AttractorWorld {
    AttractorGen gen;
    std::uint64_t hash = 0;
    int res = 0;
    float extent = 0.0f;
    glm::vec2 origin{0.0f};            // world xz of texel (0, 0)'s corner
    std::vector<float> height;         // res*res metres, row-major (z rows, x columns)
    std::vector<float> strataOffset;   // res*res: the strata coordinate minus the height (tilt, warp, fold)
    std::vector<std::uint32_t> masks;  // res*res RGBA8: r rock, g playa (valley floor), b cavity, a ridge
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<AttractorMass> masses;
    std::vector<AttractorLandmark> landmarks; // the recursive formations (ADR-1852)
    AttractorTube tube;                       // the attractor's stone ribbon (empty at influence 0)
    std::vector<NetworkBranch> branches;      // the tributary gorges
    std::vector<float> networkArc;            // res*res: network distance (m) along the gorges, or -1 off them
    std::vector<glm::vec2> valley;     // the valley's trunk, near to far (world xz)
    std::vector<glm::vec2> gorgeLine;  // the inner gorge's centreline, near to far (world xz)
    std::array<glm::vec4, 4> strataSeed{}; // [0] x tilt angle, y tilt slope, z band (m); [1] fold: x angle, y wavelength, z amp
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
    float fovDeg = 40.0f;
    float viewAzimuth = 0.0f;          // radians: atan2 of the view direction's (x, z)
    std::uint32_t noiseSeed = 0;       // the noise seed (the shader's strata use the same one)
    float sunSide = 1.0f;              // +1 the sun stands to the view's right, -1 to its left
    double generateSeconds = 0.0;

    [[nodiscard]] float sample(glm::vec2 xz) const; // bilinear height; clamps outside
};

[[nodiscard]] std::shared_ptr<const AttractorWorld> generateAttractorWorld(const AttractorGen& gen);
// The world for `gen`: the last one generated when its hash matches (a scene file loaded twice, a reload, a seed
// dialled away and back within one step), otherwise a fresh `generateAttractorWorld`. Thread-safe.
[[nodiscard]] std::shared_ptr<const AttractorWorld> acquireAttractorWorld(const AttractorGen& gen);
// How many worlds this process has generated (tests: a look change must not regenerate).
[[nodiscard]] std::uint64_t attractorWorldGenerations() noexcept;

struct AttractorWorldBlock {
    bool enabled = false;
    bool driveCamera = true; // the generated composition places the scene's camera
    AttractorGen gen;        // rest values of the structural parameters
    AttractorLook look;      // rest values of the look parameters
    // ---- this frame (written by the Composition) ----
    std::shared_ptr<const AttractorWorld> world;
    AttractorGen liveGen;
    AttractorLook live;
    float cameraDrive = 1.0f;
    [[nodiscard]] bool active() const { return enabled && world != nullptr; }
};

[[nodiscard]] Result<AttractorWorldBlock> attractorWorldFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json attractorWorldToJson(const AttractorWorldBlock& a);

struct AttractorWorldParameters {
    std::string prefix; // "attractorWorld/"
    std::vector<params::IParameter*> all;
};
[[nodiscard]] AttractorWorldParameters registerAttractorWorldParameters(params::ParameterSet& params,
                                                                        const AttractorWorldBlock& rest,
                                                                        const std::string& prefix);
// The composed camera at `seconds`: the generated eye and target, drifting on a slow lateral arc with a little dolly
// (`look.drift`). A pure function of time, so it is frame-rate independent and seekable.
struct AttractorCameraPose {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
};
[[nodiscard]] AttractorCameraPose attractorCamera(const AttractorWorld& world, float drift, double seconds);
// The finals into `live.liveGen`, `live.live` and `live.cameraDrive`.
void applyAttractorWorldParameters(const AttractorWorldParameters& p, const AttractorWorldBlock& rest,
                                   AttractorWorldBlock& live);

} // namespace avgen::scene

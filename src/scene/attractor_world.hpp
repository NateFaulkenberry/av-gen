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
#include "scene/water_surface.hpp"

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
    float branchingDensity = 0.6f; // side canyons off the gorge (0 none .. 1 many)
    float flightSeconds = 46.0f;   // the flythrough's length (20..120 s)
    // ADR-1857: the engine-native triangle mesh. `mesh` builds it with the world; `meshSpacing` is the sampling grid
    // (metres) the error-bounded triangulation starts from; `meshError` the tolerated vertical error per metre of
    // distance from the flight path (about a pixel at 1080p); `meshMinError` its floor near the path.
    bool mesh = true;
    float meshSpacing = 2.0f;
    float meshError = 0.004f;    // ~6 px at 1080p from the path; measured indistinguishable from 0.002 (Phase 1)
    float meshMinError = 0.3f;

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
};

inline constexpr int kAttractorMaxBranches = 48; // side-canyon polylines

// One side canyon of the branching network (ADR-1852): a polyline grown off the gorge.
struct NetworkBranch {
    std::vector<glm::vec2> points; // world xz, from the junction outward
    std::vector<float> widths;     // metres, per point (tapering)
    std::vector<float> depths;     // the floor's height (m), per point: rising from the gorge's floor to the plateau
    float startArc = 0.0f;         // network distance (m) from the trunk's near end to this branch's junction
    int level = 1;                 // 1 a branch of the gorge, 2 a branch of a branch
};

// The camera the block drives (`attractorWorld/camera`).
enum class AttractorCameraMode : int { Scene = 0, Composed = 1, Flight = 2 };

// ADR-1857: the canyon as engine geometry. One crack-free error-bounded triangulation of the rendered surface,
// partitioned into chunks for culling (shared border vertices are duplicated with identical positions and normals).
struct AttractorTerrainChunk {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;          // x the band coordinate (bands; the strata), y rock (0..1) + 2 x playa
    std::vector<std::uint32_t> indices;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};
struct AttractorTerrainMesh {
    std::vector<AttractorTerrainChunk> chunks;
    std::vector<glm::vec3> riverPositions; // the river's water surface (a flat sheet over the gorge's channels)
    std::vector<std::uint32_t> riverIndices;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    float sampleSpacing = 0.0f;
    double buildSeconds = 0.0;
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
    std::vector<float> slopeMax;       // res*res: the steepest slope within 5 texels (the march's step bound)
    std::vector<std::uint32_t> masks;  // res*res RGBA8: r rock, g playa (valley floor), b cavity, a ridge
    std::vector<std::uint32_t> flow;   // res*res RGBA8: rg the river's downstream direction, b distance from the channel (m), a in a channel
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<AttractorMass> masses;
    std::vector<NetworkBranch> branches;      // the side canyons
    std::shared_ptr<const AttractorTerrainMesh> terrainMesh; // ADR-1857: null when `gen.mesh` is off
    std::vector<float> networkArc;            // res*res: network distance (m) along the gorges, or -1 off them
    std::vector<glm::vec2> valley;     // the valley's trunk, near to far (world xz)
    std::vector<glm::vec2> gorgeLine;  // the inner gorge's centreline, near to far (world xz)
    std::array<glm::vec4, 4> strataSeed{}; // [0] x tilt angle, y tilt slope, z band (m); [1] fold: x angle, y wavelength, z amp
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
    float fovDeg = 40.0f;
    // ADR-1853: the flythrough, sampled at kFlightRate: xyz the eye, w the bank (radians). Precomputed with the
    // world, so a frame's pose is a pure function of its second.
    std::vector<glm::vec4> flight;
    std::vector<glm::vec4> flightAim;  // per sample: xyz the unit view direction (smoothed; ADR-1854)
    static constexpr float kFlightRate = 30.0f;
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
    AttractorCameraMode camera = AttractorCameraMode::Composed; // which camera the block drives
    AttractorGen gen;        // rest values of the structural parameters
    AttractorLook look;      // rest values of the look parameters
    // ADR-1857: the engine-native path. The terrain chunks are drawn by the scene pass with this material program
    // (authored in the scene file's `materialPrograms`), the river through the engine's water pipeline, and the sun
    // is a directional key light the look parameters steer.
    std::string terrainProgram = "canyonRock";
    WaterSettings water;
    float sunLux = 6.0f;     // the key light's intensity at `sunIntensity` 1
    float sunShadowBias = 0.6f; // the key light's normal-offset shadow bias (world units)
    // ---- this frame (written by the Composition) ----
    std::shared_ptr<const AttractorWorld> world;
    AttractorGen liveGen;
    AttractorLook live;
    int cameraMode = 1;      // `attractorWorld/camera` final (an AttractorCameraMode)
    [[nodiscard]] bool active() const { return enabled && world != nullptr; }
};

// Toward the sun, for a world and a look: `sunAzimuth` degrees from the composed view, toward the escarpment's lit side.
[[nodiscard]] glm::vec3 attractorSunDirection(const AttractorWorld& world, const AttractorLook& look);

[[nodiscard]] Result<AttractorWorldBlock> attractorWorldFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json attractorWorldToJson(const AttractorWorldBlock& a);

struct AttractorWorldParameters {
    std::string prefix; // "attractorWorld/"
    std::vector<params::IParameter*> all;
};
[[nodiscard]] AttractorWorldParameters registerAttractorWorldParameters(params::ParameterSet& params,
                                                                        const AttractorWorldBlock& rest,
                                                                        const std::string& prefix);
// The camera at `seconds` for `mode`: the composed vista (still), or the flythrough (ADR-1853), held at its last pose
// after its end. A pure function of time, so it is frame-rate independent and seekable.
struct AttractorCameraPose {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
    float rollDegrees = 0.0f;
};
[[nodiscard]] AttractorCameraPose attractorCamera(const AttractorWorld& world, AttractorCameraMode mode, double seconds);
// The finals into `live.liveGen`, `live.live` and `live.cameraMode`.
void applyAttractorWorldParameters(const AttractorWorldParameters& p, const AttractorWorldBlock& rest,
                                   AttractorWorldBlock& live);

} // namespace avgen::scene

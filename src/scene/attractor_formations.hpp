#pragma once

// THE STRANGE ATTRACTOR's formations (ADR-1852): the parts of the world that are not the height field.
//   * the ATTRACTOR: a trajectory of a chaotic flow (Rossler, or Thomas as the fallback), integrated with fixed-step
//     RK4, its transient discarded, normalised and resampled by arc length, and stood up in the world as a stone tube
//     whose loops arch out of the gorge. Baked to a 3D distance volume (plus the nearest segment, for an exact
//     refinement at the surface) that the shader marches with the terrain.
//   * the recursive LANDMARKS: a tower of buttes, each tier repeating the plateau's own erosion-front outline at a
//     smaller scale, and a fin with arches cut inside arches. Evaluated analytically in the shader from a few numbers.
//   * the BRANCHING network: tributary gorges grown off the inner gorge, carved into the bench.
// Every function is pure and deterministic in its inputs. Budgets (samples, segments, voxels, recursion depth,
// branches) are explicit constants below and are tested.

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace avgen::scene {

inline constexpr int kAttractorMaxSegments = 1024;     // tube segments after arc-length resampling
inline constexpr int kAttractorVolumeRes = 160;        // voxels per side of the tube's distance volume
inline constexpr int kAttractorMaxRecursion = 5;       // landmark recursion depth
inline constexpr int kAttractorMaxBranches = 48;       // tributary polylines
inline constexpr int kAttractorMaxLandmarks = 4;

enum class AttractorSystem : int { Rossler = 0, Thomas = 1 };

// A raw trajectory in the system's own coordinates, after the transient.
struct Trajectory {
    std::vector<glm::vec3> points;
    bool finite = true;
};
[[nodiscard]] Trajectory integrateAttractor(AttractorSystem system, glm::vec3 start, float dt, int transientSteps,
                                            int keepSteps);

// Resamples a polyline to segments of equal length (at most `maxSegments`).
[[nodiscard]] std::vector<glm::vec3> resampleByArcLength(const std::vector<glm::vec3>& points, int maxSegments);

struct AttractorTube {
    std::vector<glm::vec4> points;      // world xyz, w = radius (m); consecutive points are the segments
    glm::vec3 boundsMin{0.0f};          // the volume's box (world)
    glm::vec3 boundsMax{0.0f};
    int volumeRes = 0;
    std::vector<glm::vec2> volume;      // res^3: x = distance to the tube's surface (m, clamped), y = nearest segment
    [[nodiscard]] bool empty() const { return points.size() < 2; }
};

// Distance from `p` to the tube (negative inside), exactly, over all segments. The CPU reference of the shader.
[[nodiscard]] float tubeDistance(const AttractorTube& tube, glm::vec3 p);
// Bakes `tube.volume` over `tube.bounds*` (filled by the caller) at `res` per side, on all cores.
void bakeTubeVolume(AttractorTube& tube, int res);

struct AttractorLandmark {
    int kind = 0;                 // 0 butte tower, 1 nested arches
    glm::vec3 base{0.0f};         // world position of the base's centre (on the ground)
    float scale = 300.0f;         // metres: the first tier's radius, or the fin's half length
    float angle = 0.0f;           // radians about +Y
    float height = 200.0f;        // metres: the first tier's height, or the fin's height
    int depth = 3;                // recursion levels after the first
    std::uint32_t seed = 1;
};

struct NetworkBranch {
    std::vector<glm::vec2> points; // world xz, from the junction outward
    std::vector<float> widths;     // metres, per point (tapering)
    std::vector<float> depths;     // metres carved below the bench, per point
    float startArc = 0.0f;         // network distance (m) from the trunk's near end to this branch's junction
    int level = 1;                 // 1 a tributary of the trunk, 2 of a tributary
};

} // namespace avgen::scene

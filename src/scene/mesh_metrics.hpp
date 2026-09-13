#pragma once

// What a mesh costs to draw, as distinct from how big it is (renderer upgrade, Phase C).
//
// The gate measurement (`docs/renderer-upgrade/01-audit-and-baseline.md` §4.5) established that
// this renderer's fragment cost tracks *triangle size* and not pixel count: at identical coverage,
// 2.1 M triangles cost 4.9x what 2,048 cost, with the knee at the 2x2-quad threshold. So the number
// that predicts what a drawable costs is **pixels per triangle**, and nothing in the engine could
// state it: `MeshData` knows its vertices and its bounds, and a bounding box says how large a thing
// is on screen but not how finely it is divided.
//
// These are the two numbers that together answer it -- a triangle count and a surface area -- held
// in the mesh's own space so they can be scaled and projected per instance. They are *derived*,
// never authoritative: `MeshMetricsCache` recomputes them whenever the scene's mesh version moves,
// under the same rule `Scene::meshBounds` already follows.

#include "scene/scene_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace avgen::scene {

struct Scene; // scene/scene.hpp -- only the cache needs it, and only in the implementation

struct MeshMetrics {
    std::uint32_t triangles = 0;
    // Total area of the mesh's triangles, in the mesh's own units. Degenerate triangles contribute
    // zero rather than being dropped, so `triangles` still counts what the GPU will be asked to
    // rasterise -- a zero-area triangle is submitted, binned and (usually) killed, and pretending
    // it is not there would flatter every estimate built on this.
    float surfaceArea = 0.0f;
    float boundsRadius = 0.0f;      // sphere about boundsCenter containing every vertex
    glm::vec3 boundsCenter{0.0f};   // centre of the axis-aligned bounds, not the centroid

    [[nodiscard]] bool valid() const { return triangles > 0; }
    [[nodiscard]] float meanTriangleArea() const {
        return triangles == 0 ? 0.0f : surfaceArea / static_cast<float>(triangles);
    }
    // How `surfaceArea` scales under a possibly non-uniform scale. Area does not have one scale
    // factor when the three axes differ, so this is the mean of the three axis-pair products --
    // exact for an axis-aligned box, and the right order everywhere else. A uniform scale s gives
    // s^2, which is the case that has to be exact because it is the common one.
    [[nodiscard]] static float areaScale(glm::vec3 scale);
};

// Triangle count, surface area and bounding sphere of one mesh. O(indices); call it once per mesh
// per version, not per frame -- `MeshMetricsCache` is what enforces that.
[[nodiscard]] MeshMetrics meshMetrics(const MeshData& mesh);

// Per-scene metrics, recomputed when `Scene::meshVersion` moves.
//
// A derived copy with a lifetime longer than a frame is exactly the shape of defect this engine's
// forensics work found nine times, so the invalidation rule is stated rather than implied: the
// cache is keyed on the pair (`Scene::identity`, `Scene::meshVersion`) and a mismatch in either
// rebuilds all of it. Identity as well as version because every fresh Scene starts its version at
// the same value, so a new scene at a recycled address is otherwise indistinguishable from the old
// one -- the same trap `Scene::identity` was minted to close.
class MeshMetricsCache {
public:
    // Returns the metrics for `mesh` in `scene`, rebuilding the whole table if the scene moved.
    // An out-of-range or empty mesh yields a default-constructed (invalid) MeshMetrics.
    [[nodiscard]] const MeshMetrics& metrics(const Scene& scene, MeshId mesh);
    // Whether this cache currently describes `scene` at its present mesh version.
    [[nodiscard]] bool matches(const Scene& scene) const;
    [[nodiscard]] std::size_t size() const { return metrics_.size(); }
    // Number of times the table has been rebuilt. Exposed so a test can assert that a cache which
    // is asked the same question twice answers it once.
    [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
    void clear();

private:
    void rebuild(const Scene& scene);

    std::vector<MeshMetrics> metrics_;
    std::uint64_t identity_ = 0;     // 0 is "no scene yet"; Scene::identity is never 0
    std::uint64_t meshVersion_ = 0;
    std::uint64_t rebuilds_ = 0;
};

} // namespace avgen::scene

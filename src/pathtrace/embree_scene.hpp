#pragma once

// The Embree side of the path tracer (spec section 74: Embree owns intersection, BVH and occlusion;
// everything else is AV Gen's).
//
// Threading (ADR-348, spec section 36/39). Embree is given an explicit thread count and the BVH is
// committed with `rtcJoinCommitScene` from threads this process already owns, so Embree starts no
// pool of its own. `app::JobSystem` cannot be used for this: it is a two-worker FIFO of whole jobs
// with no parallel-for, and its header forbids waiting on it from a render thread.

#include "core/error.hpp"
#include "pathtrace/snapshot.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace avgen::pathtrace {

// What a ray hit, already resolved into the quantities a BSDF needs. All in world space.
struct SurfaceHit {
    bool hit = false;
    float t = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 geometricNormal{0.0f};  // from the triangle, normalised, faces the ray
    glm::vec3 shadingNormal{0.0f};    // interpolated vertex normal, normalised, faces the ray
    glm::vec2 uv{0.0f};               // interpolated texture coordinate
    bool backface = false;            // the ray hit the far side of the triangle
    std::uint32_t meshIndex = 0;      // index into Snapshot::meshes
    std::uint32_t primIndex = 0;
};

class EmbreeScene {
public:
    EmbreeScene();
    ~EmbreeScene();
    EmbreeScene(const EmbreeScene&) = delete;
    EmbreeScene& operator=(const EmbreeScene&) = delete;

    // Builds the BVH. `buildThreads` is how many of this process's own threads join the commit;
    // 0 means "one, the caller's". Fails loudly rather than returning an empty scene (section 54).
    [[nodiscard]] Result<void> build(const Snapshot& snapshot, unsigned buildThreads = 0);

    [[nodiscard]] SurfaceHit intersect(const Snapshot& snapshot, const glm::vec3& origin,
                                       const glm::vec3& direction, float tnear, float tfar) const;

    // True if anything blocks the segment. Cheaper than `intersect`: Embree stops at the first hit.
    [[nodiscard]] bool occluded(const glm::vec3& origin, const glm::vec3& direction, float tnear,
                                float tfar) const;

    [[nodiscard]] bool valid() const { return impl_ != nullptr; }
    [[nodiscard]] std::size_t geometryCount() const { return geometryCount_; }

private:
    // Embree's types are held behind a pimpl so <embree4/rtcore.h> stays out of every translation
    // unit that merely wants to trace a ray. It also keeps `embree` a PRIVATE link dependency of
    // avgen_core rather than something the whole engine inherits.
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::size_t geometryCount_ = 0;
};

// The offset that keeps a shadow ray from hitting the surface it started on. Scaled by the hit
// distance because a fixed epsilon that works at 1 m self-intersects at 10 km and floats visibly
// off the surface at 1 mm.
[[nodiscard]] float shadowEpsilon(float t);

} // namespace avgen::pathtrace

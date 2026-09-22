#pragma once

// The Embree side of the path tracer (spec section 74: Embree owns intersection, BVH and occlusion;
// everything else is AV Gen's).
//
// Threading (ADR-351, spec section 36/39). Embree is given an explicit thread count and the BVH is
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
    // Where the hit is. `instanced` says which of the two lists `meshIndex` refers to:
    // Snapshot::meshes when false, Snapshot::instanced when true.
    bool instanced = false;
    std::uint32_t instanceIndex = 0;  // which copy, when `instanced`
    std::uint32_t meshIndex = 0;      // index into Snapshot::meshes or Snapshot::instanced
    std::uint32_t primIndex = 0;
    // Barycentric weights of v0, v1, v2, kept so a caller can interpolate anything else the
    // triangle carries -- the motion AOV interpolates the PREVIOUS frame's positions with these,
    // which is what makes a motion vector track a point on the surface rather than a screen pixel.
    float baryW = 0.0f;   // v0
    float baryU = 0.0f;   // v1
    float baryV = 0.0f;   // v2
};

// How `EmbreeScene::update` treats what it built last time (ADR-583).
enum class BvhReuse : std::uint8_t {
    // Keep every acceleration structure whose complete input is bit-for-bit what it was built
    // from, and rebuild the rest. The default, and the only mode a render should use.
    Detect,
    // The control arm: discard everything, device included, and build from scratch -- exactly
    // what every frame did before ADR-583. Kept as a live setting (`TraceSettings::reuseAcceleration`
    // and `--pt-rebuild-bvh`) so "does reuse change the picture" stays a question anybody can ask.
    Rebuild,
    // TEST ONLY. Reuse whenever the STRUCTURE matches (same objects, same counts) without
    // comparing vertices, indices or transforms. This is change detection switched off, and it
    // exists so the tests that prove detection works can show themselves failing without it
    // (ADR-182). Never reachable from a setting.
    TrustStructure,
};

// What one `update` did. Printed per frame by the sequence so the reuse is visible, not assumed.
struct BvhUpdateStats {
    bool deviceCreated = false;
    std::size_t objects = 0;            // child acceleration structures the frame needs
    std::size_t objectsBuilt = 0;       // of which were (re)built this call
    std::size_t trianglesBuilt = 0;
    std::size_t trianglesReused = 0;
    std::size_t topLevelInstances = 0;
    std::size_t transformsChanged = 0;  // top-level transforms that differ from the last frame
    bool topLevelBuilt = false;
    double compareSeconds = 0.0;        // change detection
    double objectSeconds = 0.0;         // child builds
    double topLevelSeconds = 0.0;
    // Embree's own allocations (BVHs, vertex/index copies, build scratch), from its memory monitor:
    // what is held once the update returns, and the most held at any moment during it.
    std::int64_t heldBytes = 0;
    std::int64_t peakBytes = 0;
};

class EmbreeScene {
public:
    EmbreeScene();
    ~EmbreeScene();
    EmbreeScene(const EmbreeScene&) = delete;
    EmbreeScene& operator=(const EmbreeScene&) = delete;

    // Builds the BVH from scratch. `buildThreads` is how many of this process's own threads join
    // the commit; 0 means "one, the caller's". Fails loudly rather than returning an empty scene
    // (section 54). Equivalent to `update(snapshot, buildThreads, BvhReuse::Rebuild)`.
    [[nodiscard]] Result<void> build(const Snapshot& snapshot, unsigned buildThreads = 0);

    // Brings the BVH to `snapshot`, rebuilding only what changed since the last call (ADR-583).
    //
    // Structure: meshes are grouped by (bitwise-identical) object-to-world transform, each group
    // is one child scene built over OBJECT-space vertices, and a mesh posed by a rig is always a
    // group of its own. Each procedural object is one child scene, as before. The top level holds
    // one instance per group and one per procedural copy. The structure is a pure function of the
    // snapshot, never of history, so a reused frame and a rebuilt one trace the same BVH.
    //
    // A child is reused only if its members, their counts, and every vertex and index are
    // bit-identical to the buffers Embree holds (compared against Embree's own copy, so no second
    // copy is kept and no hash can collide). The top level is reused only if every child is and
    // every transform is bit-identical. Anything else is rebuilt from scratch through the same
    // code the first frame used.
    [[nodiscard]] Result<void> update(const Snapshot& snapshot, unsigned buildThreads,
                                      BvhReuse mode = BvhReuse::Detect);

    // Releases everything, device included.
    void reset();

    [[nodiscard]] SurfaceHit intersect(const Snapshot& snapshot, const glm::vec3& origin,
                                       const glm::vec3& direction, float tnear, float tfar) const;

    // True if anything blocks the segment. Cheaper than `intersect`: Embree stops at the first hit.
    [[nodiscard]] bool occluded(const glm::vec3& origin, const glm::vec3& direction, float tnear,
                                float tfar) const;

    [[nodiscard]] bool valid() const { return impl_ != nullptr; }
    [[nodiscard]] std::size_t geometryCount() const { return geometryCount_; }
    [[nodiscard]] const BvhUpdateStats& lastUpdate() const { return lastUpdate_; }

    // The material of whatever a hit landed on, from either list. Callers should not have to know.
    [[nodiscard]] static const scene::Material& materialOf(const Snapshot& snap, const SurfaceHit& hit);

private:
    // Embree's types are held behind a pimpl so <embree4/rtcore.h> stays out of every translation
    // unit that merely wants to trace a ray. It also keeps `embree` a PRIVATE link dependency of
    // avgen_core rather than something the whole engine inherits.
    struct Impl;
    [[nodiscard]] Result<void> updateInner(const Snapshot& snapshot, unsigned buildThreads, BvhReuse mode);
    std::unique_ptr<Impl> impl_;
    std::size_t geometryCount_ = 0;
    BvhUpdateStats lastUpdate_{};
};

// The offset that keeps a shadow ray from hitting the surface it started on. Scaled by the hit
// distance because a fixed epsilon that works at 1 m self-intersects at 10 km and floats visibly
// off the surface at 1 mm.
[[nodiscard]] float shadowEpsilon(float t);

} // namespace avgen::pathtrace

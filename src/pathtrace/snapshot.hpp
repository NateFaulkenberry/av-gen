#pragma once

// The transient render snapshot (ADR-340, spec section 8) and the capability report (section 55).
//
// `scene::Scene` is mutated in place by its controller: two timeline times cannot coexist, and the
// realtime renderer rewrites per-frame fields (terrain LOD, `cameraCulled`) that a path trace must
// not inherit. So the tracer does not hold a `const Scene&` across anything -- it takes a Snapshot,
// which is a flat, world-space, immutable copy of exactly what it can see, built once.
//
// This is NOT a second scene graph. It has no hierarchy, no parameters, no update, and nothing ever
// reads it back into the scene. It is the input to one render and is thrown away.

#include "scene/scene.hpp"
#include "scene/scene_types.hpp"
#include "scene/sky.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace avgen::pathtrace {

// ---- capability report (spec section 55 / section 87) -------------------------------------------
//
// Section 87 is explicit: a scene feature that cannot be represented must be detected, reported and
// given a documented fallback -- never silently dropped and never silently altered in the realtime
// scene. This is the structure that makes "reported" true, and the render prints it at startup so
// the answer arrives before the pixels rather than after somebody notices they are wrong.

enum class Support {
    Full,       // rendered as the scene means it
    Degraded,   // rendered, but by a documented fallback that is not the realtime look
    Unsupported // not rendered at all; the reason is not a bug and is stated
};

[[nodiscard]] std::string_view supportName(Support s);

struct Capability {
    std::string feature;
    Support support = Support::Full;
    std::string detail;   // what was done, and for Degraded/Unsupported, why
    int count = 0;        // how many of this thing the snapshot actually met
};

struct CapabilityReport {
    std::vector<Capability> entries;

    void note(std::string feature, Support support, std::string detail, int count = 0);
    // True if anything was dropped or approximated -- the one-line answer a caller wants.
    [[nodiscard]] bool anyDegraded() const;
    [[nodiscard]] bool anyUnsupported() const;
    // Human-readable, one line per entry, for the log at render startup.
    [[nodiscard]] std::string format() const;
};

// ---- geometry -----------------------------------------------------------------------------------

// One Embree geometry: a world-space triangle mesh with one material.
//
// Phase 1 bakes the entity transform into the positions rather than using Embree instancing. That
// is the wrong trade for a scatter-heavy scene and is deliberately deferred: correctness before
// speed (section 76), and instancing changes hit interpretation (instID) everywhere it touches.
struct TriangleMesh {
    std::vector<glm::vec3> positions;  // world space
    std::vector<glm::vec3> normals;    // world space, normalised, NOT renormalised per-hit
    std::vector<glm::vec2> uvs;        // may be empty
    std::vector<std::uint32_t> indices;

    scene::Material material;
    std::string entityName;            // for diagnostics only
    std::uint32_t entityIndex = 0;     // index into the source Scene::entities

    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
    [[nodiscard]] bool valid() const;
};

// ---- the snapshot --------------------------------------------------------------------------------

struct Snapshot {
    std::vector<TriangleMesh> meshes;
    std::vector<scene::PunctualLight> lights;

    scene::Camera camera;

    // Environment. Phase 1 is analytic only: `scene::SkyRuntime` is the project's own CPU reference
    // for the sky the GPU pass draws (ADR-036), so a realtime/offline comparison shows transport
    // differences rather than two different skies. An image-based environment arrives with
    // section 46's importance sampling and is not built yet.
    bool skyEnabled = false;
    scene::SkyRuntime sky;
    glm::vec3 backgroundColor{0.0f};

    CapabilityReport capabilities;

    [[nodiscard]] std::size_t triangleCount() const;
    [[nodiscard]] bool empty() const { return meshes.empty(); }
};

// Builds the snapshot from an already-evaluated scene. The caller is responsible for having called
// `Engine::update(FrameTime)` first: this function does no evaluation of its own, by design
// (section 7 -- the tracer must not duplicate scene evaluation).
[[nodiscard]] Snapshot buildSnapshot(const scene::Scene& scene);

} // namespace avgen::pathtrace

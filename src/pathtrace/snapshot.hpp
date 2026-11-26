#pragma once

// The transient render snapshot (ADR-351, spec section 8) and the capability report (section 55).
//
// `scene::Scene` is mutated in place by its controller: two timeline times cannot coexist, and the
// realtime renderer rewrites per-frame fields (terrain LOD, `cameraCulled`) that a path trace must
// not inherit. So the tracer does not hold a `const Scene&` across anything -- it takes a Snapshot,
// which is a flat, world-space, immutable copy of exactly what it can see, built once.
//
// This is NOT a second scene graph. It has no hierarchy, no parameters, no update, and nothing ever
// reads it back into the scene. It is the input to one render and is thrown away.

#include "pathtrace/environment.hpp"
#include "scene/material_program.hpp"
#include "scene/scene.hpp"
#include "scene/scene_types.hpp"
#include "scene/sky.hpp"
#include "spatial/point_cloud.hpp"

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

    // Caveats are properties of the RENDERER, not of the scene: they are true of every render and
    // are listed whatever the content. They are kept apart from `entries` so that `anyDegraded()`
    // keeps meaning "something in THIS scene was approximated" -- a question a caller may want to
    // branch on -- while `format()` still prints both, because the startup log is where somebody
    // debugging actually looks.
    std::vector<std::string> caveats;

    void note(std::string feature, Support support, std::string detail, int count = 0);
    void caveat(std::string text);
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

    // The same vertices one frame earlier, in world space. Empty when motion was not requested or
    // when this mesh could not be matched to the previous frame. A motion vector needs the vertex
    // to exist at both times, and a mesh whose vertex count changed between frames is not the same
    // mesh -- re-scattered foliage and re-tessellated terrain both do this.
    std::vector<glm::vec3> previousPositions;

    // The same vertices BEFORE the entity transform (after skinning), and that transform, so the
    // BVH can be built over what is stable and the motion kept in the transform (ADR-583).
    //
    // Measured on the Tree of Life: every one of its 33 meshes changes every frame in world space,
    // because the island drifts as a rigid body, and in object space not one vertex changes. A BVH
    // keyed on `positions` would therefore be rebuilt every frame for a scene with no deforming
    // geometry at all. `positions` stays the world-space truth everything else shades from; this is
    // only what the acceleration structure is built from. Empty means "`positions` is already
    // object space and the transform is identity", which is what a hand-assembled mesh is.
    std::vector<glm::vec3> objectPositions;
    glm::mat4 objectToWorld{1.0f};
    // The vertices are posed by a rig, so they may change in place every frame. Such a mesh gets
    // an acceleration structure of its own rather than sharing one with static meshes that happen
    // to have the same transform -- one walking character must not rebuild the scenery.
    bool deforming = false;

    scene::Material material;
    std::string entityName;            // for diagnostics only
    std::uint32_t entityIndex = 0;     // index into the source Scene::entities

    // ---- what the surface's material program and emission lanes read (ADR-2246) ----------------------------------
    // The `localPosition` material input, per vertex: the mesh's own space BEFORE skinning and the entity transform for
    // an entity (pbr_skinned.wgsl keeps bind space so a pattern travels with the surface), and the raw source-mesh
    // position BEFORE `sourceTransform` for a procedural source (procedural.wgsl `out.localPos = in.position`).
    std::vector<glm::vec3> localPositions;
    // Index into `Snapshot::programs`, or -1: the program `material.program` names, resolved once.
    int program = -1;
    // ADR-903/905: the object's emission lane, applied after the program and the texture (pbr_shade.wgsl
    // `emissionLane`). No longer folded into `material.emissiveIntensity`, because the lane must act on what a
    // program emits, not on the factor the program replaces.
    float emissionGain = 1.0f;
    float emissionHue = 0.0f;

    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
    [[nodiscard]] bool valid() const;
};

// ---- emissive geometry (spec section 47) ---------------------------------------------------------
//
// An emissive triangle is a light a ray can actually HIT, which is what makes multiple importance
// sampling possible at all: both strategies can find it, so their densities can be combined.
//
// This is a different thing from `scene::PunctualLight`. An analytic light has no geometry in the
// BVH, so a BSDF ray can never find one, and MIS-weighting a sample of it against the BSDF's density
// would down-weight the only strategy that can see it -- losing energy for nothing. Analytic lights
// therefore take weight 1 and emissive geometry takes a real MIS weight. Keeping the two apart is
// the whole reason this structure exists.
//
// ADR-2247: emitters are sampled in two levels. A GROUP is one emitting mesh, or one copy of an emitting procedural
// source; it is chosen by (approximately) its world area. Within it a triangle is chosen by its area -- world area
// for a mesh, object-space area for an instanced source, shared by every copy -- and a point uniformly on that
// triangle. The density at the point is then exact whatever the copies' scales are:
//
//   mesh:      p_A = P(group) / A_group
//   instance:  p_A = P(group) * (A_obj(tri) / A_obj(group)) / A_world(tri)
//
// and both are computable at any hit from the group index alone, so a BSDF ray that lands on an emitter needs no
// per-triangle table to be MIS-weighted. Before ADR-2247 only mesh emitters were in the list and instanced ones
// (every procedural window and sign) could be found by BSDF rays alone.
struct EmitterTriangle {
    std::uint32_t prim = 0;
    float area = 0.0f;   // world area for a mesh group, object-space area for an instanced source
    float cdf = 0.0f;    // cumulative fraction within its group (or source), ending at 1
};

struct EmitterGroup {
    bool instanced = false;
    std::uint32_t object = 0;      // Snapshot::meshes index, or Snapshot::instanced index when `instanced`
    std::uint32_t instance = 0;    // which copy, when `instanced`
    std::uint32_t triBegin = 0;    // its triangles in Snapshot::emitterTriangles (shared by every copy of a source)
    std::uint32_t triCount = 0;
    float area = 0.0f;             // the triangles' total area (world for a mesh, object space for a source)
    float weight = 0.0f;           // its selection weight: world area, approximate for a scaled copy
    float cdf = 0.0f;              // cumulative selection probability, ending at 1
};

// One source mesh drawn many times (spec section 68). Embree instances it: the triangles exist once
// and each copy is a 4x4 transform, which is the difference between Glowmere's scatter costing
// 6.8 million triangles of memory and costing a few thousand plus 63,527 matrices.
//
// The source is in OBJECT space -- unlike `TriangleMesh`, which is world space -- because that is
// the whole point: the geometry is shared and only the transforms differ.
struct InstancedObject {
    TriangleMesh source;                    // positions/normals in OBJECT space
    std::vector<glm::mat4> transforms;      // object -> world, one per instance
    std::vector<glm::mat3> normalMatrices;  // inverse transpose of each, precomputed once
    std::string name;
    // The records themselves (ADR-2246): their random, colour and emissive lanes, index and id are what a program
    // reads per instance and what procedural.wgsl multiplies the material by (`instColor`, `instEmissive`).
    std::vector<spatial::InstanceRecord> records;
    // procedural.wgsl's `proc.chroma`: x = living-chroma drift (0 = off), y = 2 pi / drift scale, z = 2 pi x speed
    // (ADR-057); w = 1 when a program writes emission and leaves the instance's variation to the engine (ADR-904).
    glm::vec4 chroma{0.0f};

    [[nodiscard]] bool valid() const { return source.valid() && !transforms.empty(); }
};

// ---- the snapshot --------------------------------------------------------------------------------

struct Snapshot {
    std::vector<TriangleMesh> meshes;
    // Instanced geometry, kept separate from `meshes` because Embree builds it differently and a
    // hit has to be resolved differently. Both end up in one top-level scene.
    std::vector<InstancedObject> instanced;
    std::vector<scene::PunctualLight> lights;
    // Each light's emitted colour (colour x temperature x intensity), parallel to `lights` (ADR-2242). It was
    // recomputed per light per sample -- 7% of a Neon Dreams trace -- and depends on nothing a sample changes.
    std::vector<glm::vec3> lightEmission;

    // Copied from the scene. `scene::Material`'s texture slots are indices into this, so the two
    // must travel together -- a snapshot that referenced the live scene's texture table would be
    // exactly the dangling reference the snapshot exists to avoid.
    std::vector<scene::TextureData> textures;

    scene::Camera camera;

    // Motion (spec section 62). `previousCamera` is the camera one frame earlier; a motion vector is
    // the screen-space difference between where a surface point is now and where it was then, so
    // BOTH the geometry and the camera have to be remembered -- a camera-only or geometry-only
    // motion pass is wrong in the half it omits.
    bool hasMotion = false;
    scene::Camera previousCamera;

    // What lights the scene from infinity and what the camera sees behind it (ADR-2240).
    Environment environment;

    // Emitters (ADR-2247): the groups, their triangles, and where each mesh's and each source's groups are.
    std::vector<EmitterGroup> emitterGroups;
    std::vector<EmitterTriangle> emitterTriangles;
    float totalEmitterWeight = 0.0f;
    std::vector<std::int32_t> meshEmitterGroup;      // per mesh: its group, or -1
    std::vector<std::int32_t> instancedEmitterGroup; // per instanced object: the group of its copy 0, or -1
    [[nodiscard]] bool hasEmitters() const { return !emitterGroups.empty(); }
    // ADR-2251: some surface is alpha-masked, so rays (shadow rays included) must look at the texels they hit.
    bool hasAlphaMask = false;

    CapabilityReport capabilities;

    // The material programs, as the scene holds them (ADR-2246); `TriangleMesh::program` indexes this.
    std::vector<scene::MaterialProgram> programs;
    // The render clock this frame is evaluated at -- the `FrameTime::renderTime` the engine was updated with, which is
    // the `time` (frame.params.x) the raster hands every material program for the same frame (ADR-2246).
    double renderTime = 0.0;

    // Triangles actually stored. With instancing this is much smaller than what the camera sees,
    // which is the point; `visibleTriangleCount` is the other number.
    [[nodiscard]] std::size_t triangleCount() const;
    [[nodiscard]] std::size_t visibleTriangleCount() const;
    [[nodiscard]] std::size_t instanceCount() const;
    [[nodiscard]] bool empty() const { return meshes.empty() && instanced.empty(); }
};

// Builds the snapshot from an already-evaluated scene. The caller is responsible for having called
// `Engine::update(FrameTime)` first: this function does no evaluation of its own, by design
// (section 7 -- the tracer must not duplicate scene evaluation).
// `previous` is the SAME scene evaluated one frame earlier. Pass null for no motion AOV. The two
// evaluations must come from the same composition without a re-flatten in between, or entity
// indices do not refer to the same objects; `buildSnapshot` checks vertex counts and refuses to
// match a mesh whose shape changed rather than producing a plausible, wrong vector.
// `renderTime` is the clock the scene was evaluated at (`FrameTime::renderTime`); material programs read it as `time`.
[[nodiscard]] Snapshot buildSnapshot(const scene::Scene& scene, const scene::Scene* previous = nullptr,
                                     double renderTime = 0.0);

} // namespace avgen::pathtrace

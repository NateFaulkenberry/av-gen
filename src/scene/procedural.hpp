#pragma once

// Procedural geometry (ADR-023): one source mesh, many instances placed by a distribution and
// perturbed by seeded variation, deformed per vertex on the GPU by an ordered deformer stack,
// shaded with the normal PBR material. Everything here is data; generators are pure functions.
//
// Transform order (world = parent * distribution * placement(i) * variation(i) * source):
//   1. `sourceTransform`      : applied to the source mesh (object-local space)
//   2. variation(i)           : seeded per-instance offset/rotation/scale, in placement space
//   3. placement(i)           : the distribution's transform for instance i
//   4. `distributionTransform`: the whole arrangement's transform
//   5. parent                 : the owning composition node / scene (applied by the caller)
// Local-space deformers act on positions before step 2 (object space); world-space deformers
// act after step 5 (world space). Coordinates are right-handed, +Y up, as everywhere in avgen.
//
// Determinism: instance randoms are `hashInstance(seed, index, channel)`; nothing depends on
// wall time or call order. Structural changes (source, distribution, variation, counts) bump
// `structureVersion` so the renderer rebuilds meshes/instances only then; everything else is a
// per-frame uniform.

#include "core/error.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

// ---- source geometry ---------------------------------------------------------------------------

enum class PrimitiveKind : std::uint8_t { Box, Cylinder, Sphere, Torus };
[[nodiscard]] const char* primitiveKindName(PrimitiveKind kind);
[[nodiscard]] std::optional<PrimitiveKind> primitiveKindFromName(std::string_view name);

struct SourceSpec {
    PrimitiveKind kind = PrimitiveKind::Cylinder;
    // Box
    glm::vec3 size{1.0f, 1.0f, 1.0f};
    int subdivisions = 1;          // per edge, 1..64
    // Cylinder (also uses height, radialSegments, heightSegments, caps)
    float radius = 0.5f;           // cylinder / sphere radius
    float height = 2.0f;           // cylinder height (centred on the origin)
    int radialSegments = 24;       // 3..256
    int heightSegments = 1;        // 1..128
    bool caps = true;
    // Sphere
    int segments = 32;             // longitude, 3..256
    int rings = 16;                // latitude, 2..128
    // Torus
    float majorRadius = 1.0f;
    float minorRadius = 0.25f;
    int majorSegments = 48;        // 3..256
    int minorSegments = 16;        // 3..128

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const; // changes whenever the mesh would change
};
// Deterministic unit-tested primitive generators (positions, smooth or faceted normals as the
// primitive dictates, uvs, CCW winding facing outwards, centred on the origin).
[[nodiscard]] MeshData makeBox(glm::vec3 size, int subdivisions);
[[nodiscard]] MeshData makeCylinder(float radius, float height, int radialSegments, int heightSegments, bool caps);
[[nodiscard]] MeshData makeUvSphere(float radius, int segments, int rings);
[[nodiscard]] MeshData makeTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments);
[[nodiscard]] Result<MeshData> makeSourceMesh(const SourceSpec& spec);

// ---- distributions ------------------------------------------------------------------------------

enum class DistributionKind : std::uint8_t { Single, Linear, Grid, Radial, Spiral };
[[nodiscard]] const char* distributionKindName(DistributionKind kind);
[[nodiscard]] std::optional<DistributionKind> distributionKindFromName(std::string_view name);

enum class OrientationMode : std::uint8_t { None, Outward, Inward, Tangent };
[[nodiscard]] const char* orientationModeName(OrientationMode mode);
[[nodiscard]] std::optional<OrientationMode> orientationModeFromName(std::string_view name);

enum class DistributionPlane : std::uint8_t { XZ, XY, YZ }; // radial/spiral plane; the normal is the third axis
[[nodiscard]] const char* distributionPlaneName(DistributionPlane plane);
[[nodiscard]] std::optional<DistributionPlane> distributionPlaneFromName(std::string_view name);

struct Distribution {
    DistributionKind kind = DistributionKind::Radial;
    int count = 32;                // Linear/Radial/Spiral total; Grid uses countX*countY*countZ
    // Linear
    glm::vec3 start{-5.0f, 0.0f, 0.0f};
    glm::vec3 end{5.0f, 0.0f, 0.0f};
    bool orientAlong = false;      // rotate instances to face the start->end direction
    float spacing = 0.0f;          // > 0 overrides the end point: end = start + dir * spacing * (count - 1)
    // Grid
    glm::ivec3 gridCount{4, 1, 4};
    glm::vec3 gridSpacing{2.0f, 2.0f, 2.0f}; // centred on the origin
    // Radial / Spiral
    float radius = 6.0f;
    float startAngle = 0.0f;       // radians
    float endAngle = 6.2831853f;   // radians; == start + 2pi means a closed circle (count divides 2pi)
    DistributionPlane plane = DistributionPlane::XZ;
    glm::vec3 center{0.0f};
    OrientationMode orientation = OrientationMode::Outward;
    // Spiral
    float radiusGrowth = 0.0f;     // radius added over the whole spiral
    float turns = 3.0f;
    float spiralHeight = 8.0f;     // rise over the whole spiral along the plane normal
    float spiralAngle = 0.0f;      // extra constant rotation about the normal (radians)

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] int instanceCount() const;
    // Placement of instance i (0..count-1) in distribution space: position, rotation (unit
    // quaternion), scale 1. Pure. `u` = normalised index (0 for i=0, 1 for the last; 0 when count==1).
    [[nodiscard]] Transform placement(int index) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
};

// ---- variation ----------------------------------------------------------------------------------

struct Variation {
    std::uint32_t seed = 12345;
    glm::vec3 randomPosition{0.0f};   // max offset per axis (units), uniform in [-r, r]
    glm::vec3 randomRotation{0.0f};   // max angle per axis (radians), uniform in [-r, r]
    glm::vec3 randomScale{0.0f};      // max relative scale per axis: scale *= 1 + [-r, r]
    float randomUniformScale = 0.0f;  // max relative uniform scale
    [[nodiscard]] std::uint64_t structuralHash() const;
};
// Deterministic per-instance random in [0, 1): PCG-style mix of (seed, index, channel).
[[nodiscard]] float hashInstance(std::uint32_t seed, std::uint32_t index, std::uint32_t channel);
// The variation transform for instance i (position offset, rotation, scale), pure.
[[nodiscard]] Transform variationTransform(const Variation& v, std::uint32_t index);

// ---- deformers ----------------------------------------------------------------------------------

enum class DeformerKind : std::uint8_t { Bend, Twist, Sine, Noise, Displacement };
[[nodiscard]] const char* deformerKindName(DeformerKind kind);
[[nodiscard]] std::optional<DeformerKind> deformerKindFromName(std::string_view name);
enum class DeformSpace : std::uint8_t { Local, World };
[[nodiscard]] const char* deformSpaceName(DeformSpace space);
[[nodiscard]] std::optional<DeformSpace> deformSpaceFromName(std::string_view name);

// Semantics (p = point, t = render time, a = amount; axis is a unit vector; center in the
// deformer's space; falloff = the extent along the axis over which the effect ramps 0..1 from
// the center, 0 = no ramp):
//   Bend:  bends around `axis` (the axis the shape is bent along); angle = a * (dot(p-c, axis) /
//          max(falloff, eps)) radians; points rotate about the perpendicular `bendAxis`... in this
//          implementation the bend is the classic Barr bend: the coordinate along `axis` maps to an
//          arc of radius 1/curvature with curvature = a (radians per unit).
//   Twist: rotation about `axis` by angle = a * dot(p - c, axis) (radians per unit) [+ speed * t].
//   Sine:  p += displacementAxis * a * sin(dot(p - c, axis) * frequency + phase + speed * t).
//   Noise: q = p * scale + time * speed; p += a * axisMask * (fbm3(q), fbm3(q + 31.7), fbm3(q + 67.3)) * 2 - 1
//          (three decorrelated channels of hash-based 3-octave value noise; identical on CPU and
//          GPU within float rounding).
//   Displacement: p += n * a * (fbm(p * scale + speed * t) * 2 - 1) — displacement
//          along the vertex normal; the pattern source is `pattern` (0 = noise now; texture/audio/
//          field/user later).
struct Deformer {
    DeformerKind kind = DeformerKind::Twist;
    bool enabled = true;
    float amount = 0.0f;
    DeformSpace space = DeformSpace::Local;
    float speed = 0.0f;                 // animation speed (radians or units per second)
    float phase = 0.0f;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};   // primary axis (normalised on use)
    glm::vec3 center{0.0f};
    float falloff = 0.0f;               // 0 = none
    float frequency = 1.0f;             // sine
    glm::vec3 displacementAxis{1.0f, 0.0f, 0.0f}; // sine
    float scale = 1.0f;                 // noise / displacement spatial frequency
    std::uint32_t seed = 1;             // noise
    glm::vec3 axisMask{1.0f, 1.0f, 1.0f}; // noise per-axis mask
    int pattern = 0;                    // displacement pattern source (0 = noise)
};
// CPU reference of the whole stack, identical in meaning to the GPU shader: applies the enabled
// deformers in order; `instanceWorld` is the instance's world matrix (local deformers apply to
// `p` in object space before it, world deformers to the world position after it). Returns the
// world-space position. Used by tests and tools.
[[nodiscard]] glm::vec3 deformPoint(const std::vector<Deformer>& stack, glm::vec3 objectPoint,
                                    const glm::mat4& instanceWorld, double time);
[[nodiscard]] glm::vec3 applyDeformer(const Deformer& d, glm::vec3 p, double time);
// The GPU-side noise, evaluated on the CPU (for tests): 3-octave value fBM in [0, 1].
[[nodiscard]] float fbm3(glm::vec3 p, std::uint32_t seed);
constexpr int kMaxDeformers = 8;

// ---- material variation ------------------------------------------------------------------------

struct MaterialVariation {
    float hueShift = 0.0f;          // per-instance random hue rotation amount (turns, 0..1)
    float hueGradient = 0.0f;       // hue rotation across normalised index (turns)
    float valueRandom = 0.0f;       // per-instance brightness variation (relative)
    float emissiveRandom = 0.0f;    // per-instance emissive multiplier variation (relative)
    float emissiveGradient = 0.0f;  // emissive multiplier ramp across normalised index (adds 0..x)
};

// ---- the procedural object ----------------------------------------------------------------------

struct InstanceRecord {
    glm::vec4 position;   // xyz, w = uniform scale hint (1)
    glm::vec4 rotation;   // unit quaternion (x, y, z, w)
    glm::vec4 scale;      // xyz, w = normalised index
    glm::vec4 random;     // four hashed randoms in [0, 1)
    glm::vec4 color;      // base colour multiplier (rgb), a = instance id
    glm::vec4 emissive;   // emissive multiplier (rgb), a = unused
};
static_assert(sizeof(InstanceRecord) == 96);

struct ProceduralGeometry {
    std::string name = "procedural";
    bool visible = true;
    SourceSpec source;
    Transform sourceTransform;
    Distribution distribution;
    Transform distributionTransform;
    Variation variation;
    std::vector<Deformer> deformers;     // ordered stack, at most kMaxDeformers
    Material material;
    MaterialVariation materialVariation;
    // Structural outputs (filled by rebuild()); the renderer uploads them when the version
    // changes. `structureVersion` is bumped by rebuild() whenever the structural hash changed.
    std::vector<InstanceRecord> instances;
    std::uint64_t structureVersion = 0;
    std::uint64_t meshHash = 0;           // hash of the last generated source (renderer cache key)
    std::uint64_t builtHash = 0;          // structuralHash() at the last rebuild() (internal bookkeeping)
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f}; // of instance origins + source extent (world of the object)

    [[nodiscard]] Result<void> validate() const;
    // Regenerates instances (and reports whether the source mesh must be regenerated) when the
    // structural inputs changed since the last call; cheap when nothing changed. Returns true
    // when something was rebuilt.
    bool rebuild();
    [[nodiscard]] std::uint64_t structuralHash() const;
    // Instance world matrix within the object (distribution * placement * variation * source).
    [[nodiscard]] glm::mat4 instanceMatrix(std::uint32_t index) const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<ProceduralGeometry> fromJson(const nlohmann::json& j);
};

// ---- parameters (rest/finals pattern, like particles) ------------------------------------------

// Every field that makes sense as a modulation target, registered under `prefix` (e.g.
// "procedural/columns/"): source/*, distribution/*, transform/*, variation/*, deform/<slot>/*
// (slot = 1-based, label "<kind>/<field>"), material/*, materialVariation/*. Enum/kind fields are
// int parameters; count/segment fields are ints. Group = prefix without the trailing '/'.
struct ProceduralParameters;
[[nodiscard]] ProceduralParameters registerProceduralParameters(params::ParameterSet& params,
                                                                const ProceduralGeometry& rest,
                                                                const std::string& prefix);
// Copies parameter finals into `live` (rest values are the scene-file authored values; the
// parameters' bases default to them). Calls live.rebuild() when structure changed. Returns true
// when a structural rebuild happened.
bool applyProceduralParameters(const ProceduralParameters& p, const ProceduralGeometry& rest, ProceduralGeometry& live);
void unregisterProceduralParameters(params::ParameterSet& params, const ProceduralParameters& p);

struct ProceduralParameters {
    std::string prefix;
    std::vector<params::IParameter*> all; // everything registered (for unregister)
    // Named handles for the showcase code and tests (nullptr when not registered).
    params::Parameter<float>* sourceRadius = nullptr;
    params::Parameter<float>* sourceHeight = nullptr;
    params::Parameter<glm::vec3>* sourceSize = nullptr;
    params::Parameter<int>* distributionCount = nullptr;
    params::Parameter<float>* distributionRadius = nullptr;
    params::Parameter<glm::vec3>* transformPosition = nullptr;
    params::Parameter<glm::vec3>* transformRotation = nullptr; // Euler degrees
    params::Parameter<glm::vec3>* transformScale = nullptr;
    params::Parameter<glm::vec3>* sourceScale = nullptr;
    params::Parameter<int>* seed = nullptr;
    std::array<params::Parameter<float>*, kMaxDeformers> deformerAmount{};
    std::array<params::Parameter<float>*, kMaxDeformers> deformerSpeed{};
    params::Parameter<glm::vec3>* baseColor = nullptr;
    params::Parameter<glm::vec3>* emissiveColor = nullptr;
    params::Parameter<float>* emissive = nullptr;
    params::Parameter<float>* roughness = nullptr;
    params::Parameter<float>* metallic = nullptr;
    params::Parameter<float>* hueShift = nullptr;
    params::Parameter<bool>* visible = nullptr;
};

} // namespace avgen::scene

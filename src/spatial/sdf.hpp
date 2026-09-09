#pragma once

// Signed distance fields (ADR-027): a data tree of primitives, boolean/smooth combinations,
// domain operations and displacements. `evaluate` is the CPU reference; `shaders/sdf.wgsl`
// interprets the same tree from a packed node array (post-order, explicit stack). Meshing uses
// naive surface nets over the object's bounds. Everything is pure and deterministic.
//
// Node semantics (p = local point of the node; children evaluated at the transformed point):
//   Primitives (exact distances, Quilez): Sphere(radius); Box(size = half extents);
//   RoundedBox(size, rounding); Cylinder(radius, height along Y); Capsule(radius, height along Y);
//   Torus(radius = major, rounding = minor); Plane(axis, offset = distance along axis); Cone(radius,
//   height: apex at +height/2 on Y, base at -height/2).
//   Combinations (children in order): Union min; Intersection max; Difference (a - b - c ...);
//   SmoothUnion/SmoothIntersection/SmoothDifference with `smooth` (polynomial smin, k = smooth).
//   Domain ops (one child): Translate(offset); Rotate(rotationDegrees); Scale(uniform `scale`,
//   distance rescaled); Twist(amount radians per unit along axis Y); Bend(amount radians per unit,
//   about Z bending X); Repeat(size = cell size per axis, 0 = no repeat on that axis; `count` limits
//   copies per side, 0 = infinite); PolarRepeat(count copies about Y); Mirror(axis mask via size:
//   > 0 mirrors that axis).
//   Displacement (one child): DisplaceNoise d += amount * (fbm3(p * frequency + speed*t) * 2 - 1);
//   DisplaceVoronoi d += amount * (voronoiF1(p * frequency) - 0.5); DisplaceWave d += amount *
//   sin(dot(p, axis) * frequency + speed * t); DisplaceField d += amount * fieldScalar(reference) (GPU
//   binds by name; CPU needs the field set).

#include "core/error.hpp"
#include "scene/scene_types.hpp"
#include "spatial/field.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::spatial {

enum class SdfNodeKind : std::uint8_t {
    Sphere, Box, RoundedBox, Cylinder, Capsule, Torus, Plane, Cone,
    Union, Intersection, Difference, SmoothUnion, SmoothIntersection, SmoothDifference,
    Translate, Rotate, Scale, Twist, Bend, Repeat, PolarRepeat, Mirror,
    DisplaceNoise, DisplaceVoronoi, DisplaceWave, DisplaceField,
};
[[nodiscard]] const char* sdfNodeKindName(SdfNodeKind kind);
[[nodiscard]] std::optional<SdfNodeKind> sdfNodeKindFromName(std::string_view name);
[[nodiscard]] bool sdfNodeIsPrimitive(SdfNodeKind kind);
[[nodiscard]] int sdfNodeMaxChildren(SdfNodeKind kind); // 0 primitives, 1 unary ops, 8 combinations

struct SdfNode {
    SdfNodeKind kind = SdfNodeKind::Sphere;
    bool enabled = true;
    float radius = 1.0f;
    float height = 2.0f;
    glm::vec3 size{1.0f};
    float rounding = 0.1f;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float offset = 0.0f;
    glm::vec3 translation{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    float scale = 1.0f;
    float amount = 0.0f;
    float smooth = 0.5f;
    float frequency = 1.0f;
    float speed = 0.0f;
    int count = 0;
    std::uint32_t seed = 1;
    std::string reference;               // DisplaceField: field name
    std::vector<SdfNode> children;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<SdfNode> fromJson(const nlohmann::json& j, int depth = 0);
};
constexpr int kMaxSdfNodes = 64;
constexpr int kMaxSdfDepth = 8;
constexpr int kMaxSdfStack = 8;

struct SdfTree {
    SdfNode root;
    [[nodiscard]] Result<void> validate() const;   // node count, depth, child arity
    [[nodiscard]] int nodeCount() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    // Distance at `p` (tree-local space) and time. `fields` for DisplaceField (0 when null).
    [[nodiscard]] float evaluate(const glm::vec3& p, double time, const FieldSet* fields = nullptr) const;
    [[nodiscard]] glm::vec3 normal(const glm::vec3& p, double time, float epsilon = 1e-3f,
                                   const FieldSet* fields = nullptr) const; // tetrahedron differences
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<SdfTree> fromJson(const nlohmann::json& j);
};

// Naive surface nets over [boundsMin, boundsMax] at `resolution` cells per axis (max 256), for
// static/authoring use; positions in tree-local space; smooth normals from the SDF gradient.
[[nodiscard]] Result<scene::MeshData> meshSdf(const SdfTree& tree, glm::vec3 boundsMin, glm::vec3 boundsMax,
                                              int resolution, double time = 0.0, const FieldSet* fields = nullptr);

// ---- GPU packing (post-order node array; see shaders/sdf.wgsl) ----------------------------------

// 112 bytes per node.
struct alignas(16) SdfNodeGpu {
    std::uint32_t kind;
    std::uint32_t childCount;    // combinations pop this many; unary ops 1; primitives 0
    std::int32_t fieldSlot;      // DisplaceField
    std::uint32_t seed;
    glm::vec4 p0;                // radius, height, rounding, offset
    glm::vec4 p1;                // size.xyz, scale
    glm::vec4 p2;                // axis.xyz, amount
    glm::vec4 p3;                // translation.xyz, smooth
    glm::vec4 p4;                // rotation quaternion (xyzw) for Rotate; frequency for displacements in .x otherwise
    glm::vec4 p5;                // frequency, speed, float(count), pad
};
static_assert(sizeof(SdfNodeGpu) == 112);
// Flattens the tree post-order (children before parent; disabled subtrees removed; parents of no
// enabled children become an "empty" far-away distance) into `out`; returns the node count.
[[nodiscard]] int packSdfTree(const SdfTree& tree, std::vector<SdfNodeGpu>& out, const FieldSet* fields = nullptr);
// CPU evaluation of a packed array (the exact GPU algorithm; tests compare it with evaluate()).
[[nodiscard]] float evaluatePacked(std::span<const SdfNodeGpu> nodes, const glm::vec3& p, double time,
                                   const FieldSet* fields = nullptr);

} // namespace avgen::spatial

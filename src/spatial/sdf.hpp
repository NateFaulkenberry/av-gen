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
//   > 0 mirrors that axis); Fold(axis = plane normal, offset: the half-space behind the plane
//   dot(p, n) = offset is reflected in front of it); Recurse (ADR-1001: the child is evaluated at
//   levels 0..count and the results are unioned, d = min_l child(p_l) / scale^l, with p_0 = p and
//   p_{l+1} = conj(R) * fold(p_l) * scale - translation, where fold = |p| on the axes whose `size`
//   component is > 0 and R = rotation. Nested architecture from one node).
//   Morph (ADR-1001, a combination): interpolates between consecutive enabled children by `amount`
//   in [0, k - 1]: a = clamp(amount), i = floor(a), d = c_i + (c_{i+1} - c_i) * (a - i). Only c_i and
//   c_{i+1} are evaluated (only c_i at an integer amount), so one float switches or morphs between
//   structural states and pays for at most two of them.
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
    Union, Intersection, Difference, SmoothUnion, SmoothIntersection, SmoothDifference, Morph,
    Translate, Rotate, Scale, Twist, Bend, Repeat, PolarRepeat, Mirror, Fold, Recurse,
    DisplaceNoise, DisplaceVoronoi, DisplaceWave, DisplaceField,
    // ADR-1040 (appended so the GPU kind numbers of the kinds above are unchanged): a staircase
    // primitive, the screw repeat (a cell repeated by a translation, or by a turn about Y plus a
    // rise), and a smooth vector-noise domain warp. The range predicates in sdf.cpp and
    // shaders/sdf.wgsl special-case them.
    Stairs, Screw, Warp,
    // ADR-1040: a hollow shell of `offset` thickness centred on the child's surface (onion): a room from one box.
    Shell,
    // ADR-1144 (appended; COMPILED trees only -- validate(Interpreter) refuses them, so the packed
    // interpreter and every object it draws are untouched): the anatomical vocabulary of THE ASTRAL FORGE.
    //   Ellipsoid(size = radii): Quilez's bound k0 (k0 - 1) / k1, k0 = |p / r|, k1 = |p / r^2|.
    //   TaperedCapsule(from, to, radius at `from`, radius2 at `to`): the segment's clamped parameter h,
    //     d = |p - from - (to - from) h| - mix(radius, radius2, h) (the prototype's sdCone; a bound).
    //   Octahedron(radius): Quilez's exact octahedron.
    //   Facet(size = radii, count planes, offset = support level, speed = turn rate, amount = jitter):
    //     max over `count` Fibonacci-sphere normals n_k of dot(p / size, n_k) - offset, times min(size);
    //     the plane set turns with time: crisp faces that are never still.
    //   Blend (a combination of exactly two children): c0 + (c1 - c0) w, w = amount (times
    //     smoothstep(offset - smooth, offset + smooth, dot(p, normalize(axis))) when axis is nonzero):
    //     half a face organic, half faceted.
    //   Fray (unary): a ragged rim. f = 1 + amount (valueNoise(p frequency + (0, 0, speed t), seed) - 0.5)
    //     m, m = smoothstep(radius, offset, |(p - translation) size|); d = f child(p / f) (a uniform scale
    //     that varies in space: an ellipsoid's radii scaled by f exactly).
    //   FarField (unary): a radial guide far from a thin feature. far = |(p - translation) size|; beyond
    //     `radius` d = far - offset and the child is not evaluated, inside it d = child(p). The bound of a
    //     flat feature is nearly a plane far away, and matter attracted from afar collapses onto it.
    Ellipsoid, TaperedCapsule, Octahedron, Facet, Blend, Fray, FarField,
};
// ADR-1144: kinds the packed interpreter does not run (a tree using one must be compiled).
[[nodiscard]] bool sdfNodeIsCompiledOnly(SdfNodeKind kind);
[[nodiscard]] const char* sdfNodeKindName(SdfNodeKind kind);
[[nodiscard]] std::optional<SdfNodeKind> sdfNodeKindFromName(std::string_view name);
[[nodiscard]] bool sdfNodeIsPrimitive(SdfNodeKind kind);
[[nodiscard]] int sdfNodeMaxChildren(SdfNodeKind kind); // 0 primitives, 1 unary ops, 8 combinations

// ADR-1044: surfaces per SDF object (base colour and emission per surface id).
inline constexpr int kMaxSdfSurfaces = 8;

struct SdfNode {
    SdfNodeKind kind = SdfNodeKind::Sphere;
    // ADR-1001: an optional name. A named node's parameters are `node/<name>/<field>` instead of
    // `node/<pre-order index>/<field>`. Letters, digits, '_' and '-'; not all digits; unique in a tree.
    std::string name;
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
    // ADR-1044: the surface (0..kMaxSdfSurfaces-1) this subtree is shaded with, or -1 to inherit. Read
    // only by a compiled tree (sdfSurface); structural (compiled in as a constant).
    int material = -1;
    // ADR-1144: TaperedCapsule's end points and its second radius (JSON "from", "to", "radius2").
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f, 1.0f, 0.0f};
    float radius2 = 0.5f;
    std::string reference;               // DisplaceField: field name
    std::vector<SdfNode> children;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<SdfNode> fromJson(const nlohmann::json& j, int depth = 0);
};
// ADR-1001 raised these from 64 and 8 for architectural trees (a state, its rules, a mirror, a
// repetition, a placement and a primitive is already 7 levels). The limits that bind the GPU are the
// two 8-entry interpreter stacks, which validate() checks independently of depth. ADR-1005 raised the
// depth to 16 and lifted the stack checks for compiled trees (SdfEvaluator::Compiled), which have no
// stacks: an interpreted tree is still bound by the stacks, a compiled one by the depth.
constexpr int kMaxSdfNodes = 96;
constexpr int kMaxSdfDepth = 16;
constexpr int kMaxSdfStack = 8;
constexpr int kMaxSdfLoops = 2;       // nested Recurse nodes (the interpreter's loop frames)
constexpr int kMaxSdfRecurseLevels = 8;

// Which evaluator a tree is validated for (ADR-1005). The interpreter (shaders/sdf.wgsl, and
// evaluatePacked) runs a program with two kMaxSdfStack-entry stacks; a compiled tree (sdfCompileWgsl)
// is straight-line code with no stacks, so only the node, depth and loop limits apply to it.
enum class SdfEvaluator { Interpreter, Compiled };

struct SdfTree {
    SdfNode root;
    // Node count, depth, child arity, parameters and names; for the interpreter also the two stacks.
    [[nodiscard]] Result<void> validate(SdfEvaluator evaluator = SdfEvaluator::Interpreter) const;
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
    std::int32_t fieldSlot;      // DisplaceField; a Recurse END record: the index of its BEGIN in the program
    std::uint32_t seed;
    glm::vec4 p0;                // radius, height, rounding, offset
    glm::vec4 p1;                // size.xyz, scale
    glm::vec4 p2;                // axis.xyz, amount
    glm::vec4 p3;                // translation.xyz, smooth (a Morph fold record: its weight)
    glm::vec4 p4;                // rotation quaternion (xyzw) for Rotate and Recurse; frequency for displacements in .x otherwise
    glm::vec4 p5;                // frequency, speed, float(count), pad
};
static_assert(sizeof(SdfNodeGpu) == 112);
// Flattens the tree post-order (children before parent; disabled subtrees removed; parents of no
// enabled children become an "empty" far-away distance) into `out`; returns the node count.
[[nodiscard]] int packSdfTree(const SdfTree& tree, std::vector<SdfNodeGpu>& out, const FieldSet* fields = nullptr);
// ADR-1003: the tree compiled to straight-line WGSL. `sdfCompileWgsl` returns the source of
//   fn sdfField(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32
// which evaluates the effective tree with the same helpers the interpreter uses (sdfPrimitive,
// sdfCombine, sdfWarp, sdfFinishUnary, sdfRecurseStep) and reads every node's parameters from
// sdfNodes[offset + k], k = the node's slot in `table` (one record per effective node, pre-order).
// Parameters stay live: only the structure is compiled. `sdfCompileKey` hashes exactly what the
// source depends on (kinds, child structure, enabled flags), so a value change never recompiles
// (a morph's amount, a count, a size, ...) and a structural one always does.
[[nodiscard]] std::string sdfCompileWgsl(const SdfTree& tree, std::vector<SdfNodeGpu>& table,
                                         const FieldSet* fields = nullptr);
[[nodiscard]] std::uint64_t sdfCompileKey(const SdfTree& tree);
// ADR-1154: the tree's domain chain -- how many enabled unary nodes (Recurse excluded) lead from the root to its
// first primitive or combination. They are the first records of both the packed program and the compiled table
// (both are pre-order), so a shader carries a point into the tree's domain by their sdfWarp in turn.
[[nodiscard]] int sdfDomainChainLength(const SdfTree& tree);
// Fills `table` exactly as sdfCompileWgsl does (per frame; no source generated).
void sdfCompileTable(const SdfTree& tree, std::vector<SdfNodeGpu>& table, const FieldSet* fields = nullptr);

// CPU evaluation of a packed array (the exact GPU algorithm; tests compare it with evaluate()).
[[nodiscard]] float evaluatePacked(std::span<const SdfNodeGpu> nodes, const glm::vec3& p, double time,
                                   const FieldSet* fields = nullptr);

} // namespace avgen::spatial

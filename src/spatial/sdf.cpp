// Signed distance fields (ADR-027): CPU reference evaluation of the node tree, tetrahedron
// normals, validation, structural hashing, JSON, naive surface nets meshing, GPU packing and the
// CPU interpreter of the packed array (the algorithm `shaders/sdf.wgsl` transliterates).
//
// Conventions chosen here (the header fixes the node semantics; these are the remaining choices):
//
// * Distances are Inigo Quilez's exact formulas (https://iquilezles.org/articles/distfunctions/).
//   Cylinder/Capsule/Cone use h = height / 2. Capsule: the segment runs from (0,-h,0) to (0,h,0)
//   and the caps add `radius` beyond it (total length height + 2 radius). Cone (apex at +h on Y,
//   base of `radius` at -h): with w = (|p.xz|, p.y - h) and q = (radius, -height):
//     a = w - q * clamp(dot(w, q) / dot(q, q), 0, 1)
//     b = w - q * (clamp(w.x / q.x, 0, 1), 1)
//     k = sign(q.y) (= -1), d = min(dot(a, a), dot(b, b))
//     s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y))
//     sdCone = sqrt(d) * sign(s)
//   Plane normalises `axis` (a zero axis falls back to +Y; validate rejects it).
// * Combinations fold their enabled children in order: d = c0, then d = op(d, ci). Difference is
//   d = max(d, -ci); smooth ops use the polynomial smin (k = max(smooth, 1e-4)):
//     smin(a, b, k): h = clamp(0.5 + 0.5 (b - a) / k, 0, 1); mix(b, a, h) - k h (1 - h)
//   smoothIntersection = -smin(-a, -b, k), smoothDifference = -smin(-a, b, k). A combination with
//   no enabled children evaluates to kFar = 1e9.
// * Disabled nodes are absent: a disabled unary op is replaced by its (first) child; a disabled
//   primitive or combination is skipped by its parent combination, and evaluates to kFar when it
//   is the root or the child of a unary op (the op is then applied to kFar, e.g. Scale gives
//   kFar * scale; both paths do the same). A unary op with several children uses the first.
// * Domain ops (q = the child's point): Translate q = p - translation; Rotate q = conj(R) * p with
//   R = quat(radians(rotationDegrees)) (glm: Rz * Ry * Rx, like composition nodes); Scale
//   q = p / scale, d = child(q) * scale; Twist (Quilez opTwist about Y) angle = amount * p.y,
//   q = (c x + s z, y, -s x + c z) with c = cos(angle), s = sin(angle) -- d is not rescaled, so
//   the result is only a bound (Lipschitz constant > 1; ray marchers must under-step); Bend
//   (Quilez opCheapBend about Z) c = cos(amount x), s = sin(amount x), q = (c x - s y, s x + c y, z),
//   same caveat; Repeat per axis with size > 0: q = p - size * clamp(rnd(p / size), -count, count)
//   (count = 0: no clamp) with rnd(x) = floor(x + 0.5) on both paths (WGSL's round() is
//   half-to-even, floor(x + 0.5) is not); PolarRepeat (count >= 1) about Y: sector = 2 pi / count,
//   a = atan2(p.z, p.x), a' = a - sector * rnd(a / sector), q = (cos a' r, p.y, sin a' r) with
//   r = |p.xz|; Mirror q_i = |p_i| where size_i > 0.
// * Displacements: t = float(time); Noise d += amount (fbm3(p f + vec3(speed t), seed) 2 - 1);
//   Voronoi d += amount (voronoiF1(p f, seed) - 0.5); Wave d += amount sin(dot(p, axis) f + speed t)
//   (axis as given, not normalised); Field d += amount sampleScalar(field, p, time, fields), 0 when
//   the field set or the name is missing.
// * Normal: tetrahedron differences, n = sum_i k_i f(p + k_i eps) with k in {(1,-1,-1), (-1,-1,1),
//   (-1,1,-1), (1,1,1)}, normalised (+Y when degenerate).
//
// Packed execution scheme (packSdfTree / evaluatePacked; the WGSL interpreter mirrors it exactly):
//
//   The packed array is a linear program. The interpreter keeps
//     dist[kMaxSdfStack]  a stack of distances (sp = count),
//     pts[kMaxSdfStack]   a stack of saved points (pp = count),
//     cur                 the current point (starts at the query point p).
//   Node classes by `childCount`:
//     primitive          childCount = 0        push d(kind, cur)
//     combination        childCount = n        n == 0: push kFar (a combination with no enabled
//                                              children). Otherwise pop the top n entries, which sit
//                                              at dist[sp-n .. sp-1] in evaluation order, fold
//                                              d = dist[sp-n], d = op(d, dist[sp-n+j]) for j = 1..n-1
//                                              and push d. The packer only ever emits n = 0 or n = 2:
//                                              a combination with k >= 1 enabled children c0..ck-1 is
//                                              emitted as c0, c1, OP, c2, OP, ..., ck-1, OP (one binary
//                                              fold node after every child but the first; k == 1
//                                              emits nothing). The fold order equals evaluate()'s and
//                                              the distance stack never exceeds the combination
//                                              nesting depth + 1.
//     unary BEGIN        childCount = 0xFFFF   emitted BEFORE the child's subtree: push cur on pts;
//                                              domain ops set cur = warp(kind, cur) (Scale divides by
//                                              `scale`), displacements leave cur unchanged.
//     unary END          childCount = 1        emitted AFTER the subtree: cur = pop pts (the op's own
//                                              local point); then Scale multiplies dist[sp-1] by
//                                              `scale`, displacements add their term evaluated at
//                                              cur to dist[sp-1], other domain ops change nothing.
//   Every unary op (domain and displacement) is exactly one BEGIN + subtree + END, and a
//   combination with k enabled children costs max(k - 1, 1 if k == 0) nodes, so the packed count is
//   <= 2 * kMaxSdfNodes = kMaxSdfPackedNodes. A unary op whose child is absent (disabled) wraps an
//   empty combination (kind Union, childCount 0). Disabled subtrees are not emitted. The final
//   distance is dist[sp-1] (kFar for an empty program).
//   Stack use is bounded: validate() checks that the distance stack (1 + the number of enclosing
//   combinations in which the path is not the first enabled child) and the point stack (nesting
//   of unary ops) both fit in kMaxSdfStack. Over/underflow in evaluatePacked returns kFar instead
//   of reading out of bounds.
//   Per-node payload (SdfNodeGpu): p0 = (radius, height, rounding, offset); p1 = (size, scale);
//   p2 = (axis, amount); p3 = (translation, smooth); p4 = the FORWARD rotation quaternion (x, y, z,
//   w) for Rotate (the interpreter applies its conjugate), otherwise (frequency, 0, 0, 0);
//   p5 = (frequency, speed, float(count), 0); seed; fieldSlot = index of `reference` in the field
//   set for DisplaceField (-1 when unbound: displacement 0).
//
// Meshing (naive surface nets): values on the (resolution + 1)^3 lattice over the bounds; every
// cell whose corners differ in sign gets one vertex at the mean of the linear edge crossings;
// every lattice edge with a sign change joins the four cells around it with two triangles (the
// quad ordered counter-clockwise about the edge's axis when the lower corner is inside, reversed
// otherwise: outward winding). Cells are visited x-fastest, edges by lattice point (x-fastest)
// then axis, so the output is deterministic. Vertex normals are tree normals with epsilon a quarter
// of the smallest cell edge; uv = 0.

#include "spatial/sdf.hpp"

#include "core/noise.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace avgen::spatial {

using nlohmann::json;

namespace {

constexpr float kFar = 1e9f;
constexpr std::uint32_t kBeginMarker = 0xFFFFu;
constexpr int kMaxSdfPackedNodes = kMaxSdfNodes * 2;
constexpr float kTwoPi = 6.283185307179586f;

constexpr std::array<const char*, 29> kKindNames = {
    "sphere",       "box",           "roundedBox",         "cylinder",         "capsule",   "torus",
    "plane",        "cone",          "union",              "intersection",     "difference", "smoothUnion",
    "smoothIntersection", "smoothDifference", "morph", "translate", "rotate",  "scale",     "twist",
    "bend",         "repeat",        "polarRepeat",        "mirror",           "fold",      "recurse",
    "displaceNoise", "displaceVoronoi", "displaceWave", "displaceField",
};

bool isCombination(SdfNodeKind kind) {
    return kind >= SdfNodeKind::Union && kind <= SdfNodeKind::Morph;
}

bool isUnary(SdfNodeKind kind) {
    return kind >= SdfNodeKind::Translate;
}

bool isDisplacement(SdfNodeKind kind) {
    return kind >= SdfNodeKind::DisplaceNoise;
}

// The node that stands in for `n` once disabled nodes are removed: a disabled unary op passes
// through to its child; a disabled primitive/combination is absent (nullptr).
const SdfNode* effective(const SdfNode& n) {
    const SdfNode* cur = &n;
    while (!cur->enabled) {
        if (isUnary(cur->kind) && !cur->children.empty()) {
            cur = &cur->children[0];
        } else {
            return nullptr;
        }
    }
    return cur;
}

const SdfNode* effectiveChild(const SdfNode& unary) {
    return unary.children.empty() ? nullptr : effective(unary.children[0]);
}

// ---- shared maths (both evaluation paths go through these) -------------------------------------

struct NodeParams {
    float radius = 1.0f;
    float height = 2.0f;
    float rounding = 0.1f;
    float offset = 0.0f;
    glm::vec3 size{1.0f};
    float scale = 1.0f;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float amount = 0.0f;
    glm::vec3 translation{0.0f};
    float smooth = 0.5f;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float frequency = 1.0f;
    float speed = 0.0f;
    int count = 0;
    std::uint32_t seed = 1;
    const FieldSpec* field = nullptr;
};

NodeParams paramsOf(const SdfNode& n, const FieldSet* fields) {
    NodeParams p;
    p.radius = n.radius;
    p.height = n.height;
    p.rounding = n.rounding;
    p.offset = n.offset;
    p.size = n.size;
    p.scale = n.scale;
    p.axis = n.axis;
    p.amount = n.amount;
    p.translation = n.translation;
    p.smooth = n.smooth;
    p.rotation = glm::quat(glm::radians(n.rotationDegrees));
    p.frequency = n.frequency;
    p.speed = n.speed;
    p.count = n.count;
    p.seed = n.seed;
    if (n.kind == SdfNodeKind::DisplaceField && fields != nullptr) {
        p.field = fields->find(n.reference);
    }
    return p;
}

NodeParams paramsOf(const SdfNodeGpu& g, const FieldSet* fields) {
    NodeParams p;
    p.radius = g.p0.x;
    p.height = g.p0.y;
    p.rounding = g.p0.z;
    p.offset = g.p0.w;
    p.size = glm::vec3(g.p1);
    p.scale = g.p1.w;
    p.axis = glm::vec3(g.p2);
    p.amount = g.p2.w;
    p.translation = glm::vec3(g.p3);
    p.smooth = g.p3.w;
    p.rotation = glm::quat(g.p4.w, g.p4.x, g.p4.y, g.p4.z);
    p.frequency = g.p5.x;
    p.speed = g.p5.y;
    p.count = static_cast<int>(g.p5.z);
    p.seed = g.seed;
    if (g.kind == static_cast<std::uint32_t>(SdfNodeKind::DisplaceField) && fields != nullptr && g.fieldSlot >= 0 &&
        static_cast<std::size_t>(g.fieldSlot) < fields->fields.size()) {
        p.field = &fields->fields[static_cast<std::size_t>(g.fieldSlot)];
    }
    return p;
}

float vmax(const glm::vec3& v) {
    return std::max(v.x, std::max(v.y, v.z));
}

float sdSphere(const glm::vec3& p, float r) {
    return glm::length(p) - r;
}

float sdBox(const glm::vec3& p, const glm::vec3& b) {
    const glm::vec3 q = glm::abs(p) - b;
    return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(vmax(q), 0.0f);
}

float sdRoundBox(const glm::vec3& p, const glm::vec3& b, float r) {
    const glm::vec3 q = glm::abs(p) - b + r;
    return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(vmax(q), 0.0f) - r;
}

float sdCappedCylinder(const glm::vec3& p, float r, float h) {
    const glm::vec2 d = glm::abs(glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y)) - glm::vec2(r, h);
    return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
}

float sdCapsule(const glm::vec3& p, float r, float h) {
    const glm::vec3 q(p.x, p.y - glm::clamp(p.y, -h, h), p.z);
    return glm::length(q) - r;
}

float sdTorus(const glm::vec3& p, float major, float minor) {
    const glm::vec2 q(glm::length(glm::vec2(p.x, p.z)) - major, p.y);
    return glm::length(q) - minor;
}

glm::vec3 safeNormalize(const glm::vec3& v) {
    const float len = glm::length(v);
    return len > 1e-8f ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

float sdPlane(const glm::vec3& p, const glm::vec3& axis, float offset) {
    return glm::dot(p, safeNormalize(axis)) - offset;
}

float signOf(float v) {
    return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
}

// Quilez sdCone (exact), apex at +height/2, base of `radius` at -height/2.
float sdCone(const glm::vec3& p, float radius, float height) {
    const glm::vec2 q(radius, -height);
    const glm::vec2 w(glm::length(glm::vec2(p.x, p.z)), p.y - height * 0.5f);
    const float qq = std::max(glm::dot(q, q), 1e-12f);
    const glm::vec2 a = w - q * glm::clamp(glm::dot(w, q) / qq, 0.0f, 1.0f);
    const glm::vec2 b = w - q * glm::vec2(glm::clamp(w.x / std::max(q.x, 1e-6f), 0.0f, 1.0f), 1.0f);
    const float k = signOf(q.y);
    const float d = std::min(glm::dot(a, a), glm::dot(b, b));
    const float s = std::max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return std::sqrt(d) * signOf(s);
}

float primitiveDistance(SdfNodeKind kind, const NodeParams& n, const glm::vec3& p) {
    switch (kind) {
    case SdfNodeKind::Sphere:
        return sdSphere(p, n.radius);
    case SdfNodeKind::Box:
        return sdBox(p, n.size);
    case SdfNodeKind::RoundedBox:
        return sdRoundBox(p, n.size, n.rounding);
    case SdfNodeKind::Cylinder:
        return sdCappedCylinder(p, n.radius, n.height * 0.5f);
    case SdfNodeKind::Capsule:
        return sdCapsule(p, n.radius, n.height * 0.5f);
    case SdfNodeKind::Torus:
        return sdTorus(p, n.radius, n.rounding);
    case SdfNodeKind::Plane:
        return sdPlane(p, n.axis, n.offset);
    case SdfNodeKind::Cone:
        return sdCone(p, n.radius, n.height);
    default:
        return kFar;
    }
}

float smin(float a, float b, float k) {
    const float h = glm::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return (b * (1.0f - h) + a * h) - k * h * (1.0f - h);
}

float combine(SdfNodeKind kind, float d, float c, float smooth) {
    const float k = std::max(smooth, 1e-4f);
    switch (kind) {
    case SdfNodeKind::Union:
        return std::min(d, c);
    case SdfNodeKind::Intersection:
        return std::max(d, c);
    case SdfNodeKind::Difference:
        return std::max(d, -c);
    case SdfNodeKind::SmoothUnion:
        return smin(d, c, k);
    case SdfNodeKind::SmoothIntersection:
        return -smin(-d, -c, k);
    case SdfNodeKind::SmoothDifference:
        return -smin(-d, c, k);
    case SdfNodeKind::Morph:
        return d + (c - d) * smooth; // ADR-1001: `smooth` carries this fold's weight
    default:
        return d;
    }
}

// ADR-1001: which enabled children a Morph blends. a = amount clamped to [0, children - 1]; the result
// is c_first + (c_second - c_first) * weight with first = floor(a), second = first + 1 and weight =
// a - first. Only those (one when weight is 0) are evaluated or packed, so a settled morph costs one
// structure, and the far value (1e9) of an unrelated child never enters the arithmetic.
struct MorphPick {
    int first = 0;
    int second = -1; // -1: `first` alone
    float weight = 0.0f;
};
MorphPick morphPick(float amount, int children) {
    MorphPick m;
    if (children <= 0) {
        return m;
    }
    const float a = glm::clamp(amount, 0.0f, static_cast<float>(children - 1));
    m.first = std::min(static_cast<int>(std::floor(a)), children - 1);
    m.weight = a - static_cast<float>(m.first);
    if (m.weight > 0.0f && m.first + 1 < children) {
        m.second = m.first + 1;
    }
    return m;
}

float rnd(float x) {
    return std::floor(x + 0.5f);
}

// The child's point for a domain op (displacements return p unchanged).
glm::vec3 warpPoint(SdfNodeKind kind, const NodeParams& n, const glm::vec3& p) {
    switch (kind) {
    case SdfNodeKind::Translate:
        return p - n.translation;
    case SdfNodeKind::Rotate:
        return glm::conjugate(n.rotation) * p;
    case SdfNodeKind::Scale:
        return p / n.scale;
    case SdfNodeKind::Twist: {
        const float angle = n.amount * p.y;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return glm::vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    }
    case SdfNodeKind::Bend: {
        const float c = std::cos(n.amount * p.x);
        const float s = std::sin(n.amount * p.x);
        return glm::vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
    }
    case SdfNodeKind::Repeat: {
        glm::vec3 q = p;
        const float limit = static_cast<float>(n.count);
        for (glm::length_t i = 0; i < 3; ++i) {
            const float size = n.size[i];
            if (size > 0.0f) {
                float cell = rnd(p[i] / size);
                if (n.count > 0) {
                    cell = glm::clamp(cell, -limit, limit);
                }
                q[i] = p[i] - size * cell;
            }
        }
        return q;
    }
    case SdfNodeKind::PolarRepeat: {
        if (n.count <= 0) {
            return p;
        }
        const float sector = kTwoPi / static_cast<float>(n.count);
        const float r = glm::length(glm::vec2(p.x, p.z));
        const float a = std::atan2(p.z, p.x);
        const float a2 = a - sector * rnd(a / sector);
        return glm::vec3(std::cos(a2) * r, p.y, std::sin(a2) * r);
    }
    case SdfNodeKind::Mirror:
        return glm::vec3(n.size.x > 0.0f ? std::fabs(p.x) : p.x, n.size.y > 0.0f ? std::fabs(p.y) : p.y,
                         n.size.z > 0.0f ? std::fabs(p.z) : p.z);
    case SdfNodeKind::Fold: {
        // ADR-1001: reflect the half-space behind the plane dot(p, n) = offset in front of it.
        const glm::vec3 axis = safeNormalize(n.axis);
        return p - 2.0f * std::min(glm::dot(p, axis) - n.offset, 0.0f) * axis;
    }
    default:
        return p; // Recurse: level 0 is the node's own point; recurseStep makes the next levels
    }
}

// ADR-1001: one Recurse level, p_{l+1} = conj(R) * fold(p_l) * scale - translation.
glm::vec3 recurseStep(const NodeParams& n, const glm::vec3& p) {
    glm::vec3 q(n.size.x > 0.0f ? std::fabs(p.x) : p.x, n.size.y > 0.0f ? std::fabs(p.y) : p.y,
                n.size.z > 0.0f ? std::fabs(p.z) : p.z);
    q = glm::conjugate(n.rotation) * q;
    return q * n.scale - n.translation;
}

float displace(SdfNodeKind kind, const NodeParams& n, float d, const glm::vec3& p, double time, const FieldSet* fields) {
    const float t = static_cast<float>(time);
    switch (kind) {
    case SdfNodeKind::DisplaceNoise:
        return d + n.amount * (noise::fbm3(p * n.frequency + glm::vec3(n.speed * t), n.seed) * 2.0f - 1.0f);
    case SdfNodeKind::DisplaceVoronoi:
        return d + n.amount * (noise::voronoiF1(p * n.frequency, n.seed) - 0.5f);
    case SdfNodeKind::DisplaceWave:
        return d + n.amount * std::sin(glm::dot(p, n.axis) * n.frequency + n.speed * t);
    case SdfNodeKind::DisplaceField:
        return d + n.amount * (n.field != nullptr ? sampleScalar(*n.field, p, time, fields) : 0.0f);
    default:
        return d;
    }
}

// Applies the "END" half of a unary op to the child's distance (p = the op's own local point).
float finishUnary(SdfNodeKind kind, const NodeParams& n, float d, const glm::vec3& p, double time, const FieldSet* fields) {
    if (kind == SdfNodeKind::Scale) {
        return d * n.scale;
    }
    if (isDisplacement(kind)) {
        return displace(kind, n, d, p, time, fields);
    }
    return d;
}

// ---- tree evaluation --------------------------------------------------------------------------

float evalEffective(const SdfNode* node, const glm::vec3& p, double time, const FieldSet* fields, int depth) {
    if (node == nullptr || depth > kMaxSdfDepth) {
        return kFar;
    }
    const SdfNodeKind kind = node->kind;
    if (sdfNodeIsPrimitive(kind)) {
        return primitiveDistance(kind, paramsOf(*node, fields), p);
    }
    if (isCombination(kind)) {
        int enabledChildren = 0;
        for (const SdfNode& child : node->children) {
            enabledChildren += effective(child) != nullptr ? 1 : 0;
        }
        const MorphPick pick = morphPick(node->amount, enabledChildren);
        int index = 0;
        int folded = 0;
        float d = kFar;
        for (const SdfNode& child : node->children) {
            const SdfNode* e = effective(child);
            if (e == nullptr) {
                continue;
            }
            const int own = index++;
            float k = node->smooth;
            if (kind == SdfNodeKind::Morph) {
                if (own != pick.first && own != pick.second) {
                    continue;
                }
                k = pick.weight;
            }
            const float c = evalEffective(e, p, time, fields, depth + 1);
            d = folded == 0 ? c : combine(kind, d, c, k);
            ++folded;
        }
        return d;
    }
    const NodeParams params = paramsOf(*node, fields);
    if (kind == SdfNodeKind::Recurse) {
        // The packed interpreter's loop, level by level, in the same operation order.
        const SdfNode* child = effectiveChild(*node);
        glm::vec3 q = p;
        float acc = kFar;
        float invScale = 1.0f;
        for (int level = 0;; ++level) {
            acc = std::min(acc, evalEffective(child, q, time, fields, depth + 1) * invScale);
            if (level >= params.count) {
                break;
            }
            q = recurseStep(params, q);
            invScale = invScale / params.scale;
        }
        return acc;
    }
    const glm::vec3 q = warpPoint(kind, params, p);
    const float d = evalEffective(effectiveChild(*node), q, time, fields, depth + 1);
    return finishUnary(kind, params, d, p, time, fields);
}

// ---- validation helpers ------------------------------------------------------------------------

bool finite(float v) {
    return std::isfinite(v);
}

bool finite(const glm::vec3& v) {
    return finite(v.x) && finite(v.y) && finite(v.z);
}

bool nonNegative(const glm::vec3& v) {
    return v.x >= 0.0f && v.y >= 0.0f && v.z >= 0.0f;
}

const char* kindLabel(SdfNodeKind kind) {
    return sdfNodeKindName(kind);
}

Result<void> validateNode(const SdfNode& n, int depth, int& count) {
    if (depth > kMaxSdfDepth) {
        return fail("sdf tree deeper than {} levels", kMaxSdfDepth);
    }
    if (++count > kMaxSdfNodes) {
        return fail("sdf tree has more than {} nodes", kMaxSdfNodes);
    }
    const char* label = kindLabel(n.kind);
    if (!finite(n.radius) || !finite(n.height) || !finite(n.size) || !finite(n.rounding) || !finite(n.axis) ||
        !finite(n.offset) || !finite(n.translation) || !finite(n.rotationDegrees) || !finite(n.scale) ||
        !finite(n.amount) || !finite(n.smooth) || !finite(n.frequency) || !finite(n.speed)) {
        return fail("sdf node '{}' has a non-finite parameter", label);
    }
    if (n.radius < 0.0f || n.height < 0.0f || n.rounding < 0.0f || !nonNegative(n.size)) {
        return fail("sdf node '{}': radius, height, rounding and size must be >= 0", label);
    }
    if (n.count < 0) {
        return fail("sdf node '{}': count must be >= 0", label);
    }
    if (n.kind == SdfNodeKind::Scale && !(n.scale > 0.0f)) {
        return fail("sdf node 'scale': scale must be > 0");
    }
    if ((n.kind == SdfNodeKind::Plane || n.kind == SdfNodeKind::Fold) && glm::length(n.axis) < 1e-8f) {
        return fail("sdf node '{}' has a zero axis", label);
    }
    if (n.kind == SdfNodeKind::Recurse) {
        if (!(n.scale > 0.0f)) {
            return fail("sdf node 'recurse': scale must be > 0");
        }
        if (n.count > kMaxSdfRecurseLevels) {
            return fail("sdf node 'recurse': count must be <= {} (got {})", kMaxSdfRecurseLevels, n.count);
        }
    }
    if (!n.name.empty()) {
        bool digitsOnly = true;
        for (const char c : n.name) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                            c == '-';
            if (!ok) {
                return fail("sdf node name '{}': only letters, digits, '_' and '-' are allowed", n.name);
            }
            digitsOnly = digitsOnly && c >= '0' && c <= '9';
        }
        if (digitsOnly) {
            return fail("sdf node name '{}' is all digits, which would read as a node index", n.name);
        }
    }
    const auto childCount = static_cast<int>(n.children.size());
    if (sdfNodeIsPrimitive(n.kind)) {
        if (childCount != 0) {
            return fail("sdf primitive '{}' cannot have children", label);
        }
    } else if (isUnary(n.kind)) {
        if (childCount != 1) {
            return fail("sdf node '{}' needs exactly one child (got {})", label, childCount);
        }
        // A disabled unary child passes through to its own child (effective()), so only a disabled
        // primitive or combination leaves this op without a child (ADR-1001: a rule node such as a
        // fold can be switched off by its `enabled` parameter wherever it sits).
        if (!n.children[0].enabled && !isUnary(n.children[0].kind)) {
            return fail("sdf node '{}': its child must be enabled", label);
        }
    } else {
        if (childCount < 1 || childCount > sdfNodeMaxChildren(n.kind)) {
            return fail("sdf node '{}' needs 1..{} children (got {})", label, sdfNodeMaxChildren(n.kind), childCount);
        }
    }
    for (const SdfNode& child : n.children) {
        if (auto ok = validateNode(child, depth + 1, count); !ok) {
            return ok;
        }
    }
    return {};
}

// Packed-program metrics over the effective tree (nullptr = the empty combination).
struct PackedMetrics {
    int nodes = 0;      // packed node count
    int distStack = 0;  // distance stack entries needed
    int pointStack = 0; // point stack entries needed
};

PackedMetrics packedMetrics(const SdfNode* node) {
    PackedMetrics m;
    if (node == nullptr || sdfNodeIsPrimitive(node->kind)) {
        m.nodes = 1;
        m.distStack = 1;
        return m;
    }
    if (isCombination(node->kind)) {
        int evaluated = 0;
        for (const SdfNode& child : node->children) {
            const SdfNode* e = effective(child);
            if (e == nullptr) {
                continue;
            }
            const PackedMetrics c = packedMetrics(e);
            const int held = evaluated > 0 ? 1 : 0; // the running fold result stays on the stack
            m.nodes += c.nodes + held;
            m.distStack = std::max(m.distStack, held + c.distStack);
            m.pointStack = std::max(m.pointStack, c.pointStack);
            ++evaluated;
        }
        if (evaluated == 0) {
            m.nodes = 1;
            m.distStack = 1;
        }
        return m;
    }
    const PackedMetrics c = packedMetrics(effectiveChild(*node));
    m.nodes = c.nodes + 2;
    m.distStack = c.distStack;
    m.pointStack = c.pointStack + 1;
    return m;
}

// The deepest nesting of enabled Recurse nodes (the packed interpreter's loop frames).
int recurseNesting(const SdfNode* node) {
    if (node == nullptr) {
        return 0;
    }
    int deepest = 0;
    if (isUnary(node->kind)) {
        deepest = recurseNesting(effectiveChild(*node));
    } else {
        for (const SdfNode& child : node->children) {
            deepest = std::max(deepest, recurseNesting(effective(child)));
        }
    }
    return deepest + (node->kind == SdfNodeKind::Recurse ? 1 : 0);
}

Result<void> uniqueNames(const SdfNode& n, std::vector<std::string>& seen) {
    if (!n.name.empty()) {
        if (std::find(seen.begin(), seen.end(), n.name) != seen.end()) {
            return fail("sdf node name '{}' is used twice", n.name);
        }
        seen.push_back(n.name);
    }
    for (const SdfNode& child : n.children) {
        if (auto ok = uniqueNames(child, seen); !ok) {
            return ok;
        }
    }
    return {};
}

// ---- hashing (FNV-1a over bit patterns) ----------------------------------------------------------

class StructHash {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h_ = (h_ ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    }
    void i32(int v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); } // -0 == +0
    void boolean(bool v) { u32(v ? 1u : 0u); }
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        for (const char c : s) {
            u32(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
        }
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

void hashNode(StructHash& h, const SdfNode& n) {
    h.u32(static_cast<std::uint32_t>(n.kind));
    h.str(n.name);
    h.boolean(n.enabled);
    h.f32(n.radius);
    h.f32(n.height);
    h.v3(n.size);
    h.f32(n.rounding);
    h.v3(n.axis);
    h.f32(n.offset);
    h.v3(n.translation);
    h.v3(n.rotationDegrees);
    h.f32(n.scale);
    h.f32(n.amount);
    h.f32(n.smooth);
    h.f32(n.frequency);
    h.f32(n.speed);
    h.i32(n.count);
    h.u32(n.seed);
    h.str(n.reference);
    h.u32(static_cast<std::uint32_t>(n.children.size()));
    for (const SdfNode& child : n.children) {
        hashNode(h, child);
    }
}

int countNodes(const SdfNode& n) {
    int count = 1;
    for (const SdfNode& child : n.children) {
        count += countNodes(child);
    }
    return count;
}

// ---- JSON --------------------------------------------------------------------------------------

json vecToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

Result<glm::vec3> readVec3(const json& j, const char* key, const glm::vec3& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 3) {
        return fail("'{}' must be an array of 3 numbers", key);
    }
    glm::vec3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const json& e = a.at(i);
        if (!e.is_number()) {
            return fail("'{}' must be an array of 3 numbers", key);
        }
        out[static_cast<glm::length_t>(i)] = e.get<float>();
    }
    return out;
}

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number()) {
        return fail("'{}' must be a number", key);
    }
    return v.get<float>();
}

Result<int> readInt(const json& j, const char* key, int def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number_integer()) {
        return fail("'{}' must be an integer", key);
    }
    return v.get<int>();
}

Result<std::uint32_t> readU32(const json& j, const char* key, std::uint32_t def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<long long>() >= 0)) {
        return fail("'{}' must be a non-negative integer", key);
    }
    return v.get<std::uint32_t>();
}

Result<bool> readBool(const json& j, const char* key, bool def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_boolean()) {
        return fail("'{}' must be a boolean", key);
    }
    return v.get<bool>();
}

Result<std::string> readString(const json& j, const char* key, const std::string& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_string()) {
        return fail("'{}' must be a string", key);
    }
    return v.get<std::string>();
}

#define AVGEN_SDF_READ(target, key, reader)                                                             \
    do {                                                                                                \
        auto value_ = reader(j, key, target);                                                           \
        if (!value_) {                                                                                  \
            return std::unexpected(value_.error());                                                     \
        }                                                                                               \
        target = *value_;                                                                               \
    } while (false)

// ---- packing -----------------------------------------------------------------------------------

SdfNodeGpu packNode(const SdfNode& n, std::uint32_t childCount, const FieldSet* fields) {
    SdfNodeGpu g{};
    g.kind = static_cast<std::uint32_t>(n.kind);
    g.childCount = childCount;
    g.fieldSlot = -1;
    if (n.kind == SdfNodeKind::DisplaceField && fields != nullptr) {
        g.fieldSlot = fields->indexOf(n.reference);
    }
    g.seed = n.seed;
    g.p0 = glm::vec4(n.radius, n.height, n.rounding, n.offset);
    g.p1 = glm::vec4(n.size, n.scale);
    g.p2 = glm::vec4(n.axis, n.amount);
    g.p3 = glm::vec4(n.translation, n.smooth);
    if (n.kind == SdfNodeKind::Rotate || n.kind == SdfNodeKind::Recurse) {
        const glm::quat q = glm::quat(glm::radians(n.rotationDegrees));
        g.p4 = glm::vec4(q.x, q.y, q.z, q.w);
    } else {
        g.p4 = glm::vec4(n.frequency, 0.0f, 0.0f, 0.0f);
    }
    g.p5 = glm::vec4(n.frequency, n.speed, static_cast<float>(n.count), 0.0f);
    return g;
}

SdfNodeGpu emptyNode() {
    SdfNodeGpu g{};
    g.kind = static_cast<std::uint32_t>(SdfNodeKind::Union);
    g.childCount = 0;
    g.fieldSlot = -1;
    g.p1 = glm::vec4(1.0f);
    g.p3 = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
    g.p4 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    g.p5 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return g;
}

void emitPacked(const SdfNode* node, std::vector<SdfNodeGpu>& out, const FieldSet* fields) {
    if (node == nullptr) {
        out.push_back(emptyNode());
        return;
    }
    if (sdfNodeIsPrimitive(node->kind)) {
        out.push_back(packNode(*node, 0, fields));
        return;
    }
    if (isCombination(node->kind)) {
        int enabledChildren = 0;
        for (const SdfNode& child : node->children) {
            enabledChildren += effective(child) != nullptr ? 1 : 0;
        }
        const MorphPick pick = morphPick(node->amount, enabledChildren);
        std::uint32_t emitted = 0;
        int index = 0;
        for (const SdfNode& child : node->children) {
            const SdfNode* e = effective(child);
            if (e == nullptr) {
                continue;
            }
            const int own = index++;
            if (node->kind == SdfNodeKind::Morph && own != pick.first && own != pick.second) {
                continue; // a morph packs only the one or two children it blends
            }
            emitPacked(e, out, fields);
            if (emitted > 0) {
                SdfNodeGpu fold = packNode(*node, 2, fields); // binary fold with the running result
                if (node->kind == SdfNodeKind::Morph) {
                    fold.p3.w = pick.weight;
                }
                out.push_back(fold);
            }
            ++emitted;
        }
        if (emitted == 0) {
            out.push_back(packNode(*node, 0, fields));
        }
        return;
    }
    const auto begin = static_cast<std::int32_t>(out.size());
    out.push_back(packNode(*node, kBeginMarker, fields));
    emitPacked(effectiveChild(*node), out, fields);
    SdfNodeGpu end = packNode(*node, 1, fields);
    if (node->kind == SdfNodeKind::Recurse) {
        end.fieldSlot = begin; // the loop jumps back to the record after its BEGIN
    }
    out.push_back(end);
}

} // namespace

// ---- kind tables --------------------------------------------------------------------------------

const char* sdfNodeKindName(SdfNodeKind kind) {
    const auto i = static_cast<std::size_t>(kind);
    return i < kKindNames.size() ? kKindNames[i] : "sphere";
}

std::optional<SdfNodeKind> sdfNodeKindFromName(std::string_view name) {
    for (std::size_t i = 0; i < kKindNames.size(); ++i) {
        if (name == kKindNames[i]) {
            return static_cast<SdfNodeKind>(i);
        }
    }
    return std::nullopt;
}

bool sdfNodeIsPrimitive(SdfNodeKind kind) {
    return kind <= SdfNodeKind::Cone;
}

int sdfNodeMaxChildren(SdfNodeKind kind) {
    if (sdfNodeIsPrimitive(kind)) {
        return 0;
    }
    return isCombination(kind) ? 8 : 1;
}

// ---- SdfNode -----------------------------------------------------------------------------------

json SdfNode::toJson() const {
    static const SdfNode def;
    json j = json::object();
    j["kind"] = sdfNodeKindName(kind);
    if (!name.empty()) {
        j["name"] = name;
    }
    if (enabled != def.enabled) {
        j["enabled"] = enabled;
    }
    if (radius != def.radius) {
        j["radius"] = radius;
    }
    if (height != def.height) {
        j["height"] = height;
    }
    if (size != def.size) {
        j["size"] = vecToJson(size);
    }
    if (rounding != def.rounding) {
        j["rounding"] = rounding;
    }
    if (axis != def.axis) {
        j["axis"] = vecToJson(axis);
    }
    if (offset != def.offset) {
        j["offset"] = offset;
    }
    if (translation != def.translation) {
        j["translation"] = vecToJson(translation);
    }
    if (rotationDegrees != def.rotationDegrees) {
        j["rotation"] = vecToJson(rotationDegrees);
    }
    if (scale != def.scale) {
        j["scale"] = scale;
    }
    if (amount != def.amount) {
        j["amount"] = amount;
    }
    if (smooth != def.smooth) {
        j["smooth"] = smooth;
    }
    if (frequency != def.frequency) {
        j["frequency"] = frequency;
    }
    if (speed != def.speed) {
        j["speed"] = speed;
    }
    if (count != def.count) {
        j["count"] = count;
    }
    if (seed != def.seed) {
        j["seed"] = seed;
    }
    if (reference != def.reference) {
        j["reference"] = reference;
    }
    if (!children.empty()) {
        json arr = json::array();
        for (const SdfNode& child : children) {
            arr.push_back(child.toJson());
        }
        j["children"] = std::move(arr);
    }
    return j;
}

Result<SdfNode> SdfNode::fromJson(const json& j, int depth) {
    if (!j.is_object()) {
        return fail("sdf node must be a JSON object");
    }
    if (depth >= kMaxSdfDepth) {
        return fail("sdf tree deeper than {} levels", kMaxSdfDepth);
    }
    SdfNode n;
    if (j.contains("kind")) {
        const json& k = j.at("kind");
        if (!k.is_string()) {
            return fail("'kind' must be a string");
        }
        const auto kind = sdfNodeKindFromName(k.get<std::string>());
        if (!kind) {
            return fail("unknown sdf node kind '{}'", k.get<std::string>());
        }
        n.kind = *kind;
    }
    AVGEN_SDF_READ(n.name, "name", readString);
    AVGEN_SDF_READ(n.enabled, "enabled", readBool);
    AVGEN_SDF_READ(n.radius, "radius", readFloat);
    AVGEN_SDF_READ(n.height, "height", readFloat);
    AVGEN_SDF_READ(n.size, "size", readVec3);
    AVGEN_SDF_READ(n.rounding, "rounding", readFloat);
    AVGEN_SDF_READ(n.axis, "axis", readVec3);
    AVGEN_SDF_READ(n.offset, "offset", readFloat);
    AVGEN_SDF_READ(n.translation, "translation", readVec3);
    AVGEN_SDF_READ(n.rotationDegrees, "rotation", readVec3);
    AVGEN_SDF_READ(n.scale, "scale", readFloat);
    AVGEN_SDF_READ(n.amount, "amount", readFloat);
    AVGEN_SDF_READ(n.smooth, "smooth", readFloat);
    AVGEN_SDF_READ(n.frequency, "frequency", readFloat);
    AVGEN_SDF_READ(n.speed, "speed", readFloat);
    AVGEN_SDF_READ(n.count, "count", readInt);
    AVGEN_SDF_READ(n.seed, "seed", readU32);
    AVGEN_SDF_READ(n.reference, "reference", readString);
    if (j.contains("children")) {
        const json& arr = j.at("children");
        if (!arr.is_array()) {
            return fail("'children' must be an array");
        }
        n.children.reserve(arr.size());
        for (const json& c : arr) {
            auto child = SdfNode::fromJson(c, depth + 1);
            if (!child) {
                return std::unexpected(child.error());
            }
            n.children.push_back(std::move(*child));
        }
    }
    return n;
}

// ---- SdfTree -----------------------------------------------------------------------------------

Result<void> SdfTree::validate(SdfEvaluator evaluator) const {
    int count = 0;
    if (auto ok = validateNode(root, 1, count); !ok) {
        return ok;
    }
    const PackedMetrics m = packedMetrics(effective(root));
    if (m.nodes > kMaxSdfPackedNodes) {
        return fail("sdf tree packs to {} nodes (max {})", m.nodes, kMaxSdfPackedNodes);
    }
    // ADR-1005: the stacks are the interpreter's; a compiled tree has none.
    const bool stacks = evaluator == SdfEvaluator::Interpreter;
    if (stacks && m.distStack > kMaxSdfStack) {
        return fail("sdf tree needs a distance stack of {} (max {}); nest wide combinations less deeply",
                    m.distStack, kMaxSdfStack);
    }
    if (stacks && m.pointStack > kMaxSdfStack) {
        return fail("sdf tree nests {} unary operations (max {} interpreted; compile it to lift this)", m.pointStack,
                    kMaxSdfStack);
    }
    if (const int loops = recurseNesting(effective(root)); loops > kMaxSdfLoops) {
        return fail("sdf tree nests {} recurse nodes (max {})", loops, kMaxSdfLoops);
    }
    std::vector<std::string> names;
    if (auto ok = uniqueNames(root, names); !ok) {
        return ok;
    }
    return {};
}

int SdfTree::nodeCount() const {
    return countNodes(root);
}

std::uint64_t SdfTree::structuralHash() const {
    StructHash h;
    hashNode(h, root);
    return h.value();
}

float SdfTree::evaluate(const glm::vec3& p, double time, const FieldSet* fields) const {
    return evalEffective(effective(root), p, time, fields, 1);
}

glm::vec3 SdfTree::normal(const glm::vec3& p, double time, float epsilon, const FieldSet* fields) const {
    const glm::vec3 k0(1.0f, -1.0f, -1.0f);
    const glm::vec3 k1(-1.0f, -1.0f, 1.0f);
    const glm::vec3 k2(-1.0f, 1.0f, -1.0f);
    const glm::vec3 k3(1.0f, 1.0f, 1.0f);
    const glm::vec3 n = k0 * evaluate(p + k0 * epsilon, time, fields) + k1 * evaluate(p + k1 * epsilon, time, fields) +
                        k2 * evaluate(p + k2 * epsilon, time, fields) + k3 * evaluate(p + k3 * epsilon, time, fields);
    return safeNormalize(n);
}

json SdfTree::toJson() const {
    json j = json::object();
    j["root"] = root.toJson();
    return j;
}

Result<SdfTree> SdfTree::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("sdf tree must be a JSON object");
    }
    SdfTree tree;
    const json& rootJson = j.contains("root") ? j.at("root") : j;
    auto root = SdfNode::fromJson(rootJson, 0);
    if (!root) {
        return std::unexpected(root.error());
    }
    tree.root = std::move(*root);
    return tree;
}

// ---- meshing (naive surface nets) ---------------------------------------------------------------

Result<scene::MeshData> meshSdf(const SdfTree& tree, glm::vec3 boundsMin, glm::vec3 boundsMax, int resolution,
                                double time, const FieldSet* fields) {
    if (resolution < 2 || resolution > 256) {
        return fail("sdf mesh resolution must be in 2..256 (got {})", resolution);
    }
    if (!(boundsMax.x > boundsMin.x && boundsMax.y > boundsMin.y && boundsMax.z > boundsMin.z)) {
        return fail("sdf mesh bounds must have a positive extent on every axis");
    }
    const auto n = static_cast<std::size_t>(resolution) + 1;
    const std::size_t sampleCount = n * n * n;
    if (sampleCount > 8'000'000) {
        return fail("sdf mesh resolution {} needs {} samples (max 8000000)", resolution, sampleCount);
    }
    const glm::vec3 cell = (boundsMax - boundsMin) / static_cast<float>(resolution);
    const auto lattice = [&](std::size_t x, std::size_t y, std::size_t z) {
        return boundsMin + cell * glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    };
    const auto sampleIndex = [n](std::size_t x, std::size_t y, std::size_t z) { return x + n * (y + n * z); };

    std::vector<float> values(sampleCount);
    for (std::size_t z = 0; z < n; ++z) {
        for (std::size_t y = 0; y < n; ++y) {
            for (std::size_t x = 0; x < n; ++x) {
                values[sampleIndex(x, y, z)] = tree.evaluate(lattice(x, y, z), time, fields);
            }
        }
    }

    const auto res = static_cast<std::size_t>(resolution);
    const auto cellIndex = [res](std::size_t x, std::size_t y, std::size_t z) { return x + res * (y + res * z); };
    std::vector<std::int32_t> cellVertex(res * res * res, -1);

    // Corner c of a cell: bit 0 = +x, bit 1 = +y, bit 2 = +z. Cell edges as corner pairs.
    constexpr std::array<std::array<int, 2>, 12> kEdges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                                            {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    const float epsilon = std::max(1e-4f, 0.25f * std::min(cell.x, std::min(cell.y, cell.z)));

    scene::MeshData mesh;
    mesh.name = "sdf";
    for (std::size_t z = 0; z < res; ++z) {
        for (std::size_t y = 0; y < res; ++y) {
            for (std::size_t x = 0; x < res; ++x) {
                std::array<float, 8> v{};
                std::array<glm::vec3, 8> corner{};
                unsigned insideMask = 0;
                for (int c = 0; c < 8; ++c) {
                    const auto cu = static_cast<unsigned>(c);
                    const std::size_t cx = x + (cu & 1u);
                    const std::size_t cy = y + ((cu >> 1u) & 1u);
                    const std::size_t cz = z + ((cu >> 2u) & 1u);
                    v[static_cast<std::size_t>(c)] = values[sampleIndex(cx, cy, cz)];
                    corner[static_cast<std::size_t>(c)] = lattice(cx, cy, cz);
                    if (v[static_cast<std::size_t>(c)] < 0.0f) {
                        insideMask |= 1u << cu;
                    }
                }
                if (insideMask == 0u || insideMask == 0xFFu) {
                    continue;
                }
                glm::vec3 sum(0.0f);
                int crossings = 0;
                for (const auto& e : kEdges) {
                    const float a = v[static_cast<std::size_t>(e[0])];
                    const float b = v[static_cast<std::size_t>(e[1])];
                    if ((a < 0.0f) == (b < 0.0f)) {
                        continue;
                    }
                    const float t = a / (a - b);
                    const glm::vec3& pa = corner[static_cast<std::size_t>(e[0])];
                    const glm::vec3& pb = corner[static_cast<std::size_t>(e[1])];
                    sum += pa + (pb - pa) * t;
                    ++crossings;
                }
                if (crossings == 0) {
                    continue;
                }
                const glm::vec3 position = sum / static_cast<float>(crossings);
                cellVertex[cellIndex(x, y, z)] = static_cast<std::int32_t>(mesh.vertices.size());
                mesh.vertices.push_back(
                    scene::Vertex{position, tree.normal(position, time, epsilon, fields), glm::vec2(0.0f)});
            }
        }
    }

    // Quads across every sign-changing lattice edge: (a, b, c) cyclic so that b x c = +a.
    for (std::size_t z = 0; z < n; ++z) {
        for (std::size_t y = 0; y < n; ++y) {
            for (std::size_t x = 0; x < n; ++x) {
                const std::array<std::size_t, 3> coord = {x, y, z};
                for (int axis = 0; axis < 3; ++axis) {
                    const auto a = static_cast<std::size_t>(axis);
                    const std::size_t b = (a + 1) % 3;
                    const std::size_t c = (a + 2) % 3;
                    if (coord[a] + 1 > res || coord[b] < 1 || coord[b] > res - 1 || coord[c] < 1 || coord[c] > res - 1) {
                        continue;
                    }
                    std::array<std::size_t, 3> next = coord;
                    next[a] += 1;
                    const float va = values[sampleIndex(coord[0], coord[1], coord[2])];
                    const float vb = values[sampleIndex(next[0], next[1], next[2])];
                    if ((va < 0.0f) == (vb < 0.0f)) {
                        continue;
                    }
                    const auto cellAt = [&](std::size_t db, std::size_t dc) {
                        std::array<std::size_t, 3> cc = coord;
                        cc[b] -= db;
                        cc[c] -= dc;
                        return cellVertex[cellIndex(cc[0], cc[1], cc[2])];
                    };
                    const std::int32_t v0 = cellAt(1, 1);
                    const std::int32_t v1 = cellAt(0, 1);
                    const std::int32_t v2 = cellAt(0, 0);
                    const std::int32_t v3 = cellAt(1, 0);
                    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
                        continue;
                    }
                    const auto u0 = static_cast<std::uint32_t>(v0);
                    const auto u1 = static_cast<std::uint32_t>(v1);
                    const auto u2 = static_cast<std::uint32_t>(v2);
                    const auto u3 = static_cast<std::uint32_t>(v3);
                    if (va < 0.0f) {
                        mesh.indices.insert(mesh.indices.end(), {u0, u1, u2, u0, u2, u3});
                    } else {
                        mesh.indices.insert(mesh.indices.end(), {u0, u3, u2, u0, u2, u1});
                    }
                }
            }
        }
    }
    return mesh;
}

// ---- ADR-1003: the tree compiled to WGSL ---------------------------------------------------------

namespace {

class WgslEmitter {
public:
    WgslEmitter(std::vector<SdfNodeGpu>& table, const FieldSet* fields, bool emitSource)
        : table_(table), fields_(fields), emit_(emitSource) {}

    // Emits the statements computing `node`'s distance at point expression `p`; returns the name of
    // the variable holding it. nullptr = the far value. Every call is kind-specialised (the exact
    // per-kind helper the interpreter's dispatch reaches), so the generated module carries only
    // the code its tree uses: a generic sdfPrimitive/sdfWarp/sdfFinishUnary per node inlines every
    // kind's body (fbm and voronoi included) at every node, which took Metal's compiler over ten
    // minutes and 2.9 GB on the example's 54-node tree.
    std::string node(const SdfNode* n, const std::string& p, int indent) {
        const std::string v = fresh("d");
        if (n == nullptr) {
            line(indent, "let " + v + " = SDF_FAR;");
            return v;
        }
        const std::uint32_t slot = record(*n);
        const std::string r = "sdfNodes[offset + " + std::to_string(slot) + "u]";
        switch (n->kind) {
        case SdfNodeKind::Sphere: return let(indent, v, "sdfSphere(" + p + ", " + r + ".p0.x)");
        case SdfNodeKind::Box: return let(indent, v, "sdfBox(" + p + ", " + r + ".p1.xyz)");
        case SdfNodeKind::RoundedBox: return let(indent, v, "sdfRoundBox(" + p + ", " + r + ".p1.xyz, " + r + ".p0.z)");
        case SdfNodeKind::Cylinder:
            return let(indent, v, "sdfCappedCylinder(" + p + ", " + r + ".p0.x, " + r + ".p0.y * 0.5)");
        case SdfNodeKind::Capsule: return let(indent, v, "sdfCapsule(" + p + ", " + r + ".p0.x, " + r + ".p0.y * 0.5)");
        case SdfNodeKind::Torus: return let(indent, v, "sdfTorus(" + p + ", " + r + ".p0.x, " + r + ".p0.z)");
        case SdfNodeKind::Plane: return let(indent, v, "sdfPlane(" + p + ", " + r + ".p2.xyz, " + r + ".p0.w)");
        case SdfNodeKind::Cone: return let(indent, v, "sdfCone(" + p + ", " + r + ".p0.x, " + r + ".p0.y)");
        default: break;
        }
        if (isCombination(n->kind)) {
            std::vector<const SdfNode*> kids;
            for (const SdfNode& child : n->children) {
                if (const SdfNode* e = effective(child)) {
                    kids.push_back(e);
                }
            }
            if (kids.empty()) {
                return let(indent, v, "SDF_FAR");
            }
            if (n->kind == SdfNodeKind::Morph) {
                // Each child is emitted once, behind a branch on whether the amount selects it.
                const std::string k = std::to_string(kids.size());
                const std::string a = fresh("a");
                const std::string first = fresh("f");
                const std::string w = fresh("w");
                const std::string c0 = fresh("c");
                const std::string c1 = fresh("c");
                line(indent, "let " + a + " = clamp(" + r + ".p2.w, 0.0, " + k + ".0 - 1.0);");
                line(indent, "let " + first + " = min(u32(floor(" + a + ")), " + k + "u - 1u);");
                line(indent, "let " + w + " = " + a + " - f32(" + first + ");");
                line(indent, "var " + c0 + " = SDF_FAR;");
                line(indent, "var " + c1 + " = SDF_FAR;");
                for (std::size_t i = 0; i < kids.size(); ++i) {
                    const std::string idx = std::to_string(i) + "u";
                    line(indent, "if (" + first + " == " + idx + " || (" + w + " > 0.0 && " + first + " + 1u == " + idx + ")) {");
                    const std::string c = node(kids[i], p, indent + 1);
                    line(indent + 1, "if (" + first + " == " + idx + ") { " + c0 + " = " + c + "; } else { " + c1 + " = " + c + "; }");
                    line(indent, "}");
                }
                line(indent, "var " + v + " = " + c0 + ";");
                line(indent, "if (" + w + " > 0.0) { " + v + " = " + v + " + (" + c1 + " - " + v + ") * " + w + "; }");
                return v;
            }
            const std::string first = node(kids[0], p, indent);
            line(indent, "var " + v + " = " + first + ";");
            for (std::size_t i = 1; i < kids.size(); ++i) {
                const std::string c = node(kids[i], p, indent);
                std::string op;
                switch (n->kind) {
                case SdfNodeKind::Union: op = "min(" + v + ", " + c + ")"; break;
                case SdfNodeKind::Intersection: op = "max(" + v + ", " + c + ")"; break;
                case SdfNodeKind::Difference: op = "max(" + v + ", -" + c + ")"; break;
                case SdfNodeKind::SmoothUnion: op = "sdfSmin(" + v + ", " + c + ", max(" + r + ".p3.w, 1e-4))"; break;
                case SdfNodeKind::SmoothIntersection:
                    op = "-sdfSmin(-" + v + ", -" + c + ", max(" + r + ".p3.w, 1e-4))";
                    break;
                case SdfNodeKind::SmoothDifference:
                    op = "-sdfSmin(-" + v + ", " + c + ", max(" + r + ".p3.w, 1e-4))";
                    break;
                default: op = v; break;
                }
                line(indent, v + " = " + op + ";");
            }
            return v;
        }
        const SdfNode* child = effectiveChild(*n);
        if (n->kind == SdfNodeKind::Recurse) {
            const std::string inv = fresh("inv");
            const std::string q = fresh("q");
            const std::string lvl = fresh("l");
            line(indent, "var " + v + " = SDF_FAR;");
            line(indent, "var " + inv + " = 1.0;");
            line(indent, "var " + q + " = " + p + ";");
            line(indent, "for (var " + lvl + " = 0; " + lvl + " <= " + std::to_string(kMaxSdfRecurseLevels) + "; " + lvl +
                             " = " + lvl + " + 1) {");
            const std::string c = node(child, q, indent + 1);
            line(indent + 1, v + " = min(" + v + ", " + c + " * " + inv + ");");
            line(indent + 1, "if (" + lvl + " >= i32(" + r + ".p5.z)) {");
            line(indent + 2, "break;");
            line(indent + 1, "}");
            line(indent + 1, q + " = sdfRecurseStep(" + r + ", " + q + ");");
            line(indent + 1, inv + " = " + inv + " / " + r + ".p1.w;");
            line(indent, "}");
            return v;
        }
        std::string warp;
        switch (n->kind) {
        case SdfNodeKind::Translate: warp = p + " - " + r + ".p3.xyz"; break;
        case SdfNodeKind::Rotate: warp = "sdfQuatRotate(vec4<f32>(-" + r + ".p4.xyz, " + r + ".p4.w), " + p + ")"; break;
        case SdfNodeKind::Scale: warp = p + " / " + r + ".p1.w"; break;
        case SdfNodeKind::Twist: warp = "sdfWarpTwist(" + r + ", " + p + ")"; break;
        case SdfNodeKind::Bend: warp = "sdfWarpBend(" + r + ", " + p + ")"; break;
        case SdfNodeKind::Repeat: warp = "sdfWarpRepeat(" + r + ", " + p + ")"; break;
        case SdfNodeKind::PolarRepeat: warp = "sdfWarpPolar(" + r + ", " + p + ")"; break;
        case SdfNodeKind::Mirror: warp = "sdfWarpMirror(" + r + ", " + p + ")"; break;
        case SdfNodeKind::Fold: warp = "sdfWarpFold(" + r + ", " + p + ")"; break;
        default: warp = p; break; // displacements leave the point alone
        }
        std::string q = p;
        if (warp != p) {
            q = fresh("q");
            line(indent, "let " + q + " = " + warp + ";");
        }
        const std::string c = node(child, q, indent);
        switch (n->kind) {
        case SdfNodeKind::Scale: return let(indent, v, c + " * " + r + ".p1.w");
        case SdfNodeKind::DisplaceNoise: return let(indent, v, "sdfFinishUnaryNoise(" + r + ", " + c + ", " + p + ", t, world)");
        case SdfNodeKind::DisplaceVoronoi:
            return let(indent, v, "sdfFinishUnaryVoronoi(" + r + ", " + c + ", " + p + ", t, world)");
        case SdfNodeKind::DisplaceWave: return let(indent, v, "sdfFinishUnaryWave(" + r + ", " + c + ", " + p + ", t, world)");
        case SdfNodeKind::DisplaceField: return let(indent, v, "sdfFinishUnaryField(" + r + ", " + c + ", " + p + ", t, world)");
        default: return c;
        }
    }

    [[nodiscard]] const std::string& source() const { return out_; }

private:
    std::uint32_t record(const SdfNode& n) {
        // One table record per emitted node. A morph child emitted twice (in two cases) gets two
        // records with identical contents, which is harmless and keeps emission a single walk.
        const auto slot = static_cast<std::uint32_t>(table_.size());
        table_.push_back(packNode(n, 0, fields_));
        return slot;
    }
    std::string fresh(const char* stem) { return std::string(stem) + std::to_string(next_++); }
    std::string let(int indent, const std::string& v, const std::string& expr) {
        line(indent, "let " + v + " = " + expr + ";");
        return v;
    }
    void line(int indent, const std::string& text) {
        if (emit_) {
            out_.append(static_cast<std::size_t>(indent) * 4, ' ');
            out_ += text;
            out_ += '\n';
        }
    }

    std::vector<SdfNodeGpu>& table_;
    const FieldSet* fields_;
    bool emit_;
    std::string out_;
    int next_ = 0;
};

void hashStructure(StructHash& h, const SdfNode& n) {
    h.u32(static_cast<std::uint32_t>(n.kind));
    h.boolean(n.enabled);
    h.u32(static_cast<std::uint32_t>(n.children.size()));
    for (const SdfNode& child : n.children) {
        hashStructure(h, child);
    }
}

} // namespace

std::string sdfCompileWgsl(const SdfTree& tree, std::vector<SdfNodeGpu>& table, const FieldSet* fields) {
    table.clear();
    WgslEmitter e(table, fields, true);
    const std::string result = e.node(effective(tree.root), "p", 1);
    std::string src = "// ADR-1003: generated from an SDF tree by spatial::sdfCompileWgsl. Do not edit.\n"
                      "fn sdfField(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {\n";
    src += e.source();
    src += "    return " + result + ";\n}\n";
    return src;
}

void sdfCompileTable(const SdfTree& tree, std::vector<SdfNodeGpu>& table, const FieldSet* fields) {
    table.clear();
    WgslEmitter e(table, fields, false);
    (void)e.node(effective(tree.root), "p", 1);
}

std::uint64_t sdfCompileKey(const SdfTree& tree) {
    StructHash h;
    h.str("sdf-compile-v1");
    hashStructure(h, tree.root);
    return h.value();
}

// ---- GPU packing and the packed interpreter ----------------------------------------------------

int packSdfTree(const SdfTree& tree, std::vector<SdfNodeGpu>& out, const FieldSet* fields) {
    out.clear();
    emitPacked(effective(tree.root), out, fields);
    return static_cast<int>(out.size());
}

float evaluatePacked(std::span<const SdfNodeGpu> nodes, const glm::vec3& p, double time, const FieldSet* fields) {
    std::array<float, kMaxSdfStack> dist{};
    std::array<glm::vec3, kMaxSdfStack> pts{};
    // ADR-1001: Recurse loop frames (level, running union, 1 / scale^level).
    std::array<int, kMaxSdfLoops> loopLevel{};
    std::array<float, kMaxSdfLoops> loopAcc{};
    std::array<float, kMaxSdfLoops> loopInv{};
    int sp = 0;
    int pp = 0;
    int lp = 0;
    glm::vec3 cur = p;
    const auto total = static_cast<std::int64_t>(nodes.size());
    for (std::int64_t i = 0; i < total; ++i) {
        const SdfNodeGpu& g = nodes[static_cast<std::size_t>(i)];
        const auto kind = static_cast<SdfNodeKind>(g.kind);
        if (sdfNodeIsPrimitive(kind)) {
            if (sp >= kMaxSdfStack) {
                return kFar;
            }
            dist[static_cast<std::size_t>(sp++)] = primitiveDistance(kind, paramsOf(g, fields), cur);
        } else if (isCombination(kind)) {
            const auto count = static_cast<int>(g.childCount);
            if (count == 0) {
                if (sp >= kMaxSdfStack) {
                    return kFar;
                }
                dist[static_cast<std::size_t>(sp++)] = kFar;
                continue;
            }
            const int base = sp - count;
            if (base < 0) {
                return kFar;
            }
            float d = dist[static_cast<std::size_t>(base)];
            for (int j = 1; j < count; ++j) {
                d = combine(kind, d, dist[static_cast<std::size_t>(base + j)], g.p3.w);
            }
            sp = base;
            dist[static_cast<std::size_t>(sp++)] = d;
        } else if (g.childCount == kBeginMarker) {
            if (pp >= kMaxSdfStack) {
                return kFar;
            }
            pts[static_cast<std::size_t>(pp++)] = cur;
            if (kind == SdfNodeKind::Recurse) {
                if (lp >= kMaxSdfLoops) {
                    return kFar;
                }
                loopLevel[static_cast<std::size_t>(lp)] = 0;
                loopAcc[static_cast<std::size_t>(lp)] = kFar;
                loopInv[static_cast<std::size_t>(lp)] = 1.0f;
                ++lp;
            }
            cur = warpPoint(kind, paramsOf(g, fields), cur);
        } else if (kind == SdfNodeKind::Recurse) {
            // END of a level: `cur` is this level's point again (the subtree restored it).
            if (lp <= 0 || sp <= 0 || pp <= 0) {
                return kFar;
            }
            const auto f = static_cast<std::size_t>(lp - 1);
            const NodeParams params = paramsOf(g, fields);
            loopAcc[f] = std::min(loopAcc[f], dist[static_cast<std::size_t>(--sp)] * loopInv[f]);
            if (loopLevel[f] < params.count && g.fieldSlot >= 0 && g.fieldSlot < i) {
                ++loopLevel[f];
                cur = recurseStep(params, cur);
                loopInv[f] = loopInv[f] / params.scale;
                i = g.fieldSlot; // the loop's ++i resumes at the record after BEGIN
                continue;
            }
            cur = pts[static_cast<std::size_t>(--pp)];
            dist[static_cast<std::size_t>(sp++)] = loopAcc[f];
            --lp;
        } else {
            if (pp <= 0 || sp <= 0) {
                return kFar;
            }
            cur = pts[static_cast<std::size_t>(--pp)];
            float& d = dist[static_cast<std::size_t>(sp - 1)];
            d = finishUnary(kind, paramsOf(g, fields), d, cur, time, fields);
        }
    }
    return sp > 0 ? dist[static_cast<std::size_t>(sp - 1)] : kFar;
}

} // namespace avgen::spatial

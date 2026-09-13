// Procedural geometry (ADR-023): primitive generators, distributions, seeded variation, the CPU
// reference of the deformer stack, instance record generation, JSON and parameters.
//
// Conventions chosen here (the header fixes the semantics; these are the remaining choices):
//
// * Radial/spiral angles increase counter-clockwise about the plane normal (right-hand rule).
//   The plane frame (a, b, n) is right-handed: XZ = (+X, -Z, +Y), XY = (+X, +Y, +Z),
//   YZ = (+Y, +Z, +X); position = center + r * (cos(theta) * a + sin(theta) * b) + h * n.
//   Orientation modes rotate a base rotation R0 (instance +Z -> a, +Y -> n) about n by theta
//   (Outward), theta + pi (Inward) or theta + pi/2 (Tangent, direction of increasing theta); for
//   the spiral, Tangent follows the true helix tangent (radius growth and height included).
// * Linear `orientAlong` and every "look" rotation map the instance's +Z onto the direction and
//   keep +Y as close to world +Y as possible (fallback +Z, then +X, when the direction is
//   vertical).
// * Variation randoms use hashInstance channels 16..25 (position 16-18, rotation 19-21, scale
//   22-24, uniform scale 25); the record's `random` lanes are channels 0..3; material variation
//   uses 4 (hue), 5 (value) and 6 (emissive). Scales are clamped to >= 1e-3.
// * Bend is the classic Barr bend: the coordinate y along `axis` maps to an arc of radius
//   1 / amount whose centre of curvature lies at +1/amount along the bend direction (the
//   `displacementAxis` projected perpendicular to `axis`, +X by default); `falloff` > 0 limits
//   the bent region to |y| <= falloff, beyond which points continue straight along the arc's end
//   tangent (rigid). Bend ignores `speed`.
// * For Twist/Sine/Noise/Displacement, `falloff` > 0 multiplies the effect by
//   clamp(|dot(p - c, axis)| / falloff, 0, 1) (a ramp from the centre); 0 = full effect.
// * Displacement in `applyDeformer`/`deformPoint` (no vertex normal available) displaces along
//   `axis`; the GPU displaces along the vertex normal.
// * Noise/Displacement sample the pattern at p * scale + vec3(speed * t) with `seed` (the
//   "seed offset" is the seed argument of fbm3, no positional offset).
// * Point sources are a pointSize x pointSize quad in XY facing +Z (billboarded by the shader);
//   their bounds half-extent is pointSize / 2 on every axis.
// * Field deformers (ADR-025): `applyFieldDeformer` samples the named field at the point it is
//   given; in `deformPoint` a Local-space Field deformer therefore samples at the object-space
//   point (the field acts in object space) and a World-space one at the world point with the
//   normal rotated by the instance matrix. `applyDeformer` (no field set) leaves the point alone.
//   Field deformers ignore `falloff`, `speed` and `phase`.
// * Spline distributions (ADR-026): instance i sits at distance d = length * (splineStart +
//   (splineEnd - splineStart) * u) with u = i / (n - 1) (open span) or i / n (a closed spline
//   whose span is a whole turn: no duplicate end); position = S.position + frame * splineOffset
//   (x = binormal, y = normal, z = tangent), rotation = S.rotation() then `roll` about the
//   tangent when alignToSpline, scale = S.scale (the interpolated per-point factor). Spacing mode
//   counts floor(length * span / spacing) + 1 instances (floor(length * span / spacing) for a
//   closed whole-turn span, minimum 1) and places them exactly `spacing` apart from splineStart
//   towards splineEnd (like Linear, whose spacing also overrides the end), i.e. u is replaced by
//   i * spacing / (length * span). Without a spline every placement is the identity.
// * Path deformers (ADR-026): the perpendicular basis of `axis` is u = normalize(cross(up, axis)),
//   v = cross(axis, u) with up = +Y (+X when the axis is within 0.001 of +-Y), so for the default
//   +Z-axis-along-+Z case (u, v) = (+X, +Y) coincide with (binormal, normal) of a +Z line spline
//   and the deformer is the identity up to translation. The cross-section is rotated by
//   `pathRoll` and scaled by the sample's `scale`. World-space Path deformers are skipped (the
//   source extent has no meaning there); the GPU packs them as disabled slots.
// * generateCloud() writes the point cloud the instance records are projected from: position,
//   rotation, scale from the composed transform, id = index, seed = variation.seed (so
//   random(i, c) == hashInstance(variation.seed, i, c) and the records stay bit-identical to
//   the pre-ADR-024 loop), density 1, index u, colour/emissive from material variation, bounds =
//   the source half-extent x |sourceTransform.scale|.
//
// GPU reference (the WGSL the shader implements; the C++ below is a transliteration and must
// stay identical to float rounding):
//
//   fn pcg3d(vIn: vec3<u32>) -> vec3<u32> {
//       var v = vIn * 1664525u + 1013904223u;
//       v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
//       v ^= v >> vec3<u32>(16u);
//       v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
//       return v;
//   }
//   fn hash01(cell: vec3<i32>, seed: u32) -> f32 {
//       let h = pcg3d(vec3<u32>(bitcast<u32>(cell.x) + seed * 7919u,
//                               bitcast<u32>(cell.y) + seed * 104729u,
//                               bitcast<u32>(cell.z) + seed * 1299709u));
//       return f32(h.x) * (1.0 / 4294967296.0);
//   }
//   fn valueNoise(p: vec3<f32>, seed: u32) -> f32 {
//       let c = floor(p);
//       let f = p - c;
//       let u = f * f * (3.0 - 2.0 * f);
//       let ci = vec3<i32>(c);
//       let n000 = hash01(ci + vec3<i32>(0, 0, 0), seed);
//       let n100 = hash01(ci + vec3<i32>(1, 0, 0), seed);
//       let n010 = hash01(ci + vec3<i32>(0, 1, 0), seed);
//       let n110 = hash01(ci + vec3<i32>(1, 1, 0), seed);
//       let n001 = hash01(ci + vec3<i32>(0, 0, 1), seed);
//       let n101 = hash01(ci + vec3<i32>(1, 0, 1), seed);
//       let n011 = hash01(ci + vec3<i32>(0, 1, 1), seed);
//       let n111 = hash01(ci + vec3<i32>(1, 1, 1), seed);
//       let x00 = mix(n000, n100, u.x);
//       let x10 = mix(n010, n110, u.x);
//       let x01 = mix(n001, n101, u.x);
//       let x11 = mix(n011, n111, u.x);
//       let y0 = mix(x00, x10, u.y);
//       let y1 = mix(x01, x11, u.y);
//       return mix(y0, y1, u.z);
//   }
//   fn fbm3(p: vec3<f32>, seed: u32) -> f32 {
//       return (0.5 * valueNoise(p, seed)
//             + 0.25 * valueNoise(p * 2.03 + 17.0, seed)
//             + 0.125 * valueNoise(p * 4.11 + 31.0, seed)) / 0.875;
//   }

#include "scene/procedural.hpp"

#include "assets/mesh_lod.hpp"

#include "core/log.hpp"
#include "core/color.hpp"
#include "core/noise.hpp"
#include "scene/procedural_detail.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <string>
#include <utility>

namespace avgen::scene {

using nlohmann::json;

namespace {

constexpr float kTwoPi = 6.283185307179586f;
constexpr int kMaxInstances = 1048576; // 1M points (brief performance target)

// ---- structural hashing (FNV-1a over the bit patterns) -----------------------------------------

class StructHash {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h_ = (h_ ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void i32(int v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); } // -0 == +0
    void boolean(bool v) { u32(v ? 1u : 0u); }
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void iv3(const glm::ivec3& v) {
        i32(v.x);
        i32(v.y);
        i32(v.z);
    }
    void quat(const glm::quat& q) {
        f32(q.x);
        f32(q.y);
        f32(q.z);
        f32(q.w);
    }
    void transform(const Transform& t) {
        v3(t.position);
        quat(t.rotation);
        v3(t.scale);
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

// ---- small vector helpers ------------------------------------------------------------------------

glm::vec3 unitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-8f ? v / len : fallback;
}

bool uniformScale(const glm::vec3& s) {
    return std::abs(s.x - s.y) < 1e-6f && std::abs(s.x - s.z) < 1e-6f;
}

// outer * inner (inner applied first). Exact when the outer scale is uniform (no shear can
// appear); matrix decomposition otherwise.
Transform compose(const Transform& outer, const Transform& inner) {
    if (uniformScale(outer.scale)) {
        Transform t;
        t.position = outer.position + outer.rotation * (inner.position * outer.scale.x);
        t.rotation = outer.rotation * inner.rotation;
        t.scale = inner.scale * outer.scale.x;
        return t;
    }
    return Transform::fromMatrix(outer.matrix() * inner.matrix());
}

// Rotation mapping +Z onto `forward` and +Y as close to `up` as possible. Falls back to +Z, then
// +X, as the reference when `forward` is (anti)parallel to `up`. Identity for a zero vector.
glm::quat lookRotation(const glm::vec3& forward, const glm::vec3& up) {
    const float len = glm::length(forward);
    if (len < 1e-8f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const glm::vec3 z = forward / len;
    glm::vec3 ref = unitOr(up, glm::vec3(0.0f, 1.0f, 0.0f));
    if (std::abs(glm::dot(z, ref)) > 0.9999f) {
        ref = std::abs(z.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    }
    const glm::vec3 x = glm::normalize(glm::cross(ref, z));
    const glm::vec3 y = glm::cross(z, x);
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
}

glm::vec3 anyPerpendicular(const glm::vec3& axis) {
    const glm::vec3 ref = std::abs(axis.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::cross(axis, ref));
}

// Euler (degrees) <-> quaternion, matching the composition node convention (glm::quat(vec3)
// builds Rz * Ry * Rx; atan2 recovery avoids the asin precision loss near +-90 degrees).
glm::quat quatFromEulerDegrees(const glm::vec3& degrees) {
    return glm::quat(glm::radians(degrees));
}

glm::vec3 eulerDegrees(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(q); // m[column][row]
    const float m00 = m[0][0];
    const float m10 = m[0][1];
    const float m20 = m[0][2];
    const float m01 = m[1][0];
    const float m11 = m[1][1];
    const float m21 = m[1][2];
    const float m22 = m[2][2];
    const float cy = std::sqrt(m00 * m00 + m10 * m10);
    const float y = std::atan2(-m20, cy);
    float x = 0.0f;
    float z = 0.0f;
    if (cy > 1e-6f) {
        x = std::atan2(m21, m22);
        z = std::atan2(m10, m00);
    } else {
        x = std::atan2(-m20 * m01, m11);
    }
    return glm::degrees(glm::vec3(x, y, z));
}

// ---- plane frames ---------------------------------------------------------------------------------

struct PlaneFrame {
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 n; // a x b == n
};

PlaneFrame planeFrame(DistributionPlane plane) {
    switch (plane) {
    case DistributionPlane::XY:
        return {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    case DistributionPlane::YZ:
        return {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    case DistributionPlane::XZ:
    default:
        return {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}};
    }
}

// Rotation for the radial/spiral orientation modes: R0 maps the instance's +Z onto the frame's
// first axis `a` and +Y onto the normal; Outward/Inward rotate R0 about the normal by theta /
// theta + pi; Tangent looks along `tangent` (the circle or helix tangent) with +Y towards the
// normal; None is the identity.
glm::quat orientationRotation(OrientationMode mode, float theta, const glm::vec3& tangent, const PlaneFrame& frame) {
    switch (mode) {
    case OrientationMode::None:
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    case OrientationMode::Outward:
        return glm::normalize(glm::angleAxis(theta, frame.n) * lookRotation(frame.a, frame.n));
    case OrientationMode::Inward:
        return glm::normalize(glm::angleAxis(theta + glm::pi<float>(), frame.n) * lookRotation(frame.a, frame.n));
    case OrientationMode::Tangent:
        return lookRotation(tangent, frame.n);
    }
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

// ---- JSON helpers ------------------------------------------------------------------------------

json vecToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json ivecToJson(const glm::ivec3& v) {
    return json::array({v.x, v.y, v.z});
}

json transformToJson(const Transform& t) {
    json j = json::object();
    j["position"] = vecToJson(t.position);
    j["rotation"] = vecToJson(eulerDegrees(t.rotation)); // degrees, like composition nodes
    j["scale"] = vecToJson(t.scale);
    return j;
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

Result<glm::ivec3> readIvec3(const json& j, const char* key, const glm::ivec3& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 3) {
        return fail("'{}' must be an array of 3 integers", key);
    }
    glm::ivec3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const json& e = a.at(i);
        if (!e.is_number_integer()) {
            return fail("'{}' must be an array of 3 integers", key);
        }
        out[static_cast<glm::length_t>(i)] = e.get<int>();
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

Result<Transform> readTransform(const json& parent, const char* key, const Transform& def) {
    if (!parent.contains(key)) {
        return def;
    }
    const json& j = parent.at(key);
    if (!j.is_object()) {
        return fail("'{}' must be an object", key);
    }
    Transform t = def;
    auto position = readVec3(j, "position", t.position);
    if (!position) {
        return std::unexpected(position.error());
    }
    auto rotation = readVec3(j, "rotation", eulerDegrees(t.rotation));
    if (!rotation) {
        return std::unexpected(rotation.error());
    }
    auto scale = readVec3(j, "scale", t.scale);
    if (!scale) {
        return std::unexpected(scale.error());
    }
    t.position = *position;
    t.rotation = quatFromEulerDegrees(*rotation);
    t.scale = *scale;
    return t;
}

// Assigns `field` from `reader(j, key, field)` or returns the error.
#define AVGEN_PROC_READ(target, key, reader)                                                            \
    do {                                                                                                \
        auto value_ = reader(j, key, target);                                                            \
        if (!value_) {                                                                                  \
            return std::unexpected(value_.error());                                                     \
        }                                                                                               \
        target = *value_;                                                                               \
    } while (false)

template <typename Enum>
Result<void> readEnum(const json& j, const char* key, Enum& target, std::optional<Enum> (*fromName)(std::string_view),
                      const char* what) {
    if (!j.contains(key)) {
        return {};
    }
    auto name = readString(j, key, "");
    if (!name) {
        return std::unexpected(name.error());
    }
    const auto value = fromName(*name);
    if (!value) {
        return fail("unknown {} '{}'", what, *name);
    }
    target = *value;
    return {};
}

// ---- material variation --------------------------------------------------------------------------

// The rgb multiplier that turns `base` into its hue rotation by `turns` (rotation about the grey
// axis, luminance-preserving). A pure hue rotation leaves white unchanged, so the multiplier is
// computed against the colour it will multiply (the base at rebuild time); channels near zero
// cannot gain energy through a multiplier, so strongly saturated bases shift less than ideal.
// The same contract as hueRotationMultiplier -- a multiplier on the material's base colour --
// but the rotation happens in OKLCH, where equal angles are equal perceived hue steps and L and
// C come through unchanged.
glm::vec3 hueRotationMultiplierOklab(const glm::vec3& base, float turns) {
    if (std::abs(turns) < 1e-7f) {
        return glm::vec3(1.0f);
    }
    const glm::vec3 safe = glm::max(base, glm::vec3(1e-3f));
    const glm::vec3 rotated = glm::max(color::hueShift(safe, turns), glm::vec3(0.0f));
    // A far higher ceiling than the legacy path's 8. Instance colour is carried as a per-channel
    // multiplier of the material's, and a saturated emitter has a channel near zero -- the teal
    // the ferns glow is (0.02, 1.0, 0.86). Turning that hue means raising the small channel by
    // tens, and a ceiling of 8 clips exactly that, which is why rotating a saturated colour used
    // to desaturate it into grey instead of moving it round the wheel.
    return glm::clamp(rotated / safe, glm::vec3(0.0f), glm::vec3(96.0f));
}

glm::vec3 hueRotationMultiplier(const glm::vec3& base, float turns) {
    if (std::abs(turns) < 1e-7f) {
        return glm::vec3(1.0f);
    }
    const glm::mat3 rot = glm::mat3(glm::rotate(glm::mat4(1.0f), turns * kTwoPi, glm::normalize(glm::vec3(1.0f))));
    const glm::vec3 safe = glm::max(base, glm::vec3(1e-3f));
    const glm::vec3 rotated = glm::max(rot * safe, glm::vec3(0.0f));
    return glm::clamp(rotated / safe, glm::vec3(0.0f), glm::vec3(8.0f));
}

glm::vec3 sourceHalfExtent(const SourceSpec& s) {
    switch (s.kind) {
    case PrimitiveKind::Box:
        return s.size * 0.5f;
    case PrimitiveKind::Cylinder:
        return {s.radius, s.height * 0.5f, s.radius};
    case PrimitiveKind::Sphere:
        return glm::vec3(s.radius);
    case PrimitiveKind::Point:
        return glm::vec3(s.pointSize * 0.5f);
    case PrimitiveKind::Procedural:
        return glm::vec3(0.0f); // resolved through the referenced object (detail::sourceHalfExtent)
    case PrimitiveKind::Mesh: {
        // The imported mesh's own bounds; without it (not yet resolved) a unit box is a safe
        // placeholder that keeps the culler from discarding the object outright.
        if (!s.assetMesh || s.assetMesh->vertices.empty()) {
            return glm::vec3(0.5f);
        }
        glm::vec3 extent(0.0f);
        for (const Vertex& v : s.assetMesh->vertices) {
            extent = glm::max(extent, glm::abs(v.position));
        }
        return extent;
    }
    case PrimitiveKind::Tube: {
        // A handful of samples along the curve, grown by the widest the profile ever gets. The
        // culler only needs a bound that contains the tube, and meshing it here would be waste.
        glm::vec3 extent(0.0f);
        for (const spatial::SplineSample& sample : s.curve.samples(16)) {
            extent = glm::max(extent, glm::abs(sample.position));
        }
        return extent + glm::vec3(s.tubeRadius * std::max(1.0f, s.tubeTaper));
    }
    case PrimitiveKind::Torus:
    default:
        return {s.majorRadius + s.minorRadius, s.minorRadius, s.majorRadius + s.minorRadius};
    }
}

} // namespace

// Internal helpers shared with hierarchy.cpp (scene/procedural_detail.hpp).
Transform detail::composeTransforms(const Transform& outer, const Transform& inner) {
    return compose(outer, inner);
}

glm::vec3 detail::hueMultiplier(const glm::vec3& base, float turns) {
    return hueRotationMultiplier(base, turns);
}

glm::vec3 detail::primitiveHalfExtent(const SourceSpec& s) {
    return scene::sourceHalfExtent(s); // the file-local one, not detail::sourceHalfExtent
}

// ================================================================================================
// Enum names
// ================================================================================================

const char* primitiveKindName(PrimitiveKind kind) {
    switch (kind) {
    case PrimitiveKind::Box:
        return "box";
    case PrimitiveKind::Cylinder:
        return "cylinder";
    case PrimitiveKind::Sphere:
        return "sphere";
    case PrimitiveKind::Torus:
        return "torus";
    case PrimitiveKind::Point:
        return "point";
    case PrimitiveKind::Procedural:
        return "procedural";
    case PrimitiveKind::Tube:
        return "tube";
    case PrimitiveKind::Mesh:
        return "mesh";
    }
    return "cylinder";
}

std::optional<PrimitiveKind> primitiveKindFromName(std::string_view name) {
    for (const auto kind : {PrimitiveKind::Box, PrimitiveKind::Cylinder, PrimitiveKind::Sphere, PrimitiveKind::Torus,
                            PrimitiveKind::Point, PrimitiveKind::Procedural, PrimitiveKind::Tube,
                            PrimitiveKind::Mesh}) {
        if (name == primitiveKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

const char* distributionKindName(DistributionKind kind) {
    switch (kind) {
    case DistributionKind::Single:
        return "single";
    case DistributionKind::Linear:
        return "linear";
    case DistributionKind::Grid:
        return "grid";
    case DistributionKind::Radial:
        return "radial";
    case DistributionKind::Spiral:
        return "spiral";
    case DistributionKind::Spline:
        return "spline";
    case DistributionKind::Grammar:
        return "grammar";
    case DistributionKind::Scatter:
        return "scatter";
    }
    return "single";
}

std::optional<DistributionKind> distributionKindFromName(std::string_view name) {
    for (const auto kind : {DistributionKind::Single, DistributionKind::Linear, DistributionKind::Grid,
                            DistributionKind::Radial, DistributionKind::Spiral, DistributionKind::Spline,
                            DistributionKind::Grammar, DistributionKind::Scatter}) {
        if (name == distributionKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

const char* orientationModeName(OrientationMode mode) {
    switch (mode) {
    case OrientationMode::None:
        return "none";
    case OrientationMode::Outward:
        return "outward";
    case OrientationMode::Inward:
        return "inward";
    case OrientationMode::Tangent:
        return "tangent";
    }
    return "none";
}

std::optional<OrientationMode> orientationModeFromName(std::string_view name) {
    for (const auto mode : {OrientationMode::None, OrientationMode::Outward, OrientationMode::Inward, OrientationMode::Tangent}) {
        if (name == orientationModeName(mode)) {
            return mode;
        }
    }
    return std::nullopt;
}

const char* distributionPlaneName(DistributionPlane plane) {
    switch (plane) {
    case DistributionPlane::XZ:
        return "xz";
    case DistributionPlane::XY:
        return "xy";
    case DistributionPlane::YZ:
        return "yz";
    }
    return "xz";
}

std::optional<DistributionPlane> distributionPlaneFromName(std::string_view name) {
    for (const auto plane : {DistributionPlane::XZ, DistributionPlane::XY, DistributionPlane::YZ}) {
        if (name == distributionPlaneName(plane)) {
            return plane;
        }
    }
    return std::nullopt;
}

const char* deformerKindName(DeformerKind kind) {
    switch (kind) {
    case DeformerKind::Bend:
        return "bend";
    case DeformerKind::Twist:
        return "twist";
    case DeformerKind::Sine:
        return "sine";
    case DeformerKind::Noise:
        return "noise";
    case DeformerKind::Displacement:
        return "displacement";
    case DeformerKind::Field:
        return "field";
    case DeformerKind::Path:
        return "path";
    }
    return "twist";
}

std::optional<DeformerKind> deformerKindFromName(std::string_view name) {
    for (const auto kind : {DeformerKind::Bend, DeformerKind::Twist, DeformerKind::Sine, DeformerKind::Noise,
                            DeformerKind::Displacement, DeformerKind::Field, DeformerKind::Path}) {
        if (name == deformerKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

const char* deformSpaceName(DeformSpace space) {
    return space == DeformSpace::World ? "world" : "local";
}

std::optional<DeformSpace> deformSpaceFromName(std::string_view name) {
    if (name == "local") {
        return DeformSpace::Local;
    }
    if (name == "world") {
        return DeformSpace::World;
    }
    return std::nullopt;
}

// ================================================================================================
// Source geometry
// ================================================================================================

Result<void> SourceSpec::validate() const {
    const auto inRange = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
    switch (kind) {
    case PrimitiveKind::Box:
        if (!(size.x > 0.0f && size.y > 0.0f && size.z > 0.0f)) {
            return fail("box size must be positive on every axis");
        }
        if (!inRange(subdivisions, 1, 64)) {
            return fail("box subdivisions must be in 1..64 (got {})", subdivisions);
        }
        if (!(bevel >= 0.0f)) {
            return fail("box bevel must be >= 0 (got {})", bevel);
        }
        if (!inRange(bevelSegments, 1, 16)) {
            return fail("box bevelSegments must be in 1..16 (got {})", bevelSegments);
        }
        break;
    case PrimitiveKind::Mesh:
        if (asset.empty()) {
            return fail("mesh source needs an asset path");
        }
        break;
    case PrimitiveKind::Tube:
        if (!(tubeRadius > 0.0f)) {
            return fail("tube radius must be positive (got {})", tubeRadius);
        }
        if (!(tubeTaper >= 0.0f)) {
            return fail("tube taper must be >= 0 (got {})", tubeTaper);
        }
        if (!inRange(tubeSides, 3, 64)) {
            return fail("tube sides must be in 3..64 (got {})", tubeSides);
        }
        if (!inRange(tubeSegments, 2, 512)) {
            return fail("tube segments must be in 2..512 (got {})", tubeSegments);
        }
        if (auto ok = curve.validate(); !ok) {
            return fail("tube curve: {}", ok.error().message);
        }
        break;
    case PrimitiveKind::Cylinder:
        if (!(radius > 0.0f) || !(height > 0.0f)) {
            return fail("cylinder radius and height must be positive");
        }
        if (!inRange(radialSegments, 3, 256)) {
            return fail("cylinder radialSegments must be in 3..256 (got {})", radialSegments);
        }
        if (!inRange(heightSegments, 1, 128)) {
            return fail("cylinder heightSegments must be in 1..128 (got {})", heightSegments);
        }
        break;
    case PrimitiveKind::Sphere:
        if (!(radius > 0.0f)) {
            return fail("sphere radius must be positive");
        }
        if (!inRange(segments, 3, 256)) {
            return fail("sphere segments must be in 3..256 (got {})", segments);
        }
        if (!inRange(rings, 2, 128)) {
            return fail("sphere rings must be in 2..128 (got {})", rings);
        }
        break;
    case PrimitiveKind::Torus:
        if (!(majorRadius > 0.0f) || !(minorRadius > 0.0f)) {
            return fail("torus radii must be positive");
        }
        if (!inRange(majorSegments, 3, 256)) {
            return fail("torus majorSegments must be in 3..256 (got {})", majorSegments);
        }
        if (!inRange(minorSegments, 3, 128)) {
            return fail("torus minorSegments must be in 3..128 (got {})", minorSegments);
        }
        break;
    case PrimitiveKind::Point:
        if (!(pointSize > 0.0f)) {
            return fail("point size must be positive");
        }
        break;
    case PrimitiveKind::Procedural:
        if (reference.empty()) {
            return fail("a procedural source needs a reference (the name of another procedural object)");
        }
        break;
    }
    return {};
}

std::uint64_t SourceSpec::structuralHash() const {
    StructHash h;
    h.u32(static_cast<std::uint32_t>(kind));
    switch (kind) {
    case PrimitiveKind::Box:
        h.v3(size);
        h.i32(subdivisions);
        h.f32(bevel);
        h.i32(bevelSegments);
        break;
    case PrimitiveKind::Cylinder:
        h.f32(radius);
        h.f32(height);
        h.i32(radialSegments);
        h.i32(heightSegments);
        h.boolean(caps);
        h.f32(bevel);
        h.i32(bevelSegments);
        break;
    case PrimitiveKind::Mesh:
        h.i32(meshBudget);
        h.i32(assetPart);
        h.u64(asset.size());
        for (const char c : asset) {
            h.u32(static_cast<std::uint8_t>(c));
        }
        break;
    case PrimitiveKind::Tube:
        h.f32(tubeRadius);
        h.f32(tubeTaper);
        h.i32(tubeSides);
        h.i32(tubeSegments);
        h.f32(tubeTwist);
        h.boolean(tubeCaps);
        h.u64(curve.structuralHash());
        break;
    case PrimitiveKind::Sphere:
        h.f32(radius);
        h.i32(segments);
        h.i32(rings);
        break;
    case PrimitiveKind::Torus:
        h.f32(majorRadius);
        h.f32(minorRadius);
        h.i32(majorSegments);
        h.i32(minorSegments);
        break;
    case PrimitiveKind::Point:
        h.f32(pointSize);
        break;
    case PrimitiveKind::Procedural:
        h.u64(reference.size());
        for (const char c : reference) {
            h.u32(static_cast<std::uint8_t>(c));
        }
        break;
    }
    return h.value();
}

// Six (n+1)^2 grids with faceted normals; uv 0..1 per face. 6(n+1)^2 vertices, 36n^2 indices.
MeshData makeBox(glm::vec3 size, int subdivisions) {
    struct Face {
        glm::vec3 normal;
        glm::vec3 u;
        glm::vec3 v; // u x v == normal
    };
    const std::array<Face, 6> faces{{{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
                                     {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                                     {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}}};
    const auto n = static_cast<std::uint32_t>(std::clamp(subdivisions, 1, 64));
    const glm::vec3 half = size * 0.5f;

    MeshData mesh;
    mesh.name = "box";
    mesh.vertices.reserve(static_cast<std::size_t>(6 * (n + 1) * (n + 1)));
    mesh.indices.reserve(static_cast<std::size_t>(36 * n * n));
    for (const Face& face : faces) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t row = 0; row <= n; ++row) {
            const float tv = static_cast<float>(row) / static_cast<float>(n);
            for (std::uint32_t col = 0; col <= n; ++col) {
                const float tu = static_cast<float>(col) / static_cast<float>(n);
                const glm::vec3 unitPos = face.normal + face.u * (tu * 2.0f - 1.0f) + face.v * (tv * 2.0f - 1.0f);
                mesh.vertices.push_back(Vertex{unitPos * half, face.normal, {tu, tv}});
            }
        }
        for (std::uint32_t row = 0; row < n; ++row) {
            for (std::uint32_t col = 0; col < n; ++col) {
                const std::uint32_t p00 = base + row * (n + 1) + col;
                const std::uint32_t p10 = p00 + 1;
                const std::uint32_t p01 = p00 + (n + 1);
                const std::uint32_t p11 = p01 + 1;
                // +u then +v: u x v == normal, so counter-clockwise from outside.
                mesh.indices.insert(mesh.indices.end(), {p00, p10, p11, p00, p11, p01});
            }
        }
    }
    return mesh;
}

// Side: (R+1)(H+1) vertices (seam duplicated for uv), 6RH indices; each cap adds R+2 vertices
// (centre + ring) and 3R indices. u runs around, v along the height; caps are mapped radially.
// A box grown from a smaller box by a sphere of radius r: six flat faces over the inner
// rectangle, twelve quarter-cylinder fillets along the inner edges, eight spherical octants at
// the inner corners. Every patch is parametrised so its boundary vertices land on exactly the
// same positions and normals as its neighbour's, which is why the surface has no seam and needs
// no welding. Normals are analytic rather than averaged, so a bevel this small still shades
// correctly.
MeshData makeBeveledBox(glm::vec3 size, int subdivisions, float bevel, int bevelSegments) {
    const glm::vec3 half = glm::abs(size) * 0.5f;
    const float smallest = std::min({half.x, half.y, half.z});
    const float r = std::clamp(bevel, 0.0f, smallest * 0.999f);
    if (r <= 1e-6f) {
        return makeBox(size, subdivisions);
    }
    const auto n = static_cast<std::uint32_t>(std::clamp(subdivisions, 1, 64));
    const auto b = static_cast<std::uint32_t>(std::clamp(bevelSegments, 1, 16));
    const glm::vec3 inner = half - glm::vec3(r); // the box the sphere is swept over

    MeshData mesh;
    mesh.name = "beveled-box";

    // Emits an (su+1) x (sv+1) grid of vertices from `point(u, v)` and its triangles. Winding is
    // CCW seen from outside when point() is orientation-consistent, which the callers arrange.
    const auto patch = [&](std::uint32_t su, std::uint32_t sv, const auto& point) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t j = 0; j <= sv; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(sv);
            for (std::uint32_t i = 0; i <= su; ++i) {
                const float u = static_cast<float>(i) / static_cast<float>(su);
                mesh.vertices.push_back(point(u, v));
            }
        }
        // The corner octants converge to a pole, where a quad's top edge collapses to a point.
        // The test is for two coincident corners rather than for zero area: with FMA contraction
        // cross(v, v) does not evaluate to exactly zero, so an area threshold cannot tell a
        // collapsed edge from a legitimately small triangle.
        const auto emit = [&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2) {
            const glm::vec3& p0 = mesh.vertices[i0].position;
            const glm::vec3& p1 = mesh.vertices[i1].position;
            const glm::vec3& p2 = mesh.vertices[i2].position;
            if (p0 != p1 && p1 != p2 && p0 != p2) {
                mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
            }
        };
        for (std::uint32_t j = 0; j < sv; ++j) {
            for (std::uint32_t i = 0; i < su; ++i) {
                const std::uint32_t a = base + j * (su + 1) + i;
                const std::uint32_t c = a + 1;
                const std::uint32_t d = a + su + 1;
                const std::uint32_t e = d + 1;
                emit(a, d, c);
                emit(c, d, e);
            }
        }
    };

    constexpr float kHalfPi = 1.57079632679f;
    // cos(pi/2) is 4e-8 rather than 0 in float, which leaves a patch's boundary vertices a hair
    // away from its neighbour's and its pole a hair away from itself. Exact endpoints make the
    // seams bit-identical and collapse the pole properly, so no sliver triangles survive.
    const auto quarterCos = [](float t) { return t <= 0.0f ? 1.0f : (t >= 1.0f ? 0.0f : std::cos(t * kHalfPi)); };
    const auto quarterSin = [](float t) { return t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : std::sin(t * kHalfPi)); };
    // Axis k with its two tangents, ordered so that cross(t0, t1) == +axis.
    const auto tangents = [](int k) { return std::pair<int, int>{(k + 1) % 3, (k + 2) % 3}; };
    const auto axisVec = [](int k) {
        glm::vec3 v(0.0f);
        v[k] = 1.0f;
        return v;
    };

    // ---- 6 flat faces, over the inner rectangle only ----
    for (int k = 0; k < 3; ++k) {
        const auto [j0, j1] = tangents(k);
        for (const float s : {1.0f, -1.0f}) {
            const glm::vec3 normal = axisVec(k) * s;
            // patch() winds (a, d, c), so the outward face needs cross(t1, t0) == normal: the
            // second tangent is the one that carries the sign, and they are ordered j1 then j0.
            const glm::vec3 t0 = axisVec(j1) * s;
            const glm::vec3 t1 = axisVec(j0);
            patch(n, n, [&](float u, float v) {
                const glm::vec3 p = normal * half[k] + t0 * ((u * 2.0f - 1.0f) * inner[j1]) +
                                    t1 * ((v * 2.0f - 1.0f) * inner[j0]);
                return Vertex{p, normal, {u, v}};
            });
        }
    }

    // ---- 12 edge fillets: a quarter cylinder of radius r along each inner edge ----
    for (int k = 0; k < 3; ++k) {
        const auto [j0, j1] = tangents(k);
        for (const float s0 : {1.0f, -1.0f}) {
            for (const float s1 : {1.0f, -1.0f}) {
                // The sweep runs from the j0 face to the j1 face; reversing it for one diagonal
                // pair keeps the winding outwards on all four edges.
                const bool flip = s0 * s1 < 0.0f;
                // u runs along the edge and v sweeps the fillet: patch() winds (a, d, c), so the
                // sweep has to be the second parameter for the quads to face outwards.
                patch(n, b, [&](float u, float v) {
                    const float t = flip ? 1.0f - v : v;
                    const glm::vec3 normal =
                        axisVec(j0) * (s0 * quarterCos(t)) + axisVec(j1) * (s1 * quarterSin(t));
                    const glm::vec3 p = axisVec(j0) * (s0 * inner[j0]) + axisVec(j1) * (s1 * inner[j1]) +
                                        normal * r + axisVec(k) * ((u * 2.0f - 1.0f) * inner[k]);
                    return Vertex{p, normal, {u, v}};
                });
            }
        }
    }

    // ---- 8 corners: a spherical octant of radius r at each inner corner ----
    for (const float sx : {1.0f, -1.0f}) {
        for (const float sy : {1.0f, -1.0f}) {
            for (const float sz : {1.0f, -1.0f}) {
                const glm::vec3 sign(sx, sy, sz);
                const bool flip = sx * sy * sz < 0.0f;
                patch(b, b, [&](float u, float v) {
                    const float t = flip ? 1.0f - u : u;      // around +Y, from +X to +Z
                    const float cosPhi = quarterCos(v);       // v = 1 is the pole on +Y
                    const glm::vec3 normal =
                        glm::vec3(cosPhi * quarterCos(t), quarterSin(v), cosPhi * quarterSin(t)) * sign;
                    return Vertex{inner * sign + normal * r, normal, {u, v}};
                });
            }
        }
    }
    return mesh;
}

MeshData makeCylinder(float radius, float height, int radialSegments, int heightSegments, bool caps) {
    const auto rs = static_cast<std::uint32_t>(std::clamp(radialSegments, 3, 256));
    const auto hs = static_cast<std::uint32_t>(std::clamp(heightSegments, 1, 128));
    const float halfHeight = height * 0.5f;

    MeshData mesh;
    mesh.name = "cylinder";
    mesh.vertices.reserve(static_cast<std::size_t>((rs + 1) * (hs + 1) + (caps ? 2 * (rs + 2) : 0)));
    mesh.indices.reserve(static_cast<std::size_t>(6 * rs * hs + (caps ? 6 * rs : 0)));

    const auto ringDir = [rs](std::uint32_t j) {
        const float theta = kTwoPi * static_cast<float>(j % rs) / static_cast<float>(rs);
        return glm::vec3(std::cos(theta), 0.0f, std::sin(theta));
    };

    for (std::uint32_t row = 0; row <= hs; ++row) {
        const float tv = static_cast<float>(row) / static_cast<float>(hs);
        const float y = -halfHeight + height * tv;
        for (std::uint32_t j = 0; j <= rs; ++j) {
            const glm::vec3 dir = ringDir(j);
            const float tu = static_cast<float>(j) / static_cast<float>(rs);
            mesh.vertices.push_back(Vertex{dir * radius + glm::vec3(0.0f, y, 0.0f), dir, {tu, tv}});
        }
    }
    for (std::uint32_t row = 0; row < hs; ++row) {
        for (std::uint32_t j = 0; j < rs; ++j) {
            const std::uint32_t p00 = row * (rs + 1) + j;
            const std::uint32_t p10 = p00 + 1;
            const std::uint32_t p01 = p00 + (rs + 1);
            const std::uint32_t p11 = p01 + 1;
            // +y then +theta: up x tangent == outward normal.
            mesh.indices.insert(mesh.indices.end(), {p00, p01, p11, p00, p11, p10});
        }
    }
    if (caps) {
        for (const float sign : {1.0f, -1.0f}) {
            const glm::vec3 normal(0.0f, sign, 0.0f);
            const float y = sign * halfHeight;
            const auto centre = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Vertex{{0.0f, y, 0.0f}, normal, {0.5f, 0.5f}});
            for (std::uint32_t j = 0; j <= rs; ++j) {
                const glm::vec3 dir = ringDir(j);
                mesh.vertices.push_back(Vertex{dir * radius + glm::vec3(0.0f, y, 0.0f), normal,
                                               {0.5f + 0.5f * dir.x, 0.5f + 0.5f * dir.z}});
            }
            for (std::uint32_t j = 0; j < rs; ++j) {
                const std::uint32_t a = centre + 1 + j;
                const std::uint32_t b = a + 1;
                if (sign > 0.0f) {
                    mesh.indices.insert(mesh.indices.end(), {centre, b, a}); // top: CCW seen from +Y
                } else {
                    mesh.indices.insert(mesh.indices.end(), {centre, a, b}); // bottom: CCW seen from -Y
                }
            }
        }
    }
    return mesh;
}

// (S+1)(R+1) vertices (seam and pole rows duplicated for uv), 6S(R-1) indices (pole quads are
// single triangles). u around +Y from +X, v from the north pole (0) to the south pole (1).
// A cylinder whose two rims are quarter-round fillets, built by revolving a profile. A sharp rim
// is the same tell as a sharp box edge: it is the one place a real machined part always carries a
// radius. Normals come from the profile's own 2D normal rather than from averaging, so a small
// bevel still shades correctly. With `caps` off there is nothing for a rim fillet to meet, so the
// bevel is ignored and the plain tube comes back.
MeshData makeBeveledCylinder(float radius, float height, int radialSegments, int heightSegments, bool caps,
                             float bevel, int bevelSegments) {
    const float halfHeight = std::abs(height) * 0.5f;
    const float r = std::clamp(bevel, 0.0f, std::min(std::abs(radius), halfHeight) * 0.999f);
    if (r <= 1e-6f || !caps) {
        return makeCylinder(radius, height, radialSegments, heightSegments, caps);
    }
    const auto radial = static_cast<std::uint32_t>(std::clamp(radialSegments, 3, 256));
    const auto rings = static_cast<std::uint32_t>(std::clamp(heightSegments, 1, 128));
    const auto b = static_cast<std::uint32_t>(std::clamp(bevelSegments, 1, 16));
    constexpr float kHalfPi = 1.57079632679f;
    const float inner = std::abs(radius) - r;

    // The profile, bottom pole to top pole, as (radius, y) with its own 2D normal. Consecutive
    // sections share their boundary point exactly, so the revolved surface has no seam.
    struct ProfilePoint {
        float radius;
        float y;
        float nr;
        float ny;
    };
    std::vector<ProfilePoint> profile;
    profile.reserve(static_cast<std::size_t>(2 * b + rings + 4));
    profile.push_back({0.0f, -halfHeight, 0.0f, -1.0f});
    for (std::uint32_t i = 0; i <= b; ++i) { // bottom fillet: normal swings from -Y out to +radial
        const float t = static_cast<float>(i) / static_cast<float>(b);
        const float angle = (t - 1.0f) * kHalfPi;
        const float nr = std::cos(angle);
        const float ny = std::sin(angle);
        profile.push_back({inner + r * nr, -halfHeight + r + r * ny, nr, ny});
    }
    for (std::uint32_t i = 1; i < rings; ++i) { // the straight side, between the two fillets
        const float t = static_cast<float>(i) / static_cast<float>(rings);
        profile.push_back({std::abs(radius), -halfHeight + r + t * (2.0f * halfHeight - 2.0f * r), 1.0f, 0.0f});
    }
    for (std::uint32_t i = 0; i <= b; ++i) { // top fillet: +radial round to +Y
        const float t = static_cast<float>(i) / static_cast<float>(b);
        const float angle = t * kHalfPi;
        const float nr = std::cos(angle);
        const float ny = std::sin(angle);
        profile.push_back({inner + r * nr, halfHeight - r + r * ny, nr, ny});
    }
    profile.push_back({0.0f, halfHeight, 0.0f, 1.0f});

    MeshData mesh;
    mesh.name = "beveled-cylinder";
    const auto columns = radial + 1; // the seam column is duplicated so uvs run 0..1
    mesh.vertices.reserve(profile.size() * columns);
    for (std::size_t row = 0; row < profile.size(); ++row) {
        const ProfilePoint& p = profile[row];
        const float v = static_cast<float>(row) / static_cast<float>(profile.size() - 1);
        for (std::uint32_t col = 0; col < columns; ++col) {
            const float u = static_cast<float>(col) / static_cast<float>(radial);
            const float theta = u * kTwoPi;
            const float c = std::cos(theta);
            const float sn = std::sin(theta);
            const glm::vec3 position(p.radius * c, p.y, p.radius * sn);
            const glm::vec3 normal = glm::normalize(glm::vec3(p.nr * c, p.ny, p.nr * sn));
            mesh.vertices.push_back(Vertex{position, normal, {u, v}});
        }
    }
    // Winding (a, d, c) puts the first edge along the profile and the second around the axis, so
    // the geometric normal is cross(dProfile, dTheta) -- the outward one. The poles collapse, and
    // those triangles are dropped by their coincident corners.
    for (std::uint32_t row = 0; row + 1 < profile.size(); ++row) {
        for (std::uint32_t col = 0; col < radial; ++col) {
            const std::uint32_t a = row * columns + col;
            const std::uint32_t c = a + 1;
            const std::uint32_t d = a + columns;
            const std::uint32_t e = d + 1;
            const auto emit = [&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2) {
                const glm::vec3& p0 = mesh.vertices[i0].position;
                const glm::vec3& p1 = mesh.vertices[i1].position;
                const glm::vec3& p2 = mesh.vertices[i2].position;
                if (p0 != p1 && p1 != p2 && p0 != p2) {
                    mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
                }
            };
            emit(a, d, c);
            emit(c, d, e);
        }
    }
    return mesh;
}

// A profile swept along a curve. Positions are laid out first and the normals come from the
// swept surface itself (the cross of the two surface tangents), so a tapering tube shades as a
// cone rather than as a cylinder and a twisting one shades along its twist. The radial direction
// is used only to orient the result outwards.
MeshData makeTube(const spatial::Spline& curve, float radius, float taper, int sides, int segments, float twist,
                  bool caps) {
    MeshData mesh;
    mesh.name = "tube";
    const float length = curve.length();
    if (!(length > 1e-6f) || !(radius > 0.0f)) {
        return mesh; // a curve with no extent has no tube; an empty mesh is better than a fold
    }
    const auto around = static_cast<std::uint32_t>(std::clamp(sides, 3, 64));
    const auto along = static_cast<std::uint32_t>(std::clamp(segments, 2, 512));
    const float endScale = std::max(taper, 0.0f);
    const std::uint32_t columns = around + 1; // duplicated seam so uvs run 0..1

    struct Ring {
        spatial::SplineSample sample;
        float radius;
    };
    std::vector<Ring> rings;
    rings.reserve(along + 1);
    for (std::uint32_t i = 0; i <= along; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(along);
        const spatial::SplineSample sample = curve.sampleByDistance(u * length);
        rings.push_back({sample, radius * std::max(sample.scale, 0.0f) * ((1.0f - u) + u * endScale)});
    }

    const auto pointAt = [&](std::uint32_t row, float angle) {
        const Ring& ring = rings[row];
        const float u = static_cast<float>(row) / static_cast<float>(along);
        const float theta = angle + twist * u;
        const glm::vec3 offset = ring.sample.normal * std::cos(theta) + ring.sample.binormal * std::sin(theta);
        return ring.sample.position + offset * ring.radius;
    };

    for (std::uint32_t row = 0; row <= along; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(along);
        for (std::uint32_t col = 0; col < columns; ++col) {
            const float u = static_cast<float>(col) / static_cast<float>(around);
            const float theta = u * kTwoPi;
            const glm::vec3 position = pointAt(row, theta);
            // Surface tangents by central difference in both parameters: exact enough that the
            // taper's slope shows up in the shading, and free of the special cases an analytic
            // derivative would need at the ends and at a zero-radius ring.
            const float dTheta = kTwoPi / static_cast<float>(around);
            const glm::vec3 dAround = pointAt(row, theta + dTheta) - pointAt(row, theta - dTheta);
            const std::uint32_t prev = row > 0 ? row - 1 : row;
            const std::uint32_t next = row < along ? row + 1 : row;
            const glm::vec3 dAlong = pointAt(next, theta) - pointAt(prev, theta);
            glm::vec3 normal = glm::cross(dAlong, dAround);
            const glm::vec3 radial = position - rings[row].sample.position;
            if (glm::dot(normal, normal) < 1e-16f) {
                normal = glm::dot(radial, radial) > 1e-16f ? radial : rings[row].sample.normal;
            }
            normal = glm::normalize(normal);
            if (glm::dot(normal, radial) < 0.0f) {
                normal = -normal;
            }
            mesh.vertices.push_back(Vertex{position, normal, {u, v}});
        }
    }

    const auto emit = [&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2) {
        const glm::vec3& p0 = mesh.vertices[i0].position;
        const glm::vec3& p1 = mesh.vertices[i1].position;
        const glm::vec3& p2 = mesh.vertices[i2].position;
        if (p0 != p1 && p1 != p2 && p0 != p2) {
            mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
        }
    };
    for (std::uint32_t row = 0; row < along; ++row) {
        for (std::uint32_t col = 0; col < around; ++col) {
            const std::uint32_t a = row * columns + col;
            const std::uint32_t c = a + 1;
            const std::uint32_t d = a + columns;
            const std::uint32_t e = d + 1;
            // The surface normal is cross(dAlong, dAround), so the first edge of each triangle
            // has to run along the curve and the second around it.
            emit(a, d, c);
            emit(c, d, e);
        }
    }

    if (caps) {
        for (const bool atEnd : {false, true}) {
            const Ring& ring = atEnd ? rings.back() : rings.front();
            if (!(ring.radius > 1e-6f)) {
                continue; // a fully tapered end is already closed
            }
            const glm::vec3 normal = atEnd ? ring.sample.tangent : -ring.sample.tangent;
            const auto centre = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Vertex{ring.sample.position, normal, {0.5f, 0.5f}});
            const float roll = atEnd ? twist : 0.0f;
            for (std::uint32_t col = 0; col < around; ++col) {
                const float u = static_cast<float>(col) / static_cast<float>(around);
                const float theta = u * kTwoPi + roll;
                const glm::vec3 offset = ring.sample.normal * std::cos(theta) + ring.sample.binormal * std::sin(theta);
                mesh.vertices.push_back(
                    Vertex{ring.sample.position + offset * ring.radius, normal,
                           {0.5f + 0.5f * std::cos(theta), 0.5f + 0.5f * std::sin(theta)}});
            }
            for (std::uint32_t col = 0; col < around; ++col) {
                const std::uint32_t first = centre + 1 + col;
                const std::uint32_t second = centre + 1 + (col + 1) % around;
                if (atEnd) {
                    emit(centre, second, first);
                } else {
                    emit(centre, first, second);
                }
            }
        }
    }
    return mesh;
}

MeshData makeUvSphere(float radius, int segments, int rings) {
    const auto ss = static_cast<std::uint32_t>(std::clamp(segments, 3, 256));
    const auto rr = static_cast<std::uint32_t>(std::clamp(rings, 2, 128));

    MeshData mesh;
    mesh.name = "sphere";
    mesh.vertices.reserve(static_cast<std::size_t>((ss + 1) * (rr + 1)));
    mesh.indices.reserve(static_cast<std::size_t>(6 * ss * (rr - 1)));
    for (std::uint32_t row = 0; row <= rr; ++row) {
        const float tv = static_cast<float>(row) / static_cast<float>(rr);
        const float phi = glm::pi<float>() * tv;
        const float sinPhi = row == 0 || row == rr ? 0.0f : std::sin(phi);
        const float cosPhi = row == 0 ? 1.0f : (row == rr ? -1.0f : std::cos(phi));
        for (std::uint32_t j = 0; j <= ss; ++j) {
            const float tu = static_cast<float>(j) / static_cast<float>(ss);
            const float theta = kTwoPi * static_cast<float>(j % ss) / static_cast<float>(ss);
            const glm::vec3 dir(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta));
            mesh.vertices.push_back(Vertex{dir * radius, dir, {tu, tv}});
        }
    }
    for (std::uint32_t row = 0; row < rr; ++row) {
        for (std::uint32_t j = 0; j < ss; ++j) {
            const std::uint32_t p00 = row * (ss + 1) + j;
            const std::uint32_t p10 = p00 + 1;
            const std::uint32_t p01 = p00 + (ss + 1);
            const std::uint32_t p11 = p01 + 1;
            // +theta then +phi (southwards): dtheta x dphi == outward normal.
            if (row != 0) {
                mesh.indices.insert(mesh.indices.end(), {p00, p10, p11});
            }
            if (row != rr - 1) {
                mesh.indices.insert(mesh.indices.end(), {p00, p11, p01});
            }
        }
    }
    return mesh;
}

// (M+1)(m+1) vertices (both seams duplicated for uv), 6Mm indices. u around the major circle
// (about +Y from +X), v around the tube (0 = outer equator, 0.25 = top).
MeshData makeTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments) {
    const auto ms = static_cast<std::uint32_t>(std::clamp(majorSegments, 3, 256));
    const auto ns = static_cast<std::uint32_t>(std::clamp(minorSegments, 3, 128));

    MeshData mesh;
    mesh.name = "torus";
    mesh.vertices.reserve(static_cast<std::size_t>((ms + 1) * (ns + 1)));
    mesh.indices.reserve(static_cast<std::size_t>(6 * ms * ns));
    for (std::uint32_t i = 0; i <= ms; ++i) {
        const float tu = static_cast<float>(i) / static_cast<float>(ms);
        const float theta = kTwoPi * static_cast<float>(i % ms) / static_cast<float>(ms);
        const glm::vec3 radial(std::cos(theta), 0.0f, std::sin(theta));
        const glm::vec3 ringCentre = radial * majorRadius;
        for (std::uint32_t j = 0; j <= ns; ++j) {
            const float tv = static_cast<float>(j) / static_cast<float>(ns);
            const float phi = kTwoPi * static_cast<float>(j % ns) / static_cast<float>(ns);
            const glm::vec3 normal = radial * std::cos(phi) + glm::vec3(0.0f, std::sin(phi), 0.0f);
            mesh.vertices.push_back(Vertex{ringCentre + normal * minorRadius, normal, {tu, tv}});
        }
    }
    for (std::uint32_t i = 0; i < ms; ++i) {
        for (std::uint32_t j = 0; j < ns; ++j) {
            const std::uint32_t p00 = i * (ns + 1) + j;
            const std::uint32_t p01 = p00 + 1;        // +phi
            const std::uint32_t p10 = p00 + (ns + 1); // +theta
            const std::uint32_t p11 = p10 + 1;
            // +phi then +theta: dphi x dtheta == outward normal.
            mesh.indices.insert(mesh.indices.end(), {p00, p01, p11, p00, p11, p10});
        }
    }
    return mesh;
}

// A pointSize x pointSize quad in the XY plane facing +Z: 4 vertices, 2 CCW triangles, uv 0..1.
MeshData makePointQuad(float size) {
    MeshData mesh;
    mesh.name = "point";
    const float h = size * 0.5f;
    const glm::vec3 n(0.0f, 0.0f, 1.0f);
    mesh.vertices = {Vertex{{-h, -h, 0.0f}, n, {0.0f, 0.0f}}, Vertex{{h, -h, 0.0f}, n, {1.0f, 0.0f}},
                     Vertex{{h, h, 0.0f}, n, {1.0f, 1.0f}}, Vertex{{-h, h, 0.0f}, n, {0.0f, 1.0f}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

Result<MeshData> makeSourceMesh(const SourceSpec& spec) {
    if (auto ok = spec.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    switch (spec.kind) {
    case PrimitiveKind::Box:
        return makeBeveledBox(spec.size, spec.subdivisions, spec.bevel, spec.bevelSegments);
    case PrimitiveKind::Cylinder:
        return makeBeveledCylinder(spec.radius, spec.height, spec.radialSegments, spec.heightSegments, spec.caps,
                                   spec.bevel, spec.bevelSegments);
    case PrimitiveKind::Sphere:
        return makeUvSphere(spec.radius, spec.segments, spec.rings);
    case PrimitiveKind::Torus:
        return makeTorus(spec.majorRadius, spec.minorRadius, spec.majorSegments, spec.minorSegments);
    case PrimitiveKind::Tube:
        return makeTube(spec.curve, spec.tubeRadius, spec.tubeTaper, spec.tubeSides, spec.tubeSegments,
                        spec.tubeTwist, spec.tubeCaps);
    case PrimitiveKind::Mesh:
        if (!spec.assetMesh) {
            return fail("mesh source '{}' has not been resolved", spec.asset);
        }
        // ADR-110: LOD0 through meshoptimizer, like every rung below it. The budget is reached by
        // simplification rather than by a grid clustering that cannot reach a triangle count, and
        // the mesh the near field draws is welded, vertex-cache ordered and vertex-fetch ordered --
        // which, before this, was true of every level except the one that carries most of the
        // triangles.
        return assets::sourceLodMesh(*spec.assetMesh, spec.meshBudget, assets::lod0Settings());
    case PrimitiveKind::Point:
        return makePointQuad(spec.pointSize);
    case PrimitiveKind::Procedural:
        return fail("procedural source '{}' resolves through ProceduralGeometry::resolveSourceMesh", spec.reference);
    }
    return fail("unknown primitive kind");
}

float sourceBoundingRadius(const SourceSpec& spec) {
    return std::max(glm::length(sourceHalfExtent(spec)), 1e-4f);
}

// Vertex-clustering decimation. The grid is sized so its occupied cells land near the requested
// triangle count; each cell collapses to one vertex averaged over its members, and any triangle
// whose corners fall in fewer than three distinct cells disappears. Deterministic: the grid comes
// from the mesh's own bounds and the target, and the averaging order follows vertex order.
MeshData decimateMesh(const MeshData& mesh, int targetTriangles) {
    const auto triangleCount = static_cast<int>(mesh.indices.size() / 3);
    if (targetTriangles <= 0 || triangleCount <= targetTriangles || mesh.vertices.empty()) {
        return mesh;
    }
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const Vertex& v : mesh.vertices) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    const glm::vec3 size = glm::max(hi - lo, glm::vec3(1e-6f));
    // Roughly two triangles per occupied cell on a surface, and a surface occupies about n^2 of
    // an n^3 grid, so n ~ sqrt(target / 2). Clamped so a tiny budget still leaves a shape.
    const float target = static_cast<float>(std::max(targetTriangles, 8));
    const auto n = static_cast<int>(std::clamp(std::sqrt(target * 0.5f), 4.0f, 512.0f));
    const glm::vec3 cell = size / static_cast<float>(n);

    struct Cluster {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f};
        glm::vec2 uv{0.0f};
        int count = 0;
    };
    std::unordered_map<std::uint64_t, std::uint32_t> cellIndex;
    std::vector<Cluster> clusters;
    std::vector<std::uint32_t> remap(mesh.vertices.size());
    cellIndex.reserve(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vertex& v = mesh.vertices[i];
        const glm::vec3 g = glm::floor((v.position - lo) / cell);
        const auto cx = static_cast<std::uint64_t>(std::clamp(static_cast<int>(g.x), 0, n));
        const auto cy = static_cast<std::uint64_t>(std::clamp(static_cast<int>(g.y), 0, n));
        const auto cz = static_cast<std::uint64_t>(std::clamp(static_cast<int>(g.z), 0, n));
        const std::uint64_t key = (cx << 42) | (cy << 21) | cz;
        auto [it, inserted] = cellIndex.emplace(key, static_cast<std::uint32_t>(clusters.size()));
        if (inserted) {
            clusters.emplace_back();
        }
        Cluster& c = clusters[it->second];
        c.position += v.position;
        c.normal += v.normal;
        c.uv += v.uv;
        ++c.count;
        remap[i] = it->second;
    }

    MeshData out;
    out.name = mesh.name;
    out.vertices.reserve(clusters.size());
    for (const Cluster& c : clusters) {
        const float inv = 1.0f / static_cast<float>(std::max(c.count, 1));
        const glm::vec3 normal = c.normal * inv;
        out.vertices.push_back(Vertex{c.position * inv,
                                      glm::dot(normal, normal) > 1e-12f ? glm::normalize(normal)
                                                                        : glm::vec3(0.0f, 1.0f, 0.0f),
                                      c.uv * inv});
    }
    out.indices.reserve(mesh.indices.size());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = remap[mesh.indices[i]];
        const std::uint32_t b = remap[mesh.indices[i + 1]];
        const std::uint32_t c = remap[mesh.indices[i + 2]];
        if (a != b && b != c && a != c) {
            out.indices.insert(out.indices.end(), {a, b, c});
        }
    }
    return out;
}

Result<MeshData> makeLodMesh(const SourceSpec& spec, int level, float impostorSize) {
    // An imported mesh has no generator parameters to halve, so its levels are simplifications of
    // whatever the budget already left.
    //
    // They go through meshoptimizer (ADR-078), not through `decimateMesh`. The difference is not a
    // refinement: `decimateMesh` is a vertex clustering on a uniform grid, and a grid has no way to
    // *reach* a triangle count -- it snaps vertices into cells and keeps whatever triangles survive.
    // On the two assets Glowmere leans on hardest that is close to nothing. Measured, before this:
    //
    //     valley_canopy  lod1 = 97% of the source, lod2 = 72%, lod3 = 72%   (asked 35 / 12 / 4)
    //     valley_bushes  lod1 = 96%,               lod2 = 78%, lod3 = 78%
    //
    // -- a ladder whose rungs are all the same height, which every counter in the frame reports as
    // a working LOD system because `lod=2/30/124/0` says how many instances chose each level and
    // nothing said how big the levels were. The scene pass is fragment-bound and its fragment
    // invocations are set by triangle count rather than by pixels (sub-pixel triangles still cost a
    // quad each), so those percentages are very nearly the whole cost of those two layers.
    //
    // `vegetationLodSettings()` is the calibration ADR-078 measured for exactly this geometry and
    // then left unused: the same ratios this function already asked for, lower attribute weights,
    // and the sloppy fallback armed, which is what gets past a Quaternius tree's non-manifold
    // branch junctions. It is used for every mesh source, not only for scatter: the fallback fires
    // only on a level that stalled, so geometry that simplifies cleanly is unaffected by arming it.
    //
    // LOD0 is deliberately not routed through here -- it stays exactly the mesh `makeSourceMesh`
    // builds -- so the near field is unchanged and only levels a viewer sees at a distance move.
    if (spec.kind == PrimitiveKind::Mesh && level > 0 && level <= 3 && spec.assetMesh) {
        Result<MeshData> base = makeSourceMesh(spec);
        if (!base) {
            return base;
        }
        auto chain = assets::buildLodChain(*base, assets::vegetationLodSettings());
        if (chain && static_cast<std::size_t>(level) < chain->levels.size()) {
            MeshData out = std::move(chain->levels[static_cast<std::size_t>(level)].mesh);
            if (!out.indices.empty() && out.valid()) {
                return out;
            }
        }
        // A mesh the simplifier refuses is still better served by the old clustering than by the
        // source at every distance.
        const auto full = static_cast<int>(base->indices.size() / 3);
        static constexpr std::array<float, 4> kLevelShare{1.0f, 0.35f, 0.12f, 0.04f};
        return decimateMesh(*base,
                            std::max(static_cast<int>(static_cast<float>(full) * kLevelShare[level]), 24));
    }
    if (level <= 0) {
        return makeSourceMesh(spec);
    }
    if (level == 1) {
        // Half the segment counts of every generator; the floors keep the spec valid (3 for the
        // radial-style counts, 2 for sphere rings, 1 for height segments and box subdivisions).
        const auto halve = [](int v, int floorValue) { return std::max(v / 2, floorValue); };
        SourceSpec reduced = spec;
        reduced.subdivisions = halve(spec.subdivisions, 1);
        reduced.radialSegments = halve(spec.radialSegments, 3);
        reduced.heightSegments = halve(spec.heightSegments, 1);
        reduced.segments = halve(spec.segments, 3);
        reduced.rings = halve(spec.rings, 2);
        reduced.majorSegments = halve(spec.majorSegments, 3);
        reduced.minorSegments = halve(spec.minorSegments, 3);
        return makeSourceMesh(reduced);
    }
    if (level > 3) {
        return fail("lod level must be in 0..3 (got {})", level);
    }
    if (auto ok = spec.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    // LOD2: a billboard circumscribing the bounding sphere. LOD3: an eighth of that - a dot.
    const float size = 2.0f * std::max(impostorSize, 1e-4f) * sourceBoundingRadius(spec);
    MeshData quad = makePointQuad(level == 2 ? size : size * 0.125f);
    quad.name = level == 2 ? "lod-impostor" : "lod-point";
    return quad;
}

// ================================================================================================
// Distributions
// ================================================================================================

Result<void> Distribution::validate() const {
    if (kind == DistributionKind::Grid) {
        if (gridCount.x < 1 || gridCount.y < 1 || gridCount.z < 1) {
            return fail("grid counts must be >= 1");
        }
        const long long total = static_cast<long long>(gridCount.x) * gridCount.y * gridCount.z;
        if (total > kMaxInstances) {
            return fail("grid instance count {} exceeds {}", total, kMaxInstances);
        }
    } else if (kind != DistributionKind::Single && kind != DistributionKind::Grammar &&
               kind != DistributionKind::Scatter) {
        if (count < 1 || count > kMaxInstances) {
            return fail("count must be in 1..{} (got {})", kMaxInstances, count);
        }
    }
    if ((kind == DistributionKind::Linear || kind == DistributionKind::Spline) && spacing < 0.0f) {
        return fail("spacing must be >= 0");
    }
    if (kind == DistributionKind::Spline) {
        if (spline.empty()) {
            return fail("spline distribution needs a spline name");
        }
        if (!std::isfinite(splineStart) || !std::isfinite(splineEnd) || !std::isfinite(roll)) {
            return fail("splineStart, splineEnd and roll must be finite");
        }
    }
    if ((kind == DistributionKind::Radial || kind == DistributionKind::Spiral) && radius < 0.0f) {
        return fail("radius must be >= 0");
    }
    return {};
}

namespace {

// A closed spline whose distributed span is a whole number of turns has no distinct end: the
// instances divide the span without the duplicate at the seam.
bool splineSpanWraps(const Distribution& d, const spatial::Spline& curve) {
    const float span = std::abs(d.splineEnd - d.splineStart);
    return curve.closed && std::abs(span - std::round(span)) < 1e-5f && span >= 0.5f;
}

} // namespace

int Distribution::instanceCount(const spatial::Spline* curve) const {
    switch (kind) {
    case DistributionKind::Single:
        return 1;
    case DistributionKind::Grid:
        return std::max(gridCount.x, 1) * std::max(gridCount.y, 1) * std::max(gridCount.z, 1);
    case DistributionKind::Linear:
    case DistributionKind::Radial:
    case DistributionKind::Spiral:
        return std::max(count, 1);
    case DistributionKind::Spline: {
        if (curve == nullptr || !(spacing > 0.0f)) {
            return std::max(count, 1);
        }
        const float covered = curve->length() * std::abs(splineEnd - splineStart);
        const float steps = std::floor(covered / spacing);
        const float n = splineSpanWraps(*this, *curve) ? steps : steps + 1.0f;
        return static_cast<int>(std::clamp(n, 1.0f, static_cast<float>(kMaxInstances)));
    }
    case DistributionKind::Grammar:
        return std::max(count, 1); // the owner replaces this with the expansion size
    case DistributionKind::Scatter:
        return scatterCloud ? std::max(static_cast<int>(scatterCloud->count()), 1) : 1;
    }
    return 1;
}

Transform Distribution::placement(int index, const spatial::Spline* curve) const {
    Transform t;
    const int n = instanceCount(curve);
    const int i = std::clamp(index, 0, n - 1);
    const float u = n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;

    switch (kind) {
    case DistributionKind::Single:
    case DistributionKind::Grammar: // placements come from the grammar expansion (generateCloud)
    case DistributionKind::Scatter: // placements come from the supplied cloud (generateCloud)
        break;

    case DistributionKind::Spline: {
        if (curve == nullptr) {
            break; // identity placements without the spline
        }
        // The normalised position within the span: evenly spread by index in count mode (the
        // closed whole-turn case divides by n so the seam is not duplicated), exactly `spacing`
        // apart from splineStart in spacing mode (running towards splineEnd).
        const float len = curve->length();
        float w = splineSpanWraps(*this, *curve) ? static_cast<float>(i) / static_cast<float>(n) : u;
        if (spacing > 0.0f) {
            const float span = splineEnd - splineStart;
            const float dir = span < 0.0f ? -1.0f : 1.0f;
            w = span != 0.0f ? static_cast<float>(i) * spacing * dir / (span * std::max(len, 1e-6f)) : 0.0f;
        }
        const float d = len * (splineStart + (splineEnd - splineStart) * w);
        const spatial::SplineSample s = curve->sampleByDistance(d);
        t.position = s.position + s.binormal * splineOffset.x + s.normal * splineOffset.y + s.tangent * splineOffset.z;
        if (alignToSpline) {
            t.rotation = glm::normalize(s.rotation() * glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f)));
        }
        t.scale = glm::vec3(std::max(s.scale, 1e-3f));
        break;
    }

    case DistributionKind::Linear: {
        glm::vec3 dir = end - start;
        const float len = glm::length(dir);
        dir = len > 1e-8f ? dir / len : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 to = spacing > 0.0f ? start + dir * (spacing * static_cast<float>(n - 1)) : end;
        t.position = glm::mix(start, to, u);
        if (orientAlong) {
            t.rotation = lookRotation(dir, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        break;
    }

    case DistributionKind::Grid: {
        const int cx = std::max(gridCount.x, 1);
        const int cy = std::max(gridCount.y, 1);
        const int cz = std::max(gridCount.z, 1);
        const int ix = i % cx;
        const int iy = (i / cx) % cy;
        const int iz = i / (cx * cy);
        t.position = {(static_cast<float>(ix) - static_cast<float>(cx - 1) * 0.5f) * gridSpacing.x,
                      (static_cast<float>(iy) - static_cast<float>(cy - 1) * 0.5f) * gridSpacing.y,
                      (static_cast<float>(iz) - static_cast<float>(cz - 1) * 0.5f) * gridSpacing.z};
        break;
    }

    case DistributionKind::Radial: {
        const PlaneFrame frame = planeFrame(plane);
        const float span = endAngle - startAngle;
        const bool closed = std::abs(std::abs(span) - kTwoPi) < 1e-4f;
        const float step = closed ? span / static_cast<float>(n) : span / static_cast<float>(std::max(n - 1, 1));
        const float theta = startAngle + step * static_cast<float>(i);
        t.position = center + (frame.a * std::cos(theta) + frame.b * std::sin(theta)) * radius;
        const glm::vec3 tangent = -frame.a * std::sin(theta) + frame.b * std::cos(theta);
        t.rotation = orientationRotation(orientation, theta, tangent, frame);
        break;
    }

    case DistributionKind::Spiral: {
        const PlaneFrame frame = planeFrame(plane);
        const float theta = startAngle + kTwoPi * turns * u + spiralAngle;
        const float r = radius + radiusGrowth * u;
        const glm::vec3 radial = frame.a * std::cos(theta) + frame.b * std::sin(theta);
        t.position = center + radial * r + frame.n * (spiralHeight * u);
        // dp/du: radius growth along the radial, angular motion along the circle tangent, rise
        // along the normal.
        const glm::vec3 circleTangent = -frame.a * std::sin(theta) + frame.b * std::cos(theta);
        const glm::vec3 helixTangent = radial * radiusGrowth + circleTangent * (kTwoPi * turns * r) + frame.n * spiralHeight;
        t.rotation = orientationRotation(orientation, theta, glm::length(helixTangent) > 1e-8f ? helixTangent : circleTangent, frame);
        break;
    }
    }
    return t;
}

std::uint64_t Distribution::structuralHash() const {
    StructHash h;
    h.u32(static_cast<std::uint32_t>(kind));
    switch (kind) {
    case DistributionKind::Single:
        break;
    case DistributionKind::Linear:
        h.i32(count);
        h.v3(start);
        h.v3(end);
        h.boolean(orientAlong);
        h.f32(spacing);
        break;
    case DistributionKind::Grid:
        h.iv3(gridCount);
        h.v3(gridSpacing);
        break;
    case DistributionKind::Radial:
        h.i32(count);
        h.f32(radius);
        h.f32(startAngle);
        h.f32(endAngle);
        h.u32(static_cast<std::uint32_t>(plane));
        h.v3(center);
        h.u32(static_cast<std::uint32_t>(orientation));
        break;
    case DistributionKind::Spiral:
        h.i32(count);
        h.f32(radius);
        h.f32(startAngle);
        h.u32(static_cast<std::uint32_t>(plane));
        h.v3(center);
        h.u32(static_cast<std::uint32_t>(orientation));
        h.f32(radiusGrowth);
        h.f32(turns);
        h.f32(spiralHeight);
        h.f32(spiralAngle);
        break;
    case DistributionKind::Spline:
        h.i32(count);
        h.f32(spacing);
        h.u64(spline.size());
        for (const char c : spline) {
            h.u32(static_cast<std::uint8_t>(c));
        }
        h.f32(splineStart);
        h.f32(splineEnd);
        h.boolean(alignToSpline);
        h.f32(roll);
        h.v3(splineOffset);
        break;
    case DistributionKind::Grammar:
        break; // the owner hashes its grammar (ProceduralGeometry::structuralHash)
    case DistributionKind::Scatter:
        h.u64(scatterHash); // the generator's own summary of the cloud it supplied
        break;
    }
    return h.value();
}

// ================================================================================================
// Variation
// ================================================================================================

std::uint64_t LodSettings::structuralHash() const {
    StructHash h;
    h.i32(lodCount); // the only structural field: it decides how many LOD meshes are generated
    return h.value();
}

std::uint64_t Variation::structuralHash() const {
    StructHash h;
    h.u32(seed);
    h.v3(randomPosition);
    h.v3(randomRotation);
    h.v3(randomScale);
    h.f32(randomUniformScale);
    return h.value();
}

float hashInstance(std::uint32_t seed, std::uint32_t index, std::uint32_t channel) {
    return noise::hashIndex(seed, index, channel);
}

namespace {
constexpr std::uint32_t kVariationChannel = 16;
constexpr std::uint32_t kHueChannel = 4;
constexpr std::uint32_t kValueChannel = 5;
constexpr std::uint32_t kEmissiveChannel = 6;
constexpr std::uint32_t kSparsityChannel = 7;

// Uniform in [-1, 1).
float signedRandom(std::uint32_t seed, std::uint32_t index, std::uint32_t channel) {
    return hashInstance(seed, index, channel) * 2.0f - 1.0f;
}
} // namespace

Transform variationTransform(const Variation& v, std::uint32_t index) {
    Transform t;
    const auto r = [&](std::uint32_t channel) { return signedRandom(v.seed, index, kVariationChannel + channel); };
    t.position = v.randomPosition * glm::vec3(r(0), r(1), r(2));
    const glm::vec3 euler = v.randomRotation * glm::vec3(r(3), r(4), r(5));
    t.rotation = glm::length(euler) > 0.0f ? glm::normalize(glm::quat(euler)) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const float uniform = 1.0f + v.randomUniformScale * r(9);
    const glm::vec3 perAxis = glm::vec3(1.0f) + v.randomScale * glm::vec3(r(6), r(7), r(8));
    t.scale = glm::max(perAxis * uniform, glm::vec3(1e-3f));
    return t;
}

// ================================================================================================
// Deformers
// ================================================================================================

namespace {

// Ramp weight for Twist/Sine/Noise/Displacement: 1 without falloff, else |y| / falloff clamped.
float falloffWeight(const Deformer& d, float y) {
    if (d.falloff <= 0.0f) {
        return 1.0f;
    }
    return std::clamp(std::abs(y) / d.falloff, 0.0f, 1.0f);
}

glm::vec3 applyBend(const Deformer& d, const glm::vec3& p) {
    const float k = d.amount;
    if (std::abs(k) < 1e-6f) {
        return p;
    }
    const glm::vec3 axis = unitOr(d.axis, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 dir = d.displacementAxis - axis * glm::dot(d.displacementAxis, axis);
    dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : anyPerpendicular(axis);
    const glm::vec3 side = glm::cross(axis, dir);

    const glm::vec3 q = p - d.center;
    const float y = glm::dot(q, axis);
    const float x = glm::dot(q, dir);
    const float z = glm::dot(q, side);

    // Bent region |y| <= falloff (whole axis when falloff == 0); the remainder continues straight
    // along the arc's end tangent.
    float yBent = y;
    float rest = 0.0f;
    if (d.falloff > 0.0f) {
        yBent = std::clamp(y, -d.falloff, d.falloff);
        rest = y - yBent;
    }
    const float radius = 1.0f / k;
    const float theta = k * yBent;
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    // Arc: the axis point at arc length y sits at (R - R cos, R sin) in the (dir, axis) plane
    // (centre of curvature at +R along dir); an offset x along dir stays at distance R - x from it.
    const float xb = radius - (radius - x) * c + rest * s;
    const float yb = (radius - x) * s + rest * c;
    return d.center + dir * xb + axis * yb + side * z;
}

glm::vec3 applyTwist(const Deformer& d, const glm::vec3& p, float t) {
    const glm::vec3 axis = unitOr(d.axis, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 q = p - d.center;
    const float y = glm::dot(q, axis);
    const float angle = (d.amount * y + d.speed * t) * falloffWeight(d, y);
    if (angle == 0.0f) {
        return p;
    }
    return d.center + glm::angleAxis(angle, axis) * q;
}

glm::vec3 applySine(const Deformer& d, const glm::vec3& p, float t) {
    const glm::vec3 axis = unitOr(d.axis, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 dir = unitOr(d.displacementAxis, glm::vec3(0.0f));
    const float y = glm::dot(p - d.center, axis);
    const float wave = std::sin(y * d.frequency + d.phase + d.speed * t);
    return p + dir * (d.amount * falloffWeight(d, y) * wave);
}

glm::vec3 applyNoise(const Deformer& d, const glm::vec3& p, float t) {
    const glm::vec3 axis = unitOr(d.axis, glm::vec3(0.0f, 1.0f, 0.0f));
    const float y = glm::dot(p - d.center, axis);
    // Three decorrelated channels (input offset per axis), exactly as procedural.wgsl.
    const glm::vec3 q = p * d.scale + glm::vec3(d.speed * t);
    const glm::vec3 n(fbm3(q, d.seed) * 2.0f - 1.0f, fbm3(q + glm::vec3(31.7f), d.seed) * 2.0f - 1.0f,
                      fbm3(q + glm::vec3(67.3f), d.seed) * 2.0f - 1.0f);
    return p + n * d.axisMask * (d.amount * falloffWeight(d, y));
}

glm::vec3 applyDisplacement(const Deformer& d, const glm::vec3& p, float t) {
    const glm::vec3 axis = unitOr(d.axis, glm::vec3(0.0f, 1.0f, 0.0f));
    const float y = glm::dot(p - d.center, axis);
    const float n = fbm3(p * d.scale + glm::vec3(d.speed * t), d.seed) * 2.0f - 1.0f; // as the shader
    return p + axis * (d.amount * falloffWeight(d, y) * n);
}

} // namespace

glm::vec3 applyDeformer(const Deformer& d, glm::vec3 p, double time) {
    const auto t = static_cast<float>(time);
    switch (d.kind) {
    case DeformerKind::Bend:
        return applyBend(d, p);
    case DeformerKind::Twist:
        return applyTwist(d, p, t);
    case DeformerKind::Sine:
        return applySine(d, p, t);
    case DeformerKind::Noise:
        return applyNoise(d, p, t);
    case DeformerKind::Displacement:
        return applyDisplacement(d, p, t);
    case DeformerKind::Field:
        return p; // needs the field set: see applyFieldDeformer
    case DeformerKind::Path:
        return p; // needs the spline: see applyPathDeformer
    }
    return p;
}

glm::vec3 applyFieldDeformer(const Deformer& d, glm::vec3 p, glm::vec3 normal, double time,
                             const spatial::FieldSet& fields) {
    if (d.kind != DeformerKind::Field || d.amount == 0.0f) {
        return p;
    }
    const spatial::FieldSpec* field = fields.find(d.field);
    if (field == nullptr || !field->enabled) {
        return p;
    }
    if (field->type() == spatial::FieldType::Vector) {
        return p + spatial::sampleVector(*field, p, time, &fields) * d.amount;
    }
    const float s = spatial::sampleScalar(*field, p, time, &fields);
    const glm::vec3 dir = d.alongNormal ? unitOr(normal, glm::vec3(0.0f)) : unitOr(d.axis, glm::vec3(0.0f, 1.0f, 0.0f));
    return p + dir * (s * d.amount);
}

glm::vec3 deformPoint(const std::vector<Deformer>& stack, glm::vec3 objectPoint, const glm::mat4& instanceWorld,
                      double time, const spatial::FieldSet* fields, glm::vec3 normal) {
    const auto apply = [&](const Deformer& d, const glm::vec3& p, const glm::vec3& n) {
        if (d.kind == DeformerKind::Field) {
            return fields != nullptr ? applyFieldDeformer(d, p, n, time, *fields) : p;
        }
        return applyDeformer(d, p, time);
    };
    glm::vec3 p = objectPoint;
    for (const Deformer& d : stack) {
        if (d.enabled && d.space == DeformSpace::Local) {
            p = apply(d, p, normal);
        }
    }
    p = glm::vec3(instanceWorld * glm::vec4(p, 1.0f));
    const glm::vec3 worldNormal = unitOr(glm::mat3(instanceWorld) * normal, glm::vec3(0.0f));
    for (const Deformer& d : stack) {
        if (d.enabled && d.space == DeformSpace::World) {
            p = apply(d, p, worldNormal);
        }
    }
    return p;
}

glm::vec3 applyPathDeformer(const Deformer& d, glm::vec3 p, const spatial::Spline& spline, float sourceExtentAlongAxis) {
    if (d.kind != DeformerKind::Path || d.amount == 0.0f) {
        return p;
    }
    const glm::vec3 axis = unitOr(d.axis, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 up = std::abs(axis.y) > 0.999f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 u = glm::normalize(glm::cross(up, axis));
    const glm::vec3 v = glm::cross(axis, u);
    const glm::vec3 q = p - d.center;
    const float coord = glm::dot(q, axis);
    const float cu = glm::dot(q, u);
    const float cv = glm::dot(q, v);
    float scale = d.pathScale;
    if (scale == 0.0f) {
        scale = spline.length() / std::max(sourceExtentAlongAxis, 1e-6f);
    }
    const spatial::SplineSample s = spline.sampleByDistance(d.pathOffset + coord * scale);
    const float cr = std::cos(d.pathRoll);
    const float sr = std::sin(d.pathRoll);
    const float ru = cu * cr - cv * sr;
    const float rv = cu * sr + cv * cr;
    const glm::vec3 bent = s.position + (s.binormal * ru + s.normal * rv) * s.scale;
    return glm::mix(p, bent, d.amount);
}

glm::vec3 deformPointWith(const std::vector<Deformer>& stack, glm::vec3 objectPoint, const glm::mat4& instanceWorld,
                          double time, const DeformContext& ctx, glm::vec3 normal) {
    const auto apply = [&](const Deformer& d, const glm::vec3& p, const glm::vec3& n) {
        if (d.kind == DeformerKind::Field) {
            return ctx.fields != nullptr ? applyFieldDeformer(d, p, n, time, *ctx.fields) : p;
        }
        if (d.kind == DeformerKind::Path) {
            const spatial::Spline* spline = ctx.splines != nullptr ? ctx.splines->find(d.spline) : nullptr;
            return spline != nullptr ? applyPathDeformer(d, p, *spline, ctx.sourceExtent) : p;
        }
        return applyDeformer(d, p, time);
    };
    glm::vec3 p = objectPoint;
    for (const Deformer& d : stack) {
        if (d.enabled && d.space == DeformSpace::Local) {
            p = apply(d, p, normal);
        }
    }
    p = glm::vec3(instanceWorld * glm::vec4(p, 1.0f));
    const glm::vec3 worldNormal = unitOr(glm::mat3(instanceWorld) * normal, glm::vec3(0.0f));
    for (const Deformer& d : stack) {
        if (d.enabled && d.space == DeformSpace::World && d.kind != DeformerKind::Path) {
            p = apply(d, p, worldNormal);
        }
    }
    return p;
}

float fbm3(glm::vec3 p, std::uint32_t seed) {
    return noise::fbm3(p, seed);
}

// ================================================================================================
// ProceduralGeometry
// ================================================================================================

Result<void> ProceduralGeometry::validate() const {
    if (name.empty()) {
        return fail("procedural geometry needs a name");
    }
    if (auto ok = source.validate(); !ok) {
        return fail("procedural '{}': {}", name, ok.error().message);
    }
    if (auto ok = distribution.validate(); !ok) {
        return fail("procedural '{}': {}", name, ok.error().message);
    }
    if (source.kind == PrimitiveKind::Procedural && source.reference == name) {
        return fail("procedural '{}': a procedural source cannot reference itself", name);
    }
    if (hierarchy.recursionDepth < 0 || hierarchy.recursionDepth > kMaxHierarchyDepth) {
        return fail("procedural '{}': hierarchy depth must be in 0..{} (got {})", name, kMaxHierarchyDepth,
                    hierarchy.recursionDepth);
    }
    if (hierarchy.maxInstances < 1 || hierarchy.maxInstances > kMaxInstances) {
        return fail("procedural '{}': hierarchy maxInstances must be in 1..{} (got {})", name, kMaxInstances,
                    hierarchy.maxInstances);
    }
    if (!(hierarchy.scalePerLevel > 0.0f)) {
        return fail("procedural '{}': hierarchy scalePerLevel must be > 0", name);
    }
    if (distribution.kind == DistributionKind::Grammar) {
        if (auto ok = grammar.validate(); !ok) {
            return fail("procedural '{}': {}", name, ok.error().message);
        }
    }
    if (deformers.size() > static_cast<std::size_t>(kMaxDeformers)) {
        return fail("procedural '{}': at most {} deformers (got {})", name, kMaxDeformers, deformers.size());
    }
    for (std::size_t i = 0; i < deformers.size(); ++i) {
        const Deformer& d = deformers[i];
        if (glm::length(d.axis) < 1e-8f) {
            return fail("procedural '{}': deformer {} has a zero axis", name, i + 1);
        }
        if (d.falloff < 0.0f) {
            return fail("procedural '{}': deformer {} falloff must be >= 0", name, i + 1);
        }
        if (!(d.scale > 0.0f)) {
            return fail("procedural '{}': deformer {} scale must be > 0", name, i + 1);
        }
        if (d.kind == DeformerKind::Field && d.field.empty()) {
            return fail("procedural '{}': deformer {} (field) needs a field name", name, i + 1);
        }
        if (d.kind == DeformerKind::Path && d.spline.empty()) {
            return fail("procedural '{}': deformer {} (path) needs a spline name", name, i + 1);
        }
    }
    if (effectors.size() > static_cast<std::size_t>(kMaxEffectors)) {
        return fail("procedural '{}': at most {} effectors (got {})", name, kMaxEffectors, effectors.size());
    }
    for (std::size_t i = 0; i < effectors.size(); ++i) {
        if (effectors[i].field.empty()) {
            return fail("procedural '{}': effector {} needs a field name", name, i + 1);
        }
        if (effectors[i].op == spatial::EffectorOp::Attribute && effectors[i].target.empty()) {
            return fail("procedural '{}': effector {} (attribute) needs a target", name, i + 1);
        }
    }
    for (std::size_t i = 0; i < pointOps.size(); ++i) {
        const spatial::PointOp& op = pointOps[i];
        if (op.copies < 0) {
            return fail("procedural '{}': op {} copies must be >= 0", name, i + 1);
        }
        if (op.stride < 1) {
            return fail("procedural '{}': op {} stride must be >= 1", name, i + 1);
        }
        if (op.kind == spatial::PointOpKind::Attribute && op.attributeOp.target.empty()) {
            return fail("procedural '{}': op {} (attribute) needs a target", name, i + 1);
        }
    }
    if (lod.lodCount < 1 || lod.lodCount > kMaxLodLevels) {
        return fail("procedural '{}': lod count must be in 1..{} (got {})", name, kMaxLodLevels, lod.lodCount);
    }
    if (lod.maxDistance < 0.0f || lod.minScreenRadius < 0.0f) {
        return fail("procedural '{}': lod maxDistance and minScreenRadius must be >= 0", name);
    }
    if (!(lod.impostorSize > 0.0f)) {
        return fail("procedural '{}': lod impostorSize must be > 0", name);
    }
    if (material.roughness < 0.0f || material.roughness > 1.0f || material.metallic < 0.0f || material.metallic > 1.0f) {
        return fail("procedural '{}': roughness and metallic must be in 0..1", name);
    }
    if (material.emissiveIntensity < 0.0f) {
        return fail("procedural '{}': emissive intensity must be >= 0", name);
    }
    return {};
}

std::uint64_t ProceduralGeometry::structuralHash() const {
    StructHash h;
    h.u64(source.structuralHash());
    h.u64(distribution.structuralHash());
    h.u64(variation.structuralHash());
    h.transform(sourceTransform);
    h.transform(distributionTransform);
    h.f32(materialVariation.hueShift);
    h.f32(materialVariation.hueGradient);
    h.f32(materialVariation.valueRandom);
    h.f32(materialVariation.emissiveRandom);
    h.f32(materialVariation.emissiveSparsity);
    h.f32(materialVariation.hueField);
    h.f32(materialVariation.hueFieldScale);
    h.f32(materialVariation.chromaDrift);
    h.f32(materialVariation.chromaDriftScale);
    h.f32(materialVariation.chromaDriftSpeed);
    h.u32(materialVariation.perceptualHue ? 1u : 0u);
    h.f32(materialVariation.emissiveGradient);
    h.u64(pointOps.size());
    for (const spatial::PointOp& op : pointOps) {
        h.u64(spatial::pointOpHash(op));
    }
    h.u64(extraLane.size());
    for (const char c : extraLane) {
        h.u32(static_cast<std::uint8_t>(c));
    }
    // Hierarchy and grammar (ADR-029): structural like the distribution.
    h.u64(hierarchy.structuralHash());
    h.u64(grammar.structuralHash());
    h.u64(lod.structuralHash()); // lodCount only; the rest of LodSettings is per-frame uniforms
    return h.value();
}

spatial::PointCloud ProceduralGeometry::generateCloud(const GenerationContext& ctx) const {
    // Self-recursion and procedural sources compose this flat generator (hierarchy.cpp).
    if (hierarchy.recursionDepth > 0 || source.kind == PrimitiveKind::Procedural) {
        return detail::generateHierarchicalCloud(*this, ctx);
    }
    // Grammar distributions: the expansion's points are the placements (their depth/rule/branch
    // columns are kept); instanceCount() does not know the expansion size.
    const bool fromGrammar = distribution.kind == DistributionKind::Grammar;
    // Scatter placements are supplied rather than derived, so the cloud is read straight through:
    // whoever generated it knew things this object cannot (which slopes a fern grows on), and the
    // point of the kind is that it does not have to.
    const bool fromScatter = distribution.kind == DistributionKind::Scatter && distribution.scatterCloud &&
                             distribution.scatterCloud->count() > 0;
    const spatial::PointCloud expanded = fromGrammar    ? grammar.expand()
                                         : fromScatter ? *distribution.scatterCloud
                                                       : spatial::PointCloud();
    const bool supplied = fromGrammar || fromScatter;
    const spatial::Spline* curve = nullptr;
    if (distribution.kind == DistributionKind::Spline && ctx.splines != nullptr) {
        curve = ctx.splines->find(distribution.spline);
        if (curve != nullptr) {
            curve->prepare();
        }
    }
    const auto count = supplied ? expanded.count()
                                : static_cast<std::size_t>(std::max(distribution.instanceCount(curve), 1));
    spatial::PointCloud out(count);
    const float invLast = count > 1 ? 1.0f / static_cast<float>(count - 1) : 0.0f;
    const glm::vec3 sourceExtent = sourceHalfExtent(source) * glm::abs(sourceTransform.scale);
    auto positions = out.positions();
    auto rotations = out.rotations();
    auto scales = out.scales();
    auto ids = out.ids();
    auto seeds = out.seeds();
    auto densities = out.densities();
    auto colors = out.colors();
    auto emissives = out.emissives();
    auto indices = out.indices();
    auto extents = out.attributes.view<glm::vec3>(spatial::attr::bounds);

    for (std::size_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        const auto signedIndex = static_cast<int>(i);
        const float u = static_cast<float>(i) * invLast;
        Transform placement;
        if (supplied) {
            placement.position = expanded.positions()[i];
            const glm::vec4 r = expanded.rotations()[i];
            placement.rotation = glm::quat(r.w, r.x, r.y, r.z);
            placement.scale = expanded.scales()[i];
        } else {
            placement = distribution.placement(signedIndex, curve);
        }
        const Transform t = compose(distributionTransform, compose(placement, variationTransform(variation, index)));

        positions[i] = t.position;
        rotations[i] = glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
        scales[i] = t.scale;
        ids[i] = signedIndex;
        seeds[i] = static_cast<std::int32_t>(variation.seed);
        densities[i] = 1.0f;
        indices[i] = u;
        if (extents) {
            (*extents)[i] = sourceExtent;
        }

        // Regions of agreement, not per-instance noise: the field is sampled where the instance
        // stands, so a patch shares a hue and the next patch over does not.
        float hueTurns = materialVariation.hueShift * hashInstance(variation.seed, index, kHueChannel) +
                         materialVariation.hueGradient * u;
        if (materialVariation.hueField != 0.0f) {
            const float scale = std::max(materialVariation.hueFieldScale, 1e-3f);
            hueTurns += materialVariation.hueField *
                        noise::regionField(t.position / scale, variation.seed ^ 0x9e37u);
        }
        const auto rotate = materialVariation.perceptualHue ? hueRotationMultiplierOklab
                                                            : hueRotationMultiplier;
        const float value =
            std::max(0.0f, 1.0f + materialVariation.valueRandom * signedRandom(variation.seed, index, kValueChannel));
        float emissiveMul = std::max(0.0f, 1.0f + materialVariation.emissiveRandom *
                                                                 signedRandom(variation.seed, index, kEmissiveChannel) +
                                                     materialVariation.emissiveGradient * u);
        if (materialVariation.emissiveSparsity > 0.0f) {
            const float lit = 1.0f - std::clamp(materialVariation.emissiveSparsity, 0.0f, 1.0f);
            emissiveMul *= hashInstance(variation.seed, index, kSparsityChannel) < lit ? 1.0f : 0.0f;
        }
        colors[i] = glm::vec4(rotate(material.baseColor, hueTurns) * value, 1.0f);
        emissives[i] = rotate(material.emissiveColor, hueTurns) * emissiveMul;
    }
    if (fromGrammar) {
        for (const char* column : {"depth", "rule", "branch"}) {
            auto src = expanded.attributes.view<std::int32_t>(column);
            auto dst = out.attributes.ensure<std::int32_t>(column, spatial::AttributeType::Int);
            if (src && dst) {
                std::copy(src->values.begin(), src->values.end(), dst->values.begin());
            }
        }
    }
    return out;
}

bool ProceduralGeometry::rebuild(const GenerationContext& ctx) {
    const std::uint64_t hash = contextualHash(ctx);
    if (structureVersion != 0 && hash == builtHash) {
        return false;
    }
    builtHash = hash;
    meshHash = detail::resolvedSourceHash(*this, ctx); // the referenced object's for a Procedural source
    ++structureVersion;

    spatial::PointCloud built = generateCloud(ctx);
    if (auto ok = spatial::applyPointOps(built, pointOps); !ok) {
        log::warn("procedural '{}': {}", name, ok.error().message);
    }
    spatial::projectInstances(built, instances, extraLane);

    // Bounds: the source's bounding sphere carried by each point (rotation-invariant), padded by
    // the reach of the position effectors. A Procedural source resolves to the referenced object's
    // (leaf) primitive; every intermediate scale is already in the point scales.
    const glm::vec3 sourceExtent = detail::sourceHalfExtent(*this, ctx) * glm::abs(sourceTransform.scale);
    const float sourceRadius = glm::length(sourceExtent);
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    const auto positions = built.positions();
    const auto rotations = built.rotations();
    const auto scales = built.scales();
    for (std::size_t i = 0; i < built.count(); ++i) {
        const glm::quat rotation(rotations[i].w, rotations[i].x, rotations[i].y, rotations[i].z);
        const glm::vec3 centre = positions[i] + rotation * (sourceTransform.position * scales[i]);
        const float radius = sourceRadius * std::max({std::abs(scales[i].x), std::abs(scales[i].y), std::abs(scales[i].z)});
        lo = glm::min(lo, centre - glm::vec3(radius));
        hi = glm::max(hi, centre + glm::vec3(radius));
    }
    if (built.count() == 0) {
        lo = hi = glm::vec3(0.0f);
    }
    float padding = 0.0f;
    for (const spatial::Effector& e : effectors) {
        if (e.enabled && e.op == spatial::EffectorOp::PositionOffset) {
            padding += std::abs(e.strength);
        }
    }
    boundsMin = lo - glm::vec3(padding);
    boundsMax = hi + glm::vec3(padding);

    if (built.count() <= kKeepCloudMax) {
        cloud = std::move(built);
    } else {
        cloud.clear();
    }
    return true;
}

glm::mat4 ProceduralGeometry::instanceMatrix(std::uint32_t index) const {
    return distributionTransform.matrix() * distribution.placement(static_cast<int>(index)).matrix() *
           variationTransform(variation, index).matrix() * sourceTransform.matrix();
}

json ProceduralGeometry::toJson() const {
    json j = json::object();
    j["name"] = name;
    if (!partOf.empty()) {
        j["partOf"] = partOf;
    }
    j["visible"] = visible;
    {
        json s = json::object();
        s["kind"] = primitiveKindName(source.kind);
        s["size"] = vecToJson(source.size);
        s["subdivisions"] = source.subdivisions;
        s["bevel"] = source.bevel;
        s["asset"] = source.asset;
        s["meshBudget"] = source.meshBudget;
        s["tubeRadius"] = source.tubeRadius;
        s["tubeTaper"] = source.tubeTaper;
        s["tubeSides"] = source.tubeSides;
        s["tubeSegments"] = source.tubeSegments;
        s["tubeTwist"] = source.tubeTwist;
        s["tubeCaps"] = source.tubeCaps;
        s["curve"] = source.curve.toJson();
        s["bevelSegments"] = source.bevelSegments;
        s["radius"] = source.radius;
        s["height"] = source.height;
        s["radialSegments"] = source.radialSegments;
        s["heightSegments"] = source.heightSegments;
        s["caps"] = source.caps;
        s["segments"] = source.segments;
        s["rings"] = source.rings;
        s["majorRadius"] = source.majorRadius;
        s["minorRadius"] = source.minorRadius;
        s["majorSegments"] = source.majorSegments;
        s["minorSegments"] = source.minorSegments;
        s["pointSize"] = source.pointSize;
        s["reference"] = source.reference;
        j["source"] = std::move(s);
    }
    j["sourceTransform"] = transformToJson(sourceTransform);
    {
        const Distribution& d = distribution;
        json s = json::object();
        s["kind"] = distributionKindName(d.kind);
        s["count"] = d.count;
        s["start"] = vecToJson(d.start);
        s["end"] = vecToJson(d.end);
        s["orientAlong"] = d.orientAlong;
        s["spacing"] = d.spacing;
        s["gridCount"] = ivecToJson(d.gridCount);
        s["gridSpacing"] = vecToJson(d.gridSpacing);
        s["radius"] = d.radius;
        s["startAngle"] = d.startAngle;
        s["endAngle"] = d.endAngle;
        s["plane"] = distributionPlaneName(d.plane);
        s["center"] = vecToJson(d.center);
        s["orientation"] = orientationModeName(d.orientation);
        s["radiusGrowth"] = d.radiusGrowth;
        s["turns"] = d.turns;
        s["spiralHeight"] = d.spiralHeight;
        s["spiralAngle"] = d.spiralAngle;
        s["spline"] = d.spline;
        s["splineStart"] = d.splineStart;
        s["splineEnd"] = d.splineEnd;
        s["alignToSpline"] = d.alignToSpline;
        s["roll"] = d.roll;
        s["splineOffset"] = vecToJson(d.splineOffset);
        j["distribution"] = std::move(s);
    }
    j["distributionTransform"] = transformToJson(distributionTransform);
    {
        json s = json::object();
        s["seed"] = variation.seed;
        s["position"] = vecToJson(variation.randomPosition);
        s["rotation"] = vecToJson(variation.randomRotation);
        s["scale"] = vecToJson(variation.randomScale);
        s["uniformScale"] = variation.randomUniformScale;
        j["variation"] = std::move(s);
    }
    {
        json arr = json::array();
        for (const Deformer& d : deformers) {
            json s = json::object();
            s["kind"] = deformerKindName(d.kind);
            s["enabled"] = d.enabled;
            s["amount"] = d.amount;
            s["space"] = deformSpaceName(d.space);
            s["speed"] = d.speed;
            s["phase"] = d.phase;
            s["axis"] = vecToJson(d.axis);
            s["center"] = vecToJson(d.center);
            s["falloff"] = d.falloff;
            s["frequency"] = d.frequency;
            s["displacementAxis"] = vecToJson(d.displacementAxis);
            s["scale"] = d.scale;
            s["seed"] = d.seed;
            s["axisMask"] = vecToJson(d.axisMask);
            s["pattern"] = d.pattern;
            s["field"] = d.field;
            s["alongNormal"] = d.alongNormal;
            s["spline"] = d.spline;
            s["pathOffset"] = d.pathOffset;
            s["pathScale"] = d.pathScale;
            s["pathRoll"] = d.pathRoll;
            arr.push_back(std::move(s));
        }
        j["deformers"] = std::move(arr);
    }
    {
        json arr = json::array();
        for (const spatial::PointOp& op : pointOps) {
            arr.push_back(spatial::pointOpToJson(op));
        }
        j["ops"] = std::move(arr);
    }
    {
        json arr = json::array();
        for (const spatial::Effector& e : effectors) {
            arr.push_back(e.toJson());
        }
        j["effectors"] = std::move(arr);
    }
    j["emissiveField"] = emissiveField;
    j["emissiveFieldAmount"] = emissiveFieldAmount;
    j["extraLane"] = extraLane;
    {
        json s2 = json::object();
        s2["cull"] = lod.cull;
        s2["maxDistance"] = lod.maxDistance;
        s2["minScreenRadius"] = lod.minScreenRadius;
        s2["count"] = lod.lodCount;
        s2["distance1"] = lod.lodDistances[0];
        s2["distance2"] = lod.lodDistances[1];
        s2["distance3"] = lod.lodDistances[2];
        s2["byScreenSize"] = lod.lodByScreenSize;
        s2["spread"] = lod.lodSpread;
        s2["hysteresis"] = lod.lodHysteresis;
        s2["impostorSize"] = lod.impostorSize;
        j["lod"] = std::move(s2);
    }
    {
        json s = json::object();
        s["baseColor"] = vecToJson(material.baseColor);
        s["opacity"] = material.opacity;
        s["emissiveColor"] = vecToJson(material.emissiveColor);
        s["emissiveIntensity"] = material.emissiveIntensity;
        s["roughness"] = material.roughness;
        s["metallic"] = material.metallic;
        s["doubleSided"] = material.doubleSided;
        s["unlit"] = material.unlit;
        if (!material.program.empty()) {
            s["program"] = material.program; // ADR-030 material program name
        }
        j["material"] = std::move(s);
    }
    {
        json s = json::object();
        s["hueShift"] = materialVariation.hueShift;
        s["hueGradient"] = materialVariation.hueGradient;
        s["valueRandom"] = materialVariation.valueRandom;
        s["emissiveRandom"] = materialVariation.emissiveRandom;
        s["emissiveSparsity"] = materialVariation.emissiveSparsity;
        s["hueField"] = materialVariation.hueField;
        s["hueFieldScale"] = materialVariation.hueFieldScale;
        s["chromaDrift"] = materialVariation.chromaDrift;
        s["chromaDriftScale"] = materialVariation.chromaDriftScale;
        s["chromaDriftSpeed"] = materialVariation.chromaDriftSpeed;
        s["perceptualHue"] = materialVariation.perceptualHue;
        s["emissiveGradient"] = materialVariation.emissiveGradient;
        j["materialVariation"] = std::move(s);
    }
    {
        json s = json::object();
        s["recursionDepth"] = hierarchy.recursionDepth;
        s["maxInstances"] = hierarchy.maxInstances;
        s["scalePerLevel"] = hierarchy.scalePerLevel;
        s["offsetPerLevel"] = vecToJson(hierarchy.offsetPerLevel);
        s["rotationPerLevel"] = vecToJson(hierarchy.rotationPerLevelDegrees);
        s["colorPerLevel"] = hierarchy.colorPerLevel;
        j["hierarchy"] = std::move(s);
    }
    if (distribution.kind == DistributionKind::Grammar || !grammar.rules.empty()) {
        j["grammar"] = grammar.toJson();
    }
    return j;
}

Result<ProceduralGeometry> ProceduralGeometry::fromJson(const json& root) {
    if (!root.is_object()) {
        return fail("procedural geometry must be a JSON object");
    }
    ProceduralGeometry g;
    {
        const json& j = root;
        AVGEN_PROC_READ(g.name, "name", readString);
        AVGEN_PROC_READ(g.partOf, "partOf", readString);
        AVGEN_PROC_READ(g.visible, "visible", readBool);
        AVGEN_PROC_READ(g.sourceTransform, "sourceTransform", readTransform);
        AVGEN_PROC_READ(g.distributionTransform, "distributionTransform", readTransform);
        AVGEN_PROC_READ(g.emissiveField, "emissiveField", readString);
        AVGEN_PROC_READ(g.emissiveFieldAmount, "emissiveFieldAmount", readFloat);
        AVGEN_PROC_READ(g.extraLane, "extraLane", readString);
    }
    if (root.contains("source")) {
        const json& j = root.at("source");
        if (!j.is_object()) {
            return fail("'source' must be an object");
        }
        SourceSpec& s = g.source;
        if (auto ok = readEnum(j, "kind", s.kind, &primitiveKindFromName, "primitive kind"); !ok) {
            return std::unexpected(ok.error());
        }
        AVGEN_PROC_READ(s.size, "size", readVec3);
        AVGEN_PROC_READ(s.subdivisions, "subdivisions", readInt);
        AVGEN_PROC_READ(s.bevel, "bevel", readFloat);
        AVGEN_PROC_READ(s.asset, "asset", readString);
        AVGEN_PROC_READ(s.meshBudget, "meshBudget", readInt);
        AVGEN_PROC_READ(s.tubeRadius, "tubeRadius", readFloat);
        AVGEN_PROC_READ(s.tubeTaper, "tubeTaper", readFloat);
        AVGEN_PROC_READ(s.tubeSides, "tubeSides", readInt);
        AVGEN_PROC_READ(s.tubeSegments, "tubeSegments", readInt);
        AVGEN_PROC_READ(s.tubeTwist, "tubeTwist", readFloat);
        AVGEN_PROC_READ(s.tubeCaps, "tubeCaps", readBool);
        if (j.contains("curve")) {
            auto curve = spatial::Spline::fromJson(j.at("curve"));
            if (!curve) {
                return fail("tube curve: {}", curve.error().message);
            }
            s.curve = std::move(*curve);
        }
        AVGEN_PROC_READ(s.bevelSegments, "bevelSegments", readInt);
        AVGEN_PROC_READ(s.radius, "radius", readFloat);
        AVGEN_PROC_READ(s.height, "height", readFloat);
        AVGEN_PROC_READ(s.radialSegments, "radialSegments", readInt);
        AVGEN_PROC_READ(s.heightSegments, "heightSegments", readInt);
        AVGEN_PROC_READ(s.caps, "caps", readBool);
        AVGEN_PROC_READ(s.segments, "segments", readInt);
        AVGEN_PROC_READ(s.rings, "rings", readInt);
        AVGEN_PROC_READ(s.majorRadius, "majorRadius", readFloat);
        AVGEN_PROC_READ(s.minorRadius, "minorRadius", readFloat);
        AVGEN_PROC_READ(s.majorSegments, "majorSegments", readInt);
        AVGEN_PROC_READ(s.minorSegments, "minorSegments", readInt);
        AVGEN_PROC_READ(s.pointSize, "pointSize", readFloat);
        AVGEN_PROC_READ(s.reference, "reference", readString);
    }
    if (root.contains("distribution")) {
        const json& j = root.at("distribution");
        if (!j.is_object()) {
            return fail("'distribution' must be an object");
        }
        Distribution& d = g.distribution;
        if (auto ok = readEnum(j, "kind", d.kind, &distributionKindFromName, "distribution kind"); !ok) {
            return std::unexpected(ok.error());
        }
        AVGEN_PROC_READ(d.count, "count", readInt);
        AVGEN_PROC_READ(d.start, "start", readVec3);
        AVGEN_PROC_READ(d.end, "end", readVec3);
        AVGEN_PROC_READ(d.orientAlong, "orientAlong", readBool);
        AVGEN_PROC_READ(d.spacing, "spacing", readFloat);
        AVGEN_PROC_READ(d.gridCount, "gridCount", readIvec3);
        AVGEN_PROC_READ(d.gridSpacing, "gridSpacing", readVec3);
        AVGEN_PROC_READ(d.radius, "radius", readFloat);
        AVGEN_PROC_READ(d.startAngle, "startAngle", readFloat);
        AVGEN_PROC_READ(d.endAngle, "endAngle", readFloat);
        if (auto ok = readEnum(j, "plane", d.plane, &distributionPlaneFromName, "distribution plane"); !ok) {
            return std::unexpected(ok.error());
        }
        AVGEN_PROC_READ(d.center, "center", readVec3);
        if (auto ok = readEnum(j, "orientation", d.orientation, &orientationModeFromName, "orientation mode"); !ok) {
            return std::unexpected(ok.error());
        }
        AVGEN_PROC_READ(d.radiusGrowth, "radiusGrowth", readFloat);
        AVGEN_PROC_READ(d.turns, "turns", readFloat);
        AVGEN_PROC_READ(d.spiralHeight, "spiralHeight", readFloat);
        AVGEN_PROC_READ(d.spiralAngle, "spiralAngle", readFloat);
        AVGEN_PROC_READ(d.spline, "spline", readString);
        AVGEN_PROC_READ(d.splineStart, "splineStart", readFloat);
        AVGEN_PROC_READ(d.splineEnd, "splineEnd", readFloat);
        AVGEN_PROC_READ(d.alignToSpline, "alignToSpline", readBool);
        AVGEN_PROC_READ(d.roll, "roll", readFloat);
        AVGEN_PROC_READ(d.splineOffset, "splineOffset", readVec3);
    }
    if (root.contains("variation")) {
        const json& j = root.at("variation");
        if (!j.is_object()) {
            return fail("'variation' must be an object");
        }
        Variation& v = g.variation;
        AVGEN_PROC_READ(v.seed, "seed", readU32);
        AVGEN_PROC_READ(v.randomPosition, "position", readVec3);
        AVGEN_PROC_READ(v.randomRotation, "rotation", readVec3);
        AVGEN_PROC_READ(v.randomScale, "scale", readVec3);
        AVGEN_PROC_READ(v.randomUniformScale, "uniformScale", readFloat);
    }
    if (root.contains("deformers")) {
        const json& arr = root.at("deformers");
        if (!arr.is_array()) {
            return fail("'deformers' must be an array");
        }
        if (arr.size() > static_cast<std::size_t>(kMaxDeformers)) {
            return fail("at most {} deformers (got {})", kMaxDeformers, arr.size());
        }
        for (const json& j : arr) {
            if (!j.is_object()) {
                return fail("each deformer must be an object");
            }
            Deformer d;
            if (auto ok = readEnum(j, "kind", d.kind, &deformerKindFromName, "deformer kind"); !ok) {
                return std::unexpected(ok.error());
            }
            AVGEN_PROC_READ(d.enabled, "enabled", readBool);
            AVGEN_PROC_READ(d.amount, "amount", readFloat);
            if (auto ok = readEnum(j, "space", d.space, &deformSpaceFromName, "deformer space"); !ok) {
                return std::unexpected(ok.error());
            }
            AVGEN_PROC_READ(d.speed, "speed", readFloat);
            AVGEN_PROC_READ(d.phase, "phase", readFloat);
            AVGEN_PROC_READ(d.axis, "axis", readVec3);
            AVGEN_PROC_READ(d.center, "center", readVec3);
            AVGEN_PROC_READ(d.falloff, "falloff", readFloat);
            AVGEN_PROC_READ(d.frequency, "frequency", readFloat);
            AVGEN_PROC_READ(d.displacementAxis, "displacementAxis", readVec3);
            AVGEN_PROC_READ(d.scale, "scale", readFloat);
            AVGEN_PROC_READ(d.seed, "seed", readU32);
            AVGEN_PROC_READ(d.axisMask, "axisMask", readVec3);
            AVGEN_PROC_READ(d.pattern, "pattern", readInt);
            AVGEN_PROC_READ(d.field, "field", readString);
            AVGEN_PROC_READ(d.alongNormal, "alongNormal", readBool);
            AVGEN_PROC_READ(d.spline, "spline", readString);
            AVGEN_PROC_READ(d.pathOffset, "pathOffset", readFloat);
            AVGEN_PROC_READ(d.pathScale, "pathScale", readFloat);
            AVGEN_PROC_READ(d.pathRoll, "pathRoll", readFloat);
            g.deformers.push_back(d);
        }
    }
    if (root.contains("ops")) {
        const json& arr = root.at("ops");
        if (!arr.is_array()) {
            return fail("'ops' must be an array");
        }
        for (const json& j : arr) {
            auto op = spatial::pointOpFromJson(j);
            if (!op) {
                return fail("op {}: {}", g.pointOps.size() + 1, op.error().message);
            }
            g.pointOps.push_back(std::move(*op));
        }
    }
    if (root.contains("effectors")) {
        const json& arr = root.at("effectors");
        if (!arr.is_array()) {
            return fail("'effectors' must be an array");
        }
        if (arr.size() > static_cast<std::size_t>(kMaxEffectors)) {
            return fail("at most {} effectors (got {})", kMaxEffectors, arr.size());
        }
        for (const json& j : arr) {
            auto e = spatial::Effector::fromJson(j);
            if (!e) {
                return fail("effector {}: {}", g.effectors.size() + 1, e.error().message);
            }
            g.effectors.push_back(std::move(*e));
        }
    }
    if (root.contains("lod")) {
        const json& j = root.at("lod");
        if (!j.is_object()) {
            return fail("'lod' must be an object");
        }
        // An unrecognised key here is silently dropped, and that is how a scene comes to look
        // configured while doing nothing. Glowmere authored `"lodCount": 2` -- the field is called
        // `count` -- so its LOD ladder read as a two-rung ladder in the file and was a single rung
        // in the engine, for as long as anyone had been looking at that file. Name the key rather
        // than reject it: a scene written by a newer build must still load in an older one.
        static constexpr std::string_view kLodKeys[] = {
            "cull",        "maxDistance", "minScreenRadius", "count",     "distance1",
            "distance2",   "distance3",   "byScreenSize",    "spread",    "hysteresis",
            "impostorSize"};
        for (const auto& entry : j.items()) {
            const std::string& key = entry.key();
            if (std::find(std::begin(kLodKeys), std::end(kLodKeys), key) == std::end(kLodKeys)) {
                log::warn("procedural '{}': lod: unknown setting '{}' ignored", g.name, key);
            }
        }
        LodSettings& l = g.lod;
        AVGEN_PROC_READ(l.cull, "cull", readBool);
        AVGEN_PROC_READ(l.maxDistance, "maxDistance", readFloat);
        AVGEN_PROC_READ(l.minScreenRadius, "minScreenRadius", readFloat);
        AVGEN_PROC_READ(l.lodCount, "count", readInt);
        AVGEN_PROC_READ(l.lodDistances[0], "distance1", readFloat);
        AVGEN_PROC_READ(l.lodDistances[1], "distance2", readFloat);
        AVGEN_PROC_READ(l.lodDistances[2], "distance3", readFloat);
        AVGEN_PROC_READ(l.lodByScreenSize, "byScreenSize", readBool);
        AVGEN_PROC_READ(l.lodSpread, "spread", readFloat);
        AVGEN_PROC_READ(l.lodHysteresis, "hysteresis", readFloat);
        AVGEN_PROC_READ(l.impostorSize, "impostorSize", readFloat);
    }
    if (root.contains("material")) {
        const json& j = root.at("material");
        if (!j.is_object()) {
            return fail("'material' must be an object");
        }
        Material& m = g.material;
        AVGEN_PROC_READ(m.baseColor, "baseColor", readVec3);
        AVGEN_PROC_READ(m.opacity, "opacity", readFloat);
        AVGEN_PROC_READ(m.emissiveColor, "emissiveColor", readVec3);
        AVGEN_PROC_READ(m.emissiveIntensity, "emissiveIntensity", readFloat);
        AVGEN_PROC_READ(m.roughness, "roughness", readFloat);
        AVGEN_PROC_READ(m.metallic, "metallic", readFloat);
        AVGEN_PROC_READ(m.doubleSided, "doubleSided", readBool);
        AVGEN_PROC_READ(m.unlit, "unlit", readBool);
        AVGEN_PROC_READ(m.program, "program", readString);
    }
    if (root.contains("materialVariation")) {
        const json& j = root.at("materialVariation");
        if (!j.is_object()) {
            return fail("'materialVariation' must be an object");
        }
        MaterialVariation& v = g.materialVariation;
        AVGEN_PROC_READ(v.hueShift, "hueShift", readFloat);
        AVGEN_PROC_READ(v.hueGradient, "hueGradient", readFloat);
        AVGEN_PROC_READ(v.valueRandom, "valueRandom", readFloat);
        AVGEN_PROC_READ(v.emissiveRandom, "emissiveRandom", readFloat);
        AVGEN_PROC_READ(v.emissiveSparsity, "emissiveSparsity", readFloat);
        AVGEN_PROC_READ(v.hueField, "hueField", readFloat);
        AVGEN_PROC_READ(v.hueFieldScale, "hueFieldScale", readFloat);
        AVGEN_PROC_READ(v.chromaDrift, "chromaDrift", readFloat);
        AVGEN_PROC_READ(v.chromaDriftScale, "chromaDriftScale", readFloat);
        AVGEN_PROC_READ(v.chromaDriftSpeed, "chromaDriftSpeed", readFloat);
        AVGEN_PROC_READ(v.perceptualHue, "perceptualHue", readBool);
        AVGEN_PROC_READ(v.emissiveGradient, "emissiveGradient", readFloat);
    }
    if (root.contains("hierarchy")) {
        const json& j = root.at("hierarchy");
        if (!j.is_object()) {
            return fail("'hierarchy' must be an object");
        }
        HierarchySpec& h = g.hierarchy;
        AVGEN_PROC_READ(h.recursionDepth, "recursionDepth", readInt);
        AVGEN_PROC_READ(h.maxInstances, "maxInstances", readInt);
        AVGEN_PROC_READ(h.scalePerLevel, "scalePerLevel", readFloat);
        AVGEN_PROC_READ(h.offsetPerLevel, "offsetPerLevel", readVec3);
        AVGEN_PROC_READ(h.rotationPerLevelDegrees, "rotationPerLevel", readVec3);
        AVGEN_PROC_READ(h.colorPerLevel, "colorPerLevel", readBool);
    }
    if (root.contains("grammar")) {
        auto grammar = Grammar::fromJson(root.at("grammar"));
        if (!grammar) {
            return fail("grammar: {}", grammar.error().message);
        }
        g.grammar = std::move(*grammar);
    }
    if (auto ok = g.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return g;
}

#undef AVGEN_PROC_READ

// ================================================================================================
// Parameters
// ================================================================================================

namespace {

struct Registrar {
    params::ParameterSet& params;
    ProceduralParameters& out;
    std::string group;

    template <typename T>
    params::Parameter<T>* add(params::ParamDesc<T> d, const char* rel) {
        d.path = out.prefix + rel;
        d.label = rel;
        d.group = group;
        auto& p = params.add(std::move(d));
        out.all.push_back(&p);
        return &p;
    }
    params::Parameter<float>* f(const char* rel, float def, float lo, float hi, float slo, float shi) {
        params::ParamDesc<float> d;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        return add(std::move(d), rel);
    }
    params::Parameter<int>* i(const char* rel, int def, int lo, int hi, int slo, int shi) {
        params::ParamDesc<int> d;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        return add(std::move(d), rel);
    }
    params::Parameter<bool>* b(const char* rel, bool def) {
        params::ParamDesc<bool> d;
        d.defaultValue = def;
        d.hardMin = false;
        d.hardMax = true;
        return add(std::move(d), rel);
    }
    params::Parameter<glm::vec3>* v3(const char* rel, glm::vec3 def, float lo, float hi, float slo, float shi,
                                     bool isColor = false) {
        params::ParamDesc<glm::vec3> d;
        d.defaultValue = def;
        d.hardMin = glm::vec3(lo);
        d.hardMax = glm::vec3(hi);
        d.softMin = glm::vec3(slo);
        d.softMax = glm::vec3(shi);
        d.isColor = isColor;
        return add(std::move(d), rel);
    }
};

// Finds a registered parameter by its path relative to the prefix (linear scan; ~80 entries).
template <typename T>
params::Parameter<T>* findRel(const ProceduralParameters& p, const std::string& rel) {
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == p.prefix.size() + rel.size() && path.compare(p.prefix.size(), rel.size(), rel) == 0) {
            return dynamic_cast<params::Parameter<T>*>(ip);
        }
    }
    return nullptr;
}

template <typename T>
void copyValue(const ProceduralParameters& p, const std::string& rel, T& target) {
    if (auto* param = findRel<T>(p, rel)) {
        target = param->value();
    }
}

template <typename Enum>
void copyEnum(const ProceduralParameters& p, const std::string& rel, Enum& target, int maxValue) {
    if (auto* param = findRel<int>(p, rel)) {
        target = static_cast<Enum>(std::clamp(param->value(), 0, maxValue));
    }
}

void copyTransform(const ProceduralParameters& p, const std::string& base, Transform& t) {
    copyValue(p, base + "position", t.position);
    if (auto* rot = findRel<glm::vec3>(p, base + "rotation")) {
        t.rotation = quatFromEulerDegrees(rot->value());
    }
    copyValue(p, base + "scale", t.scale);
}

} // namespace

ProceduralParameters registerProceduralParameters(params::ParameterSet& params, const ProceduralGeometry& rest,
                                                  const std::string& prefix) {
    ProceduralParameters p;
    p.prefix = prefix;
    std::string group = prefix;
    while (!group.empty() && group.back() == '/') {
        group.pop_back();
    }
    Registrar r{params, p, group};
    constexpr float kPi = 3.14159265f;

    // Source
    const SourceSpec& s = rest.source;
    r.i("source/kind", static_cast<int>(s.kind), 0, 7, 0, 7);
    p.sourceSize = r.v3("source/size", s.size, 0.001f, 1000.0f, 0.01f, 10.0f);
    r.i("source/subdivisions", s.subdivisions, 1, 64, 1, 16);
    r.f("source/bevel", s.bevel, 0.0f, 1e3f, 0.0f, 1.0f);
    r.i("source/bevelSegments", s.bevelSegments, 1, 16, 1, 8);
    p.sourceRadius = r.f("source/radius", s.radius, 0.001f, 1000.0f, 0.01f, 10.0f);
    p.sourceHeight = r.f("source/height", s.height, 0.001f, 1000.0f, 0.01f, 20.0f);
    r.i("source/radialSegments", s.radialSegments, 3, 256, 3, 64);
    r.i("source/heightSegments", s.heightSegments, 1, 128, 1, 32);
    r.b("source/caps", s.caps);
    r.i("source/segments", s.segments, 3, 256, 3, 64);
    r.i("source/rings", s.rings, 2, 128, 2, 32);
    r.f("source/majorRadius", s.majorRadius, 0.001f, 1000.0f, 0.01f, 10.0f);
    r.f("source/minorRadius", s.minorRadius, 0.001f, 1000.0f, 0.01f, 5.0f);
    r.i("source/majorSegments", s.majorSegments, 3, 256, 3, 96);
    r.i("source/minorSegments", s.minorSegments, 3, 128, 3, 32);
    r.f("source/pointSize", s.pointSize, 0.0001f, 100.0f, 0.001f, 1.0f);
    r.v3("source/position", rest.sourceTransform.position, -1e4f, 1e4f, -10.0f, 10.0f);
    r.v3("source/rotation", eulerDegrees(rest.sourceTransform.rotation), -360.0f, 360.0f, -360.0f, 360.0f);
    p.sourceScale = r.v3("source/scale", rest.sourceTransform.scale, 0.001f, 100.0f, 0.01f, 5.0f);

    // Distribution
    const Distribution& d = rest.distribution;
    r.i("distribution/kind", static_cast<int>(d.kind), 0, static_cast<int>(DistributionKind::Scatter), 0,
        static_cast<int>(DistributionKind::Scatter));
    p.distributionCount = r.i("distribution/count", d.count, 1, kMaxInstances, 1, 256);
    r.v3("distribution/start", d.start, -1e4f, 1e4f, -20.0f, 20.0f);
    r.v3("distribution/end", d.end, -1e4f, 1e4f, -20.0f, 20.0f);
    r.b("distribution/orientAlong", d.orientAlong);
    r.f("distribution/spacing", d.spacing, 0.0f, 1000.0f, 0.0f, 10.0f);
    r.i("distribution/gridCountX", d.gridCount.x, 1, 1000, 1, 32);
    r.i("distribution/gridCountY", d.gridCount.y, 1, 1000, 1, 32);
    r.i("distribution/gridCountZ", d.gridCount.z, 1, 1000, 1, 32);
    r.v3("distribution/gridSpacing", d.gridSpacing, -1000.0f, 1000.0f, 0.0f, 10.0f);
    p.distributionRadius = r.f("distribution/radius", d.radius, 0.0f, 1000.0f, 0.0f, 30.0f);
    r.f("distribution/startAngle", d.startAngle, -100.0f, 100.0f, -kTwoPi, kTwoPi);
    r.f("distribution/endAngle", d.endAngle, -100.0f, 100.0f, -kTwoPi, kTwoPi);
    r.i("distribution/plane", static_cast<int>(d.plane), 0, 2, 0, 2);
    r.v3("distribution/center", d.center, -1e4f, 1e4f, -20.0f, 20.0f);
    r.i("distribution/orientation", static_cast<int>(d.orientation), 0, 3, 0, 3);
    r.f("distribution/radiusGrowth", d.radiusGrowth, -1000.0f, 1000.0f, -20.0f, 20.0f);
    r.f("distribution/turns", d.turns, -100.0f, 100.0f, 0.0f, 10.0f);
    r.f("distribution/spiralHeight", d.spiralHeight, -1000.0f, 1000.0f, -20.0f, 20.0f);
    r.f("distribution/spiralAngle", d.spiralAngle, -100.0f, 100.0f, -kTwoPi, kTwoPi);
    // Spline (ADR-026): the spline name is structural and comes from the file.
    p.splineStart = r.f("distribution/splineStart", d.splineStart, -100.0f, 100.0f, 0.0f, 1.0f);
    p.splineEnd = r.f("distribution/splineEnd", d.splineEnd, -100.0f, 100.0f, 0.0f, 1.0f);
    r.b("distribution/alignToSpline", d.alignToSpline);
    r.f("distribution/roll", d.roll, -100.0f, 100.0f, -kTwoPi, kTwoPi);
    r.v3("distribution/splineOffset", d.splineOffset, -1e4f, 1e4f, -10.0f, 10.0f);

    // Distribution transform
    p.transformPosition = r.v3("transform/position", rest.distributionTransform.position, -1e4f, 1e4f, -20.0f, 20.0f);
    p.transformRotation =
        r.v3("transform/rotation", eulerDegrees(rest.distributionTransform.rotation), -360.0f, 360.0f, -360.0f, 360.0f);
    p.transformScale = r.v3("transform/scale", rest.distributionTransform.scale, 0.001f, 100.0f, 0.01f, 5.0f);

    // Variation
    const Variation& v = rest.variation;
    // Int parameters travel through float components, so seeds stay exact up to 2^24.
    p.seed = r.i("variation/seed", static_cast<int>(std::min<std::uint32_t>(v.seed, 16777216u)), 0, 16777216, 0, 1000);
    r.v3("variation/position", v.randomPosition, 0.0f, 1000.0f, 0.0f, 2.0f);
    r.v3("variation/rotation", v.randomRotation, 0.0f, kPi, 0.0f, kPi);
    r.v3("variation/scale", v.randomScale, 0.0f, 1.0f, 0.0f, 1.0f);
    r.f("variation/uniformScale", v.randomUniformScale, 0.0f, 1.0f, 0.0f, 1.0f);

    // Hierarchy (ADR-029; structural: every change rebuilds). source/reference is JSON only.
    const HierarchySpec& hs = rest.hierarchy;
    p.recursionDepth = r.i("hierarchy/depth", hs.recursionDepth, 0, kMaxHierarchyDepth, 0, kMaxHierarchyDepth);
    p.scalePerLevel = r.f("hierarchy/scalePerLevel", hs.scalePerLevel, 0.001f, 100.0f, 0.05f, 2.0f);
    r.v3("hierarchy/offsetPerLevel", hs.offsetPerLevel, -1e4f, 1e4f, -10.0f, 10.0f);
    r.v3("hierarchy/rotationPerLevel", hs.rotationPerLevelDegrees, -360.0f, 360.0f, -180.0f, 180.0f);

    // Deformers (existing slots only; kind and space are structural and come from the file)
    const std::size_t slots = std::min(rest.deformers.size(), static_cast<std::size_t>(kMaxDeformers));
    for (std::size_t slot = 0; slot < slots; ++slot) {
        const Deformer& def = rest.deformers[slot];
        const std::string base = "deform/" + std::to_string(slot + 1) + "/";
        const std::string kindName = deformerKindName(def.kind);
        const auto addF = [&](const char* field, float value, float lo, float hi, float slo, float shi) {
            params::ParamDesc<float> desc;
            desc.defaultValue = value;
            desc.hardMin = lo;
            desc.hardMax = hi;
            desc.softMin = slo;
            desc.softMax = shi;
            desc.path = prefix + base + field;
            desc.label = kindName + "/" + field;
            desc.group = group;
            auto& param = params.add(std::move(desc));
            p.all.push_back(&param);
            return &param;
        };
        const auto addV3 = [&](const char* field, glm::vec3 value, float lo, float hi, float slo, float shi) {
            params::ParamDesc<glm::vec3> desc;
            desc.defaultValue = value;
            desc.hardMin = glm::vec3(lo);
            desc.hardMax = glm::vec3(hi);
            desc.softMin = glm::vec3(slo);
            desc.softMax = glm::vec3(shi);
            desc.path = prefix + base + field;
            desc.label = kindName + "/" + field;
            desc.group = group;
            auto& param = params.add(std::move(desc));
            p.all.push_back(&param);
            return &param;
        };
        p.deformerAmount[slot] = addF("amount", def.amount, -100.0f, 100.0f, -3.0f, 3.0f);
        p.deformerSpeed[slot] = addF("speed", def.speed, -100.0f, 100.0f, -5.0f, 5.0f);
        addF("phase", def.phase, -100.0f, 100.0f, -kTwoPi, kTwoPi);
        addF("frequency", def.frequency, 0.0f, 100.0f, 0.0f, 10.0f);
        addF("scale", def.scale, 0.001f, 100.0f, 0.05f, 5.0f);
        addF("falloff", def.falloff, 0.0f, 1000.0f, 0.0f, 10.0f);
        addV3("center", def.center, -1e4f, 1e4f, -10.0f, 10.0f);
        addV3("axis", def.axis, -1.0f, 1.0f, -1.0f, 1.0f);
        if (def.kind == DeformerKind::Path) {
            addF("pathOffset", def.pathOffset, -1e4f, 1e4f, -20.0f, 20.0f);
            addF("pathScale", def.pathScale, -1000.0f, 1000.0f, 0.0f, 5.0f);
            addF("pathRoll", def.pathRoll, -100.0f, 100.0f, -kTwoPi, kTwoPi);
        }
        {
            params::ParamDesc<bool> desc;
            desc.defaultValue = def.enabled;
            desc.hardMin = false;
            desc.hardMax = true;
            desc.path = prefix + base + "enabled";
            desc.label = kindName + "/enabled";
            desc.group = group;
            p.all.push_back(&params.add(std::move(desc)));
        }
    }

    // Point ops (structural: a change rebuilds the cloud) and effectors (per frame).
    const auto addBool = [&](const std::string& rel, const std::string& label, bool value) {
        params::ParamDesc<bool> desc;
        desc.defaultValue = value;
        desc.hardMin = false;
        desc.hardMax = true;
        desc.path = prefix + rel;
        desc.label = label;
        desc.group = group;
        auto& param = params.add(std::move(desc));
        p.all.push_back(&param);
        return &param;
    };
    const auto addFloat = [&](const std::string& rel, const std::string& label, float value, float lo, float hi, float slo,
                              float shi) {
        params::ParamDesc<float> desc;
        desc.defaultValue = value;
        desc.hardMin = lo;
        desc.hardMax = hi;
        desc.softMin = slo;
        desc.softMax = shi;
        desc.path = prefix + rel;
        desc.label = label;
        desc.group = group;
        auto& param = params.add(std::move(desc));
        p.all.push_back(&param);
        return &param;
    };
    for (std::size_t slot = 0; slot < rest.pointOps.size(); ++slot) {
        const spatial::PointOp& op = rest.pointOps[slot];
        const std::string base = "ops/" + std::to_string(slot + 1) + "/";
        const std::string kindName = spatial::pointOpKindName(op.kind);
        p.opAmount.push_back(addFloat(base + "amount", kindName + "/amount", op.amount, -100.0f, 100.0f, -2.0f, 2.0f));
        addBool(base + "enabled", kindName + "/enabled", op.enabled);
    }
    const std::size_t effectorSlots = std::min(rest.effectors.size(), static_cast<std::size_t>(kMaxEffectors));
    for (std::size_t slot = 0; slot < effectorSlots; ++slot) {
        const spatial::Effector& e = rest.effectors[slot];
        const std::string base = "effector/" + std::to_string(slot + 1) + "/";
        const std::string opName = spatial::effectorOpName(e.op);
        p.effectorStrength[slot] = addFloat(base + "strength", opName + "/strength", e.strength, -100.0f, 100.0f, -5.0f, 5.0f);
        addFloat(base + "weight", opName + "/weight", e.weight, 0.0f, 1.0f, 0.0f, 1.0f);
        addBool(base + "enabled", opName + "/enabled", e.enabled);
    }
    p.emissiveFieldAmount = r.f("emissiveFieldAmount", rest.emissiveFieldAmount, 0.0f, 100.0f, 0.0f, 10.0f);

    // Culling / LOD (ADR-029): per-frame uniforms, so no rebuild when they move. `lodCount`,
    // `lodByScreenSize` and `impostorSize` are structural or mesh-shaping and stay file-authored.
    const LodSettings& lodRest = rest.lod;
    p.lodEnabled = r.b("lod/enabled", lodRest.cull);
    p.lodMaxDistance = r.f("lod/maxDistance", lodRest.maxDistance, 0.0f, 100000.0f, 0.0f, 500.0f);
    p.lodMinScreenRadius = r.f("lod/minScreenRadius", lodRest.minScreenRadius, 0.0f, 4096.0f, 0.0f, 32.0f);
    p.lodDistance[0] = r.f("lod/distance1", lodRest.lodDistances[0], 0.0f, 100000.0f, 0.0f, 500.0f);
    p.lodDistance[1] = r.f("lod/distance2", lodRest.lodDistances[1], 0.0f, 100000.0f, 0.0f, 500.0f);
    p.lodDistance[2] = r.f("lod/distance3", lodRest.lodDistances[2], 0.0f, 100000.0f, 0.0f, 500.0f);
    // Both capped at 0.5: beyond that the band is wider than the gap between adjacent thresholds
    // and a level could never be reached at all.
    p.lodSpread = r.f("lod/spread", lodRest.lodSpread, 0.0f, 0.5f, 0.0f, 0.3f);
    p.lodHysteresis = r.f("lod/hysteresis", lodRest.lodHysteresis, 0.0f, 0.5f, 0.0f, 0.3f);

    // Material
    const Material& m = rest.material;
    p.baseColor = r.v3("material/baseColor", m.baseColor, 0.0f, 1.0f, 0.0f, 1.0f, true);
    p.emissiveColor = r.v3("material/emissiveColor", m.emissiveColor, 0.0f, 1.0f, 0.0f, 1.0f, true);
    p.emissive = r.f("material/emissive", m.emissiveIntensity, 0.0f, 50.0f, 0.0f, 8.0f);
    p.roughness = r.f("material/roughness", m.roughness, 0.0f, 1.0f, 0.0f, 1.0f);
    p.metallic = r.f("material/metallic", m.metallic, 0.0f, 1.0f, 0.0f, 1.0f);

    // Material variation
    const MaterialVariation& mv = rest.materialVariation;
    p.hueShift = r.f("materialVariation/hueShift", mv.hueShift, 0.0f, 1.0f, 0.0f, 1.0f);
    r.f("materialVariation/hueGradient", mv.hueGradient, -4.0f, 4.0f, -1.0f, 1.0f);
    r.f("materialVariation/valueRandom", mv.valueRandom, 0.0f, 1.0f, 0.0f, 1.0f);
    r.f("materialVariation/emissiveRandom", mv.emissiveRandom, 0.0f, 1.0f, 0.0f, 1.0f);
    r.f("materialVariation/emissiveGradient", mv.emissiveGradient, 0.0f, 50.0f, 0.0f, 4.0f);

    p.visible = r.b("visible", rest.visible);
    return p;
}

bool applyProceduralParameters(const ProceduralParameters& p, const ProceduralGeometry& rest, ProceduralGeometry& live) {
    if (p.all.empty()) {
        return false;
    }
    // Source
    SourceSpec& s = live.source;
    copyEnum(p, "source/kind", s.kind, 7);
    copyValue(p, "source/size", s.size);
    copyValue(p, "source/subdivisions", s.subdivisions);
    copyValue(p, "source/bevel", s.bevel);
    copyValue(p, "source/bevelSegments", s.bevelSegments);
    copyValue(p, "source/radius", s.radius);
    copyValue(p, "source/height", s.height);
    copyValue(p, "source/radialSegments", s.radialSegments);
    copyValue(p, "source/heightSegments", s.heightSegments);
    copyValue(p, "source/caps", s.caps);
    copyValue(p, "source/segments", s.segments);
    copyValue(p, "source/rings", s.rings);
    copyValue(p, "source/majorRadius", s.majorRadius);
    copyValue(p, "source/minorRadius", s.minorRadius);
    copyValue(p, "source/majorSegments", s.majorSegments);
    copyValue(p, "source/minorSegments", s.minorSegments);
    copyValue(p, "source/pointSize", s.pointSize);
    copyTransform(p, "source/", live.sourceTransform);

    // Distribution
    Distribution& d = live.distribution;
    copyEnum(p, "distribution/kind", d.kind, static_cast<int>(DistributionKind::Scatter));
    copyValue(p, "distribution/count", d.count);
    copyValue(p, "distribution/start", d.start);
    copyValue(p, "distribution/end", d.end);
    copyValue(p, "distribution/orientAlong", d.orientAlong);
    copyValue(p, "distribution/spacing", d.spacing);
    copyValue(p, "distribution/gridCountX", d.gridCount.x);
    copyValue(p, "distribution/gridCountY", d.gridCount.y);
    copyValue(p, "distribution/gridCountZ", d.gridCount.z);
    copyValue(p, "distribution/gridSpacing", d.gridSpacing);
    copyValue(p, "distribution/radius", d.radius);
    copyValue(p, "distribution/startAngle", d.startAngle);
    copyValue(p, "distribution/endAngle", d.endAngle);
    copyEnum(p, "distribution/plane", d.plane, 2);
    copyValue(p, "distribution/center", d.center);
    copyEnum(p, "distribution/orientation", d.orientation, 3);
    copyValue(p, "distribution/radiusGrowth", d.radiusGrowth);
    copyValue(p, "distribution/turns", d.turns);
    copyValue(p, "distribution/spiralHeight", d.spiralHeight);
    copyValue(p, "distribution/spiralAngle", d.spiralAngle);
    d.spline = rest.distribution.spline;
    copyValue(p, "distribution/splineStart", d.splineStart);
    copyValue(p, "distribution/splineEnd", d.splineEnd);
    copyValue(p, "distribution/alignToSpline", d.alignToSpline);
    copyValue(p, "distribution/roll", d.roll);
    copyValue(p, "distribution/splineOffset", d.splineOffset);
    copyTransform(p, "transform/", live.distributionTransform);

    // Variation
    Variation& v = live.variation;
    if (auto* seed = findRel<int>(p, "variation/seed")) {
        v.seed = static_cast<std::uint32_t>(std::max(seed->value(), 0));
    }
    copyValue(p, "variation/position", v.randomPosition);
    copyValue(p, "variation/rotation", v.randomRotation);
    copyValue(p, "variation/scale", v.randomScale);
    copyValue(p, "variation/uniformScale", v.randomUniformScale);

    // Hierarchy
    HierarchySpec& hs = live.hierarchy;
    copyValue(p, "hierarchy/depth", hs.recursionDepth);
    copyValue(p, "hierarchy/scalePerLevel", hs.scalePerLevel);
    copyValue(p, "hierarchy/offsetPerLevel", hs.offsetPerLevel);
    copyValue(p, "hierarchy/rotationPerLevel", hs.rotationPerLevelDegrees);

    // Deformers: the stack shape comes from rest; per-slot fields from the parameters.
    if (live.deformers.size() != rest.deformers.size()) {
        live.deformers = rest.deformers;
    }
    for (std::size_t slot = 0; slot < live.deformers.size(); ++slot) {
        Deformer& def = live.deformers[slot];
        def.kind = rest.deformers[slot].kind;
        def.space = rest.deformers[slot].space;
        const std::string base = "deform/" + std::to_string(slot + 1) + "/";
        copyValue(p, base + "amount", def.amount);
        copyValue(p, base + "speed", def.speed);
        copyValue(p, base + "phase", def.phase);
        copyValue(p, base + "frequency", def.frequency);
        copyValue(p, base + "scale", def.scale);
        copyValue(p, base + "falloff", def.falloff);
        copyValue(p, base + "center", def.center);
        copyValue(p, base + "axis", def.axis);
        copyValue(p, base + "enabled", def.enabled);
        def.spline = rest.deformers[slot].spline;
        copyValue(p, base + "pathOffset", def.pathOffset);
        copyValue(p, base + "pathScale", def.pathScale);
        copyValue(p, base + "pathRoll", def.pathRoll);
    }

    // Point ops and effectors: the list shapes come from rest; amount/enabled/strength/weight from
    // the parameters.
    if (live.pointOps.size() != rest.pointOps.size()) {
        live.pointOps = rest.pointOps;
    }
    for (std::size_t slot = 0; slot < live.pointOps.size(); ++slot) {
        const std::string base = "ops/" + std::to_string(slot + 1) + "/";
        live.pointOps[slot].kind = rest.pointOps[slot].kind;
        copyValue(p, base + "amount", live.pointOps[slot].amount);
        copyValue(p, base + "enabled", live.pointOps[slot].enabled);
    }
    if (live.effectors.size() != rest.effectors.size()) {
        live.effectors = rest.effectors;
    }
    for (std::size_t slot = 0; slot < live.effectors.size(); ++slot) {
        const std::string base = "effector/" + std::to_string(slot + 1) + "/";
        live.effectors[slot].op = rest.effectors[slot].op;
        live.effectors[slot].field = rest.effectors[slot].field;
        copyValue(p, base + "strength", live.effectors[slot].strength);
        copyValue(p, base + "weight", live.effectors[slot].weight);
        copyValue(p, base + "enabled", live.effectors[slot].enabled);
    }
    copyValue(p, "emissiveFieldAmount", live.emissiveFieldAmount);

    // Culling / LOD: the structural / mesh-shaping fields always come from rest.
    live.lod.lodCount = rest.lod.lodCount;
    live.lod.lodByScreenSize = rest.lod.lodByScreenSize;
    live.lod.impostorSize = rest.lod.impostorSize;
    copyValue(p, "lod/enabled", live.lod.cull);
    copyValue(p, "lod/maxDistance", live.lod.maxDistance);
    copyValue(p, "lod/minScreenRadius", live.lod.minScreenRadius);
    copyValue(p, "lod/spread", live.lod.lodSpread);
    copyValue(p, "lod/hysteresis", live.lod.lodHysteresis);
    copyValue(p, "lod/distance1", live.lod.lodDistances[0]);
    copyValue(p, "lod/distance2", live.lod.lodDistances[1]);
    copyValue(p, "lod/distance3", live.lod.lodDistances[2]);

    // Material
    Material& m = live.material;
    copyValue(p, "material/baseColor", m.baseColor);
    copyValue(p, "material/emissiveColor", m.emissiveColor);
    copyValue(p, "material/emissive", m.emissiveIntensity);
    copyValue(p, "material/roughness", m.roughness);
    copyValue(p, "material/metallic", m.metallic);

    MaterialVariation& mv = live.materialVariation;
    copyValue(p, "materialVariation/hueShift", mv.hueShift);
    copyValue(p, "materialVariation/hueGradient", mv.hueGradient);
    copyValue(p, "materialVariation/valueRandom", mv.valueRandom);
    copyValue(p, "materialVariation/emissiveRandom", mv.emissiveRandom);
    copyValue(p, "materialVariation/emissiveGradient", mv.emissiveGradient);

    copyValue(p, "visible", live.visible);
    return live.rebuild();
}

void unregisterProceduralParameters(params::ParameterSet& params, const ProceduralParameters& p) {
    for (params::IParameter* ip : p.all) {
        if (ip != nullptr) {
            const std::string path = ip->path(); // remove() destroys the parameter that owns it
            params.remove(path);
        }
    }
}

} // namespace avgen::scene

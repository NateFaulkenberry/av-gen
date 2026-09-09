#include "spatial/spline.hpp"

// Implementation notes (ADR-026)
//
// * Control points: `controlPoints()` returns `points` for the Points generator and otherwise
//   produces `count` points from the generator. Generated points get tangents by central
//   differences (Catmull-Rom style, wrapped when closed, one-sided at open ends; divided by 3 for
//   the Bezier kind so the handle offset reproduces the same derivative), roll 0 and scale 1, so
//   every kind produces a smooth curve from a generator. Explicit points are used verbatim.
// * Noise: the Noise generator is a Line jittered by fbm3Vec(position * noiseScale, seed) *
//   noiseAmount; every generator (Points included) applies the same jitter when noiseAmount > 0.
// * Segments: n control points give n - 1 segments (open) or n (closed). The uniform parameter
//   t in [0, 1] maps to (segment + u) / segments.
// * Frames: the dense table (samplesPerSegment per segment, 4..256, plus the end sample) carries
//   rotation-minimising frames computed by the double-reflection method (Wang et al. 2008). The
//   first normal is `up` projected perpendicular to the first tangent. Closed splines get a linear
//   roll correction so the transported frame meets the start frame at the seam; the interpolated
//   per-point `roll` is applied afterwards. Basis: x = binormal, y = normal, z = tangent, with
//   binormal = cross(normal, tangent) (right-handed).
// * Hashing: FNV-1a over the bit patterns of every member (name included).

#include "core/noise.hpp"

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <utility>

namespace avgen::spatial {

using nlohmann::json;

namespace {

constexpr float kTwoPi = 6.283185307179586f;
constexpr int kMinSamplesPerSegment = 4;
constexpr int kMaxSamplesPerSegment = 256;
constexpr float kEpsilon = 1e-8f;

// ---- structural hashing (FNV-1a over the bit patterns) -----------------------------------------

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
    void text(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        for (const char c : s) {
            u32(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
        }
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

// ---- small vector helpers ------------------------------------------------------------------------

glm::vec3 unitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > kEpsilon ? v / len : fallback;
}

bool finite(float v) {
    return std::isfinite(v);
}

bool finite(const glm::vec3& v) {
    return finite(v.x) && finite(v.y) && finite(v.z);
}

// Unit vector perpendicular to `axis` (unit); picks a reference axis that is not parallel.
glm::vec3 anyPerpendicular(const glm::vec3& axis) {
    const glm::vec3 ref = std::abs(axis.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::cross(axis, ref));
}

// `reference` projected perpendicular to the unit `tangent`, normalised; a stable perpendicular
// when the two are (anti)parallel or the reference is zero.
glm::vec3 perpendicularTo(const glm::vec3& tangent, const glm::vec3& reference) {
    const glm::vec3 projected = reference - tangent * glm::dot(reference, tangent);
    const float len = glm::length(projected);
    if (len > 1e-5f) {
        return projected / len;
    }
    return anyPerpendicular(tangent);
}

// Rotation of `v` (perpendicular to the unit `axis`) by `angle` about `axis` (right-handed).
glm::vec3 rotateAbout(const glm::vec3& v, const glm::vec3& axis, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return v * c + glm::cross(axis, v) * s + axis * (glm::dot(axis, v) * (1.0f - c));
}

// Orthonormal basis for the circle-like generators: u, v span the plane perpendicular to `axis`
// with u = +X projected perpendicular to the axis (+Y when the axis is parallel to X) and
// v = axis x u. This reproduces the procedural radial distribution's plane frames exactly:
// axis +Y -> (u, v) = (+X, -Z) [XZ plane], +Z -> (+X, +Y) [XY], +X -> (+Y, +Z) [YZ]; the angle
// increases right-handedly about the axis.
struct PlaneBasis {
    glm::vec3 axis;
    glm::vec3 u;
    glm::vec3 v;
};

PlaneBasis planeBasis(const glm::vec3& axisIn) {
    PlaneBasis b{};
    b.axis = unitOr(axisIn, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 ref = std::abs(b.axis.x) < 0.999f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    b.u = perpendicularTo(b.axis, ref);
    b.v = glm::cross(b.axis, b.u);
    return b;
}

// ---- per-segment evaluation ---------------------------------------------------------------------

struct SegmentEval {
    glm::vec3 position;
    glm::vec3 derivative; // d position / d u (u in [0, 1] within the segment)
    float roll;
    float scale;
};

// Cubic Hermite with tangents m0 (at p0) and m1 (at p1).
void hermite(const glm::vec3& p0, const glm::vec3& m0, const glm::vec3& p1, const glm::vec3& m1, float u,
             glm::vec3& position, glm::vec3& derivative) {
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h10 = u3 - 2.0f * u2 + u;
    const float h01 = -2.0f * u3 + 3.0f * u2;
    const float h11 = u3 - u2;
    position = p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
    const float d00 = 6.0f * u2 - 6.0f * u;
    const float d10 = 3.0f * u2 - 4.0f * u + 1.0f;
    const float d01 = -6.0f * u2 + 6.0f * u;
    const float d11 = 3.0f * u2 - 2.0f * u;
    derivative = p0 * d00 + m0 * d10 + p1 * d01 + m1 * d11;
}

// Cubic Bezier p0, c1, c2, p1.
void bezier(const glm::vec3& p0, const glm::vec3& c1, const glm::vec3& c2, const glm::vec3& p1, float u,
            glm::vec3& position, glm::vec3& derivative) {
    const float w = 1.0f - u;
    const float w2 = w * w;
    const float u2 = u * u;
    position = p0 * (w2 * w) + c1 * (3.0f * w2 * u) + c2 * (3.0f * w * u2) + p1 * (u2 * u);
    derivative = (c1 - p0) * (3.0f * w2) + (c2 - c1) * (6.0f * w * u) + (p1 - c2) * (3.0f * u2);
}

// Index of control point `i` for a spline with `n` points: wrapped when closed, clamped otherwise.
int neighbourIndex(int i, int n, bool closed) {
    if (closed) {
        return ((i % n) + n) % n;
    }
    return std::clamp(i, 0, n - 1);
}

SegmentEval evaluateSegment(const Spline& spline, const std::vector<SplinePoint>& controls, int segment,
                            float u) {
    const int n = static_cast<int>(controls.size());
    const SplinePoint& a = controls[static_cast<std::size_t>(neighbourIndex(segment, n, spline.closed))];
    const SplinePoint& b = controls[static_cast<std::size_t>(neighbourIndex(segment + 1, n, spline.closed))];
    SegmentEval e{};
    e.roll = a.roll + (b.roll - a.roll) * u;
    e.scale = a.scale + (b.scale - a.scale) * u;
    switch (spline.kind) {
    case SplineKind::Polyline:
        e.position = a.position + (b.position - a.position) * u;
        e.derivative = b.position - a.position;
        break;
    case SplineKind::CatmullRom: {
        const SplinePoint& prev = controls[static_cast<std::size_t>(neighbourIndex(segment - 1, n, spline.closed))];
        const SplinePoint& next = controls[static_cast<std::size_t>(neighbourIndex(segment + 2, n, spline.closed))];
        const glm::vec3 m0 = (b.position - prev.position) * spline.tension;
        const glm::vec3 m1 = (next.position - a.position) * spline.tension;
        hermite(a.position, m0, b.position, m1, u, e.position, e.derivative);
        break;
    }
    case SplineKind::Bezier:
        bezier(a.position, a.position + a.tangent, b.position - b.tangent, b.position, u, e.position, e.derivative);
        break;
    case SplineKind::Hermite:
        hermite(a.position, a.tangent, b.position, b.tangent, u, e.position, e.derivative);
        break;
    }
    return e;
}

int segmentCountOf(std::size_t controls, bool closed) {
    if (controls < 2) {
        return 0;
    }
    return static_cast<int>(closed ? controls : controls - 1);
}

// Tangents for generated control points: central differences (wrapped when closed, one-sided at
// open ends), scaled for the Bezier kind so the handle reproduces the same derivative.
void assignGeneratedTangents(std::vector<SplinePoint>& pts, bool closed, SplineKind kind) {
    const int n = static_cast<int>(pts.size());
    if (n < 2) {
        return;
    }
    const float handleScale = kind == SplineKind::Bezier ? 1.0f / 3.0f : 1.0f;
    std::vector<glm::vec3> tangents(pts.size());
    for (int i = 0; i < n; ++i) {
        glm::vec3 t{0.0f};
        if (closed) {
            t = (pts[static_cast<std::size_t>((i + 1) % n)].position -
                 pts[static_cast<std::size_t>((i - 1 + n) % n)].position) *
                0.5f;
        } else if (i == 0) {
            t = pts[1].position - pts[0].position;
        } else if (i == n - 1) {
            t = pts[static_cast<std::size_t>(n - 1)].position - pts[static_cast<std::size_t>(n - 2)].position;
        } else {
            t = (pts[static_cast<std::size_t>(i + 1)].position - pts[static_cast<std::size_t>(i - 1)].position) * 0.5f;
        }
        tangents[static_cast<std::size_t>(i)] = t * handleScale;
    }
    for (int i = 0; i < n; ++i) {
        pts[static_cast<std::size_t>(i)].tangent = tangents[static_cast<std::size_t>(i)];
    }
}

// ---- JSON helpers ------------------------------------------------------------------------------

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

// Assigns `target` from `reader(j, key, target)` or returns the error.
#define AVGEN_SPLINE_READ(target, key, reader)                                                          \
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

json pointToJson(const SplinePoint& p) {
    json j = json::object();
    j["position"] = vecToJson(p.position);
    j["tangent"] = vecToJson(p.tangent);
    j["roll"] = p.roll;
    j["scale"] = p.scale;
    return j;
}

Result<SplinePoint> pointFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("spline point must be an object");
    }
    SplinePoint p;
    AVGEN_SPLINE_READ(p.position, "position", readVec3);
    AVGEN_SPLINE_READ(p.tangent, "tangent", readVec3);
    AVGEN_SPLINE_READ(p.roll, "roll", readFloat);
    AVGEN_SPLINE_READ(p.scale, "scale", readFloat);
    return p;
}

// Linear interpolation of two table entries; the frame is renormalised and re-orthogonalised.
SplineSample lerpSamples(const SplineSample& a, const SplineSample& b, float f) {
    SplineSample s;
    s.position = a.position + (b.position - a.position) * f;
    s.tangent = unitOr(a.tangent + (b.tangent - a.tangent) * f, a.tangent);
    const glm::vec3 n = a.normal + (b.normal - a.normal) * f;
    s.normal = perpendicularTo(s.tangent, glm::length(n) > 1e-5f ? n : a.normal);
    s.binormal = glm::cross(s.normal, s.tangent);
    s.distance = a.distance + (b.distance - a.distance) * f;
    s.t = a.t + (b.t - a.t) * f;
    s.scale = a.scale + (b.scale - a.scale) * f;
    return s;
}

} // namespace

// ================================================================================================
// Enum names
// ================================================================================================

const char* splineKindName(SplineKind kind) {
    switch (kind) {
    case SplineKind::Polyline:
        return "polyline";
    case SplineKind::CatmullRom:
        return "catmullRom";
    case SplineKind::Bezier:
        return "bezier";
    case SplineKind::Hermite:
        return "hermite";
    }
    return "catmullRom";
}

std::optional<SplineKind> splineKindFromName(std::string_view name) {
    for (const SplineKind kind : {SplineKind::Polyline, SplineKind::CatmullRom, SplineKind::Bezier, SplineKind::Hermite}) {
        if (name == splineKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

const char* splineGeneratorName(SplineGenerator generator) {
    switch (generator) {
    case SplineGenerator::Points:
        return "points";
    case SplineGenerator::Line:
        return "line";
    case SplineGenerator::Circle:
        return "circle";
    case SplineGenerator::Spiral:
        return "spiral";
    case SplineGenerator::Helix:
        return "helix";
    case SplineGenerator::Bezier:
        return "bezier";
    case SplineGenerator::Noise:
        return "noise";
    }
    return "points";
}

std::optional<SplineGenerator> splineGeneratorFromName(std::string_view name) {
    for (const SplineGenerator g : {SplineGenerator::Points, SplineGenerator::Line, SplineGenerator::Circle,
                                    SplineGenerator::Spiral, SplineGenerator::Helix, SplineGenerator::Bezier,
                                    SplineGenerator::Noise}) {
        if (name == splineGeneratorName(g)) {
            return g;
        }
    }
    return std::nullopt;
}

// ================================================================================================
// SplineSample
// ================================================================================================

glm::quat SplineSample::rotation() const {
    return glm::normalize(glm::quat_cast(glm::mat3(binormal, normal, tangent)));
}

// ================================================================================================
// Spline: validation, hashing, control points
// ================================================================================================

Result<void> Spline::validate() const {
    if (!finite(tension)) {
        return fail("spline '{}': tension must be finite", name);
    }
    if (generator != SplineGenerator::Points && count < 2) {
        return fail("spline '{}': generator count must be >= 2 (got {})", name, count);
    }
    if (!finite(radius) || radius < 0.0f) {
        return fail("spline '{}': radius must be >= 0", name);
    }
    if (samplesPerSegment < 1 || samplesPerSegment > kMaxSamplesPerSegment) {
        return fail("spline '{}': samplesPerSegment must be in 1..{} (got {})", name, kMaxSamplesPerSegment,
                    samplesPerSegment);
    }
    if (!finite(start) || !finite(end) || !finite(radiusGrowth) || !finite(turns) || !finite(height) ||
        !finite(axis) || !finite(center) || !finite(startAngle) || !finite(p0) || !finite(p1) || !finite(p2) ||
        !finite(p3) || !finite(noiseAmount) || !finite(noiseScale) || !finite(up)) {
        return fail("spline '{}': every value must be finite", name);
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const SplinePoint& p = points[i];
        if (!finite(p.position) || !finite(p.tangent) || !finite(p.roll) || !finite(p.scale)) {
            return fail("spline '{}': point {} must be finite", name, i);
        }
    }
    const std::size_t n = controlPoints().size();
    if (n < 2) {
        return fail("spline '{}': needs at least 2 control points (got {})", name, n);
    }
    return {};
}

std::uint64_t Spline::structuralHash() const {
    StructHash h;
    h.text(name);
    h.u32(static_cast<std::uint32_t>(kind));
    h.boolean(closed);
    h.f32(tension);
    h.u32(static_cast<std::uint32_t>(points.size()));
    for (const SplinePoint& p : points) {
        h.v3(p.position);
        h.v3(p.tangent);
        h.f32(p.roll);
        h.f32(p.scale);
    }
    h.u32(static_cast<std::uint32_t>(generator));
    h.i32(count);
    h.v3(start);
    h.v3(end);
    h.f32(radius);
    h.f32(radiusGrowth);
    h.f32(turns);
    h.f32(height);
    h.v3(axis);
    h.v3(center);
    h.f32(startAngle);
    h.v3(p0);
    h.v3(p1);
    h.v3(p2);
    h.v3(p3);
    h.f32(noiseAmount);
    h.f32(noiseScale);
    h.u32(seed);
    h.i32(samplesPerSegment);
    h.v3(up);
    return h.value();
}

std::vector<SplinePoint> Spline::controlPoints() const {
    std::vector<SplinePoint> pts;
    if (generator == SplineGenerator::Points) {
        pts = points;
    } else {
        const int n = std::max(count, 2);
        pts.resize(static_cast<std::size_t>(n));
        const float invLast = 1.0f / static_cast<float>(n - 1);
        switch (generator) {
        case SplineGenerator::Points:
            break;
        case SplineGenerator::Line:
        case SplineGenerator::Noise:
            for (int i = 0; i < n; ++i) {
                const float f = static_cast<float>(i) * invLast;
                pts[static_cast<std::size_t>(i)].position = start + (end - start) * f;
            }
            break;
        case SplineGenerator::Circle: {
            // A closed spline gets `count` distinct points (no duplicate seam); an open one runs
            // from startAngle to startAngle + 2 pi inclusive.
            const PlaneBasis b = planeBasis(axis);
            const float step = kTwoPi / static_cast<float>(closed ? n : n - 1);
            for (int i = 0; i < n; ++i) {
                const float a = startAngle + step * static_cast<float>(i);
                pts[static_cast<std::size_t>(i)].position = center + (b.u * std::cos(a) + b.v * std::sin(a)) * radius;
            }
            break;
        }
        case SplineGenerator::Spiral: {
            const PlaneBasis b = planeBasis(axis);
            for (int i = 0; i < n; ++i) {
                const float f = static_cast<float>(i) * invLast;
                const float a = startAngle + kTwoPi * turns * f;
                const float r = radius + radiusGrowth * f;
                pts[static_cast<std::size_t>(i)].position = center + (b.u * std::cos(a) + b.v * std::sin(a)) * r;
            }
            break;
        }
        case SplineGenerator::Helix: {
            const PlaneBasis b = planeBasis(axis);
            for (int i = 0; i < n; ++i) {
                const float f = static_cast<float>(i) * invLast;
                const float a = startAngle + kTwoPi * turns * f;
                pts[static_cast<std::size_t>(i)].position =
                    center + (b.u * std::cos(a) + b.v * std::sin(a)) * radius + b.axis * (height * f);
            }
            break;
        }
        case SplineGenerator::Bezier:
            for (int i = 0; i < n; ++i) {
                const float f = static_cast<float>(i) * invLast;
                glm::vec3 position{};
                glm::vec3 derivative{};
                bezier(p0, p1, p2, p3, f, position, derivative);
                pts[static_cast<std::size_t>(i)].position = position;
            }
            break;
        }
    }
    if (noiseAmount > 0.0f || generator == SplineGenerator::Noise) {
        for (SplinePoint& p : pts) {
            p.position += noise::fbm3Vec(p.position * noiseScale, seed) * noiseAmount;
        }
    }
    if (generator != SplineGenerator::Points) {
        assignGeneratedTangents(pts, closed, kind);
    }
    return pts;
}

int Spline::segmentCount() const {
    return segmentCountOf(controlPoints().size(), closed);
}

// ================================================================================================
// Spline: arc-length table and frames
// ================================================================================================

void Spline::ensureCache() const {
    const std::uint64_t hash = structuralHash();
    if (cache_.hash == hash && !cache_.table.empty()) {
        return;
    }
    Cache c;
    c.hash = hash;
    c.controls = controlPoints();
    const int segments = segmentCountOf(c.controls.size(), closed);
    if (segments == 0) {
        // Degenerate: a single (or no) control point. One sample at the point (or the origin).
        SplineSample s;
        if (!c.controls.empty()) {
            s.position = c.controls.front().position;
            s.scale = c.controls.front().scale;
        }
        s.tangent = glm::vec3(0.0f, 0.0f, 1.0f);
        s.normal = perpendicularTo(s.tangent, up);
        s.binormal = glm::cross(s.normal, s.tangent);
        c.table.push_back(s);
        c.length = 0.0f;
        cache_ = std::move(c);
        return;
    }

    const int perSegment = std::clamp(samplesPerSegment, kMinSamplesPerSegment, kMaxSamplesPerSegment);
    const std::size_t total = static_cast<std::size_t>(segments) * static_cast<std::size_t>(perSegment) + 1;
    c.table.resize(total);
    std::vector<float> rolls(total);

    // Positions, analytic derivatives, uniform parameter.
    for (std::size_t k = 0; k < total; ++k) {
        int segment = static_cast<int>(k / static_cast<std::size_t>(perSegment));
        int j = static_cast<int>(k % static_cast<std::size_t>(perSegment));
        if (segment >= segments) { // the final sample: end of the last segment
            segment = segments - 1;
            j = perSegment;
        }
        const float u = static_cast<float>(j) / static_cast<float>(perSegment);
        const SegmentEval e = evaluateSegment(*this, c.controls, segment, u);
        SplineSample& s = c.table[k];
        s.position = e.position;
        s.tangent = e.derivative; // normalised below (with fallbacks for zero derivatives)
        s.t = (static_cast<float>(segment) + u) / static_cast<float>(segments);
        s.scale = e.scale;
        rolls[k] = e.roll;
    }

    // Unit tangents: analytic derivative, else the chord to the next/previous sample, else the
    // previous tangent.
    glm::vec3 lastTangent(0.0f, 0.0f, 1.0f);
    for (std::size_t k = 0; k < total; ++k) {
        SplineSample& s = c.table[k];
        glm::vec3 t = s.tangent;
        if (glm::length(t) <= 1e-6f && k + 1 < total) {
            t = c.table[k + 1].position - s.position;
        }
        if (glm::length(t) <= 1e-6f && k > 0) {
            t = s.position - c.table[k - 1].position;
        }
        s.tangent = unitOr(t, lastTangent);
        lastTangent = s.tangent;
    }

    // Cumulative chord distance.
    c.table[0].distance = 0.0f;
    for (std::size_t k = 1; k < total; ++k) {
        c.table[k].distance = c.table[k - 1].distance + glm::length(c.table[k].position - c.table[k - 1].position);
    }
    c.length = c.table.back().distance;

    // Rotation-minimising frames by double reflection (Wang et al. 2008).
    c.table[0].normal = perpendicularTo(c.table[0].tangent, up);
    for (std::size_t k = 0; k + 1 < total; ++k) {
        const SplineSample& a = c.table[k];
        SplineSample& b = c.table[k + 1];
        const glm::vec3 v1 = b.position - a.position;
        const float c1 = glm::dot(v1, v1);
        glm::vec3 rL = a.normal;
        glm::vec3 tL = a.tangent;
        if (c1 > 1e-12f) {
            rL = a.normal - v1 * (2.0f / c1 * glm::dot(v1, a.normal));
            tL = a.tangent - v1 * (2.0f / c1 * glm::dot(v1, a.tangent));
        }
        const glm::vec3 v2 = b.tangent - tL;
        const float c2 = glm::dot(v2, v2);
        glm::vec3 r = rL;
        if (c2 > 1e-12f) {
            r = rL - v2 * (2.0f / c2 * glm::dot(v2, rL));
        }
        b.normal = perpendicularTo(b.tangent, glm::length(r) > 1e-5f ? r : a.normal);
    }

    // Closed splines: distribute the seam mismatch linearly along the curve so the transported
    // frame at the end coincides with the start frame.
    if (closed && c.length > kEpsilon) {
        const SplineSample& last = c.table.back();
        const glm::vec3 target = perpendicularTo(last.tangent, c.table[0].normal);
        const float angle = std::atan2(glm::dot(glm::cross(last.normal, target), last.tangent),
                                       glm::dot(last.normal, target));
        if (std::abs(angle) > 1e-7f) {
            for (std::size_t k = 1; k < total; ++k) {
                SplineSample& s = c.table[k];
                s.normal = perpendicularTo(s.tangent, rotateAbout(s.normal, s.tangent, angle * (s.distance / c.length)));
            }
        }
    }

    // Per-point roll and the binormal.
    for (std::size_t k = 0; k < total; ++k) {
        SplineSample& s = c.table[k];
        if (rolls[k] != 0.0f) {
            s.normal = perpendicularTo(s.tangent, rotateAbout(s.normal, s.tangent, rolls[k]));
        }
        s.binormal = glm::cross(s.normal, s.tangent);
    }

    cache_ = std::move(c);
}

void Spline::prepare() const {
    ensureCache();
}

// ================================================================================================
// Spline: sampling
// ================================================================================================

SplineSample Spline::sample(float t) const {
    ensureCache();
    const std::vector<SplineSample>& table = cache_.table;
    if (table.size() < 2) {
        return table.front();
    }
    if (!std::isfinite(t)) {
        t = 0.0f;
    }
    if (closed) {
        t = t - std::floor(t);
    } else {
        t = std::clamp(t, 0.0f, 1.0f);
    }
    const float pos = t * static_cast<float>(table.size() - 1);
    const std::size_t i = std::min(static_cast<std::size_t>(std::floor(pos)), table.size() - 2);
    const float f = std::clamp(pos - static_cast<float>(i), 0.0f, 1.0f);
    SplineSample s = lerpSamples(table[i], table[i + 1], f);
    s.t = t;
    return s;
}

SplineSample Spline::sampleByDistance(float distance) const {
    ensureCache();
    const std::vector<SplineSample>& table = cache_.table;
    const float len = cache_.length;
    if (table.size() < 2 || len <= kEpsilon) {
        return table.front();
    }
    if (!std::isfinite(distance)) {
        distance = 0.0f;
    }
    if (closed) {
        distance = distance - std::floor(distance / len) * len;
        if (distance >= len) {
            distance = 0.0f;
        }
    } else {
        distance = std::clamp(distance, 0.0f, len);
    }
    // First entry whose distance is > `distance`; the segment is [it - 1, it].
    auto it = std::upper_bound(table.begin(), table.end(), distance,
                               [](float d, const SplineSample& s) { return d < s.distance; });
    std::size_t hi = static_cast<std::size_t>(std::distance(table.begin(), it));
    hi = std::clamp<std::size_t>(hi, 1, table.size() - 1);
    const SplineSample& a = table[hi - 1];
    const SplineSample& b = table[hi];
    const float span = b.distance - a.distance;
    const float f = span > kEpsilon ? std::clamp((distance - a.distance) / span, 0.0f, 1.0f) : 0.0f;
    SplineSample s = lerpSamples(a, b, f);
    s.distance = distance;
    return s;
}

glm::vec3 Spline::position(float t) const {
    return sample(t).position;
}

glm::vec3 Spline::tangent(float t) const {
    return sample(t).tangent;
}

float Spline::length() const {
    ensureCache();
    return cache_.length;
}

std::vector<SplineSample> Spline::samples(int sampleCount) const {
    std::vector<SplineSample> out;
    if (sampleCount < 1) {
        return out;
    }
    ensureCache();
    out.reserve(static_cast<std::size_t>(sampleCount));
    const float len = cache_.length;
    if (sampleCount == 1) {
        out.push_back(sampleByDistance(0.0f));
        return out;
    }
    const float step = closed ? len / static_cast<float>(sampleCount) : len / static_cast<float>(sampleCount - 1);
    for (int k = 0; k < sampleCount; ++k) {
        float d = step * static_cast<float>(k);
        if (!closed && k == sampleCount - 1) {
            d = len;
        }
        out.push_back(sampleByDistance(d));
    }
    return out;
}

// ================================================================================================
// JSON
// ================================================================================================

json Spline::toJson() const {
    json j = json::object();
    j["name"] = name;
    j["kind"] = splineKindName(kind);
    j["closed"] = closed;
    j["tension"] = tension;
    json pts = json::array();
    for (const SplinePoint& p : points) {
        pts.push_back(pointToJson(p));
    }
    j["points"] = std::move(pts);
    j["generator"] = splineGeneratorName(generator);
    j["count"] = count;
    j["start"] = vecToJson(start);
    j["end"] = vecToJson(end);
    j["radius"] = radius;
    j["radiusGrowth"] = radiusGrowth;
    j["turns"] = turns;
    j["height"] = height;
    j["axis"] = vecToJson(axis);
    j["center"] = vecToJson(center);
    j["startAngle"] = startAngle;
    j["p0"] = vecToJson(p0);
    j["p1"] = vecToJson(p1);
    j["p2"] = vecToJson(p2);
    j["p3"] = vecToJson(p3);
    j["noiseAmount"] = noiseAmount;
    j["noiseScale"] = noiseScale;
    j["seed"] = seed;
    j["samplesPerSegment"] = samplesPerSegment;
    j["up"] = vecToJson(up);
    return j;
}

Result<Spline> Spline::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("spline must be an object");
    }
    Spline s;
    AVGEN_SPLINE_READ(s.name, "name", readString);
    if (auto r = readEnum(j, "kind", s.kind, &splineKindFromName, "spline kind"); !r) {
        return std::unexpected(r.error());
    }
    AVGEN_SPLINE_READ(s.closed, "closed", readBool);
    AVGEN_SPLINE_READ(s.tension, "tension", readFloat);
    if (j.contains("points")) {
        const json& pts = j.at("points");
        if (!pts.is_array()) {
            return fail("'points' must be an array");
        }
        s.points.clear();
        s.points.reserve(pts.size());
        for (const json& pj : pts) {
            auto p = pointFromJson(pj);
            if (!p) {
                return std::unexpected(p.error());
            }
            s.points.push_back(*p);
        }
    }
    if (auto r = readEnum(j, "generator", s.generator, &splineGeneratorFromName, "spline generator"); !r) {
        return std::unexpected(r.error());
    }
    AVGEN_SPLINE_READ(s.count, "count", readInt);
    AVGEN_SPLINE_READ(s.start, "start", readVec3);
    AVGEN_SPLINE_READ(s.end, "end", readVec3);
    AVGEN_SPLINE_READ(s.radius, "radius", readFloat);
    AVGEN_SPLINE_READ(s.radiusGrowth, "radiusGrowth", readFloat);
    AVGEN_SPLINE_READ(s.turns, "turns", readFloat);
    AVGEN_SPLINE_READ(s.height, "height", readFloat);
    AVGEN_SPLINE_READ(s.axis, "axis", readVec3);
    AVGEN_SPLINE_READ(s.center, "center", readVec3);
    AVGEN_SPLINE_READ(s.startAngle, "startAngle", readFloat);
    AVGEN_SPLINE_READ(s.p0, "p0", readVec3);
    AVGEN_SPLINE_READ(s.p1, "p1", readVec3);
    AVGEN_SPLINE_READ(s.p2, "p2", readVec3);
    AVGEN_SPLINE_READ(s.p3, "p3", readVec3);
    AVGEN_SPLINE_READ(s.noiseAmount, "noiseAmount", readFloat);
    AVGEN_SPLINE_READ(s.noiseScale, "noiseScale", readFloat);
    AVGEN_SPLINE_READ(s.seed, "seed", readU32);
    AVGEN_SPLINE_READ(s.samplesPerSegment, "samplesPerSegment", readInt);
    AVGEN_SPLINE_READ(s.up, "up", readVec3);
    return s;
}

#undef AVGEN_SPLINE_READ

// ================================================================================================
// GPU table and sets
// ================================================================================================

std::vector<SplineSampleGpu> packSplineTable(const Spline& spline, int count) {
    std::vector<SplineSampleGpu> out;
    const std::vector<SplineSample> samples = spline.samples(std::max(count, 1));
    out.reserve(samples.size());
    for (const SplineSample& s : samples) {
        SplineSampleGpu g{};
        g.position = glm::vec4(s.position, s.distance);
        g.tangent = glm::vec4(s.tangent, s.scale);
        g.normal = glm::vec4(s.normal, s.t);
        g.binormal = glm::vec4(s.binormal, 1.0f);
        out.push_back(g);
    }
    return out;
}

const Spline* SplineSet::find(std::string_view name) const {
    const int i = indexOf(name);
    return i < 0 ? nullptr : &splines[static_cast<std::size_t>(i)];
}

int SplineSet::indexOf(std::string_view name) const {
    for (std::size_t i = 0; i < splines.size(); ++i) {
        if (splines[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace avgen::spatial

// Fields (ADR-025): CPU reference sampling, falloff curves, packing for the GPU, JSON.
//
// The maths below is the specification shaders/fields.wgsl transliterates. Order of evaluation
// for a sample at world point p and time t (float):
//   q     = worldToLocal * p              (worldToLocal = inverse(T(position) R(euler XYZ deg) S(scale)))
//   n     = unitOr(axis, (0, 1, 0))       (axis normalised in local space; (0,1,0) for a zero axis)
//   d     = falloff distance measure      (see falloffDistance)
//   w     = strength * falloff.weight(d)  (weight is 1 for d <= inner, 0 for d >= outer)
//   tau   = speed * t + phase
//   scalar kinds : v = shape(q); invert -> 1 - v; result v * w
//   vector kinds : dir(q) (unit or zero, CurlNoise unnormalised); invert -> -dir; result R * (dir * w)
//                  where R is the rotation-only 3x3 of the field (no scale)
//   colour kinds : rgb = colour(q) (invert swaps colorA/colorB); result (rgb, w)
//   compound     : combine(children sampled at p in the requested type); invert as the requested
//                  type (scalar 1 - v, vector -v, colour 1 - rgb); then * w (colour: alpha * w)
// Cross-type reads: scalar as vector = s * (R n); vector as scalar = |v|; colour as scalar =
// luminance(rgb) * a (luminance = dot(rgb, (0.2126, 0.7152, 0.0722))); scalar as colour =
// (mix(colorA.rgb, colorB.rgb, saturate(s)), w); vector as colour = (v * 0.5 + 0.5, w); colour as
// vector = luminance(rgb) * a * axis (colour -> scalar -> vector).
//
// Choices/deviations to carry to the GPU (the brief left these open):
// * The NoiseModulated falloff samples fbm3(q * noiseScale, seed) at the LOCAL point q (so the
//   modulation moves with the field), not the world point.
// * Radial and Cylindrical wave distances are the same quantity (distance from the axis line
//   through `point` along n); both are implemented by the same function.
// * Waves use t (seconds) directly: s = waveDistance(q) - waveOrigin - waveSpeed * t; `speed`
//   and `phase` (tau) only animate the noise kinds.
// * CustomCurve follows the brief (cubic Bezier on y through 1, curve.x, curve.y, curve.z at
//   x = 0, 1/3, 2/3, 1), so the curve reaches curve.z at the outer radius, not 0 — a jump to 0 at
//   d >= outer unless curve.z is 0.
// * A disabled field samples as 0 (vector 0, colour (0, 0, 0, 0)); disabled compound children are
//   skipped (they do not count towards Average).
// * Compound recursion: a field at depth > 4 samples as 0, which is also how cycles terminate.
// * SdfDistance is 0 on the CPU and 0 on the GPU (unbound; ADR-027 reserved the seam and
//   never bound it). Since ADR-576 a spec that names it is REFUSED by `validate`, so this
//   arm is reachable only by a field constructed in code that never validated -- the zero
//   is kept so that path stays defined rather than undefined.

#include "spatial/field.hpp"

#include "core/noise.hpp"
#include "spatial/detail.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::spatial {

using nlohmann::json;

namespace {

constexpr float kTwoPi = 6.283185307179586f;
constexpr int kMaxDepth = 4;
constexpr float kEps = 1e-6f;

float saturate(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

glm::vec3 unitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-8f ? v / len : fallback;
}

glm::vec3 unitOrZero(const glm::vec3& v) {
    return unitOr(v, glm::vec3(0.0f));
}

glm::vec3 fieldAxis(const FieldSpec& f) {
    return unitOr(f.axis, glm::vec3(0.0f, 1.0f, 0.0f));
}

float luminance(const glm::vec3& c) {
    return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float fract(float v) {
    return v - std::floor(v);
}

glm::vec3 fract(const glm::vec3& v) {
    return v - glm::floor(v);
}

float maxComponent(const glm::vec3& v) {
    return std::max({v.x, v.y, v.z});
}

glm::vec3 animatedNoiseInput(const FieldSpec& f, const glm::vec3& q, float tau) {
    return q * f.frequency + tau * glm::vec3(1.0f, 0.7f, 1.3f);
}

// ---- frame -----------------------------------------------------------------------------------

glm::quat fieldRotation(const FieldSpec& f) {
    return glm::quat(glm::radians(f.rotationDegrees));
}

struct Frame {
    glm::mat4 worldToLocal;
    glm::mat3 rotation; // rotation-only local -> world
};

Frame frameOf(const FieldSpec& f) {
    return Frame{f.worldToLocal(), glm::mat3_cast(fieldRotation(f))};
}

// ---- falloff -----------------------------------------------------------------------------------

float falloffDistance(const FieldSpec& f, const glm::vec3& q) {
    switch (f.kind) {
    case FieldKind::LinearGradient:
    case FieldKind::Plane:
    case FieldKind::Direction:
    case FieldKind::Gradient:
        return std::abs(glm::dot(q, fieldAxis(f)));
    case FieldKind::Box:
        return std::max(maxComponent(glm::abs(q) - f.size), 0.0f);
    default:
        return glm::length(q - f.point);
    }
}

float fieldWeight(const FieldSpec& f, const glm::vec3& q) {
    return f.strength * f.falloff.weight(falloffDistance(f, q), q, f.seed);
}

// ---- waves -------------------------------------------------------------------------------------

// Distance measure of the wave geometry and the (unit) direction in which it grows.
float waveDistance(const FieldSpec& f, const glm::vec3& q) {
    const glm::vec3 n = fieldAxis(f);
    const glm::vec3 r = q - f.point;
    switch (f.waveGeometry) {
    case WaveGeometry::Planar:
        return glm::dot(q, n);
    case WaveGeometry::Spherical:
        return glm::length(r);
    case WaveGeometry::Radial:
    case WaveGeometry::Cylindrical:
        return glm::length(r - n * glm::dot(r, n));
    }
    return glm::length(r);
}

glm::vec3 waveGradient(const FieldSpec& f, const glm::vec3& q) {
    const glm::vec3 n = fieldAxis(f);
    const glm::vec3 r = q - f.point;
    switch (f.waveGeometry) {
    case WaveGeometry::Planar:
        return n;
    case WaveGeometry::Spherical:
        return unitOrZero(r);
    case WaveGeometry::Radial:
    case WaveGeometry::Cylindrical:
        return unitOrZero(r - n * glm::dot(r, n));
    }
    return unitOrZero(r);
}

float waveShapeValue(WaveShape shape, float x) { // x = k s
    switch (shape) {
    case WaveShape::Sine:
        return std::sin(x);
    case WaveShape::Pulse:
        return std::exp(-(x * x));
    case WaveShape::Triangle:
        return 4.0f * std::abs(fract(x / kTwoPi + 0.75f) - 0.5f) - 1.0f;
    }
    return std::sin(x);
}

float waveValue(const FieldSpec& f, const glm::vec3& q, float t) {
    const float s = waveDistance(f, q) - f.waveOrigin - f.waveSpeed * t;
    const float k = kTwoPi / std::max(f.wavelength, kEps);
    const float envelope =
        f.waveWidth > 0.0f ? falloffCurve(FalloffKind::Smoothstep, saturate(std::abs(s) / f.waveWidth), 2.0f, glm::vec4(0.0f))
                           : 1.0f;
    return f.amplitude * envelope * waveShapeValue(f.waveShape, k * s);
}

// ---- shapes ------------------------------------------------------------------------------------

// Scalar kinds, before invert and weight.
float scalarShape(const FieldSpec& f, const glm::vec3& q, float t, float tau) {
    const glm::vec3 n = fieldAxis(f);
    switch (f.kind) {
    case FieldKind::Constant:
        return 1.0f;
    case FieldKind::LinearGradient:
        return saturate(glm::dot(q, n) / std::max(f.length, kEps) + 0.5f);
    case FieldKind::Radial:
        return 1.0f - saturate(glm::length(q - f.point) / std::max(f.radius, kEps));
    case FieldKind::Box:
        return 1.0f - saturate(std::max(maxComponent(glm::abs(q) - f.size), 0.0f) / std::max(f.softness, kEps));
    case FieldKind::Sphere:
        return 1.0f - saturate((glm::length(q - f.point) - f.radius) / std::max(f.softness, kEps));
    case FieldKind::Plane:
        return saturate(glm::dot(q, n) / std::max(f.softness, kEps));
    case FieldKind::Noise:
        return noise::fbm3(animatedNoiseInput(f, q, tau), f.seed);
    case FieldKind::Voronoi:
        return saturate(noise::voronoiF1(animatedNoiseInput(f, q, tau), f.seed));
    case FieldKind::Distance:
        return saturate(glm::length(q - f.point) / std::max(f.radius, kEps));
    case FieldKind::SdfDistance:
        return 0.0f;
    case FieldKind::Wave:
        return waveValue(f, q, t);
    default:
        return 0.0f;
    }
}

// Vector kinds, before invert and weight, in the field's local orientation.
glm::vec3 vectorShape(const FieldSpec& f, const glm::vec3& q, float t, float tau) {
    const glm::vec3 n = fieldAxis(f);
    const glm::vec3 r = q - f.point;
    switch (f.kind) {
    case FieldKind::Direction:
        return n;
    case FieldKind::RadialVector:
        return unitOrZero(r);
    case FieldKind::Attractor:
        return unitOrZero(-r);
    case FieldKind::Repulsor:
        return unitOrZero(r);
    case FieldKind::Vortex:
        return unitOrZero(glm::cross(n, r));
    case FieldKind::CurlNoise:
        return noise::curlNoise(animatedNoiseInput(f, q, tau), f.seed);
    case FieldKind::Spiral:
        return unitOrZero(unitOrZero(glm::cross(n, r)) + f.spiralBias * unitOrZero(r));
    case FieldKind::WaveVector:
        return waveValue(f, q, t) * waveGradient(f, q);
    default:
        return glm::vec3(0.0f);
    }
}

// Colour kinds (rgb), invert already applied through a/b.
glm::vec3 colorShape(const FieldSpec& f, const glm::vec3& q, float tau, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 n = fieldAxis(f);
    switch (f.kind) {
    case FieldKind::ConstantColor:
        return a;
    case FieldKind::Gradient:
        return glm::mix(a, b, saturate(glm::dot(q, n) / std::max(f.length, kEps) + 0.5f));
    case FieldKind::RadialGradient:
        return glm::mix(a, b, saturate(glm::length(q - f.point) / std::max(f.radius, kEps)));
    case FieldKind::NoiseColor:
        return glm::mix(a, b, noise::fbm3(animatedNoiseInput(f, q, tau), f.seed));
    case FieldKind::PositionColor:
        return fract(q * f.frequency);
    default:
        return glm::vec3(0.0f);
    }
}

// ---- compound ----------------------------------------------------------------------------------

template <typename T>
T combineValues(FieldCombine combine, float mixAmount, const std::vector<T>& values) {
    if (values.empty()) {
        return T(0.0f);
    }
    switch (combine) {
    case FieldCombine::Add: {
        T sum(0.0f);
        for (const T& v : values) {
            sum += v;
        }
        return sum;
    }
    case FieldCombine::Multiply: {
        T product(1.0f);
        for (const T& v : values) {
            product *= v;
        }
        return product;
    }
    case FieldCombine::Max: {
        T m = values.front();
        for (const T& v : values) {
            m = glm::max(m, v);
        }
        return m;
    }
    case FieldCombine::Min: {
        T m = values.front();
        for (const T& v : values) {
            m = glm::min(m, v);
        }
        return m;
    }
    case FieldCombine::Mix:
        return values.size() > 1 ? glm::mix(values[0], values[1], mixAmount) : values[0];
    case FieldCombine::Average: {
        T sum(0.0f);
        for (const T& v : values) {
            sum += v;
        }
        return sum / static_cast<float>(values.size());
    }
    }
    return T(0.0f);
}

float scalarAt(const FieldSpec& f, const glm::vec3& p, float t, const FieldSet* set, int depth);
glm::vec3 vectorAt(const FieldSpec& f, const glm::vec3& p, float t, const FieldSet* set, int depth);
glm::vec4 colorAt(const FieldSpec& f, const glm::vec3& p, float t, const FieldSet* set, int depth);

// Enabled, resolvable children (first kMaxCompoundChildren names).
std::vector<const FieldSpec*> compoundChildren(const FieldSpec& f, const FieldSet* set) {
    std::vector<const FieldSpec*> out;
    if (set == nullptr) {
        return out;
    }
    const std::size_t n = std::min(f.children.size(), static_cast<std::size_t>(kMaxCompoundChildren));
    for (std::size_t i = 0; i < n; ++i) {
        const FieldSpec* child = set->find(f.children[i]);
        if (child != nullptr && child->enabled) {
            out.push_back(child);
        }
    }
    return out;
}

// The grid a Grid field references (null when unbound, disabled or unallocated).
const GridField* boundGrid(const FieldSpec& f, const FieldSet* set) {
    if (f.kind != FieldKind::Grid || set == nullptr) {
        return nullptr;
    }
    const GridField* g = set->findGrid(f.reference);
    return (g != nullptr && g->enabled) ? g : nullptr;
}

float scalarAt(const FieldSpec& f, const glm::vec3& p, float t, const FieldSet* set, int depth) {
    if (!f.enabled || depth > kMaxDepth) {
        return 0.0f;
    }
    if (f.kind == FieldKind::Grid) {
        const GridField* g = boundGrid(f, set);
        if (g != nullptr && g->mode == GridMode::Vector) {
            return glm::length(vectorAt(f, p, t, set, depth)); // vector as scalar
        }
        const glm::vec3 q = glm::vec3(f.worldToLocal() * glm::vec4(p, 1.0f));
        float v = g != nullptr ? g->sampleScalar(q) : 0.0f;
        if (f.invert) {
            v = 1.0f - v;
        }
        return v * fieldWeight(f, q);
    }
    switch (f.type()) {
    case FieldType::Vector:
        return glm::length(vectorAt(f, p, t, set, depth));
    case FieldType::Color: {
        const glm::vec4 c = colorAt(f, p, t, set, depth);
        return luminance(glm::vec3(c)) * c.a;
    }
    case FieldType::Scalar:
        break;
    }
    const glm::vec3 q = glm::vec3(f.worldToLocal() * glm::vec4(p, 1.0f));
    const float w = fieldWeight(f, q);
    float v = 0.0f;
    if (f.kind == FieldKind::Compound) {
        std::vector<float> values;
        for (const FieldSpec* child : compoundChildren(f, set)) {
            values.push_back(scalarAt(*child, p, t, set, depth + 1));
        }
        v = combineValues(f.combine, f.mix, values);
    } else {
        v = scalarShape(f, q, t, f.speed * t + f.phase);
    }
    if (f.invert) {
        v = 1.0f - v;
    }
    return v * w;
}

glm::vec3 vectorAt(const FieldSpec& f, const glm::vec3& p, float t, const FieldSet* set, int depth) {
    if (!f.enabled || depth > kMaxDepth) {
        return glm::vec3(0.0f);
    }
    const Frame frame = frameOf(f);
    const glm::vec3 q = glm::vec3(frame.worldToLocal * glm::vec4(p, 1.0f));
    if (f.kind == FieldKind::Grid) {
        const GridField* g = boundGrid(f, set);
        if (g == nullptr || g->mode != GridMode::Vector) {
            return scalarAt(f, p, t, set, depth) * (frame.rotation * fieldAxis(f)); // scalar as vector
        }
        glm::vec3 dir = g->sampleVector(q);
        if (f.invert) {
            dir = -dir;
        }
        return frame.rotation * (dir * fieldWeight(f, q));
    }
    if (f.kind == FieldKind::Compound) {
        std::vector<glm::vec3> values;
        for (const FieldSpec* child : compoundChildren(f, set)) {
            values.push_back(vectorAt(*child, p, t, set, depth + 1));
        }
        glm::vec3 v = combineValues(f.combine, f.mix, values);
        if (f.invert) {
            v = -v;
        }
        return v * fieldWeight(f, q);
    }
    switch (f.type()) {
    case FieldType::Scalar:
        return scalarAt(f, p, t, set, depth) * (frame.rotation * fieldAxis(f));
    case FieldType::Color: {
        // Colour as vector = (colour as scalar) along the axis, like the GPU (fields.wgsl basicVector).
        const glm::vec4 c = colorAt(f, p, t, set, depth);
        const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        return (lum * c.a) * (frame.rotation * fieldAxis(f));
    }
    case FieldType::Vector:
        break;
    }
    const float w = fieldWeight(f, q);
    glm::vec3 dir = vectorShape(f, q, t, f.speed * t + f.phase);
    if (f.invert) {
        dir = -dir;
    }
    return frame.rotation * (dir * w);
}

glm::vec4 colorAt(const FieldSpec& f, const glm::vec3& p, float t, const FieldSet* set, int depth) {
    if (!f.enabled || depth > kMaxDepth) {
        return glm::vec4(0.0f);
    }
    const glm::vec3 q = glm::vec3(f.worldToLocal() * glm::vec4(p, 1.0f));
    const float w = fieldWeight(f, q);
    if (f.kind == FieldKind::Grid) {
        const GridField* g = boundGrid(f, set);
        if (g != nullptr && g->mode == GridMode::Vector) {
            return glm::vec4(vectorAt(f, p, t, set, depth) * 0.5f + 0.5f, w);
        }
        const float s = scalarAt(f, p, t, set, depth);
        return glm::vec4(glm::mix(glm::vec3(f.colorA), glm::vec3(f.colorB), saturate(s)), w);
    }
    if (f.kind == FieldKind::Compound) {
        std::vector<glm::vec3> rgb;
        float alpha = 0.0f;
        for (const FieldSpec* child : compoundChildren(f, set)) {
            const glm::vec4 c = colorAt(*child, p, t, set, depth + 1);
            rgb.push_back(glm::vec3(c));
            alpha = std::max(alpha, c.a);
        }
        glm::vec3 v = combineValues(f.combine, f.mix, rgb);
        if (f.invert) {
            v = glm::vec3(1.0f) - v;
        }
        return glm::vec4(v, alpha * w);
    }
    switch (f.type()) {
    case FieldType::Scalar: {
        const float s = scalarAt(f, p, t, set, depth);
        return glm::vec4(glm::mix(glm::vec3(f.colorA), glm::vec3(f.colorB), saturate(s)), w);
    }
    case FieldType::Vector: {
        const glm::vec3 v = vectorAt(f, p, t, set, depth);
        return glm::vec4(v * 0.5f + 0.5f, w);
    }
    case FieldType::Color:
        break;
    }
    const glm::vec3 a = glm::vec3(f.invert ? f.colorB : f.colorA);
    const glm::vec3 b = glm::vec3(f.invert ? f.colorA : f.colorB);
    return glm::vec4(colorShape(f, q, f.speed * t + f.phase, a, b), w);
}

} // namespace

// ================================================================================================
// Names
// ================================================================================================

const char* fieldKindName(FieldKind kind) {
    switch (kind) {
    case FieldKind::Constant:
        return "constant";
    case FieldKind::LinearGradient:
        return "linearGradient";
    case FieldKind::Radial:
        return "radial";
    case FieldKind::Box:
        return "box";
    case FieldKind::Sphere:
        return "sphere";
    case FieldKind::Plane:
        return "plane";
    case FieldKind::Noise:
        return "noise";
    case FieldKind::Voronoi:
        return "voronoi";
    case FieldKind::Distance:
        return "distance";
    case FieldKind::SdfDistance:
        return "sdfDistance";
    case FieldKind::Wave:
        return "wave";
    case FieldKind::Direction:
        return "direction";
    case FieldKind::RadialVector:
        return "radialVector";
    case FieldKind::Attractor:
        return "attractor";
    case FieldKind::Repulsor:
        return "repulsor";
    case FieldKind::Vortex:
        return "vortex";
    case FieldKind::CurlNoise:
        return "curlNoise";
    case FieldKind::Spiral:
        return "spiral";
    case FieldKind::WaveVector:
        return "waveVector";
    case FieldKind::ConstantColor:
        return "constantColor";
    case FieldKind::Gradient:
        return "gradient";
    case FieldKind::RadialGradient:
        return "radialGradient";
    case FieldKind::NoiseColor:
        return "noiseColor";
    case FieldKind::PositionColor:
        return "positionColor";
    case FieldKind::Compound:
        return "compound";
    case FieldKind::Grid:
        return "grid";
    }
    return "radial";
}

namespace {
constexpr FieldKind kAllFieldKinds[] = {
    FieldKind::Constant,      FieldKind::LinearGradient, FieldKind::Radial,        FieldKind::Box,
    FieldKind::Sphere,        FieldKind::Plane,          FieldKind::Noise,         FieldKind::Voronoi,
    FieldKind::Distance,      FieldKind::SdfDistance,    FieldKind::Wave,          FieldKind::Direction,
    FieldKind::RadialVector,  FieldKind::Attractor,      FieldKind::Repulsor,      FieldKind::Vortex,
    FieldKind::CurlNoise,     FieldKind::Spiral,         FieldKind::WaveVector,    FieldKind::ConstantColor,
    FieldKind::Gradient,      FieldKind::RadialGradient, FieldKind::NoiseColor,    FieldKind::PositionColor,
    FieldKind::Compound,      FieldKind::Grid,
};
}

std::optional<FieldKind> fieldKindFromName(std::string_view name) {
    for (const FieldKind kind : kAllFieldKinds) {
        if (name == fieldKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

FieldType fieldTypeOf(FieldKind kind) {
    switch (kind) {
    case FieldKind::Direction:
    case FieldKind::RadialVector:
    case FieldKind::Attractor:
    case FieldKind::Repulsor:
    case FieldKind::Vortex:
    case FieldKind::CurlNoise:
    case FieldKind::Spiral:
    case FieldKind::WaveVector:
        return FieldType::Vector;
    case FieldKind::ConstantColor:
    case FieldKind::Gradient:
    case FieldKind::RadialGradient:
    case FieldKind::NoiseColor:
    case FieldKind::PositionColor:
        return FieldType::Color;
    default:
        return FieldType::Scalar;
    }
}

const char* fieldTypeName(FieldType type) {
    switch (type) {
    case FieldType::Scalar:
        return "scalar";
    case FieldType::Vector:
        return "vector";
    case FieldType::Color:
        return "color";
    }
    return "scalar";
}

const char* falloffKindName(FalloffKind kind) {
    switch (kind) {
    case FalloffKind::None:
        return "none";
    case FalloffKind::Linear:
        return "linear";
    case FalloffKind::Smoothstep:
        return "smoothstep";
    case FalloffKind::Smooth:
        return "smooth";
    case FalloffKind::EaseIn:
        return "easeIn";
    case FalloffKind::EaseOut:
        return "easeOut";
    case FalloffKind::EaseInOut:
        return "easeInOut";
    case FalloffKind::Exponential:
        return "exponential";
    case FalloffKind::CustomCurve:
        return "customCurve";
    case FalloffKind::NoiseModulated:
        return "noiseModulated";
    }
    return "none";
}

std::optional<FalloffKind> falloffKindFromName(std::string_view name) {
    for (const auto kind : {FalloffKind::None, FalloffKind::Linear, FalloffKind::Smoothstep, FalloffKind::Smooth,
                            FalloffKind::EaseIn, FalloffKind::EaseOut, FalloffKind::EaseInOut, FalloffKind::Exponential,
                            FalloffKind::CustomCurve, FalloffKind::NoiseModulated}) {
        if (name == falloffKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

const char* fieldSpaceName(FieldSpace space) {
    return space == FieldSpace::Local ? "local" : "world";
}

std::optional<FieldSpace> fieldSpaceFromName(std::string_view name) {
    for (const auto space : {FieldSpace::World, FieldSpace::Local}) {
        if (name == fieldSpaceName(space)) {
            return space;
        }
    }
    return std::nullopt;
}

const char* waveGeometryName(WaveGeometry geometry) {
    switch (geometry) {
    case WaveGeometry::Planar:
        return "planar";
    case WaveGeometry::Radial:
        return "radial";
    case WaveGeometry::Spherical:
        return "spherical";
    case WaveGeometry::Cylindrical:
        return "cylindrical";
    }
    return "radial";
}

std::optional<WaveGeometry> waveGeometryFromName(std::string_view name) {
    for (const auto g : {WaveGeometry::Planar, WaveGeometry::Radial, WaveGeometry::Spherical, WaveGeometry::Cylindrical}) {
        if (name == waveGeometryName(g)) {
            return g;
        }
    }
    return std::nullopt;
}

const char* waveShapeName(WaveShape shape) {
    switch (shape) {
    case WaveShape::Sine:
        return "sine";
    case WaveShape::Pulse:
        return "pulse";
    case WaveShape::Triangle:
        return "triangle";
    }
    return "sine";
}

std::optional<WaveShape> waveShapeFromName(std::string_view name) {
    for (const auto s : {WaveShape::Sine, WaveShape::Pulse, WaveShape::Triangle}) {
        if (name == waveShapeName(s)) {
            return s;
        }
    }
    return std::nullopt;
}

const char* fieldCombineName(FieldCombine combine) {
    switch (combine) {
    case FieldCombine::Add:
        return "add";
    case FieldCombine::Multiply:
        return "multiply";
    case FieldCombine::Max:
        return "max";
    case FieldCombine::Min:
        return "min";
    case FieldCombine::Mix:
        return "mix";
    case FieldCombine::Average:
        return "average";
    }
    return "add";
}

std::optional<FieldCombine> fieldCombineFromName(std::string_view name) {
    for (const auto c : {FieldCombine::Add, FieldCombine::Multiply, FieldCombine::Max, FieldCombine::Min, FieldCombine::Mix,
                         FieldCombine::Average}) {
        if (name == fieldCombineName(c)) {
            return c;
        }
    }
    return std::nullopt;
}

// ================================================================================================
// Falloff
// ================================================================================================

float falloffCurve(FalloffKind kind, float t, float exponent, const glm::vec4& curve) {
    t = saturate(t);
    switch (kind) {
    case FalloffKind::None:
        return 1.0f;
    case FalloffKind::Linear:
        return 1.0f - t;
    case FalloffKind::Smoothstep:
    case FalloffKind::NoiseModulated:
        return 1.0f - t * t * (3.0f - 2.0f * t);
    case FalloffKind::Smooth:
        return 1.0f - t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
    case FalloffKind::EaseIn:
        return 1.0f - t * t * t;
    case FalloffKind::EaseOut: {
        const float u = 1.0f - t;
        return u * u * u;
    }
    case FalloffKind::EaseInOut: {
        const float u = -2.0f * t + 2.0f;
        return 1.0f - (t < 0.5f ? 4.0f * t * t * t : 1.0f - u * u * u * 0.5f);
    }
    case FalloffKind::Exponential:
        return std::pow(1.0f - t, exponent);
    case FalloffKind::CustomCurve: {
        // Cubic Bezier on y with control values 1, curve.x, curve.y, curve.z; x treated as t.
        const float u = 1.0f - t;
        return u * u * u * 1.0f + 3.0f * u * u * t * curve.x + 3.0f * u * t * t * curve.y + t * t * t * curve.z;
    }
    }
    return 1.0f;
}

float Falloff::weight(float distance, const glm::vec3& p, std::uint32_t seed) const {
    if (kind == FalloffKind::None) {
        return 1.0f;
    }
    if (outer <= inner) {
        return distance <= inner ? 1.0f : 0.0f;
    }
    if (distance <= inner) {
        return 1.0f;
    }
    if (distance >= outer) {
        return 0.0f;
    }
    const float t = (distance - inner) / (outer - inner);
    if (kind == FalloffKind::NoiseModulated) {
        const float n = noise::fbm3(p * noiseScale, seed) * 2.0f - 1.0f;
        return falloffCurve(FalloffKind::Smoothstep, t, exponent, curve) * saturate(1.0f + noiseAmount * n);
    }
    return falloffCurve(kind, t, exponent, curve);
}

// ================================================================================================
// FieldSpec
// ================================================================================================

glm::mat4 FieldSpec::localToWorld() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(fieldRotation(*this)) *
           glm::scale(glm::mat4(1.0f), scale);
}

glm::mat4 FieldSpec::worldToLocal() const {
    return glm::inverse(localToWorld());
}

Result<void> FieldSpec::validate() const {
    if (name.empty()) {
        return fail("field needs a name");
    }
    if (!(scale.x != 0.0f && scale.y != 0.0f && scale.z != 0.0f)) {
        return fail("field '{}': scale must be non-zero on every axis", name);
    }
    if (falloff.inner < 0.0f || falloff.outer < 0.0f) {
        return fail("field '{}': falloff radii must be >= 0", name);
    }
    if (falloff.outer < falloff.inner) {
        return fail("field '{}': falloff outer must be >= inner", name);
    }
    if (!(radius > 0.0f)) {
        return fail("field '{}': radius must be > 0", name);
    }
    if (!(length > 0.0f)) {
        return fail("field '{}': length must be > 0", name);
    }
    if (softness < 0.0f) {
        return fail("field '{}': softness must be >= 0", name);
    }
    if (frequency < 0.0f) {
        return fail("field '{}': frequency must be >= 0", name);
    }
    if (!(wavelength > 0.0f)) {
        return fail("field '{}': wavelength must be > 0", name);
    }
    if (waveWidth < 0.0f) {
        return fail("field '{}': waveWidth must be >= 0", name);
    }
    if (kind == FieldKind::Compound) {
        if (children.size() > static_cast<std::size_t>(kMaxCompoundChildren)) {
            return fail("field '{}': at most {} compound children (got {})", name, kMaxCompoundChildren, children.size());
        }
        for (const std::string& child : children) {
            if (child.empty()) {
                return fail("field '{}': compound child names must not be empty", name);
            }
            if (child == name) {
                return fail("field '{}': a compound cannot contain itself", name);
            }
        }
    }
    // ADR-576: `sdfDistance` REFUSES rather than returning zero.
    //
    // It is a declared kind that evaluates to 0 on the CPU (`sampleScalar` below) and 0 on the GPU
    // (`fields.wgsl` returns 0 for it), so a density field using it produced a density of zero --
    // which for the volumetric march means NO FOG AT ALL. A scene author selected it from a
    // documented list, got an empty sky, and had no way to learn why. The system answered a
    // question with silence.
    //
    // The kind is not vestigial and is deliberately not removed: `Scene::sdfs` exists and
    // `SdfRenderer` draws it, so the binding target is real and ADR-027 reserved this seam on
    // purpose. What is missing is the binding, and that is a feature decision. Refusing costs one
    // branch and turns an hour of debugging somebody's fog density into a message.
    if (kind == FieldKind::SdfDistance) {
        return fail("field '{}': kind 'sdfDistance' is declared but not implemented -- it "
                    "evaluates to 0 everywhere, on the CPU and on the GPU, so a density field "
                    "using it produces nothing at all. ADR-027 reserved the kind and never bound "
                    "it. Use 'distance' for a point, 'sphere' or 'box' for a volume, or ask for "
                    "the SDF binding to be built",
                    name);
    }
    if (kind == FieldKind::Grid && reference.empty()) {
        return fail("field '{}': grid needs a 'reference' (the grid's name)", name);
    }
    return {};
}

std::uint64_t FieldSpec::structuralHash() const {
    detail::Fnv h;
    h.str(name);
    h.u8(static_cast<std::uint8_t>(kind));
    h.boolean(enabled);
    h.u8(static_cast<std::uint8_t>(space));
    h.v3(position);
    h.v3(rotationDegrees);
    h.v3(scale);
    h.f32(strength);
    h.boolean(invert);
    h.u8(static_cast<std::uint8_t>(falloff.kind));
    h.f32(falloff.inner);
    h.f32(falloff.outer);
    h.f32(falloff.exponent);
    h.v4(falloff.curve);
    h.f32(falloff.noiseAmount);
    h.f32(falloff.noiseScale);
    h.f32(speed);
    h.f32(phase);
    h.v3(axis);
    h.v3(point);
    h.f32(radius);
    h.f32(length);
    h.v3(size);
    h.f32(softness);
    h.f32(frequency);
    h.u32(seed);
    h.f32(spiralBias);
    h.u8(static_cast<std::uint8_t>(waveGeometry));
    h.u8(static_cast<std::uint8_t>(waveShape));
    h.f32(amplitude);
    h.f32(wavelength);
    h.f32(waveSpeed);
    h.f32(waveWidth);
    h.f32(waveOrigin);
    h.v4(colorA);
    h.v4(colorB);
    h.u64(children.size());
    for (const std::string& child : children) {
        h.str(child);
    }
    h.u8(static_cast<std::uint8_t>(combine));
    h.f32(mix);
    h.str(reference);
    return h.value();
}

json FieldSpec::toJson() const {
    json j = json::object();
    j["name"] = name;
    j["kind"] = fieldKindName(kind);
    j["enabled"] = enabled;
    j["space"] = fieldSpaceName(space);
    j["position"] = detail::vecToJson(position);
    j["rotation"] = detail::vecToJson(rotationDegrees);
    j["scale"] = detail::vecToJson(scale);
    j["strength"] = strength;
    j["invert"] = invert;
    {
        json f = json::object();
        f["kind"] = falloffKindName(falloff.kind);
        f["inner"] = falloff.inner;
        f["outer"] = falloff.outer;
        f["exponent"] = falloff.exponent;
        f["curve"] = detail::vecToJson(falloff.curve);
        f["noiseAmount"] = falloff.noiseAmount;
        f["noiseScale"] = falloff.noiseScale;
        j["falloff"] = std::move(f);
    }
    j["speed"] = speed;
    j["phase"] = phase;
    j["axis"] = detail::vecToJson(axis);
    j["point"] = detail::vecToJson(point);
    j["radius"] = radius;
    j["length"] = length;
    j["size"] = detail::vecToJson(size);
    j["softness"] = softness;
    j["frequency"] = frequency;
    j["seed"] = seed;
    j["spiralBias"] = spiralBias;
    j["waveGeometry"] = waveGeometryName(waveGeometry);
    j["waveShape"] = waveShapeName(waveShape);
    j["amplitude"] = amplitude;
    j["wavelength"] = wavelength;
    j["waveSpeed"] = waveSpeed;
    j["waveWidth"] = waveWidth;
    j["waveOrigin"] = waveOrigin;
    j["colorA"] = detail::vecToJson(colorA);
    j["colorB"] = detail::vecToJson(colorB);
    j["children"] = children;
    j["combine"] = fieldCombineName(combine);
    j["mix"] = mix;
    j["reference"] = reference;
    return j;
}

Result<FieldSpec> FieldSpec::fromJson(const json& root) {
    if (!root.is_object()) {
        return fail("field must be a JSON object");
    }
    FieldSpec f;
    {
        const json& j = root;
        AVGEN_SPATIAL_READ(f.name, "name", detail::readString);
        AVGEN_SPATIAL_READ_ENUM(f.kind, "kind", &fieldKindFromName, "field kind");
        AVGEN_SPATIAL_READ(f.enabled, "enabled", detail::readBool);
        AVGEN_SPATIAL_READ_ENUM(f.space, "space", &fieldSpaceFromName, "field space");
        AVGEN_SPATIAL_READ(f.position, "position", detail::readVec3);
        AVGEN_SPATIAL_READ(f.rotationDegrees, "rotation", detail::readVec3);
        AVGEN_SPATIAL_READ(f.scale, "scale", detail::readVec3);
        AVGEN_SPATIAL_READ(f.strength, "strength", detail::readFloat);
        AVGEN_SPATIAL_READ(f.invert, "invert", detail::readBool);
        AVGEN_SPATIAL_READ(f.speed, "speed", detail::readFloat);
        AVGEN_SPATIAL_READ(f.phase, "phase", detail::readFloat);
        AVGEN_SPATIAL_READ(f.axis, "axis", detail::readVec3);
        AVGEN_SPATIAL_READ(f.point, "point", detail::readVec3);
        AVGEN_SPATIAL_READ(f.radius, "radius", detail::readFloat);
        AVGEN_SPATIAL_READ(f.length, "length", detail::readFloat);
        AVGEN_SPATIAL_READ(f.size, "size", detail::readVec3);
        AVGEN_SPATIAL_READ(f.softness, "softness", detail::readFloat);
        AVGEN_SPATIAL_READ(f.frequency, "frequency", detail::readFloat);
        AVGEN_SPATIAL_READ(f.seed, "seed", detail::readU32);
        AVGEN_SPATIAL_READ(f.spiralBias, "spiralBias", detail::readFloat);
        AVGEN_SPATIAL_READ_ENUM(f.waveGeometry, "waveGeometry", &waveGeometryFromName, "wave geometry");
        AVGEN_SPATIAL_READ_ENUM(f.waveShape, "waveShape", &waveShapeFromName, "wave shape");
        AVGEN_SPATIAL_READ(f.amplitude, "amplitude", detail::readFloat);
        AVGEN_SPATIAL_READ(f.wavelength, "wavelength", detail::readFloat);
        AVGEN_SPATIAL_READ(f.waveSpeed, "waveSpeed", detail::readFloat);
        AVGEN_SPATIAL_READ(f.waveWidth, "waveWidth", detail::readFloat);
        AVGEN_SPATIAL_READ(f.waveOrigin, "waveOrigin", detail::readFloat);
        AVGEN_SPATIAL_READ(f.colorA, "colorA", detail::readVec4);
        AVGEN_SPATIAL_READ(f.colorB, "colorB", detail::readVec4);
        AVGEN_SPATIAL_READ_ENUM(f.combine, "combine", &fieldCombineFromName, "field combine");
        AVGEN_SPATIAL_READ(f.mix, "mix", detail::readFloat);
        AVGEN_SPATIAL_READ(f.reference, "reference", detail::readString);
    }
    if (root.contains("falloff")) {
        const json& j = root.at("falloff");
        if (!j.is_object()) {
            return fail("'falloff' must be an object");
        }
        AVGEN_SPATIAL_READ_ENUM(f.falloff.kind, "kind", &falloffKindFromName, "falloff kind");
        AVGEN_SPATIAL_READ(f.falloff.inner, "inner", detail::readFloat);
        AVGEN_SPATIAL_READ(f.falloff.outer, "outer", detail::readFloat);
        AVGEN_SPATIAL_READ(f.falloff.exponent, "exponent", detail::readFloat);
        AVGEN_SPATIAL_READ(f.falloff.curve, "curve", detail::readVec4);
        AVGEN_SPATIAL_READ(f.falloff.noiseAmount, "noiseAmount", detail::readFloat);
        AVGEN_SPATIAL_READ(f.falloff.noiseScale, "noiseScale", detail::readFloat);
    }
    if (root.contains("children")) {
        const json& arr = root.at("children");
        if (!arr.is_array()) {
            return fail("'children' must be an array of field names");
        }
        f.children.clear();
        for (const json& c : arr) {
            if (!c.is_string()) {
                return fail("'children' must be an array of field names");
            }
            f.children.push_back(c.get<std::string>());
        }
    }
    if (auto ok = f.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return f;
}

// ================================================================================================
// FieldSet and sampling
// ================================================================================================

const FieldSpec* FieldSet::find(std::string_view name) const {
    for (const FieldSpec& f : fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

const GridField* FieldSet::findGrid(std::string_view name) const {
    for (const GridField& g : grids) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

int FieldSet::gridIndexOf(std::string_view name) const {
    for (std::size_t i = 0; i < grids.size(); ++i) {
        if (grids[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FieldSet::indexOf(std::string_view name) const {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float sampleScalar(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set) {
    return scalarAt(field, p, static_cast<float>(time), set, 0);
}

glm::vec3 sampleVector(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set) {
    return vectorAt(field, p, static_cast<float>(time), set, 0);
}

glm::vec4 sampleColor(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set) {
    return colorAt(field, p, static_cast<float>(time), set, 0);
}

float sampleWeight(const FieldSpec& field, const glm::vec3& p, double /*time*/) {
    const glm::vec3 q = glm::vec3(field.worldToLocal() * glm::vec4(p, 1.0f));
    return fieldWeight(field, q);
}

// ================================================================================================
// GPU packing
// ================================================================================================

FieldGpu packField(const FieldSpec& field, double time, const FieldSet* set) {
    const auto t = static_cast<float>(time);
    const Frame frame = frameOf(field);
    FieldGpu g{};
    g.kind = static_cast<std::uint32_t>(field.kind);
    g.type = static_cast<std::uint32_t>(field.type());
    g.falloffKind = static_cast<std::uint32_t>(field.falloff.kind);
    g.seed = field.seed;
    g.worldToLocal = frame.worldToLocal;
    const glm::mat3& r = frame.rotation; // r[column][row]
    g.localToWorldRow0 = glm::vec4(r[0][0], r[1][0], r[2][0], 0.0f);
    g.localToWorldRow1 = glm::vec4(r[0][1], r[1][1], r[2][1], 0.0f);
    g.localToWorldRow2 = glm::vec4(r[0][2], r[1][2], r[2][2], 0.0f);
    g.strengthInnerOuterTau = glm::vec4(field.strength, field.falloff.inner, field.falloff.outer, field.speed * t + field.phase);
    g.axisRadius = glm::vec4(field.axis, field.radius);
    g.pointLength = glm::vec4(field.point, field.length);
    g.sizeSoftness = glm::vec4(field.size, field.softness);
    g.freqExpInvertBias = glm::vec4(field.frequency, field.falloff.exponent, field.invert ? 1.0f : 0.0f, field.spiralBias);
    g.wave0 = glm::vec4(field.amplitude, field.wavelength, field.waveSpeed, field.waveWidth);
    g.wave1 = glm::vec4(field.waveOrigin, static_cast<float>(static_cast<int>(field.waveGeometry)),
                        static_cast<float>(static_cast<int>(field.waveShape)), t);
    g.colorA = field.colorA;
    g.colorB = field.colorB;
    g.curve = field.falloff.curve;
    g.noiseCombineMix = glm::vec4(field.falloff.noiseAmount, field.falloff.noiseScale,
                                  static_cast<float>(static_cast<int>(field.combine)), field.mix);
    if (field.kind == FieldKind::Grid && set != nullptr) {
        const int gi = set->gridIndexOf(field.reference);
        if (gi >= 0) {
            const GridField& grid = set->grids[static_cast<std::size_t>(gi)];
            if (grid.enabled && grid.floatCount() > 0) {
                const auto offset = static_cast<float>(gridTableOffset(set->grids, static_cast<std::size_t>(gi)));
                g.gridBounds0 = glm::vec4(grid.boundsMin, offset);
                g.gridBounds1 = glm::vec4(grid.boundsMax, static_cast<float>(grid.components()));
                g.gridRes = glm::vec4(static_cast<float>(grid.resolution.x), static_cast<float>(grid.resolution.y),
                                      static_cast<float>(grid.resolution.z),
                                      grid.wrap == GridWrap::Wrap ? 3.0f : 1.0f);
                // The bound grid's mode decides how the record reads across types.
                g.type = static_cast<std::uint32_t>(grid.mode == GridMode::Vector ? FieldType::Vector
                                                                                 : FieldType::Scalar);
            }
        }
    }
    g.children = glm::ivec4(-1);
    for (int c = 0; c < kMaxCompoundChildren; ++c) {
        const auto i = static_cast<std::size_t>(c);
        if (set != nullptr && i < field.children.size()) {
            g.children[c] = set->indexOf(field.children[i]);
        }
    }
    return g;
}

} // namespace avgen::spatial

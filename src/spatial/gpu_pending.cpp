// TEMPORARY (GPU wave of the spatial phase): minimal CPU implementations of the parts of the
// spatial API the GPU side needs before the CPU wave (src/spatial/*.cpp) lands. Discarded at
// merge; the real implementations replace every function here. Keep it small.
//
//   - spatial::packField / sampleScalar / sampleVector / sampleColor / sampleWeight: the CPU
//     reference the GPU parity test compares against. Implemented on the packed FieldGpu record
//     so it evaluates exactly the expressions shaders/fields.wgsl evaluates, in the same order.
//   - spatial::packEffector / applyEffectorsToRecords: the effector reference for points.wgsl.
//   - spatial::PointCloud::PointCloud, scene::deformPoint (6-arg), scene::applyFieldDeformer:
//     link stubs for symbols declared by the wave-1 headers.
#include "core/noise.hpp"
#include "scene/procedural.hpp"
#include "spatial/effector.hpp"
#include "spatial/field.hpp"
#include "spatial/point_cloud.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace avgen::spatial {

PointCloud::PointCloud() = default;

const char* fieldKindName(FieldKind kind) {
    static constexpr const char* names[] = {
        "constant",      "linearGradient", "radial",       "box",       "sphere",     "plane",        "noise",
        "voronoi",       "distance",       "sdfDistance",  "wave",      "direction",  "radialVector", "attractor",
        "repulsor",      "vortex",         "curlNoise",    "spiral",    "waveVector", "constantColor", "gradient",
        "radialGradient", "noiseColor",    "positionColor", "compound"};
    const auto i = static_cast<std::size_t>(kind);
    return i < std::size(names) ? names[i] : "unknown";
}

FieldType fieldTypeOf(FieldKind kind) {
    const auto k = static_cast<std::uint8_t>(kind);
    if (k >= static_cast<std::uint8_t>(FieldKind::ConstantColor) && k <= static_cast<std::uint8_t>(FieldKind::PositionColor)) {
        return FieldType::Color;
    }
    if (k >= static_cast<std::uint8_t>(FieldKind::Direction) && k <= static_cast<std::uint8_t>(FieldKind::WaveVector)) {
        return FieldType::Vector;
    }
    return FieldType::Scalar;
}

glm::mat4 FieldSpec::localToWorld() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(glm::quat(glm::radians(rotationDegrees))) *
           glm::scale(glm::mat4(1.0f), scale);
}

glm::mat4 FieldSpec::worldToLocal() const {
    return glm::inverse(localToWorld());
}

const FieldSpec* FieldSet::find(std::string_view name) const {
    for (const auto& f : fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

int FieldSet::indexOf(std::string_view name) const {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

float sat(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}
float fract(float x) {
    return x - std::floor(x);
}
glm::vec3 fract3(const glm::vec3& v) {
    return glm::vec3(fract(v.x), fract(v.y), fract(v.z));
}
float wmix(float a, float b, float t) {
    return a * (1.0f - t) + b * t;
}
glm::vec3 wmix(const glm::vec3& a, const glm::vec3& b, float t) {
    return a * (1.0f - t) + b * t;
}
glm::vec4 wmix(const glm::vec4& a, const glm::vec4& b, float t) {
    return a * (1.0f - t) + b * t;
}
glm::vec3 nrm(const glm::vec3& v) {
    const float l2 = glm::dot(v, v);
    return l2 > 1e-20f ? v / std::sqrt(l2) : glm::vec3(0.0f);
}
float smoothstepFalloff(float t) {
    return 1.0f - t * t * (3.0f - 2.0f * t);
}
float luminance(const glm::vec3& c) {
    return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

glm::vec3 axisOf(const FieldGpu& f) {
    return nrm(glm::vec3(f.axisRadius));
}
glm::vec3 pointOf(const FieldGpu& f) {
    return glm::vec3(f.pointLength);
}
glm::vec3 localOf(const FieldGpu& f, const glm::vec3& p) {
    return glm::vec3(f.worldToLocal * glm::vec4(p, 1.0f));
}
glm::vec3 toWorld(const FieldGpu& f, const glm::vec3& v) {
    return glm::vec3(glm::dot(glm::vec3(f.localToWorldRow0), v), glm::dot(glm::vec3(f.localToWorldRow1), v),
                     glm::dot(glm::vec3(f.localToWorldRow2), v));
}
bool inverted(const FieldGpu& f) {
    return f.freqExpInvertBias.z > 0.5f;
}
glm::vec3 noiseOffset(const FieldGpu& f) {
    return f.strengthInnerOuterTau.w * glm::vec3(1.0f, 0.7f, 1.3f);
}

float curveValue(FalloffKind kind, float t, float exponent, const glm::vec4& curve, float noiseAmount, float noiseScale,
                 const glm::vec3& p, std::uint32_t seed) {
    switch (kind) {
    case FalloffKind::None:
        return 1.0f;
    case FalloffKind::Linear:
        return 1.0f - t;
    case FalloffKind::Smoothstep:
        return smoothstepFalloff(t);
    case FalloffKind::Smooth:
        return 1.0f - t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
    case FalloffKind::EaseIn:
        return 1.0f - t * t * t;
    case FalloffKind::EaseOut: {
        const float u = 1.0f - t;
        return u * u * u;
    }
    case FalloffKind::EaseInOut: {
        const float a = 4.0f * t * t * t;
        const float b = -2.0f * t + 2.0f;
        const float c = 1.0f - b * b * b / 2.0f;
        return 1.0f - (t < 0.5f ? a : c);
    }
    case FalloffKind::Exponential:
        return std::pow(1.0f - t, exponent);
    case FalloffKind::CustomCurve: {
        const float b01 = wmix(1.0f, curve.x, t);
        const float b12 = wmix(curve.x, curve.y, t);
        const float b23 = wmix(curve.y, curve.z, t);
        const float b012 = wmix(b01, b12, t);
        const float b123 = wmix(b12, b23, t);
        return wmix(b012, b123, t);
    }
    case FalloffKind::NoiseModulated: {
        const float n = noise::fbm3(p * noiseScale, seed) * 2.0f - 1.0f;
        return smoothstepFalloff(t) * sat(1.0f + noiseAmount * n);
    }
    }
    return 1.0f;
}

float falloffWeightRec(const FieldGpu& f, float d, const glm::vec3& p) {
    const auto kind = static_cast<FalloffKind>(f.falloffKind);
    if (kind == FalloffKind::None) {
        return 1.0f;
    }
    const float inner = f.strengthInnerOuterTau.y;
    const float outer = f.strengthInnerOuterTau.z;
    if (d <= inner) {
        return 1.0f;
    }
    if (d >= outer) {
        return 0.0f;
    }
    const float t = (d - inner) / (outer - inner);
    return curveValue(kind, t, f.freqExpInvertBias.y, f.curve, f.noiseCombineMix.x, f.noiseCombineMix.y, p, f.seed);
}

float distanceRec(const FieldGpu& f, const glm::vec3& q) {
    const auto kind = static_cast<FieldKind>(f.kind);
    if (kind == FieldKind::LinearGradient || kind == FieldKind::Plane || kind == FieldKind::Direction ||
        kind == FieldKind::Gradient) {
        return std::abs(glm::dot(q, axisOf(f)));
    }
    if (kind == FieldKind::Box) {
        const glm::vec3 e = glm::abs(q) - glm::vec3(f.sizeSoftness);
        return std::max(std::max(e.x, std::max(e.y, e.z)), 0.0f);
    }
    return glm::length(q - pointOf(f));
}

float weightOf(const FieldGpu& f, const glm::vec3& q, const glm::vec3& p) {
    return f.strengthInnerOuterTau.x * falloffWeightRec(f, distanceRec(f, q), p);
}

float waveDistance(const FieldGpu& f, const glm::vec3& q) {
    const auto geometry = static_cast<std::uint32_t>(f.wave1.y + 0.5f);
    const glm::vec3 n = axisOf(f);
    if (geometry == 0) {
        return glm::dot(q, n);
    }
    const glm::vec3 r = q - pointOf(f);
    if (geometry == 2) {
        return glm::length(r);
    }
    return glm::length(r - n * glm::dot(r, n));
}

glm::vec3 waveDirection(const FieldGpu& f, const glm::vec3& q) {
    const auto geometry = static_cast<std::uint32_t>(f.wave1.y + 0.5f);
    const glm::vec3 n = axisOf(f);
    if (geometry == 0) {
        return n;
    }
    const glm::vec3 r = q - pointOf(f);
    if (geometry == 2) {
        return nrm(r);
    }
    return nrm(r - n * glm::dot(r, n));
}

float waveValue(const FieldGpu& f, const glm::vec3& q) {
    const float t = f.wave1.w;
    const float s = waveDistance(f, q) - f.wave1.x - f.wave0.z * t;
    const float width = f.wave0.w;
    float envelope = 1.0f;
    if (width > 0.0f) {
        envelope = smoothstepFalloff(sat(std::abs(s) / width));
    }
    const float k = kTwoPi / std::max(f.wave0.y, 1e-6f);
    const auto shape = static_cast<std::uint32_t>(f.wave1.z + 0.5f);
    float v = std::sin(k * s);
    if (shape == 1) {
        const float ks = k * s;
        v = std::exp(-(ks * ks));
    } else if (shape == 2) {
        v = 4.0f * std::abs(fract(k * s / kTwoPi + 0.75f) - 0.5f) - 1.0f;
    }
    return f.wave0.x * envelope * v;
}

float scalarShape(const FieldGpu& f, const glm::vec3& q) {
    switch (static_cast<FieldKind>(f.kind)) {
    case FieldKind::Constant:
        return 1.0f;
    case FieldKind::LinearGradient:
        return sat(glm::dot(q, axisOf(f)) / std::max(f.pointLength.w, 1e-6f) + 0.5f);
    case FieldKind::Radial:
        return 1.0f - sat(glm::length(q - pointOf(f)) / std::max(f.axisRadius.w, 1e-6f));
    case FieldKind::Box: {
        const glm::vec3 e = glm::abs(q) - glm::vec3(f.sizeSoftness);
        return 1.0f - sat(std::max(std::max(e.x, std::max(e.y, e.z)), 0.0f) / std::max(f.sizeSoftness.w, 1e-6f));
    }
    case FieldKind::Sphere:
        return 1.0f - sat((glm::length(q - pointOf(f)) - f.axisRadius.w) / std::max(f.sizeSoftness.w, 1e-6f));
    case FieldKind::Plane:
        return sat(glm::dot(q, axisOf(f)) / std::max(f.sizeSoftness.w, 1e-6f));
    case FieldKind::Noise:
        return noise::fbm3(q * f.freqExpInvertBias.x + noiseOffset(f), f.seed);
    case FieldKind::Voronoi:
        return sat(noise::voronoiF1(q * f.freqExpInvertBias.x + noiseOffset(f), f.seed));
    case FieldKind::Distance:
        return sat(glm::length(q - pointOf(f)) / std::max(f.axisRadius.w, 1e-6f));
    case FieldKind::Wave:
        return waveValue(f, q);
    default:
        return 0.0f;
    }
}

glm::vec3 vectorDirection(const FieldGpu& f, const glm::vec3& q) {
    const glm::vec3 n = axisOf(f);
    const glm::vec3 r = q - pointOf(f);
    switch (static_cast<FieldKind>(f.kind)) {
    case FieldKind::Direction:
        return n;
    case FieldKind::RadialVector:
    case FieldKind::Repulsor:
        return nrm(r);
    case FieldKind::Attractor:
        return nrm(-r);
    case FieldKind::Vortex:
        return nrm(glm::cross(n, r));
    case FieldKind::CurlNoise:
        return noise::curlNoise(q * f.freqExpInvertBias.x + noiseOffset(f), f.seed);
    case FieldKind::Spiral:
        return nrm(nrm(glm::cross(n, r)) + f.freqExpInvertBias.w * nrm(r));
    case FieldKind::WaveVector:
        return waveValue(f, q) * waveDirection(f, q);
    default:
        return glm::vec3(0.0f);
    }
}

glm::vec3 colorShape(const FieldGpu& f, const glm::vec3& q, const glm::vec4& a, const glm::vec4& b) {
    switch (static_cast<FieldKind>(f.kind)) {
    case FieldKind::ConstantColor:
        return glm::vec3(a);
    case FieldKind::Gradient:
        return wmix(glm::vec3(a), glm::vec3(b), sat(glm::dot(q, axisOf(f)) / std::max(f.pointLength.w, 1e-6f) + 0.5f));
    case FieldKind::RadialGradient:
        return wmix(glm::vec3(a), glm::vec3(b), sat(glm::length(q - pointOf(f)) / std::max(f.axisRadius.w, 1e-6f)));
    case FieldKind::NoiseColor:
        return wmix(glm::vec3(a), glm::vec3(b), noise::fbm3(q * f.freqExpInvertBias.x + noiseOffset(f), f.seed));
    case FieldKind::PositionColor:
        return fract3(q * f.freqExpInvertBias.x);
    default:
        return glm::vec3(0.0f);
    }
}

float ownScalar(const FieldGpu& f, const glm::vec3& q, float w) {
    float v = scalarShape(f, q);
    if (inverted(f)) {
        v = 1.0f - v;
    }
    return v * w;
}
glm::vec3 ownVector(const FieldGpu& f, const glm::vec3& q, float w) {
    glm::vec3 v = vectorDirection(f, q);
    if (inverted(f)) {
        v = -v;
    }
    return toWorld(f, v * w);
}
glm::vec4 ownColor(const FieldGpu& f, const glm::vec3& q, float w) {
    glm::vec4 a = f.colorA;
    glm::vec4 b = f.colorB;
    if (inverted(f)) {
        a = f.colorB;
        b = f.colorA;
    }
    return glm::vec4(colorShape(f, q, a, b), w);
}

float basicScalar(const FieldGpu& f, const glm::vec3& p) {
    const glm::vec3 q = localOf(f, p);
    const float w = weightOf(f, q, p);
    if (f.type == static_cast<std::uint32_t>(FieldType::Vector)) {
        return glm::length(ownVector(f, q, w));
    }
    if (f.type == static_cast<std::uint32_t>(FieldType::Color)) {
        const glm::vec4 c = ownColor(f, q, w);
        return luminance(glm::vec3(c)) * c.a;
    }
    return ownScalar(f, q, w);
}
glm::vec3 basicVector(const FieldGpu& f, const glm::vec3& p) {
    const glm::vec3 q = localOf(f, p);
    const float w = weightOf(f, q, p);
    if (f.type == static_cast<std::uint32_t>(FieldType::Vector)) {
        return ownVector(f, q, w);
    }
    float s = 0.0f;
    if (f.type == static_cast<std::uint32_t>(FieldType::Color)) {
        const glm::vec4 c = ownColor(f, q, w);
        s = luminance(glm::vec3(c)) * c.a;
    } else {
        s = ownScalar(f, q, w);
    }
    return s * toWorld(f, axisOf(f));
}
glm::vec4 basicColor(const FieldGpu& f, const glm::vec3& p) {
    const glm::vec3 q = localOf(f, p);
    const float w = weightOf(f, q, p);
    if (f.type == static_cast<std::uint32_t>(FieldType::Color)) {
        return ownColor(f, q, w);
    }
    if (f.type == static_cast<std::uint32_t>(FieldType::Vector)) {
        return glm::vec4(ownVector(f, q, w) * 0.5f + 0.5f, w);
    }
    const float s = ownScalar(f, q, w);
    return glm::vec4(wmix(glm::vec3(f.colorA), glm::vec3(f.colorB), sat(s)), w);
}

// Generic combine over the children (T = float, vec3 or vec4), the same arithmetic as the WGSL.
template <typename T, typename Sample>
T combineChildren(const FieldSpec& field, const FieldSet* set, FieldCombine combine, float mixAmount, Sample sample) {
    T acc{0.0f};
    T first{0.0f};
    T second{0.0f};
    int n = 0;
    if (set != nullptr) {
        for (std::size_t c = 0; c < field.children.size() && c < static_cast<std::size_t>(kMaxCompoundChildren); ++c) {
            const FieldSpec* child = set->find(field.children[c]);
            if (child == nullptr) {
                continue;
            }
            const T v = sample(*child);
            if (n == 0) first = v;
            if (n == 1) second = v;
            switch (combine) {
            case FieldCombine::Multiply:
                acc = n == 0 ? v : acc * v;
                break;
            case FieldCombine::Max:
                acc = n == 0 ? v : glm::max(acc, v);
                break;
            case FieldCombine::Min:
                acc = n == 0 ? v : glm::min(acc, v);
                break;
            default:
                acc = acc + v;
                break;
            }
            ++n;
        }
    }
    if (n == 0) {
        return T{0.0f};
    }
    if (combine == FieldCombine::Mix) {
        return wmix(first, second, mixAmount);
    }
    if (combine == FieldCombine::Average) {
        return acc / static_cast<float>(n);
    }
    return acc;
}

constexpr int kMaxDepth = 8;

float scalarDepth(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set, int depth);
glm::vec3 vectorDepth(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set, int depth);
glm::vec4 colorDepth(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set, int depth);

float scalarDepth(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set, int depth) {
    const FieldGpu f = packField(field, time, set);
    if (field.kind != FieldKind::Compound) {
        return basicScalar(f, p);
    }
    if (depth >= kMaxDepth) {
        return 0.0f;
    }
    const float combined = combineChildren<float>(field, set, field.combine, field.mix, [&](const FieldSpec& child) {
        return scalarDepth(child, p, time, set, depth + 1);
    });
    return combined * weightOf(f, localOf(f, p), p);
}

glm::vec3 vectorDepth(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set, int depth) {
    const FieldGpu f = packField(field, time, set);
    if (field.kind != FieldKind::Compound) {
        return basicVector(f, p);
    }
    if (depth >= kMaxDepth) {
        return glm::vec3(0.0f);
    }
    const glm::vec3 combined =
        combineChildren<glm::vec3>(field, set, field.combine, field.mix, [&](const FieldSpec& child) {
            return vectorDepth(child, p, time, set, depth + 1);
        });
    return combined * weightOf(f, localOf(f, p), p);
}

glm::vec4 colorDepth(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set, int depth) {
    const FieldGpu f = packField(field, time, set);
    if (field.kind != FieldKind::Compound) {
        return basicColor(f, p);
    }
    if (depth >= kMaxDepth) {
        return glm::vec4(0.0f);
    }
    const glm::vec4 combined =
        combineChildren<glm::vec4>(field, set, field.combine, field.mix, [&](const FieldSpec& child) {
            return colorDepth(child, p, time, set, depth + 1);
        });
    return combined * weightOf(f, localOf(f, p), p);
}

} // namespace

float falloffCurve(FalloffKind kind, float t, float exponent, const glm::vec4& curve) {
    return curveValue(kind, t, exponent, curve, 0.0f, 1.0f, glm::vec3(0.0f), 0);
}

float Falloff::weight(float distance, const glm::vec3& p, std::uint32_t seed) const {
    if (kind == FalloffKind::None) {
        return 1.0f;
    }
    if (distance <= inner) {
        return 1.0f;
    }
    if (distance >= outer) {
        return 0.0f;
    }
    const float t = (distance - inner) / (outer - inner);
    return curveValue(kind, t, exponent, curve, noiseAmount, noiseScale, p, seed);
}

FieldGpu packField(const FieldSpec& field, double time, const FieldSet* set) {
    FieldGpu g{};
    g.kind = static_cast<std::uint32_t>(field.kind);
    g.type = static_cast<std::uint32_t>(field.type());
    g.falloffKind = static_cast<std::uint32_t>(field.falloff.kind);
    g.seed = field.seed;
    const glm::mat4 l2w = field.localToWorld();
    g.worldToLocal = glm::inverse(l2w);
    // Rotation-only rows (normalised columns keep the scale sign).
    glm::mat3 r(1.0f);
    for (int c = 0; c < 3; ++c) {
        r[c] = nrm(glm::vec3(l2w[c]));
    }
    g.localToWorldRow0 = glm::vec4(r[0][0], r[1][0], r[2][0], 0.0f);
    g.localToWorldRow1 = glm::vec4(r[0][1], r[1][1], r[2][1], 0.0f);
    g.localToWorldRow2 = glm::vec4(r[0][2], r[1][2], r[2][2], 0.0f);
    g.strengthInnerOuterTau = glm::vec4(field.strength, field.falloff.inner, field.falloff.outer,
                                        static_cast<float>(static_cast<double>(field.speed) * time +
                                                           static_cast<double>(field.phase)));
    g.axisRadius = glm::vec4(field.axis, field.radius);
    g.pointLength = glm::vec4(field.point, field.length);
    g.sizeSoftness = glm::vec4(field.size, field.softness);
    g.freqExpInvertBias = glm::vec4(field.frequency, field.falloff.exponent, field.invert ? 1.0f : 0.0f, field.spiralBias);
    g.wave0 = glm::vec4(field.amplitude, field.wavelength, field.waveSpeed, field.waveWidth);
    g.wave1 = glm::vec4(field.waveOrigin, static_cast<float>(field.waveGeometry), static_cast<float>(field.waveShape),
                        static_cast<float>(time));
    g.colorA = field.colorA;
    g.colorB = field.colorB;
    g.curve = field.falloff.curve;
    g.noiseCombineMix = glm::vec4(field.falloff.noiseAmount, field.falloff.noiseScale, static_cast<float>(field.combine),
                                  field.mix);
    g.children = glm::ivec4(-1);
    for (int c = 0; c < kMaxCompoundChildren; ++c) {
        if (set == nullptr || c >= static_cast<int>(field.children.size())) {
            break;
        }
        const int slot = set->indexOf(field.children[static_cast<std::size_t>(c)]);
        g.children[c] = slot >= 0 && slot < kMaxGpuFields ? slot : -1;
    }
    return g;
}

float sampleScalar(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set) {
    return scalarDepth(field, p, time, set, 0);
}
glm::vec3 sampleVector(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set) {
    return vectorDepth(field, p, time, set, 0);
}
glm::vec4 sampleColor(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set) {
    return colorDepth(field, p, time, set, 0);
}
float sampleWeight(const FieldSpec& field, const glm::vec3& p, double time) {
    const FieldGpu f = packField(field, time, nullptr);
    return weightOf(f, localOf(f, p), p);
}

// ---- effectors ---------------------------------------------------------------------------------

EffectorGpu packEffector(const Effector& effector, const FieldSet& fields) {
    EffectorGpu g{};
    g.op = static_cast<std::uint32_t>(effector.op);
    g.blend = static_cast<std::uint32_t>(effector.blend);
    const int slot = fields.indexOf(effector.field);
    const bool usable = effector.enabled && slot >= 0 && slot < kMaxGpuFields &&
                        fields.fields[static_cast<std::size_t>(slot)].enabled;
    g.fieldSlot = usable ? slot : -1;
    g.strength = effector.strength;
    g.axisWeight = glm::vec4(effector.axis, effector.weight);
    g.scaleAxisPad = glm::vec4(effector.scaleAxis, 0.0f);
    return g;
}

namespace {
glm::vec3 blendVec3(EffectorBlend blend, const glm::vec3& existing, const glm::vec3& value, float weight) {
    switch (blend) {
    case EffectorBlend::Add:
        return existing + value;
    case EffectorBlend::Multiply:
        return existing * value;
    case EffectorBlend::Replace:
        return value;
    case EffectorBlend::Min:
        return glm::min(existing, value);
    case EffectorBlend::Max:
        return glm::max(existing, value);
    case EffectorBlend::Mix:
        return wmix(existing, value, weight);
    }
    return value;
}
} // namespace

int applyEffectorsToRecords(std::span<InstanceRecord> records, std::span<const Effector> effectors,
                            const FieldSet& fields, double time, const glm::mat4& objectToWorld) {
    // Rotation-only part of the object matrix (normalised columns) and its inverse (transpose).
    glm::mat3 rot(1.0f);
    for (int c = 0; c < 3; ++c) {
        rot[c] = nrm(glm::vec3(objectToWorld[c]));
    }
    const glm::mat3 rotInv = glm::transpose(rot);
    int applied = 0;
    for (const Effector& e : effectors) {
        if (!e.enabled) {
            continue;
        }
        const FieldSpec* field = fields.find(e.field);
        if (field == nullptr || !field->enabled) {
            continue;
        }
        if (e.op == EffectorOp::Velocity || e.op == EffectorOp::Attribute) {
            continue;
        }
        const bool vectorField = field->type() == FieldType::Vector;
        for (InstanceRecord& r : records) {
            const glm::vec3 pw = glm::vec3(objectToWorld * glm::vec4(glm::vec3(r.position), 1.0f));
            switch (e.op) {
            case EffectorOp::PositionOffset: {
                const glm::vec3 v = vectorField ? sampleVector(*field, pw, time, &fields)
                                                : sampleScalar(*field, pw, time, &fields) * e.axis;
                const glm::vec3 offset = rotInv * (v * e.strength);
                r.position = glm::vec4(glm::vec3(r.position) + offset, r.position.w);
                break;
            }
            case EffectorOp::Scale: {
                const float s = sampleScalar(*field, pw, time, &fields);
                const glm::vec3 scale(r.scale);
                glm::vec3 target = scale * (1.0f + s * e.strength * e.scaleAxis);
                if (e.blend == EffectorBlend::Replace) {
                    target = s * e.strength * e.scaleAxis;
                }
                r.scale = glm::vec4(blendVec3(e.blend, scale, target, e.weight), r.scale.w);
                break;
            }
            case EffectorOp::Rotation: {
                const glm::vec3 axis = vectorField ? nrm(sampleVector(*field, pw, time, &fields)) : nrm(e.axis);
                if (glm::dot(axis, axis) < 0.5f) {
                    break;
                }
                const float angle = sampleScalar(*field, pw, time, &fields) * e.strength;
                const glm::quat q(r.rotation.w, r.rotation.x, r.rotation.y, r.rotation.z);
                const glm::quat out = glm::angleAxis(angle, axis) * q;
                r.rotation = glm::vec4(out.x, out.y, out.z, out.w);
                break;
            }
            case EffectorOp::Color: {
                const glm::vec4 c = sampleColor(*field, pw, time, &fields);
                r.color = glm::vec4(wmix(glm::vec3(r.color), glm::vec3(c), c.a * e.strength), r.color.w);
                break;
            }
            case EffectorOp::Emission: {
                const float s = sampleScalar(*field, pw, time, &fields);
                const glm::vec3 em = e.blend == EffectorBlend::Replace ? glm::vec3(s * e.strength)
                                                                       : glm::vec3(r.emissive) * (1.0f + s * e.strength);
                r.emissive = glm::vec4(em, r.emissive.w);
                break;
            }
            case EffectorOp::Density: {
                const float s = sampleScalar(*field, pw, time, &fields);
                r.position.w = e.blend == EffectorBlend::Replace ? s * e.strength : r.position.w * (s * e.strength);
                break;
            }
            case EffectorOp::Velocity:
            case EffectorOp::Attribute:
                break;
            }
        }
        ++applied;
    }
    return applied;
}

} // namespace avgen::spatial

namespace avgen::scene {

namespace {
// Field deformer at a point: samples at `worldPos`, displaces `p` (in the deformer's space);
// `toLocal` rotates a world-space vector into p's space (identity for world deformers).
glm::vec3 fieldDisplace(const Deformer& d, glm::vec3 p, const glm::vec3& normal, const glm::vec3& worldPos,
                        const glm::mat3& toLocal, double time, const spatial::FieldSet& fields) {
    const spatial::FieldSpec* field = fields.find(d.field);
    if (field == nullptr || !field->enabled) {
        return p;
    }
    if (field->type() == spatial::FieldType::Vector) {
        const glm::vec3 v = toLocal * spatial::sampleVector(*field, worldPos, time, &fields);
        return p + v * d.amount;
    }
    const float s = spatial::sampleScalar(*field, worldPos, time, &fields);
    const glm::vec3 dir = d.alongNormal ? normal : glm::normalize(d.axis);
    return p + dir * (s * d.amount);
}
} // namespace

glm::vec3 applyFieldDeformer(const Deformer& d, glm::vec3 p, glm::vec3 normal, double time,
                             const spatial::FieldSet& fields) {
    return fieldDisplace(d, p, normal, p, glm::mat3(1.0f), time, fields);
}

glm::vec3 deformPoint(const std::vector<Deformer>& stack, glm::vec3 objectPoint, const glm::mat4& instanceWorld,
                      double time, const spatial::FieldSet* fields, glm::vec3 normal) {
    glm::vec3 p = objectPoint;
    const glm::mat3 linear(instanceWorld);
    const glm::mat3 toLocal = glm::inverse(linear);
    for (const Deformer& d : stack) {
        if (!d.enabled || d.space != DeformSpace::Local) {
            continue;
        }
        if (d.kind == DeformerKind::Field) {
            if (fields != nullptr) {
                const glm::vec3 worldPos = glm::vec3(instanceWorld * glm::vec4(p, 1.0f));
                p = fieldDisplace(d, p, normal, worldPos, toLocal, time, *fields);
            }
        } else {
            p = applyDeformer(d, p, time);
        }
    }
    p = glm::vec3(instanceWorld * glm::vec4(p, 1.0f));
    const glm::vec3 worldNormal = glm::normalize(glm::transpose(toLocal) * normal);
    for (const Deformer& d : stack) {
        if (!d.enabled || d.space != DeformSpace::World) {
            continue;
        }
        if (d.kind == DeformerKind::Field) {
            if (fields != nullptr) {
                p = fieldDisplace(d, p, worldNormal, p, glm::mat3(1.0f), time, *fields);
            }
        } else {
            p = applyDeformer(d, p, time);
        }
    }
    return p;
}

} // namespace avgen::scene

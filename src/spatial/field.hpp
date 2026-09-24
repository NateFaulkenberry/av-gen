#pragma once

// Fields (ADR-025): spatial control signals sampled at a point and a time. A field has a kind
// (scalar, vector or colour producing), a transform (its local frame), a falloff (where it
// acts), a strength and per-kind parameters. Sampling is a pure function; the WGSL library
// `shaders/fields.wgsl` implements the same maths from the packed `FieldGpu` record so CPU and
// GPU results agree within float rounding (tests compare them).
//
// Sampling conventions (p in world space unless the field is Local, in which case p is taken as
// already local to the field's owner; the field's own `transform` always applies):
//   q = worldToLocal(p); d = distance measure of the kind; w = strength * falloff(d)
//   scalar kinds: value = shape(q, t) * w
//   vector kinds: value = direction(q, t) * w, rotated back to world orientation
//   colour kinds: value = colour(q, t), alpha = w (blend weight)
//   Cross-type reads: scalar as vector = value * axis; vector as scalar = length; colour as
//   scalar = luminance * alpha; scalar as colour = mix(colorA, colorB, value).
// Time: tau = speed * t + phase (radians for waves / phase for noise animation).

#include "core/error.hpp"
#include "spatial/grid_field.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::spatial {

enum class FieldKind : std::uint8_t {
    // scalar
    Constant,        // value = 1
    LinearGradient,  // value = clamp(dot(q, axis) / max(length, eps) + 0.5, 0, 1) ... centred: 0 at -len/2, 1 at +len/2
    Radial,          // value = 1 - saturate(|q| / radius)
    Box,             // value = 1 inside the box (half extents `size`), smooth edge over `softness`
    Sphere,          // value = 1 inside radius, smooth edge over `softness`
    Plane,           // value = saturate(dot(q, axis) / max(softness, eps)) (0 below the plane, 1 above)
    Noise,           // value = fbm3(q * frequency + tau) (in [0, 1))
    Voronoi,         // value = saturate(voronoiF1(q * frequency + tau))
    Distance,        // value = saturate(|q - point| / radius)
    // ADR-704: `SdfDistance` was here -- declared since ADR-027, never bound, refused at load by
    // ADR-576 -- and was REMOVED on the owner's ruling. A file naming `sdfDistance` is now an
    // unknown kind. The numbering below moved down by one with it; `shaders/fields.wgsl` mirrors it.
    Wave,            // travelling wave: value = envelope(s) * shape(k s) with s = dist(q) - waveSpeed * t (see WaveShape)
    // vector
    Direction,       // value = axis
    RadialVector,    // value = normalize(q) (outward; `invert` = inward)
    Attractor,       // value = normalize(point - q)
    Repulsor,        // value = normalize(q - point)
    Vortex,          // value = cross(axis, q - point) normalised (rotation about axis)
    CurlNoise,       // value = curlNoise(q * frequency + tau)
    Spiral,          // value = normalize(vortex + radial * spiralBias)
    WaveVector,      // value = wave value * gradient direction of the wave geometry (outward)
    // colour
    ConstantColor,   // colorA
    Gradient,        // mix(colorA, colorB, linear gradient along axis)
    RadialGradient,  // mix(colorA, colorB, |q| / radius)
    NoiseColor,      // mix(colorA, colorB, fbm3(q * frequency + tau))
    PositionColor,   // rgb = fract(q * frequency) (debug/position mapping)
    // compound
    Compound,        // combine(children) with `combine`
    // simulated (ADR-032)
    Grid,            // value = the named GridField's trilinear sample at q (`reference` = grid name)
};
[[nodiscard]] const char* fieldKindName(FieldKind kind);
[[nodiscard]] std::optional<FieldKind> fieldKindFromName(std::string_view name);

enum class FieldType : std::uint8_t { Scalar, Vector, Color };
// Compound and Grid report Scalar (a compound's children and a grid's mode decide at sample time;
// packField writes the bound grid's real type into FieldGpu::type).
[[nodiscard]] FieldType fieldTypeOf(FieldKind kind);
[[nodiscard]] const char* fieldTypeName(FieldType type);

enum class FalloffKind : std::uint8_t {
    None, Linear, Smoothstep, Smooth, EaseIn, EaseOut, EaseInOut, Exponential, CustomCurve, NoiseModulated
};
[[nodiscard]] const char* falloffKindName(FalloffKind kind);
[[nodiscard]] std::optional<FalloffKind> falloffKindFromName(std::string_view name);

// Weight in [0, 1] as a function of a distance d: 1 for d <= inner, 0 for d >= outer, the curve in
// between (t = (d - inner) / (outer - inner)); None = 1 everywhere. CustomCurve evaluates a cubic
// Bezier through (0,1) (c0) (c1) (1,0) on the y axis; NoiseModulated multiplies Smoothstep by
// (1 + noiseAmount * (fbm3(p * noiseScale) * 2 - 1)) clamped to [0, 1].
struct Falloff {
    FalloffKind kind = FalloffKind::None;
    float inner = 0.0f;
    float outer = 10.0f;
    float exponent = 2.0f;                 // Exponential
    glm::vec4 curve{0.8f, 0.6f, 0.4f, 0.2f}; // CustomCurve control values (y at 0.25/0.5/0.75 and tension)
    float noiseAmount = 0.5f;              // NoiseModulated
    float noiseScale = 1.0f;
    [[nodiscard]] float weight(float distance, const glm::vec3& p = glm::vec3(0.0f), std::uint32_t seed = 0) const;
};
[[nodiscard]] float falloffCurve(FalloffKind kind, float t, float exponent, const glm::vec4& curve);

enum class FieldSpace : std::uint8_t { World, Local };
[[nodiscard]] const char* fieldSpaceName(FieldSpace space);
[[nodiscard]] std::optional<FieldSpace> fieldSpaceFromName(std::string_view name);

enum class WaveGeometry : std::uint8_t { Planar, Radial, Spherical, Cylindrical };
[[nodiscard]] const char* waveGeometryName(WaveGeometry geometry);
[[nodiscard]] std::optional<WaveGeometry> waveGeometryFromName(std::string_view name);
enum class WaveShape : std::uint8_t { Sine, Pulse, Triangle };
[[nodiscard]] const char* waveShapeName(WaveShape shape);
[[nodiscard]] std::optional<WaveShape> waveShapeFromName(std::string_view name);

enum class FieldCombine : std::uint8_t { Add, Multiply, Max, Min, Mix, Average };
[[nodiscard]] const char* fieldCombineName(FieldCombine combine);
[[nodiscard]] std::optional<FieldCombine> fieldCombineFromName(std::string_view name);

struct FieldSpec {
    std::string name = "field";
    FieldKind kind = FieldKind::Radial;
    bool enabled = true;
    FieldSpace space = FieldSpace::World;
    // Frame: position/rotation/scale of the field (rotation as Euler degrees XYZ like nodes).
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
    float strength = 1.0f;
    bool invert = false;                   // flips the sign of the shape (scalar: 1 - v; vector: -v)
    Falloff falloff;
    // Animation
    float speed = 0.0f;
    float phase = 0.0f;
    // Kind parameters
    glm::vec3 axis{0.0f, 1.0f, 0.0f};      // gradients, plane, direction, vortex, waves
    glm::vec3 point{0.0f};                 // distance, attractor, repulsor, vortex centre (local)
    float radius = 10.0f;                  // radial kinds
    float length = 10.0f;                  // linear gradient extent
    glm::vec3 size{5.0f};                  // box half extents
    float softness = 0.5f;                 // box/sphere/plane edge width
    float frequency = 0.2f;                // noise / voronoi / colour noise spatial frequency
    std::uint32_t seed = 7;                // noise kinds
    float spiralBias = 0.5f;               // spiral
    // Waves
    WaveGeometry waveGeometry = WaveGeometry::Radial;
    WaveShape waveShape = WaveShape::Sine;
    float amplitude = 1.0f;                // wave amplitude (multiplies strength)
    float wavelength = 4.0f;               // k = 2pi / wavelength
    float waveSpeed = 4.0f;                // units per second
    float waveWidth = 6.0f;                // envelope half width around the wavefront (0 = infinite)
    float waveOrigin = 0.0f;               // distance offset of the front at t = 0
    // Colours
    glm::vec4 colorA{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 colorB{0.0f, 0.0f, 0.0f, 1.0f};
    // Compound
    std::vector<std::string> children;
    FieldCombine combine = FieldCombine::Add;
    float mix = 0.5f;
    // Grid: the name of the simulated grid it samples.
    std::string reference;

    [[nodiscard]] FieldType type() const { return fieldTypeOf(kind); }
    [[nodiscard]] glm::mat4 localToWorld() const;
    [[nodiscard]] glm::mat4 worldToLocal() const;
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;  // every field
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<FieldSpec> fromJson(const nlohmann::json& j);
};

// A set of fields addressable by name (the scene-level list). Compound children and the grids a
// `Grid` field references (by `FieldSpec::reference`) resolve here.
struct FieldSet {
    std::vector<FieldSpec> fields;
    std::vector<GridField> grids; // ADR-032; simulated, sampled through a Grid field
    [[nodiscard]] const FieldSpec* find(std::string_view name) const;
    [[nodiscard]] int indexOf(std::string_view name) const; // -1 when missing
    [[nodiscard]] const GridField* findGrid(std::string_view name) const;
    [[nodiscard]] int gridIndexOf(std::string_view name) const; // -1 when missing
};

// CPU sampling (pure). `set` resolves compound children; may be empty for simple kinds. Time in
// seconds. Depth limits compound recursion (cycles yield 0).
[[nodiscard]] float sampleScalar(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set = nullptr);
[[nodiscard]] glm::vec3 sampleVector(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set = nullptr);
[[nodiscard]] glm::vec4 sampleColor(const FieldSpec& field, const glm::vec3& p, double time, const FieldSet* set = nullptr);
// The falloff weight at p (strength * falloff), useful for effectors and debugging.
[[nodiscard]] float sampleWeight(const FieldSpec& field, const glm::vec3& p, double time);

// ---- GPU packing (uniform array of kMaxGpuFields records; see shaders/fields.wgsl) -------------

constexpr int kMaxGpuFields = 16;
constexpr int kMaxCompoundChildren = 4;

// 368 bytes, std140/WGSL uniform compatible (all members 16-byte aligned).
struct alignas(16) FieldGpu {
    std::uint32_t kind;          // FieldKind
    std::uint32_t type;          // FieldType
    std::uint32_t falloffKind;   // FalloffKind
    std::uint32_t seed;
    glm::mat4 worldToLocal;      // 64
    glm::vec4 localToWorldRow0;  // orientation (rotation * scale sign) rows for rotating vectors back
    glm::vec4 localToWorldRow1;
    glm::vec4 localToWorldRow2;
    glm::vec4 strengthInnerOuterTau;   // strength, falloff.inner, falloff.outer, speed*t + phase
    glm::vec4 axisRadius;              // axis.xyz, radius
    glm::vec4 pointLength;             // point.xyz, length
    glm::vec4 sizeSoftness;            // size.xyz, softness
    glm::vec4 freqExpInvertBias;       // frequency, falloff.exponent, invert (1/0), spiralBias
    glm::vec4 wave0;                   // amplitude, wavelength, waveSpeed, waveWidth
    glm::vec4 wave1;                   // waveOrigin, geometry, shape, time (t seconds)
    glm::vec4 colorA;
    glm::vec4 colorB;
    glm::vec4 curve;                   // falloff custom curve
    glm::vec4 noiseCombineMix;         // falloff.noiseAmount, falloff.noiseScale, combine, mix
    glm::ivec4 children;               // compound child slots (-1 = none)
    // Grid kind (ADR-032). Zero for every other kind, and gridRes.w = 0 means "no grid bound",
    // which samples as 0 (so a scene without grids never reads the table).
    glm::vec4 gridBounds0;             // grid boundsMin.xyz, offset into the grid table (floats)
    glm::vec4 gridBounds1;             // grid boundsMax.xyz, components per cell
    glm::vec4 gridRes;                 // resolution.xyz, w = 1 bound + 2 when wrapping (0 = unbound)
};
static_assert(sizeof(FieldGpu) == 368);
// Packs a field for slot use; child names resolve to slots through `set` (order of `set.fields`).
[[nodiscard]] FieldGpu packField(const FieldSpec& field, double time, const FieldSet* set = nullptr);

} // namespace avgen::spatial

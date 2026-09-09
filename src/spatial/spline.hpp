#pragma once

// Splines (ADR-026): first-class spatial data. A spline is a list of control points with a kind
// (polyline, Catmull-Rom, cubic Bezier, Hermite), sampled at a parameter t in [0, 1] (uniform
// across segments) or by arc-length distance. Frames are rotation-minimising (parallel
// transport) so instances along the spline do not flip; `roll` adds a twist about the tangent.
// Everything is pure and deterministic; the arc-length table is cached by the control hash.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::spatial {

enum class SplineKind : std::uint8_t { Polyline, CatmullRom, Bezier, Hermite };
[[nodiscard]] const char* splineKindName(SplineKind kind);
[[nodiscard]] std::optional<SplineKind> splineKindFromName(std::string_view name);

struct SplinePoint {
    glm::vec3 position{0.0f};
    glm::vec3 tangent{0.0f};     // Hermite: outgoing tangent; Bezier: handle offset (in = -out)
    float roll = 0.0f;           // extra rotation about the tangent (radians), interpolated
    float scale = 1.0f;          // per-point scale factor, interpolated (for instancing/ribbons)
};

struct SplineSample {
    glm::vec3 position{0.0f};
    glm::vec3 tangent{0.0f, 0.0f, 1.0f}; // unit
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  // unit, perpendicular to tangent (rotation-minimising + roll)
    glm::vec3 binormal{1.0f, 0.0f, 0.0f};
    float distance = 0.0f;               // arc length from the start
    float t = 0.0f;                      // uniform parameter
    float scale = 1.0f;
    [[nodiscard]] glm::quat rotation() const; // frame as a quaternion (x = binormal, y = normal, z = tangent)
};

enum class SplineGenerator : std::uint8_t { Points, Line, Circle, Spiral, Helix, Bezier, Noise };
[[nodiscard]] const char* splineGeneratorName(SplineGenerator generator);
[[nodiscard]] std::optional<SplineGenerator> splineGeneratorFromName(std::string_view name);

struct Spline {
    std::string name = "spline";
    SplineKind kind = SplineKind::CatmullRom;
    bool closed = false;
    float tension = 0.5f;                // Catmull-Rom (0.5 = centripetal-like uniform)
    std::vector<SplinePoint> points;     // explicit control points (generator Points)
    // Generator (when != Points the control points are produced by `generate()`):
    SplineGenerator generator = SplineGenerator::Points;
    int count = 16;                      // generated control points
    glm::vec3 start{0.0f};               // Line
    glm::vec3 end{0.0f, 0.0f, 10.0f};    // Line
    float radius = 5.0f;                 // Circle / Spiral / Helix
    float radiusGrowth = 0.0f;           // Spiral: radius added over the whole curve
    float turns = 1.0f;                  // Spiral / Helix
    float height = 10.0f;                // Helix rise over the whole curve (along axis)
    glm::vec3 axis{0.0f, 1.0f, 0.0f};    // Circle / Spiral / Helix normal
    glm::vec3 center{0.0f};
    float startAngle = 0.0f;             // radians
    glm::vec3 p0{0.0f}, p1{0.0f, 5.0f, 5.0f}, p2{0.0f, 5.0f, 10.0f}, p3{0.0f, 0.0f, 15.0f}; // Bezier generator
    float noiseAmount = 0.0f;            // Noise generator jitter (and extra jitter for any generator)
    float noiseScale = 0.3f;
    std::uint32_t seed = 1;
    int samplesPerSegment = 16;          // arc-length table resolution (4..256)

    // Frame reference for the first normal (projected perpendicular to the first tangent).
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    // Control points after the generator (points themselves for Points).
    [[nodiscard]] std::vector<SplinePoint> controlPoints() const;
    [[nodiscard]] int segmentCount() const;        // from controlPoints()
    // Sampling (pure; builds the arc-length table on the fly unless `prepare()` was called).
    [[nodiscard]] SplineSample sample(float t) const;              // t in [0, 1] (clamped; wrapped when closed)
    [[nodiscard]] SplineSample sampleByDistance(float distance) const; // clamped/wrapped to [0, length]
    [[nodiscard]] glm::vec3 position(float t) const;
    [[nodiscard]] glm::vec3 tangent(float t) const;                // unit
    [[nodiscard]] float length() const;
    // Uniformly spaced samples by distance (count >= 1; closed splines omit the duplicate end).
    [[nodiscard]] std::vector<SplineSample> samples(int count) const;
    // Caches control points, arc-length table and frames (idempotent; keyed by structuralHash).
    void prepare() const;

    [[nodiscard]] nlohmann::json toJson() const;
    static Result<Spline> fromJson(const nlohmann::json& j);

private:
    struct Cache {
        std::uint64_t hash = 0;
        std::vector<SplinePoint> controls;
        std::vector<SplineSample> table;   // dense samples with parallel-transport frames
        float length = 0.0f;
    };
    mutable Cache cache_;
    void ensureCache() const;
};

// GPU sample table entry (64 bytes; see shaders/spline.wgsl): position.w = distance,
// tangent.w = scale, normal.w = t, binormal.w = roll-applied flag (1).
struct alignas(16) SplineSampleGpu {
    glm::vec4 position;
    glm::vec4 tangent;
    glm::vec4 normal;
    glm::vec4 binormal;
};
static_assert(sizeof(SplineSampleGpu) == 64);
constexpr int kSplineGpuSamples = 512;
[[nodiscard]] std::vector<SplineSampleGpu> packSplineTable(const Spline& spline, int count = kSplineGpuSamples);

struct SplineSet {
    std::vector<Spline> splines;
    [[nodiscard]] const Spline* find(std::string_view name) const;
    [[nodiscard]] int indexOf(std::string_view name) const;
};

} // namespace avgen::spatial

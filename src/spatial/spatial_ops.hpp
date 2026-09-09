#pragma once

// Spatial operators over point clouds (ADR-024 §4, brief §6–7). Each operator is a pure
// function of (cloud, op) — deterministic, order-preserving, ids stable. Filters remove rows;
// generators of rows (Duplicate, Scatter) create ids by `id = maxId + 1 + k` for determinism.
// A PointOp is a tagged union kept as one struct so lists serialise and register uniformly;
// attribute maths lives in AttributeOp (attributes.hpp) and is reachable through kind Attribute.

#include "core/error.hpp"
#include "spatial/attributes.hpp"
#include "spatial/point_cloud.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::spatial {

enum class PointOpKind : std::uint8_t {
    Transform,        // position = T * position; rotation = R * rotation; scale *= S (about `pivot`)
    Translate,        // position += offset
    Rotate,           // rotate about `axis` by `angle` (radians) around `pivot`; rotates rotations too
    Scale,            // position = pivot + (position - pivot) * factor; scale *= factor (when scaleInstances)
    Noise,            // position += fbm3Vec(position * scale + offset + time·0) * amount * axisMask
    Randomize,        // position/rotation/scale jitter: hashIndex-driven, like Variation (ranges below)
    Scatter,          // jitter position uniformly within ±range (hash channels 40..42)
    FilterDensity,    // keep when density >= threshold (or density >= random when probabilistic)
    FilterAttribute,  // keep when compare(attribute.x, value) (compare like AttributeOp)
    FilterDistance,   // keep when distance(position, pivot) within [minDistance, maxDistance] (invert flips)
    FilterProbability,// keep with probability `probability` (hash channel 43)
    FilterBounds,     // keep when inside the box [boundsMin, boundsMax] (invert flips)
    Delete,           // remove rows where attribute.x != 0 (attribute defaults to "delete")
    Sort,             // stable sort by attribute component (axis 0..3) ascending/descending, then renumber indices
    Merge,            // (list-level) no-op on a single cloud; provided for graph nodes (see mergeClouds)
    Duplicate,        // append `copies` copies, each transformed by offset/rotation/scale cumulatively
    Sample,           // keep every `stride`-th row, or `count` rows evenly (count > 0), starting at `start`
    Attribute,        // an AttributeOp
};
[[nodiscard]] const char* pointOpKindName(PointOpKind kind);
[[nodiscard]] std::optional<PointOpKind> pointOpKindFromName(std::string_view name);

struct PointOp {
    PointOpKind kind = PointOpKind::Translate;
    bool enabled = true;
    float amount = 1.0f;                // scales the effect of Translate/Rotate/Noise/Scatter/Randomize (0 = off)
    glm::vec3 offset{0.0f};             // Translate / Duplicate step / Noise offset
    glm::vec3 axis{0.0f, 1.0f, 0.0f};   // Rotate axis
    float angle = 0.0f;                 // Rotate (radians) / Duplicate step rotation about axis
    glm::vec3 pivot{0.0f};              // Rotate/Scale/FilterDistance centre
    glm::vec3 factor{1.0f};             // Scale / Duplicate step scale
    bool scaleInstances = true;         // Scale also multiplies the scale column
    glm::vec3 position{0.0f};           // Transform
    glm::vec3 rotationDegrees{0.0f};    // Transform (Euler degrees, XYZ)
    glm::vec3 scale{1.0f};              // Transform; Noise spatial frequency uses `frequency`
    float frequency = 1.0f;             // Noise
    glm::vec3 axisMask{1.0f};           // Noise
    std::uint32_t seed = 1;             // Noise / Randomize / Scatter / FilterProbability
    glm::vec3 randomPosition{0.0f};     // Randomize (units)
    glm::vec3 randomRotation{0.0f};     // Randomize (radians)
    glm::vec3 randomScale{0.0f};        // Randomize (relative)
    float randomUniformScale = 0.0f;    // Randomize
    glm::vec3 range{0.5f};              // Scatter
    float threshold = 0.5f;             // FilterDensity
    bool probabilistic = false;         // FilterDensity: keep when density >= random
    std::string attribute;              // FilterAttribute / Delete / Sort
    float value = 0.0f;                 // FilterAttribute
    int compare = 3;                    // FilterAttribute operator (0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 !=)
    float minDistance = 0.0f, maxDistance = 10.0f; // FilterDistance
    float probability = 0.5f;           // FilterProbability
    glm::vec3 boundsMin{-1.0f}, boundsMax{1.0f};  // FilterBounds
    bool invert = false;                // filters
    int component = 0;                  // Sort component
    bool descending = false;            // Sort
    int copies = 1;                     // Duplicate
    int stride = 2;                     // Sample
    int count = 0;                      // Sample (0 = use stride)
    int start = 0;                      // Sample
    AttributeOp attributeOp;            // Attribute
};
[[nodiscard]] Result<void> applyPointOp(PointCloud& cloud, const PointOp& op);
[[nodiscard]] Result<void> applyPointOps(PointCloud& cloud, std::span<const PointOp> ops);
// Appends `b` to `a` (union of columns), renumbering b's ids after a's max id.
void mergeClouds(PointCloud& a, const PointCloud& b);
// Resampling by count keeps `count` rows evenly spaced along the current order.
void sampleCloud(PointCloud& cloud, int count);

[[nodiscard]] nlohmann::json pointOpToJson(const PointOp& op);
[[nodiscard]] Result<PointOp> pointOpFromJson(const nlohmann::json& j);
[[nodiscard]] std::uint64_t pointOpHash(const PointOp& op); // structural hash of every field

} // namespace avgen::spatial

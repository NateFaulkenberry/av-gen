#pragma once

// Point clouds (ADR-024): an AttributeSet over the Point domain with the conventional core
// columns always present, plus the fixed instance projection the renderer consumes.
//
// Core columns (created by PointCloud::PointCloud / ensureCore):
//   position vec3, rotation vec4 (unit quaternion xyzw), scale vec3, id int (stable per
//   generator), seed int (generator seed mixed with id), density float (1), color color (1,1,1,1),
//   emissive vec3 (1,1,1), velocity vec3 (0), normal vec3 (0,1,0), bounds vec3 (half-extent, 0.5),
//   index float (normalised index u in [0, 1]).
// Determinism: all randomness is noise::hashIndex(seed, id, channel); filters keep ids.

#include "core/error.hpp"
#include "spatial/attributes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace avgen::spatial {

namespace attr {
inline constexpr std::string_view position = "position";
inline constexpr std::string_view rotation = "rotation";
inline constexpr std::string_view scale = "scale";
inline constexpr std::string_view id = "id";
inline constexpr std::string_view seed = "seed";
inline constexpr std::string_view density = "density";
inline constexpr std::string_view color = "color";
inline constexpr std::string_view emissive = "emissive";
inline constexpr std::string_view velocity = "velocity";
inline constexpr std::string_view normal = "normal";
inline constexpr std::string_view bounds = "bounds";
inline constexpr std::string_view index = "index";
} // namespace attr

// The GPU instance record (96 bytes), the fixed projection of a point (ADR-023/024).
struct InstanceRecord {
    glm::vec4 position;   // xyz, w = density
    glm::vec4 rotation;   // unit quaternion (x, y, z, w)
    glm::vec4 scale;      // xyz, w = normalised index
    glm::vec4 random;     // four hashed randoms in [0, 1) (channels 0..3 of the point's seed/id)
    glm::vec4 color;      // base colour multiplier (rgb), a = instance id
    glm::vec4 emissive;   // emissive multiplier (rgb), a = extra lane (attribute bound by name, 0)
};
static_assert(sizeof(InstanceRecord) == 96);

struct PointCloud {
    AttributeSet attributes{AttributeDomain::Point};

    PointCloud();                             // empty, core columns present
    explicit PointCloud(std::size_t count);   // `count` default points (position 0, id = index)
    void ensureCore();                        // (re)creates missing core columns
    [[nodiscard]] std::size_t count() const { return attributes.count(); }
    void resize(std::size_t count);           // new rows get id = row, index recomputed
    void clear();
    // Recomputes `index` (normalised row index) and `seed` (mix of `seed` with `id`) — call after
    // generation or after filtering when normalised indices should be re-spread.
    void renumberIndices();
    void reseed(std::uint32_t generatorSeed); // seed[i] = hash(generatorSeed, id[i])

    // Typed core accessors (always valid after ensureCore()).
    [[nodiscard]] std::span<glm::vec3> positions();
    [[nodiscard]] std::span<const glm::vec3> positions() const;
    [[nodiscard]] std::span<glm::vec4> rotations();
    [[nodiscard]] std::span<const glm::vec4> rotations() const;
    [[nodiscard]] std::span<glm::vec3> scales();
    [[nodiscard]] std::span<const glm::vec3> scales() const;
    [[nodiscard]] std::span<std::int32_t> ids();
    [[nodiscard]] std::span<const std::int32_t> ids() const;
    [[nodiscard]] std::span<std::int32_t> seeds();
    [[nodiscard]] std::span<const std::int32_t> seeds() const;
    [[nodiscard]] std::span<float> densities();
    [[nodiscard]] std::span<const float> densities() const;
    [[nodiscard]] std::span<glm::vec4> colors();
    [[nodiscard]] std::span<const glm::vec4> colors() const;
    [[nodiscard]] std::span<glm::vec3> emissives();
    [[nodiscard]] std::span<const glm::vec3> emissives() const;
    [[nodiscard]] std::span<glm::vec3> velocities();
    [[nodiscard]] std::span<const glm::vec3> velocities() const;
    [[nodiscard]] std::span<glm::vec3> normals();
    [[nodiscard]] std::span<const glm::vec3> normals() const;
    [[nodiscard]] std::span<float> indices();
    [[nodiscard]] std::span<const float> indices() const;

    // Deterministic per-point random in [0, 1): noise::hashIndex(seed[i], id[i], channel).
    [[nodiscard]] float random(std::size_t i, std::uint32_t channel) const;
    // Axis-aligned bounds of positions (± bounds half-extent × scale when `includeExtent`).
    void bounds(glm::vec3& outMin, glm::vec3& outMax, bool includeExtent = true) const;
    [[nodiscard]] glm::mat4 pointMatrix(std::size_t i) const; // translate * rotate * scale
    [[nodiscard]] std::uint64_t contentHash() const { return attributes.contentHash(); }
    [[nodiscard]] nlohmann::json toJson() const;        // attributes.toJson()
    static Result<PointCloud> fromJson(const nlohmann::json& j);
};

// Projection to instance records. `extraLane` names an attribute whose x component fills
// emissive.a (empty = 0). Records are written in row order; random lanes are channels 0..3.
void projectInstances(const PointCloud& cloud, std::vector<InstanceRecord>& out, std::string_view extraLane = {});
// Inverse for tools/tests: a cloud with core columns filled from records (random lanes are not
// invertible; ids come from color.a).
[[nodiscard]] PointCloud cloudFromInstances(std::span<const InstanceRecord> records);

} // namespace avgen::spatial

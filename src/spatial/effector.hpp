#pragma once

// Effectors (ADR-025): field × operation applied to a point cloud. `applyEffectors` is the CPU
// reference; the procedural renderer runs the identical list on the GPU over instance records
// every frame (shaders/points.wgsl), so effectors are per-frame motion, not structure.
//
// Semantics (w = sampleWeight(field, p), s = sampleScalar, v = sampleVector, c = sampleColor;
// `strength` multiplies; `blend` decides how the result meets the existing value):
//   PositionOffset : position += v * strength (scalar fields: s * axis)
//   Scale          : scale = blend(scale, scale * (1 + s * strength * scaleAxis))  (Replace: scale = s*strength*scaleAxis)
//   Rotation       : rotation = axisAngle(v or axis, s * strength) * rotation
//   Velocity       : velocity += v * strength
//   Color          : color.rgb = mix(color.rgb, c.rgb, c.a * strength)
//   Emission       : emissive *= 1 + s * strength (Replace: emissive = s * strength)
//   Density        : density *= s * strength (Replace: density = s * strength)
//   Attribute      : attribute(target) = blend(existing, value(s or v or c) * strength)
// Blend: Add (existing + new), Multiply (existing * new), Replace (new), Min, Max, Mix (mix by weight).

#include "core/error.hpp"
#include "spatial/field.hpp"
#include "spatial/point_cloud.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::spatial {

enum class EffectorOp : std::uint8_t { PositionOffset, Scale, Rotation, Velocity, Color, Emission, Density, Attribute };
[[nodiscard]] const char* effectorOpName(EffectorOp op);
[[nodiscard]] std::optional<EffectorOp> effectorOpFromName(std::string_view name);
enum class EffectorBlend : std::uint8_t { Add, Multiply, Replace, Min, Max, Mix };
[[nodiscard]] const char* effectorBlendName(EffectorBlend blend);
[[nodiscard]] std::optional<EffectorBlend> effectorBlendFromName(std::string_view name);

struct Effector {
    std::string field;                  // FieldSpec name in the owner's FieldSet
    EffectorOp op = EffectorOp::PositionOffset;
    EffectorBlend blend = EffectorBlend::Add;
    bool enabled = true;
    float strength = 1.0f;
    float weight = 1.0f;                // Mix blend factor
    glm::vec3 axis{0.0f, 1.0f, 0.0f};   // scalar-field direction / rotation axis
    glm::vec3 scaleAxis{1.0f};          // Scale per-axis mask
    std::string target;                 // Attribute op target (created as Float/Vec3/Color by the field type)
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<Effector> fromJson(const nlohmann::json& j);
    [[nodiscard]] std::uint64_t structuralHash() const;
};
constexpr int kMaxEffectors = 8;

// CPU reference: applies enabled effectors in order at `time`. Unknown fields are skipped
// (returns the count applied). `space` transform: when the cloud's positions are in object space
// and the fields are in world space, pass the object's world matrix so sampling happens in world
// space (positions are transformed to world for sampling, results rotated back into object space).
int applyEffectors(PointCloud& cloud, std::span<const Effector> effectors, const FieldSet& fields, double time,
                   const glm::mat4& objectToWorld = glm::mat4(1.0f));
// Same on a record array (the exact GPU semantics: records carry position/rotation/scale/color/
// emissive/density; Velocity and Attribute ops are ignored here).
int applyEffectorsToRecords(std::span<InstanceRecord> records, std::span<const Effector> effectors,
                            const FieldSet& fields, double time, const glm::mat4& objectToWorld = glm::mat4(1.0f));

// GPU packing (48 bytes; see shaders/points.wgsl). `fieldSlot` = index into the packed field array.
struct alignas(16) EffectorGpu {
    std::uint32_t op;
    std::uint32_t blend;
    std::int32_t fieldSlot;      // -1 = disabled
    float strength;
    glm::vec4 axisWeight;        // axis.xyz, weight
    glm::vec4 scaleAxisPad;      // scaleAxis.xyz, 0
};
static_assert(sizeof(EffectorGpu) == 48);
[[nodiscard]] EffectorGpu packEffector(const Effector& effector, const FieldSet& fields);

} // namespace avgen::spatial

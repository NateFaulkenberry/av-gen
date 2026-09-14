#pragma once

// Internal declarations shared by procedural.cpp and hierarchy.cpp (hierarchical instancing,
// ADR-029). Not part of the public API: nothing outside src/scene includes this header.

#include "scene/procedural.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::scene::detail {

// The 1M point cap every cloud respects (the same value as procedural.cpp's kMaxInstances).
constexpr int kMaxCloudInstances = 1048576;

// ---- defined in procedural.cpp ------------------------------------------------------------------

// outer * inner (inner applied first); exact when the outer scale is uniform.
[[nodiscard]] Transform composeTransforms(const Transform& outer, const Transform& inner);
// The rgb multiplier that turns `base` into its hue rotation by `turns` (see procedural.cpp).
[[nodiscard]] glm::vec3 hueMultiplier(const glm::vec3& base, float turns);
// Half-extent of a primitive source (Procedural kind: zero; resolve through sourceHalfExtent).
[[nodiscard]] glm::vec3 primitiveHalfExtent(const SourceSpec& s);
// ADR-199: the primitive's real box rather than a half-extent measured from its origin.
void primitiveBox(const SourceSpec& s, glm::vec3& centre, glm::vec3& half);

// ---- defined in hierarchy.cpp -------------------------------------------------------------------

// The referenced object of a Procedural source, or nullptr when the kind is not Procedural, the
// context has no objects, the name does not resolve, or ctx.depth exceeds kMaxHierarchyDepth.
[[nodiscard]] const ProceduralGeometry* findReference(const ProceduralGeometry& g, const GenerationContext& ctx);
// The half-extent of the mesh the object draws: its primitive's, or the referenced object's
// (recursively, times that object's |sourceTransform.scale|).
[[nodiscard]] glm::vec3 sourceHalfExtent(const ProceduralGeometry& g, const GenerationContext& ctx);
// ADR-199: the source's real box -- centre offset from its own origin, and true half-extent. Unlike
// `sourceHalfExtent`, correct for geometry that is not centred on its origin. For what a person is
// shown; culling keeps the symmetric version, where too big is safe.
void sourceBox(const ProceduralGeometry& g, const GenerationContext& ctx, glm::vec3& centre,
               glm::vec3& half);
// Structural hash of the resolved SourceSpec (the renderer's mesh cache key).
[[nodiscard]] std::uint64_t resolvedSourceHash(const ProceduralGeometry& g, const GenerationContext& ctx);
// The cloud of an object with hierarchy.recursionDepth > 0 and/or a Procedural source (see
// hierarchy.cpp for the exact semantics). generateCloud() forwards here.
[[nodiscard]] spatial::PointCloud generateHierarchicalCloud(const ProceduralGeometry& g, const GenerationContext& ctx);

} // namespace avgen::scene::detail

// Hierarchical instancing (ADR-029): self-recursion (HierarchySpec) and procedural sources
// (a procedural object as the source of another). Implements the reference-aware members of
// ProceduralGeometry declared in scene/procedural.hpp and the internal generator declared in
// scene/procedural_detail.hpp.
//
// Semantics (the header fixes the transform order; these are the remaining choices):
//
// * Level 0 = the flat placements B_i = placement(i) * variation(i) (the distribution's, or the
//   grammar expansion's, points with this object's variation applied), *without* the
//   distribution transform D. Level k = { B_i * L * q : q in level k-1 } in i-major order with
//   L = (offsetPerLevel, rotationPerLevel, uniform scalePerLevel), so every level-k point is a
//   chain of k+1 placements B_i0 L B_i1 L ... B_ik. The object's cloud is level d only
//   (d = recursionDepth): an object draws one mesh per point, so the intermediate levels would
//   just be the same chains cut short. A "tree" that shows every level needs the levels as
//   separate objects (depth 0, 1, 2 ...) or a Branch grammar; recursion here is the
//   "fractal" form (a structure made of smaller copies of its own arrangement).
// * Ids are emission order: level-k point (i, j) has id i * |level k-1| + j. Truncation to
//   min(hierarchy.maxInstances, 1M) happens per level in that order; because the first M points
//   of level k only need the first M points of level k-1, truncating each level is exact.
// * Colour: a chain's material variation comes from its root placement i0 (the outermost B):
//   hue = hueShift * random(i0) + hueGradient * u(i0) [+ (i0 mod (d+1)) / (d+1) turns when
//   colorPerLevel and d > 0], value/emissive from the same root index. The extra term cycles the
//   root branches through d+1 hue families; it is deterministic and costs nothing.
// * A Procedural source composes the referenced object C's cloud under every point: P_i * q_j *
//   C.sourceTransform (C's cloud is C.generateCloud(ctx, depth+1) with C's pointOps applied; C's
//   effectors and deformers are ignored, this object's deformers apply). C.sourceTransform is
//   baked into the points so the renderer's `record * sourceTransform` (this object's, identity
//   by default) draws C's resolved mesh where C would draw it. ids = i * |C| + j; colour and
//   emissive = this object's per-placement multipliers (root i0 as above) times C's; density =
//   C's; extra attributes (grammar depth/rule/branch, user columns) come from the inner point;
//   the bounds column is the leaf primitive's half-extent (all scales live in the point scale).
// * D (distributionTransform) applies last to every point; the seed column is variation.seed
//   (records' randoms are hashInstance(variation.seed, id, channel)); `index` is renumbered.
// * References resolve by name in ctx.objects; an object at depth >= kMaxHierarchyDepth cannot
//   reference further (chains are at most kMaxHierarchyDepth hops), so even a cyclic scene
//   terminates; validateReferences() rejects cycles and over-deep chains up front.

#include "core/log.hpp"
#include "core/noise.hpp"
#include "scene/procedural_detail.hpp"
#include "scene/struct_hash.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace avgen::scene {

namespace {

constexpr std::uint32_t kHueChannel = 4;      // the same channels as procedural.cpp
constexpr std::uint32_t kValueChannel = 5;
constexpr std::uint32_t kEmissiveChannel = 6;

GenerationContext deeper(const GenerationContext& ctx) {
    GenerationContext next = ctx;
    next.depth = ctx.depth + 1;
    return next;
}

const ProceduralGeometry* findByName(const std::vector<ProceduralGeometry>& objects, std::string_view name) {
    for (const ProceduralGeometry& o : objects) {
        if (o.name == name) {
            return &o;
        }
    }
    return nullptr;
}

bool isCoreColumn(std::string_view name) {
    using namespace spatial::attr;
    for (const std::string_view core : {position, rotation, scale, id, seed, density, color, emissive, velocity, normal,
                                        bounds, index}) {
        if (name == core) {
            return true;
        }
    }
    return false;
}

Transform pointTransform(const spatial::PointCloud& c, std::size_t i) {
    Transform t;
    t.position = c.positions()[i];
    const glm::vec4 r = c.rotations()[i];
    t.rotation = glm::quat(r.w, r.x, r.y, r.z);
    t.scale = c.scales()[i];
    return t;
}

void setPointTransform(spatial::PointCloud& c, std::size_t i, const Transform& t) {
    c.positions()[i] = t.position;
    c.rotations()[i] = glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
    c.scales()[i] = t.scale;
}

// Every non-core column of `inner` appears on `out`; row r of `out` takes inner row r % |inner|
// (the composition order is i-major, so the inner index is r modulo the inner count).
void copyExtraColumns(const spatial::PointCloud& inner, spatial::PointCloud& out) {
    const std::size_t m = inner.count();
    const std::size_t total = out.count();
    if (m == 0) {
        return;
    }
    for (const spatial::AttributeBuffer& b : inner.attributes.buffers()) {
        if (isCoreColumn(b.name)) {
            continue;
        }
        auto added = out.attributes.add(b.name, b.type);
        if (!added) {
            continue;
        }
        std::visit(
            [&](const auto& src) {
                using Storage = std::decay_t<decltype(src)>;
                auto& dst = std::get<Storage>((*added)->data);
                for (std::size_t r = 0; r < total; ++r) {
                    dst[r] = src[r % m];
                }
            },
            b.data);
    }
}

// A cloud plus, per row, the index of the root placement its chain started from.
struct Level {
    spatial::PointCloud cloud;
    std::vector<std::uint32_t> root;
    [[nodiscard]] std::size_t count() const { return cloud.count(); }
};

void truncate(Level& level, std::size_t cap) {
    if (level.count() > cap) {
        level.cloud.attributes.resize(cap);
        level.root.resize(cap);
    }
}

// The flat placements B_i of `g` without the distribution transform: the plain generator run on
// a copy with the hierarchy and the reference stripped (its baked colours are ignored; colours
// are computed here from the root placement).
Level flatPlacements(const ProceduralGeometry& g, const GenerationContext& ctx, std::size_t cap) {
    ProceduralGeometry base;
    base.name = g.name;
    base.source = g.source;
    if (base.source.kind == PrimitiveKind::Procedural) {
        base.source.kind = PrimitiveKind::Point; // the bounds column is overwritten below
    }
    base.sourceTransform = g.sourceTransform;
    base.distribution = g.distribution;
    base.variation = g.variation;
    base.material = g.material;
    base.materialVariation = g.materialVariation;
    base.grammar = g.grammar;
    Level level;
    level.cloud = base.generateCloud(ctx);
    level.root.resize(level.count());
    for (std::size_t i = 0; i < level.root.size(); ++i) {
        level.root[i] = static_cast<std::uint32_t>(i);
    }
    truncate(level, cap);
    return level;
}

// next(i, j) = B_i * L * prev_j, i-major, truncated to `cap`.
Level chainLevel(const Level& base, const Level& prev, const Transform& perLevel, std::size_t cap) {
    const std::size_t n = base.count();
    const std::size_t m = prev.count();
    const std::size_t total = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(n) * m, cap));
    Level out;
    out.cloud = spatial::PointCloud(total);
    out.root.resize(total);
    std::vector<Transform> outer(n);
    for (std::size_t i = 0; i < n; ++i) {
        outer[i] = detail::composeTransforms(pointTransform(base.cloud, i), perLevel);
    }
    for (std::size_t r = 0; r < total; ++r) {
        const std::size_t i = r / m;
        const std::size_t j = r % m;
        setPointTransform(out.cloud, r, detail::composeTransforms(outer[i], pointTransform(prev.cloud, j)));
        out.root[r] = static_cast<std::uint32_t>(i);
    }
    copyExtraColumns(prev.cloud, out.cloud);
    return out;
}

// Per-placement material variation (the flat generator's formulas, from the root index).
struct Shade {
    float hueTurns = 0.0f;
    float value = 1.0f;
    float emissiveMul = 1.0f;
};

Shade placementShade(const ProceduralGeometry& g, std::uint32_t index, float u, float extraHueTurns) {
    const MaterialVariation& mv = g.materialVariation;
    const std::uint32_t seed = g.variation.seed;
    const auto signedRandom = [&](std::uint32_t channel) { return hashInstance(seed, index, channel) * 2.0f - 1.0f; };
    Shade s;
    s.hueTurns = mv.hueShift * hashInstance(seed, index, kHueChannel) + mv.hueGradient * u + extraHueTurns;
    s.value = std::max(0.0f, 1.0f + mv.valueRandom * signedRandom(kValueChannel));
    s.emissiveMul = std::max(0.0f, 1.0f + mv.emissiveRandom * signedRandom(kEmissiveChannel) + mv.emissiveGradient * u);
    return s;
}

// Colour/emissive of every row from its root placement (n0 = the level-0 count for u).
void shadeFromRoots(const ProceduralGeometry& g, Level& level, std::size_t n0, int depth) {
    const float invLast = n0 > 1 ? 1.0f / static_cast<float>(n0 - 1) : 0.0f;
    const bool perLevel = g.hierarchy.colorPerLevel && depth > 0;
    const auto families = static_cast<std::uint32_t>(depth + 1);
    auto colors = level.cloud.colors();
    auto emissives = level.cloud.emissives();
    for (std::size_t r = 0; r < level.count(); ++r) {
        const std::uint32_t root = level.root[r];
        const float u = static_cast<float>(root) * invLast;
        const float extra = perLevel ? static_cast<float>(root % families) / static_cast<float>(families) : 0.0f;
        const Shade s = placementShade(g, root, u, extra);
        colors[r] = glm::vec4(detail::hueMultiplier(g.material.baseColor, s.hueTurns) * s.value, 1.0f);
        emissives[r] = detail::hueMultiplier(g.material.emissiveColor, s.hueTurns) * s.emissiveMul;
    }
}

// out(i, j) = P_i * child_j * childSource, i-major, truncated to `cap`; colours multiply.
Level composeChild(const Level& outer, const spatial::PointCloud& child, const Transform& childSource, std::size_t cap) {
    const std::size_t n = outer.count();
    const std::size_t m = child.count();
    const std::size_t total = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(n) * m, cap));
    Level out;
    out.cloud = spatial::PointCloud(total);
    out.root.resize(total);
    std::vector<Transform> inner(m);
    for (std::size_t j = 0; j < m; ++j) {
        inner[j] = detail::composeTransforms(pointTransform(child, j), childSource);
    }
    const auto outerColors = outer.cloud.colors();
    const auto outerEmissives = outer.cloud.emissives();
    const auto childColors = child.colors();
    const auto childEmissives = child.emissives();
    const auto childDensities = child.densities();
    auto colors = out.cloud.colors();
    auto emissives = out.cloud.emissives();
    auto densities = out.cloud.densities();
    for (std::size_t r = 0; r < total; ++r) {
        const std::size_t i = r / m;
        const std::size_t j = r % m;
        setPointTransform(out.cloud, r, detail::composeTransforms(pointTransform(outer.cloud, i), inner[j]));
        out.root[r] = outer.root[i];
        colors[r] = glm::vec4(glm::vec3(outerColors[i]) * glm::vec3(childColors[j]), childColors[j].a);
        emissives[r] = outerEmissives[i] * childEmissives[j];
        densities[r] = childDensities[j];
    }
    copyExtraColumns(child, out.cloud);
    return out;
}

} // namespace

// ================================================================================================
// detail
// ================================================================================================

namespace detail {

const ProceduralGeometry* findReference(const ProceduralGeometry& g, const GenerationContext& ctx) {
    if (g.source.kind != PrimitiveKind::Procedural || ctx.objects == nullptr || g.source.reference.empty() ||
        ctx.depth >= kMaxHierarchyDepth) {
        return nullptr;
    }
    return findByName(*ctx.objects, g.source.reference);
}

glm::vec3 sourceHalfExtent(const ProceduralGeometry& g, const GenerationContext& ctx) {
    if (g.source.kind != PrimitiveKind::Procedural) {
        return primitiveHalfExtent(g.source);
    }
    const ProceduralGeometry* ref = findReference(g, ctx);
    return ref != nullptr ? sourceHalfExtent(*ref, deeper(ctx)) : glm::vec3(0.0f);
}

std::uint64_t resolvedSourceHash(const ProceduralGeometry& g, const GenerationContext& ctx) {
    if (g.source.kind != PrimitiveKind::Procedural) {
        return g.source.structuralHash();
    }
    const ProceduralGeometry* ref = findReference(g, ctx);
    return ref != nullptr ? resolvedSourceHash(*ref, deeper(ctx)) : g.source.structuralHash();
}

spatial::PointCloud generateHierarchicalCloud(const ProceduralGeometry& g, const GenerationContext& ctx) {
    const auto cap = static_cast<std::size_t>(std::clamp(g.hierarchy.maxInstances, 1, kMaxCloudInstances));
    const int depth = std::clamp(g.hierarchy.recursionDepth, 0, kMaxHierarchyDepth);

    // Level 0: the flat placements; levels 1..d: chains.
    const Level base = flatPlacements(g, ctx, cap);
    const std::size_t n0 = base.count();
    Level level = base;
    if (depth > 0) {
        Transform perLevel;
        perLevel.position = g.hierarchy.offsetPerLevel;
        perLevel.rotation = glm::quat(glm::radians(g.hierarchy.rotationPerLevelDegrees));
        perLevel.scale = glm::vec3(g.hierarchy.scalePerLevel);
        for (int k = 1; k <= depth; ++k) {
            level = chainLevel(base, level, perLevel, cap);
        }
    }
    shadeFromRoots(g, level, n0, depth);

    // Procedural source: the referenced object's cloud under every point.
    if (g.source.kind == PrimitiveKind::Procedural) {
        if (const ProceduralGeometry* ref = findReference(g, ctx)) {
            spatial::PointCloud child = ref->generateCloud(deeper(ctx));
            if (auto ok = spatial::applyPointOps(child, ref->pointOps); !ok) {
                log::warn("procedural '{}' (source of '{}'): {}", ref->name, g.name, ok.error().message);
            }
            level = composeChild(level, child, ref->sourceTransform, cap);
        } else {
            log::warn("procedural '{}': source reference '{}' does not resolve (missing, or deeper than {} levels)", g.name,
                      g.source.reference, kMaxHierarchyDepth);
        }
    }

    // Finalise: distribution transform, ids, seeds, bounds, index. Every intermediate
    // sourceTransform is baked into the point scales, so the bounds column is the leaf
    // primitive's half-extent times this object's |sourceTransform.scale| for every row.
    spatial::PointCloud out = std::move(level.cloud);
    const std::size_t n = out.count();
    auto ids = out.ids();
    auto seeds = out.seeds();
    auto extents = out.attributes.view<glm::vec3>(spatial::attr::bounds);
    const glm::vec3 sourceExtent = sourceHalfExtent(g, ctx) * glm::abs(g.sourceTransform.scale);
    for (std::size_t r = 0; r < n; ++r) {
        setPointTransform(out, r, composeTransforms(g.distributionTransform, pointTransform(out, r)));
        ids[r] = static_cast<std::int32_t>(r);
        seeds[r] = static_cast<std::int32_t>(g.variation.seed);
        if (extents) {
            (*extents)[r] = sourceExtent;
        }
    }
    out.renumberIndices();
    return out;
}

} // namespace detail

// ================================================================================================
// ProceduralGeometry: references
// ================================================================================================

std::uint64_t HierarchySpec::structuralHash() const {
    detail::StructHash h;
    h.i32(recursionDepth);
    h.i32(maxInstances);
    h.f32(scalePerLevel);
    h.v3(offsetPerLevel);
    h.v3(rotationPerLevelDegrees);
    h.boolean(colorPerLevel);
    return h.value();
}

Result<MeshData> ProceduralGeometry::resolveSourceMesh(const GenerationContext& ctx) const {
    if (ctx.depth > kMaxHierarchyDepth) {
        return fail("procedural '{}': reference chain deeper than {}", name, kMaxHierarchyDepth);
    }
    if (source.kind != PrimitiveKind::Procedural) {
        return makeSourceMesh(source);
    }
    if (source.reference.empty()) {
        return fail("procedural '{}': a procedural source needs a reference", name);
    }
    if (ctx.objects == nullptr) {
        return fail("procedural '{}': reference '{}' needs the scene's objects", name, source.reference);
    }
    if (ctx.depth >= kMaxHierarchyDepth) {
        return fail("procedural '{}': reference chain deeper than {}", name, kMaxHierarchyDepth);
    }
    const ProceduralGeometry* ref = findByName(*ctx.objects, source.reference);
    if (ref == nullptr) {
        return fail("procedural '{}': reference '{}' is not a procedural object", name, source.reference);
    }
    return ref->resolveSourceMesh(deeper(ctx));
}

Result<void> ProceduralGeometry::validateReferences(const std::vector<ProceduralGeometry>& objects) {
    for (const ProceduralGeometry& o : objects) {
        if (o.source.kind != PrimitiveKind::Procedural) {
            continue;
        }
        std::unordered_set<std::string_view> visited{o.name};
        const ProceduralGeometry* cur = &o;
        int hops = 0;
        while (cur->source.kind == PrimitiveKind::Procedural) {
            if (cur->source.reference.empty()) {
                return fail("procedural '{}': a procedural source needs a reference", cur->name);
            }
            const ProceduralGeometry* next = findByName(objects, cur->source.reference);
            if (next == nullptr) {
                return fail("procedural '{}': reference '{}' is not a procedural object", cur->name, cur->source.reference);
            }
            if (!visited.insert(next->name).second) {
                return fail("procedural '{}': reference cycle through '{}'", o.name, next->name);
            }
            if (++hops > kMaxHierarchyDepth) {
                return fail("procedural '{}': reference chain deeper than {}", o.name, kMaxHierarchyDepth);
            }
            cur = next;
        }
    }
    return {};
}

std::uint64_t ProceduralGeometry::contextualHash(const GenerationContext& ctx) const {
    std::uint64_t h = structuralHash();
    // Procedural source: the referenced object's contextual hash, recursively (a constant when
    // the reference does not resolve, so resolving it later rebuilds).
    if (source.kind == PrimitiveKind::Procedural) {
        const ProceduralGeometry* ref = detail::findReference(*this, ctx);
        h = detail::mixHash(h, ref != nullptr ? ref->contextualHash(deeper(ctx)) : 0x5265664d697373ULL);
    }
    // Self-recursion: the levels are part of the built cloud (also in structuralHash, mixed
    // here so the term is visible next to the others).
    h = detail::mixHash(h, hierarchy.structuralHash());
    // Grammar distributions: the expansion is the placement set.
    h = detail::mixHash(h, grammar.structuralHash());
    // Spline distributions (ADR-026): the referenced curve's own hash, so editing the spline
    // rebuilds the cloud; an unbound name hashes as a constant.
    if (distribution.kind == DistributionKind::Spline) {
        const spatial::Spline* curve = ctx.splines != nullptr ? ctx.splines->find(distribution.spline) : nullptr;
        h = detail::mixHash(h, curve != nullptr ? curve->structuralHash() : 0x5b1a3e5ULL);
    }
    return h;
}

} // namespace avgen::scene

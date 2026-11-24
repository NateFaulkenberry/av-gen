// ADR-2140: applyProceduralParameterValues resolves its parameter paths once and keeps the answers in
// ProceduralParameters::lookups. These tests hold the cache to the scan it replaced: every cached answer
// is what a lookup by path returns, a warm apply writes exactly what a cold one does, and anything that
// changes `all` (a copy, a re-registration) resolves again.
#include "params/parameter_set.hpp"
#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;

namespace {

ProceduralGeometry makeRest(std::size_t deformers) {
    ProceduralGeometry rest;
    rest.name = "lanterns";
    rest.source.kind = PrimitiveKind::Cylinder;
    rest.distribution.kind = DistributionKind::Radial;
    rest.distribution.count = 4;
    const DeformerKind kinds[] = {DeformerKind::Twist, DeformerKind::Path, DeformerKind::Hinge, DeformerKind::Noise};
    for (std::size_t k = 0; k < deformers; ++k) {
        Deformer d;
        d.kind = kinds[k % 4];
        rest.deformers.push_back(d);
    }
    spatial::PointOp op;
    op.kind = spatial::PointOpKind::Translate;
    rest.pointOps = {op, op};
    spatial::Effector e;
    e.field = "pulse";
    rest.effectors = {e, e, e};
    return rest;
}

// Every component of every registered parameter moved to a distinct value inside its hard range, as
// finals (what a route writes), so a cached pointer to the wrong parameter cannot go unnoticed.
void scramble(const ProceduralParameters& p, float salt) {
    float t = salt;
    for (params::IParameter* ip : p.all) {
        for (std::size_t c = 0; c < ip->componentCount(); ++c) {
            t = t * 1.618034f + 0.1371f;
            t -= static_cast<float>(static_cast<int>(t));
            const float lo = std::max(ip->hardMin(c), -50.0f);
            const float hi = std::min(ip->hardMax(c), 50.0f);
            ip->setFinalComponent(c, lo + (hi - lo) * t);
        }
    }
}

// The fields the apply writes, compared exactly (bitwise for floats via ==; the values are finite).
void requireSameApplied(const ProceduralGeometry& a, const ProceduralGeometry& b) {
    CHECK(a.source.kind == b.source.kind);
    CHECK(a.source.radius == b.source.radius);
    CHECK(a.source.size == b.source.size);
    CHECK(a.source.radialSegments == b.source.radialSegments);
    CHECK(a.sourceTransform.position == b.sourceTransform.position);
    CHECK(a.sourceTransform.rotation == b.sourceTransform.rotation);
    CHECK(a.sourceTransform.scale == b.sourceTransform.scale);
    CHECK(a.distribution.kind == b.distribution.kind);
    CHECK(a.distribution.count == b.distribution.count);
    CHECK(a.distribution.radius == b.distribution.radius);
    CHECK(a.distribution.gridCount == b.distribution.gridCount);
    CHECK(a.distribution.center == b.distribution.center);
    CHECK(a.distribution.plane == b.distribution.plane);
    CHECK(a.distribution.orientation == b.distribution.orientation);
    CHECK(a.distribution.splineEnd == b.distribution.splineEnd);
    CHECK(a.distributionTransform.position == b.distributionTransform.position);
    CHECK(a.distributionTransform.rotation == b.distributionTransform.rotation);
    CHECK(a.distributionTransform.scale == b.distributionTransform.scale);
    CHECK(a.variation.seed == b.variation.seed);
    CHECK(a.variation.randomScale == b.variation.randomScale);
    CHECK(a.hierarchy.recursionDepth == b.hierarchy.recursionDepth);
    CHECK(a.hierarchy.offsetPerLevel == b.hierarchy.offsetPerLevel);
    REQUIRE(a.deformers.size() == b.deformers.size());
    for (std::size_t k = 0; k < a.deformers.size(); ++k) {
        INFO("deformer " << k);
        CHECK(a.deformers[k].amount == b.deformers[k].amount);
        CHECK(a.deformers[k].speed == b.deformers[k].speed);
        CHECK(a.deformers[k].phase == b.deformers[k].phase);
        CHECK(a.deformers[k].frequency == b.deformers[k].frequency);
        CHECK(a.deformers[k].scale == b.deformers[k].scale);
        CHECK(a.deformers[k].falloff == b.deformers[k].falloff);
        CHECK(a.deformers[k].center == b.deformers[k].center);
        CHECK(a.deformers[k].axis == b.deformers[k].axis);
        CHECK(a.deformers[k].enabled == b.deformers[k].enabled);
        CHECK(a.deformers[k].pathOffset == b.deformers[k].pathOffset);
        CHECK(a.deformers[k].pathScale == b.deformers[k].pathScale);
        CHECK(a.deformers[k].pathRoll == b.deformers[k].pathRoll);
        CHECK(a.deformers[k].tension == b.deformers[k].tension);
    }
    REQUIRE(a.pointOps.size() == b.pointOps.size());
    for (std::size_t k = 0; k < a.pointOps.size(); ++k) {
        CHECK(a.pointOps[k].amount == b.pointOps[k].amount);
        CHECK(a.pointOps[k].enabled == b.pointOps[k].enabled);
    }
    REQUIRE(a.effectors.size() == b.effectors.size());
    for (std::size_t k = 0; k < a.effectors.size(); ++k) {
        CHECK(a.effectors[k].strength == b.effectors[k].strength);
        CHECK(a.effectors[k].weight == b.effectors[k].weight);
        CHECK(a.effectors[k].enabled == b.effectors[k].enabled);
    }
    CHECK(a.emissiveFieldAmount == b.emissiveFieldAmount);
    CHECK(a.lod.cull == b.lod.cull);
    CHECK(a.lod.maxDistance == b.lod.maxDistance);
    for (std::size_t k = 0; k < 3; ++k) {
        CHECK(a.lod.lodDistances[k] == b.lod.lodDistances[k]);
    }
    CHECK(a.material.baseColor == b.material.baseColor);
    CHECK(a.material.emissiveColor == b.material.emissiveColor);
    CHECK(a.material.emissiveIntensity == b.material.emissiveIntensity);
    CHECK(a.material.roughness == b.material.roughness);
    CHECK(a.material.metallic == b.material.metallic);
    CHECK(a.materialVariation.hueShift == b.materialVariation.hueShift);
    CHECK(a.materialVariation.emissiveGradient == b.materialVariation.emissiveGradient);
    CHECK(a.visible == b.visible);
}

// Each cached answer is the parameter a lookup by full path finds (or nothing, when the path is not
// registered or holds another type: the entry's type is checked by the apply, so only presence here).
void requireCacheMatchesRegistry(params::ParameterSet& params, const ProceduralParameters& p) {
    const ProceduralParameterLookups& c = p.lookups;
    REQUIRE(c.resolvedFor == p.all.data());
    REQUIRE(c.resolvedSize == p.all.size());
    REQUIRE_FALSE(c.fixed.empty());
    for (const ProceduralParameterLookups::Entry& e : c.fixed) {
        INFO(e.rel);
        REQUIRE(e.type != nullptr);
        params::IParameter* byPath = params.find(p.prefix + e.rel);
        if (e.param != nullptr) {
            CHECK(e.param == byPath);
        } else {
            CHECK((byPath == nullptr || typeid(*byPath) != *e.type));
        }
    }
    const auto slots = [&](const std::vector<ProceduralParameterLookups::Entry>& table, std::size_t fields,
                           const char* base) {
        for (std::size_t i = 0; i < table.size(); ++i) {
            const ProceduralParameterLookups::Entry& e = table[i];
            if (e.type == nullptr) {
                continue; // a field this slot's rest never asked for
            }
            const std::string path = p.prefix + base + std::to_string(i / fields + 1) + "/" + e.rel;
            INFO(path);
            params::IParameter* byPath = params.find(path);
            if (e.param != nullptr) {
                CHECK(e.param == byPath);
            } else {
                CHECK((byPath == nullptr || typeid(*byPath) != *e.type));
            }
        }
    };
    slots(c.deform, 13, "deform/");
    slots(c.ops, 2, "ops/");
    slots(c.effector, 3, "effector/");
}

} // namespace

TEST_CASE("A warm parameter apply writes exactly what a freshly resolved one does", "[scene][procedural][adr2140]") {
    params::ParameterSet params;
    const ProceduralGeometry rest = makeRest(4);
    const ProceduralParameters p = registerProceduralParameters(params, rest, "procedural/lanterns/");

    ProceduralGeometry warm = rest;
    REQUIRE(applyProceduralParameterValues(p, rest, warm)); // resolves
    requireCacheMatchesRegistry(params, p);
    const std::size_t fixedCount = p.lookups.fixed.size();
    const std::vector<ProceduralParameterLookups::Entry> firstPass = p.lookups.fixed;

    for (int frame = 0; frame < 4; ++frame) {
        INFO("frame " << frame);
        scramble(p, 0.25f + 0.17f * static_cast<float>(frame));
        REQUIRE(applyProceduralParameterValues(p, rest, warm)); // from the cache

        const ProceduralParameters cold = p; // a copy has new storage, so it resolves from scratch
        CHECK(cold.lookups.resolvedFor != cold.all.data());
        ProceduralGeometry fresh = rest;
        REQUIRE(applyProceduralParameterValues(cold, rest, fresh));
        requireSameApplied(warm, fresh);

        // The cache neither grew nor changed: the warm apply read it, it did not rescan.
        REQUIRE(p.lookups.fixed.size() == fixedCount);
        for (std::size_t k = 0; k < fixedCount; ++k) {
            CHECK(p.lookups.fixed[k].param == firstPass[k].param);
            CHECK(p.lookups.fixed[k].rel == firstPass[k].rel);
        }
    }
    // The values arrive (the cache holds parameters, not values).
    CHECK(warm.deformers[1].pathScale == params.findAs<float>("procedural/lanterns/deform/2/pathScale")->value());
    CHECK(warm.effectors[2].weight == params.findAs<float>("procedural/lanterns/effector/3/weight")->value());
    CHECK(warm.material.baseColor == p.baseColor->value());
}

TEST_CASE("One parameter set applied to parts with different deformer counts keeps each part right",
          "[scene][procedural][adr2140]") {
    // A node's sub-parts take part 0's parameters against their own rest, whose slot counts can differ:
    // the per-slot answers are keyed by slot, so alternating parts never reads another slot's parameter.
    params::ParameterSet params;
    const ProceduralGeometry rest = makeRest(4);
    const ProceduralGeometry fewer = makeRest(1);
    const ProceduralParameters p = registerProceduralParameters(params, rest, "procedural/lanterns/");
    scramble(p, 0.6f);

    ProceduralGeometry part0 = rest;
    ProceduralGeometry part1 = fewer;
    for (int frame = 0; frame < 3; ++frame) {
        REQUIRE(applyProceduralParameterValues(p, rest, part0));
        REQUIRE(applyProceduralParameterValues(p, fewer, part1));
    }
    REQUIRE(part0.deformers.size() == 4);
    REQUIRE(part1.deformers.size() == 1);
    for (std::size_t k = 0; k < 4; ++k) {
        const std::string base = "procedural/lanterns/deform/" + std::to_string(k + 1) + "/";
        CHECK(part0.deformers[k].amount == params.findAs<float>(base + "amount")->value());
        CHECK(part0.deformers[k].axis == params.findAs<glm::vec3>(base + "axis")->value());
    }
    CHECK(part1.deformers[0].amount == params.findAs<float>("procedural/lanterns/deform/1/amount")->value());
    requireCacheMatchesRegistry(params, p);
}

TEST_CASE("Re-registering a procedural resolves its lookups again and never reads a removed parameter",
          "[scene][procedural][adr2140]") {
    // The composition's rebuild: unregister (the parameters are destroyed), reset the handles, register
    // again from a different rest. The new ProceduralParameters starts with no answers; nothing of the
    // old cache survives to point at a destroyed parameter.
    params::ParameterSet params;
    const std::string prefix = "procedural/lanterns/";
    ProceduralParameters p = registerProceduralParameters(params, makeRest(4), prefix);
    ProceduralGeometry live = makeRest(4);
    REQUIRE(applyProceduralParameterValues(p, makeRest(4), live));
    REQUIRE_FALSE(p.lookups.fixed.empty());

    unregisterProceduralParameters(params, p);
    p = {};
    CHECK(p.lookups.fixed.empty());
    CHECK(p.lookups.deform.empty());
    CHECK_FALSE(applyProceduralParameterValues(p, makeRest(4), live)); // nothing registered: a no-op

    const ProceduralGeometry rest2 = makeRest(2);
    p = registerProceduralParameters(params, rest2, prefix);
    CHECK(p.lookups.fixed.empty());
    scramble(p, 0.9f);
    ProceduralGeometry live2 = rest2;
    REQUIRE(applyProceduralParameterValues(p, rest2, live2));
    requireCacheMatchesRegistry(params, p);
    CHECK(live2.deformers[1].amount == params.findAs<float>(prefix + "deform/2/amount")->value());
    CHECK(params.find(prefix + "deform/3/amount") == nullptr);
}

TEST_CASE("A changed parameter list resolves again", "[scene][procedural][adr2140]") {
    // Nothing appends to `all` after registration today; if something ever does, the size no longer
    // matches the one the answers were resolved against and they are thrown away, not trusted.
    params::ParameterSet params;
    const ProceduralGeometry rest = makeRest(1);
    ProceduralParameters p = registerProceduralParameters(params, rest, "procedural/lanterns/");
    ProceduralGeometry live = rest;
    REQUIRE(applyProceduralParameterValues(p, rest, live));
    const auto* before = p.lookups.resolvedFor;
    REQUIRE(before == p.all.data());

    params::ParamDesc<float> extra;
    extra.path = "procedural/lanterns/extra";
    p.all.push_back(&params.add(std::move(extra)));
    REQUIRE(applyProceduralParameterValues(p, rest, live));
    CHECK(p.lookups.resolvedFor == p.all.data());
    CHECK(p.lookups.resolvedSize == p.all.size());
    requireCacheMatchesRegistry(params, p);
}

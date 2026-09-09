// LOD source meshes and the LodSettings data block (ADR-029): scene::makeLodMesh must halve the
// segment counts at level 1, replace the source with a billboard at levels 2 and 3, keep the
// bounds it claims to keep, and be deterministic. Plus the JSON/parameter/structural-hash rules
// of scene::LodSettings, which must leave every existing scene untouched by default.
#include "params/parameter_set.hpp"
#include "scene/procedural.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

float boundingRadiusOf(const scene::MeshData& mesh) {
    const auto [lo, hi] = mesh.bounds();
    return 0.5f * glm::length(hi - lo);
}

scene::SourceSpec cylinderSpec() {
    scene::SourceSpec s;
    s.kind = scene::PrimitiveKind::Cylinder;
    s.radius = 0.5f;
    s.height = 2.0f;
    s.radialSegments = 24;
    s.heightSegments = 4;
    return s;
}

} // namespace

TEST_CASE("makeLodMesh level 0 is the source mesh", "[lod][procedural]") {
    for (const scene::SourceSpec& spec : std::vector<scene::SourceSpec>{cylinderSpec(), scene::SourceSpec{}}) {
        auto source = scene::makeSourceMesh(spec);
        REQUIRE(source.has_value());
        auto lod0 = scene::makeLodMesh(spec, 0);
        REQUIRE(lod0.has_value());
        CHECK(lod0->vertices.size() == source->vertices.size());
        CHECK(lod0->indices == source->indices);
    }
}

TEST_CASE("makeLodMesh level 1 halves the segments and keeps the bounds", "[lod][procedural]") {
    scene::SourceSpec box;
    box.kind = scene::PrimitiveKind::Box;
    box.size = {2.0f, 1.0f, 3.0f};
    box.subdivisions = 8;
    scene::SourceSpec sphere;
    sphere.kind = scene::PrimitiveKind::Sphere;
    sphere.radius = 1.5f;
    sphere.segments = 32;
    sphere.rings = 16;
    scene::SourceSpec torus;
    torus.kind = scene::PrimitiveKind::Torus;
    torus.majorSegments = 48;
    torus.minorSegments = 16;

    for (const scene::SourceSpec& spec : {cylinderSpec(), box, sphere, torus}) {
        auto source = scene::makeSourceMesh(spec);
        REQUIRE(source.has_value());
        auto reduced = scene::makeLodMesh(spec, 1);
        REQUIRE(reduced.has_value());
        REQUIRE(reduced->valid());
        INFO(scene::primitiveKindName(spec.kind));
        CHECK(reduced->vertices.size() < source->vertices.size());
        CHECK(reduced->indices.size() < source->indices.size());
        // The silhouette shrinks a little on curved surfaces (fewer segments inscribe the circle),
        // but the bounds must stay within 10% of the source's.
        const auto [lo, hi] = source->bounds();
        const auto [rlo, rhi] = reduced->bounds();
        for (int axis = 0; axis < 3; ++axis) {
            const float extent = hi[axis] - lo[axis];
            const float reducedExtent = rhi[axis] - rlo[axis];
            CHECK(reducedExtent <= extent * 1.0001f);
            CHECK(reducedExtent >= extent * 0.9f);
        }
        CHECK(boundingRadiusOf(*reduced) >= boundingRadiusOf(*source) * 0.9f);
    }
}

TEST_CASE("makeLodMesh level 1 never falls below the generators' minimum segment counts", "[lod][procedural]") {
    scene::SourceSpec spec = cylinderSpec();
    spec.radialSegments = 3;
    spec.heightSegments = 1;
    auto reduced = scene::makeLodMesh(spec, 1);
    REQUIRE(reduced.has_value());
    REQUIRE(reduced->valid());
    auto source = scene::makeSourceMesh(spec);
    REQUIRE(source.has_value());
    CHECK(reduced->vertices.size() == source->vertices.size()); // already minimal: unchanged

    scene::SourceSpec sphere;
    sphere.kind = scene::PrimitiveKind::Sphere;
    sphere.segments = 3;
    sphere.rings = 2;
    REQUIRE(scene::makeLodMesh(sphere, 1).has_value());
    scene::SourceSpec box;
    box.kind = scene::PrimitiveKind::Box;
    box.subdivisions = 1;
    REQUIRE(scene::makeLodMesh(box, 1).has_value());
}

TEST_CASE("makeLodMesh levels 2 and 3 are billboards sized by the bounding radius", "[lod][procedural]") {
    const scene::SourceSpec spec = cylinderSpec();
    const float radius = scene::sourceBoundingRadius(spec);
    CHECK(radius == Approx(glm::length(glm::vec3(0.5f, 1.0f, 0.5f))));

    auto impostor = scene::makeLodMesh(spec, 2);
    REQUIRE(impostor.has_value());
    CHECK(impostor->vertices.size() == 4);
    CHECK(impostor->indices.size() == 6);
    const auto [lo, hi] = impostor->bounds();
    CHECK(hi.x - lo.x == Approx(2.0f * radius));   // circumscribes the bounding sphere
    CHECK(hi.y - lo.y == Approx(2.0f * radius));
    CHECK(hi.z - lo.z == Approx(0.0f));            // flat in XY: the shader faces it at the camera

    auto scaled = scene::makeLodMesh(spec, 2, 0.5f);
    REQUIRE(scaled.has_value());
    const auto [slo, shi] = scaled->bounds();
    CHECK(shi.x - slo.x == Approx(radius));

    auto point = scene::makeLodMesh(spec, 3);
    REQUIRE(point.has_value());
    CHECK(point->vertices.size() == 4);
    const auto [plo, phi] = point->bounds();
    CHECK(phi.x - plo.x < hi.x - lo.x); // a dot, not an impostor
    CHECK(phi.x - plo.x > 0.0f);

    CHECK(!scene::makeLodMesh(spec, 4).has_value());
    CHECK(!scene::makeLodMesh(spec, 99).has_value());
}

TEST_CASE("makeLodMesh is deterministic", "[lod][procedural]") {
    const scene::SourceSpec spec = cylinderSpec();
    for (int level = 0; level <= 3; ++level) {
        auto a = scene::makeLodMesh(spec, level, 1.25f);
        auto b = scene::makeLodMesh(spec, level, 1.25f);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        INFO("level " << level);
        REQUIRE(a->vertices.size() == b->vertices.size());
        CHECK(a->indices == b->indices);
        for (std::size_t i = 0; i < a->vertices.size(); ++i) {
            CHECK(a->vertices[i].position == b->vertices[i].position);
            CHECK(a->vertices[i].normal == b->vertices[i].normal);
            CHECK(a->vertices[i].uv == b->vertices[i].uv);
        }
    }
}

TEST_CASE("LodSettings defaults are off and only lodCount is structural", "[lod][procedural]") {
    const scene::LodSettings defaults;
    CHECK(defaults.cull == false);
    CHECK(defaults.maxDistance == 0.0f);
    CHECK(defaults.minScreenRadius == 0.0f);
    CHECK(defaults.lodCount == 1);
    CHECK(defaults.lodByScreenSize == true);
    CHECK(defaults.impostorSize == 1.0f);
    for (const float d : defaults.lodDistances) {
        CHECK(d == 0.0f);
    }

    scene::ProceduralGeometry g;
    const std::uint64_t base = g.structuralHash();
    // Per-frame fields never move the structural hash.
    g.lod.cull = true;
    g.lod.maxDistance = 50.0f;
    g.lod.minScreenRadius = 4.0f;
    g.lod.lodDistances[0] = 10.0f;
    g.lod.lodByScreenSize = false;
    g.lod.impostorSize = 2.0f;
    CHECK(g.structuralHash() == base);
    // lodCount does: it decides how many meshes the renderer generates.
    g.lod.lodCount = 3;
    CHECK(g.structuralHash() != base);
    g.lod.lodCount = 1;
    CHECK(g.structuralHash() == base);
}

TEST_CASE("LodSettings round-trips through JSON and is validated", "[lod][procedural]") {
    scene::ProceduralGeometry g;
    g.lod.cull = true;
    g.lod.maxDistance = 120.0f;
    g.lod.minScreenRadius = 2.5f;
    g.lod.lodCount = 4;
    g.lod.lodDistances[0] = 30.0f;
    g.lod.lodDistances[1] = 60.0f;
    g.lod.lodDistances[2] = 90.0f;
    g.lod.lodByScreenSize = false;
    g.lod.impostorSize = 1.5f;
    REQUIRE(g.validate().has_value());

    const nlohmann::json j = g.toJson();
    REQUIRE(j.contains("lod"));
    CHECK(j["lod"]["count"] == 4);
    auto back = scene::ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->lod.cull == true);
    CHECK(back->lod.maxDistance == 120.0f);
    CHECK(back->lod.minScreenRadius == 2.5f);
    CHECK(back->lod.lodCount == 4);
    CHECK(back->lod.lodDistances[1] == 60.0f);
    CHECK(back->lod.lodByScreenSize == false);
    CHECK(back->lod.impostorSize == 1.5f);
    CHECK(back->structuralHash() == g.structuralHash());

    // A file without "lod" keeps the defaults, so old scenes are untouched.
    nlohmann::json without = g.toJson();
    without.erase("lod");
    auto plain = scene::ProceduralGeometry::fromJson(without);
    REQUIRE(plain.has_value());
    CHECK(plain->lod.lodCount == 1);
    CHECK(plain->lod.cull == false);

    // Out-of-range settings are rejected.
    scene::ProceduralGeometry bad = g;
    bad.lod.lodCount = 5;
    CHECK(!bad.validate().has_value());
    bad = g;
    bad.lod.maxDistance = -1.0f;
    CHECK(!bad.validate().has_value());
    bad = g;
    bad.lod.impostorSize = 0.0f;
    CHECK(!bad.validate().has_value());
}

TEST_CASE("LOD parameters are registered and applied without a rebuild", "[lod][procedural][params]") {
    scene::ProceduralGeometry rest;
    rest.lod.lodCount = 3;
    rest.lod.impostorSize = 2.0f;
    rest.lod.lodByScreenSize = false;
    params::ParameterSet set;
    const scene::ProceduralParameters p = scene::registerProceduralParameters(set, rest, "procedural/test/");
    REQUIRE(p.lodEnabled != nullptr);
    REQUIRE(p.lodMaxDistance != nullptr);
    REQUIRE(p.lodMinScreenRadius != nullptr);
    REQUIRE(p.lodDistance[0] != nullptr);
    REQUIRE(p.lodDistance[2] != nullptr);
    CHECK(set.find("procedural/test/lod/enabled") != nullptr);
    CHECK(set.find("procedural/test/lod/enabled")->kind() == params::ParamKind::Bool);
    CHECK(set.find("procedural/test/lod/distance2") != nullptr);

    scene::ProceduralGeometry live = rest;
    set.resetFinals();
    CHECK(scene::applyProceduralParameters(p, rest, live)); // first build
    const std::uint64_t version = live.structureVersion;

    p.lodEnabled->setBase(true);
    p.lodMaxDistance->setBase(75.0f);
    p.lodMinScreenRadius->setBase(3.0f);
    p.lodDistance[0]->setBase(12.0f);
    set.resetFinals();
    CHECK_FALSE(scene::applyProceduralParameters(p, rest, live)); // per-frame uniforms: no rebuild
    CHECK(live.structureVersion == version);
    CHECK(live.lod.cull == true);
    CHECK(live.lod.maxDistance == 75.0f);
    CHECK(live.lod.minScreenRadius == 3.0f);
    CHECK(live.lod.lodDistances[0] == 12.0f);
    // Structural / mesh-shaping fields keep the authored values.
    CHECK(live.lod.lodCount == 3);
    CHECK(live.lod.impostorSize == 2.0f);
    CHECK(live.lod.lodByScreenSize == false);

    scene::unregisterProceduralParameters(set, p);
    CHECK(set.find("procedural/test/lod/enabled") == nullptr);
}

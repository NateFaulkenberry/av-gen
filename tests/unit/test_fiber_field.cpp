// ADR-1180 (the Fiber source) and ADR-1181 (the Streamline deformer): the CPU half. The GPU half --
// that the vertex stage builds the same centre line -- is tests/rendering/test_fiber_field_gpu.cpp.

#include "scene/procedural.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

ProceduralGeometry fiberObject() {
    ProceduralGeometry g;
    g.name = "fibers";
    g.source.kind = PrimitiveKind::Fiber;
    g.source.fiberLength = 2.0f;
    g.source.fiberWidth = 0.01f;
    g.source.fiberTaper = 0.25f;
    g.source.fiberSegments = 8;
    g.source.fiberMinPixels = 1.0f;
    g.distribution.kind = DistributionKind::Grid;
    g.distribution.gridCount = {4, 4, 4};
    Deformer d;
    d.kind = DeformerKind::Streamline;
    d.space = DeformSpace::World;
    d.field = "flow";
    d.amount = 3.0f;
    d.falloff = 0.5f;
    g.deformers.push_back(d);
    return g;
}

} // namespace

TEST_CASE("a fiber strip carries its arc length and its lane", "[fiber][procedural]") {
    const MeshData m = makeFiberStrip(3.0f, 0.02f, 0.5f, 6, 0.8f);
    REQUIRE(m.vertices.size() == 14);
    REQUIRE(m.indices.size() == 36);
    for (int k = 0; k <= 6; ++k) {
        const Vertex& a = m.vertices[static_cast<std::size_t>(2 * k)];
        const Vertex& b = m.vertices[static_cast<std::size_t>(2 * k + 1)];
        const float half = 0.01f * (1.0f + (0.5f - 1.0f) * static_cast<float>(k) / 6.0f);
        CHECK_THAT(a.position.y, WithinAbs(0.5f * static_cast<float>(k), 1e-6));
        CHECK_THAT(a.position.x, WithinAbs(-half, 1e-7));
        CHECK_THAT(b.position.x, WithinAbs(half, 1e-7));
        // The lane: segment length, min pixels, root half-width -- what the vertex stage reads.
        CHECK_THAT(a.normal.x, WithinAbs(0.5f, 1e-7));
        CHECK_THAT(a.normal.y, WithinAbs(0.8f, 1e-7));
        CHECK_THAT(a.normal.z, WithinAbs(0.01f, 1e-7));
    }
}

TEST_CASE("a fiber's LOD levels are the same fiber with fewer segments", "[fiber][procedural]") {
    SourceSpec s = fiberObject().source;
    s.fiberSegments = 12;
    for (int level = 0; level <= 3; ++level) {
        CHECK_FALSE(lodLevelIsImpostor(s, level));
        auto m = makeLodMesh(s, level);
        REQUIRE(m.has_value());
        const int segments = std::max(12 >> level, 1);
        CHECK(m->vertices.size() == static_cast<std::size_t>(2 * (segments + 1)));
        CHECK_THAT(m->vertices.back().position.y, WithinAbs(2.0f, 1e-5)); // same length at every level
    }
}

TEST_CASE("a fiber is refused when it cannot be drawn", "[fiber][procedural]") {
    SourceSpec s = fiberObject().source;
    CHECK(s.validate().has_value());
    s.fiberSegments = 0;
    CHECK_FALSE(s.validate().has_value());
    s.fiberSegments = 33;
    CHECK_FALSE(s.validate().has_value());
    s = fiberObject().source;
    s.fiberWidth = 0.0f;
    CHECK_FALSE(s.validate().has_value());
    s = fiberObject().source;
    s.fiberTaper = 1.5f;
    CHECK_FALSE(s.validate().has_value());
}

TEST_CASE("a streamline needs a field and a fiber source", "[fiber][procedural]") {
    ProceduralGeometry g = fiberObject();
    CHECK(g.validate().has_value());
    g.deformers[0].field.clear();
    CHECK_FALSE(g.validate().has_value());
    g = fiberObject();
    g.source.kind = PrimitiveKind::Cylinder;
    CHECK_FALSE(g.validate().has_value()); // a streamline on a cylinder is refused, not ignored
}

TEST_CASE("a fiber object round-trips through JSON", "[fiber][procedural]") {
    const ProceduralGeometry g = fiberObject();
    const nlohmann::json j = g.toJson();
    CHECK(j.at("source").at("kind") == "fiber");
    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->source.fiberLength == g.source.fiberLength);
    CHECK(back->source.fiberWidth == g.source.fiberWidth);
    CHECK(back->source.fiberTaper == g.source.fiberTaper);
    CHECK(back->source.fiberSegments == g.source.fiberSegments);
    CHECK(back->source.fiberMinPixels == g.source.fiberMinPixels);
    REQUIRE(back->deformers.size() == 1);
    CHECK(back->deformers[0].kind == DeformerKind::Streamline);
    CHECK(back->deformers[0].field == "flow");
    CHECK(back->source.structuralHash() == g.source.structuralHash());
    // A cylinder writes no fiber keys, so every scene authored before ADR-1180 round-trips unchanged.
    ProceduralGeometry c;
    c.name = "c";
    CHECK_FALSE(c.toJson().at("source").contains("fiberLength"));
}

TEST_CASE("without a field a fiber is straight along its axis", "[fiber][procedural]") {
    const auto pts = fiberCentreLine(nullptr, {1, 2, 3}, {0, 0, 2}, 4.0f, 8, 0.0, nullptr);
    REQUIRE(pts.size() == 9);
    for (int k = 0; k <= 8; ++k) {
        CHECK_THAT(pts[static_cast<std::size_t>(k)].z, WithinAbs(3.0f + 0.5f * static_cast<float>(k), 1e-5));
        CHECK_THAT(pts[static_cast<std::size_t>(k)].x, WithinAbs(1.0f, 1e-6));
    }
}

TEST_CASE("a streamline follows its field and keeps its length", "[fiber][procedural]") {
    spatial::FieldSet fields;
    spatial::FieldSpec flow;
    flow.name = "flow";
    flow.kind = spatial::FieldKind::Direction;
    flow.axis = {1, 0, 0};
    fields.fields.push_back(flow);
    Deformer d;
    d.kind = DeformerKind::Streamline;
    d.field = "flow";
    d.amount = 1000.0f; // follows the field exactly
    const auto pts = fiberCentreLine(&d, {0, 0, 0}, {0, 1, 0}, 2.0f, 8, 0.0, &fields);
    REQUIRE(pts.size() == 9);
    float arc = 0.0f;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        arc += glm::length(pts[i] - pts[i - 1]);
    }
    CHECK_THAT(arc, WithinAbs(2.0f, 1e-4)); // arc length is the fiber's length, however it bends
    CHECK(pts.back().x > 1.99f);            // turned onto the field from the first segment
    CHECK(std::abs(pts.back().y) < 0.01f);

    // Amount 0 is straight; the control that must fail the "follows" check above.
    d.amount = 0.0f;
    const auto straight = fiberCentreLine(&d, {0, 0, 0}, {0, 1, 0}, 2.0f, 8, 0.0, &fields);
    CHECK_THAT(straight.back().y, WithinAbs(2.0f, 1e-5));
    CHECK_THAT(straight.back().x, WithinAbs(0.0f, 1e-6));

    // Stiffness: no steering at the root, so the first segment leaves along the fiber's own axis.
    d.amount = 1000.0f;
    d.falloff = 1.0f;
    const auto stiff = fiberCentreLine(&d, {0, 0, 0}, {0, 1, 0}, 2.0f, 8, 0.0, &fields);
    CHECK_THAT(stiff[1].x, WithinAbs(0.0f, 1e-6));
    CHECK_THAT(stiff[1].y, WithinAbs(0.25f, 1e-6));
    CHECK(stiff.back().x > 1.0f);
}

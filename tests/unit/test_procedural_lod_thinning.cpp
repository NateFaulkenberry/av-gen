// ADR-1548: a procedural node's mesh LOD can thin small separate shells (`"thinning": true` in the lod block).
//
// A tree's leaf cards are thousands of two-triangle quads. The preserving simplifier cannot touch a quad, so before
// this a stalled level fell to the sloppy simplifier, which reaches its triangle count by deleting cards. The
// cases build a canopy of cards and check the property the flag exists for: a thinned level keeps only whole cards
// (every surviving triangle at least as large as a source triangle -- cards are removed and grown, never shrunk or
// fused), and it is still a real reduction.

#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>

using namespace avgen;
using scene::MeshData;
using scene::Vertex;

namespace {

constexpr int kCards = 2000;

std::shared_ptr<const MeshData> canopy() {
    auto mesh = std::make_shared<MeshData>();
    mesh->name = "leaves";
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int c = 0; c < kCards; ++c) {
        const glm::vec3 centre(u(rng) * 6.0f, 8.0f + u(rng) * 3.0f, u(rng) * 6.0f);
        const glm::vec3 a = glm::normalize(glm::vec3(u(rng), u(rng), u(rng)) + glm::vec3(0.01f));
        const glm::vec3 b = glm::normalize(glm::cross(a, glm::vec3(0.3f, 1.0f, 0.2f)));
        const glm::vec3 n = glm::normalize(glm::cross(a, b));
        const auto base = static_cast<std::uint32_t>(mesh->vertices.size());
        const float h = 0.4f;
        for (const glm::vec2 q : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)}) {
            mesh->vertices.push_back(Vertex{centre + (a * q.x + b * q.y) * h, n, (q + 1.0f) * 0.5f});
        }
        mesh->indices.insert(mesh->indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

float totalArea(const MeshData& m) {
    float sum = 0.0f;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::vec3 p0 = m.vertices[m.indices[i]].position;
        const glm::vec3 p1 = m.vertices[m.indices[i + 1]].position;
        const glm::vec3 p2 = m.vertices[m.indices[i + 2]].position;
        sum += 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
    }
    return sum;
}

float minTriangleArea(const MeshData& m) {
    float least = 1e30f;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::vec3 p0 = m.vertices[m.indices[i]].position;
        const glm::vec3 p1 = m.vertices[m.indices[i + 1]].position;
        const glm::vec3 p2 = m.vertices[m.indices[i + 2]].position;
        least = std::min(least, 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0)));
    }
    return least;
}

scene::SourceSpec cardSpec() {
    scene::SourceSpec spec;
    spec.kind = scene::PrimitiveKind::Mesh;
    spec.asset = "leaves.glb"; // validated, never opened: the mesh is supplied already resolved
    spec.assetMesh = canopy();
    return spec;
}

} // namespace

TEST_CASE("thinning keeps whole, grown cards and reduces the canopy", "[procedural][lod][adr1548]") {
    const scene::SourceSpec spec = cardSpec();
    const std::size_t sourceTriangles = spec.assetMesh->indices.size() / 3;
    const float sourceLeast = minTriangleArea(*spec.assetMesh);
    for (int level = 1; level <= 3; ++level) {
        auto thinned = scene::makeLodMesh(spec, level, 1.0f, glm::vec3(1.0f), false, /*thinning=*/true);
        REQUIRE(thinned.has_value());
        const std::size_t triangles = thinned->indices.size() / 3;
        INFO("level " << level << ": " << triangles << " of " << sourceTriangles);
        CHECK(triangles < sourceTriangles * 3 / 4);                      // a real reduction
        CHECK(triangles % 2 == 0);                                       // whole cards
        CHECK(minTriangleArea(*thinned) >= sourceLeast * 0.999f);        // removed and grown, never collapsed
        // The canopy keeps its cover -- up to ShellThinning::maxScale (3x a card's size), which the 4% rung exceeds:
        // there the level is honestly sparser, 36% of the source's leaf area against the sloppy simplifier's 4%.
        CHECK(totalArea(*thinned) >= (level < 3 ? 0.8f : 0.3f) * totalArea(*spec.assetMesh));
    }
}

TEST_CASE("without thinning a stalled card level loses its cover (the control)", "[procedural][lod][adr1548]") {
    const scene::SourceSpec spec = cardSpec();
    auto sloppy = scene::makeLodMesh(spec, 3, 1.0f, glm::vec3(1.0f));
    REQUIRE(sloppy.has_value());
    // The sloppy simplifier reaches its count by deleting the cards it collapses and growing none: measured, 160 of
    // 4,000 triangles at level 3 -- a canopy with 96% of its leaf area gone.
    CHECK(totalArea(*sloppy) < 0.5f * totalArea(*spec.assetMesh));
}

TEST_CASE("thinning reads, round-trips, is written only when set, and changes the structural hash",
          "[procedural][json][lod][adr1548]") {
    auto on = scene::ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"name": "leaves", "source": {"kind": "box"}, "lod": {"count": 3, "thinning": true}})"));
    REQUIRE(on.has_value());
    CHECK(on->lod.thinning);
    auto back = scene::ProceduralGeometry::fromJson(on->toJson());
    REQUIRE(back.has_value());
    CHECK(back->lod.thinning);
    auto off = scene::ProceduralGeometry::fromJson(
        nlohmann::json::parse(R"({"name": "leaves", "source": {"kind": "box"}, "lod": {"count": 3}})"));
    REQUIRE(off.has_value());
    CHECK_FALSE(off->toJson().at("lod").contains("thinning"));
    CHECK(on->lod.structuralHash() != off->lod.structuralHash());
}

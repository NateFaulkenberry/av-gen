// ADR-1959: the slab fast path. A procedural box lattice can ask to be drawn as slabs (`"slabs": true`): a three-quad
// mesh the vertex stage turns to the visible face of each axis. These cases hold the key both ways and the mesh's
// shape -- the three faces of makeBox that point along +x, +y and +z, wound counter-clockwise from outside, with
// makeBox's own vertices, normals and uv, so the material inputs read the same on either path.

#include "scene/procedural.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>

using namespace avgen;
using scene::ProceduralGeometry;

TEST_CASE("slabs reads, round-trips, and is absent when off", "[procedural][json][geography][adr1959]") {
    auto on = ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"name": "tiles", "slabs": true, "source": {"kind": "box"}, "distribution": {"kind": "single"}})"));
    REQUIRE(on.has_value());
    CHECK(on->slabs);
    const nlohmann::json j = on->toJson();
    REQUIRE(j.contains("slabs"));
    CHECK(j.at("slabs") == true);
    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->slabs);

    auto off = ProceduralGeometry::fromJson(nlohmann::json::parse(R"({"name": "rock", "source": {"kind": "box"}})"));
    REQUIRE(off.has_value());
    CHECK_FALSE(off->slabs);
    CHECK_FALSE(off->toJson().contains("slabs"));

    CHECK_FALSE(ProceduralGeometry::fromJson(nlohmann::json::parse(R"({"slabs": 1})")).has_value());
}

TEST_CASE("the slab mesh is the +x, +y and +z faces of the box", "[procedural][geography][adr1959]") {
    const glm::vec3 size{0.9f, 0.15f, 0.9f};
    const scene::MeshData slab = scene::makeSlabMesh(size);
    REQUIRE(slab.vertices.size() == 12);
    REQUIRE(slab.indices.size() == 18);

    // Every vertex is a vertex of the box, on the face its normal names.
    for (const auto& v : slab.vertices) {
        CHECK(glm::length(v.normal) == Catch::Approx(1.0f));
        CHECK(std::abs(std::abs(v.position.x) - size.x * 0.5f) < 1e-6f);
        CHECK(std::abs(std::abs(v.position.y) - size.y * 0.5f) < 1e-6f);
        CHECK(std::abs(std::abs(v.position.z) - size.z * 0.5f) < 1e-6f);
        CHECK(glm::dot(v.position, v.normal) == Catch::Approx(glm::dot(size * 0.5f, glm::abs(v.normal))));
    }
    int px = 0, py = 0, pz = 0;
    for (const auto& v : slab.vertices) {
        px += v.normal.x > 0.5f;
        py += v.normal.y > 0.5f;
        pz += v.normal.z > 0.5f;
    }
    CHECK(px == 4);
    CHECK(py == 4);
    CHECK(pz == 4);

    // Counter-clockwise from outside: each triangle's geometric normal agrees with its vertices' normal.
    for (std::size_t t = 0; t < slab.indices.size(); t += 3) {
        const auto& a = slab.vertices[slab.indices[t]];
        const auto& b = slab.vertices[slab.indices[t + 1]];
        const auto& c = slab.vertices[slab.indices[t + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        CHECK(glm::dot(n, a.normal) > 0.0f);
    }

    // The same corners, normals and uv as the full box's matching faces.
    const scene::MeshData box = scene::makeBox(size, 1);
    for (const auto& v : slab.vertices) {
        const bool found = std::any_of(box.vertices.begin(), box.vertices.end(), [&](const scene::Vertex& w) {
            return glm::length(w.position - v.position) < 1e-6f && glm::length(w.normal - v.normal) < 1e-6f &&
                   glm::length(w.uv - v.uv) < 1e-6f;
        });
        CHECK(found);
    }
}

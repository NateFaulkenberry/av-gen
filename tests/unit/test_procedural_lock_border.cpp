// ADR-1546: a procedural node's mesh LOD can hold its open border still (`"lockBorder": true` in the lod block), so a
// surface cut into chunks -- a terrain band -- can take simplified levels per chunk without opening cracks where a
// near chunk at LOD0 meets a far one at LOD2.
//
// The cases build a rolling grid (a terrain chunk in miniature), ask makeLodMesh for its simplified levels, and check
// the one property the flag exists for: every vertex on the source's border is still in the level, at the same place.
// The control shows the property is not free -- without the flag the simplifier moves or drops border vertices.

#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace avgen;
using scene::MeshData;
using scene::Vertex;

namespace {

constexpr int kN = 48;           // grid cells per side
constexpr float kExtent = 120.0f; // metres, half-width

float height(float x, float z) {
    return 6.0f * std::sin(x * 0.045f) * std::cos(z * 0.06f) + 2.0f * std::sin(x * 0.13f + z * 0.09f);
}

std::shared_ptr<const MeshData> rollingGrid() {
    auto mesh = std::make_shared<MeshData>();
    mesh->name = "chunk";
    for (int i = 0; i <= kN; ++i) {
        for (int j = 0; j <= kN; ++j) {
            const float x = -kExtent + 2.0f * kExtent * static_cast<float>(i) / kN;
            const float z = -kExtent + 2.0f * kExtent * static_cast<float>(j) / kN;
            mesh->vertices.push_back(Vertex{glm::vec3(x, height(x, z), z), glm::vec3(0.0f, 1.0f, 0.0f),
                                            glm::vec2(static_cast<float>(i) / kN, static_cast<float>(j) / kN)});
        }
    }
    for (int i = 0; i < kN; ++i) {
        for (int j = 0; j < kN; ++j) {
            const auto a = static_cast<std::uint32_t>(i * (kN + 1) + j);
            const auto b = a + 1;
            const auto c = a + static_cast<std::uint32_t>(kN + 1);
            const auto d = c + 1;
            mesh->indices.insert(mesh->indices.end(), {a, b, d, a, d, c});
        }
    }
    return mesh;
}

bool onBorder(const glm::vec3& p) {
    return std::abs(std::abs(p.x) - kExtent) < 1e-3f || std::abs(std::abs(p.z) - kExtent) < 1e-3f;
}

// How many of the source's border vertices are present, at the same position, in `level`.
int bordersKept(const MeshData& source, const MeshData& level) {
    int kept = 0;
    for (const Vertex& v : source.vertices) {
        if (!onBorder(v.position)) {
            continue;
        }
        for (const Vertex& w : level.vertices) {
            if (glm::length(w.position - v.position) < 1e-4f) {
                ++kept;
                break;
            }
        }
    }
    return kept;
}

scene::SourceSpec meshSpec() {
    scene::SourceSpec spec;
    spec.kind = scene::PrimitiveKind::Mesh;
    spec.asset = "chunk.glb"; // validated, never opened: the mesh is supplied already resolved
    spec.assetMesh = rollingGrid();
    return spec;
}

} // namespace

TEST_CASE("lockBorder keeps every border vertex of a simplified level", "[procedural][lod][adr1546]") {
    const scene::SourceSpec spec = meshSpec();
    const MeshData& source = *spec.assetMesh;
    const int border = 4 * kN;
    REQUIRE(bordersKept(source, source) == border);
    const std::size_t sourceTriangles = source.indices.size() / 3;
    for (int level = 1; level <= 3; ++level) {
        auto locked = scene::makeLodMesh(spec, level, 1.0f, glm::vec3(1.0f), /*lockBorder=*/true);
        REQUIRE(locked.has_value());
        INFO("level " << level << ": " << locked->indices.size() / 3 << " of " << sourceTriangles << " triangles");
        CHECK(locked->indices.size() / 3 < sourceTriangles); // it is still a LOD: the interior simplified
        CHECK(bordersKept(source, *locked) == border);       // and the border did not move
    }
}

TEST_CASE("without lockBorder the simplifier moves the border (the control)", "[procedural][lod][adr1546]") {
    const scene::SourceSpec spec = meshSpec();
    auto loose = scene::makeLodMesh(spec, 3, 1.0f, glm::vec3(1.0f));
    REQUIRE(loose.has_value());
    CHECK(bordersKept(*spec.assetMesh, *loose) < 4 * kN);
}

TEST_CASE("lockBorder reads, round-trips, is written only when set, and changes the structural hash",
          "[procedural][json][lod][adr1546]") {
    auto on = scene::ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"name": "band", "source": {"kind": "box"}, "lod": {"count": 3, "lockBorder": true}})"));
    REQUIRE(on.has_value());
    CHECK(on->lod.lockBorder);
    const nlohmann::json j = on->toJson();
    REQUIRE(j.at("lod").contains("lockBorder"));
    auto back = scene::ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->lod.lockBorder);

    auto off = scene::ProceduralGeometry::fromJson(
        nlohmann::json::parse(R"({"name": "band", "source": {"kind": "box"}, "lod": {"count": 3}})"));
    REQUIRE(off.has_value());
    CHECK_FALSE(off->lod.lockBorder);
    CHECK_FALSE(off->toJson().at("lod").contains("lockBorder"));
    // The levels differ, so the renderer's mesh cache must not hand one for the other.
    CHECK(on->lod.structuralHash() != off->lod.structuralHash());

    CHECK_FALSE(scene::ProceduralGeometry::fromJson(
                    nlohmann::json::parse(R"({"source": {"kind": "box"}, "lod": {"lockBorder": "yes"}})"))
                    .has_value());
}

// ADR-1814: makeLodMesh builds one LOD chain per source and settings and serves every level from it. The memo must be
// invisible: every call returns the same bytes for a level whichever level was asked first, and the lockBorder chain
// and the free one are kept apart. (That a memoised level equals the pre-ADR-1814 output is shown on whole frames:
// tools/picture_check.py, Rainbow World SAME before and after.)
TEST_CASE("makeLodMesh's chain memo serves every level the same bytes", "[procedural][lod][adr1814]") {
    const scene::SourceSpec spec = meshSpec();
    for (const bool lock : {false, true}) {
        std::vector<MeshData> first;
        for (int level = 3; level >= 1; --level) { // reversed: the memo is filled by level 3, then read by 2 and 1
            auto m = scene::makeLodMesh(spec, level, 1.0f, glm::vec3(1.0f), lock);
            REQUIRE(m.has_value());
            first.push_back(*m);
        }
        for (int level = 1; level <= 3; ++level) {
            auto again = scene::makeLodMesh(spec, level, 1.0f, glm::vec3(1.0f), lock);
            REQUIRE(again.has_value());
            const MeshData& before = first[static_cast<std::size_t>(3 - level)];
            INFO("lockBorder " << lock << ", level " << level);
            CHECK(again->indices == before.indices);
            REQUIRE(again->vertices.size() == before.vertices.size());
            CHECK(std::memcmp(again->vertices.data(), before.vertices.data(),
                              before.vertices.size() * sizeof(Vertex)) == 0);
        }
    }
    // The two settings give different chains: the memo did not hand one to the other.
    auto locked = scene::makeLodMesh(spec, 2, 1.0f, glm::vec3(1.0f), true);
    auto free = scene::makeLodMesh(spec, 2, 1.0f, glm::vec3(1.0f), false);
    REQUIRE(locked.has_value());
    REQUIRE(free.has_value());
    CHECK(locked->indices != free->indices);
}

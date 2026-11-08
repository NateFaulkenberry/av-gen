// ADR-1959: the slab fast path. A procedural box lattice can ask to be drawn as slabs (`"slabs": true`): a three-quad
// mesh the vertex stage turns to the visible face of each axis. These cases hold the key both ways and the mesh's
// shape -- the three faces of makeBox that point along +x, +y and +z, wound counter-clockwise from outside, with
// makeBox's own vertices, normals and uv, so the material inputs read the same on either path.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/material_program.hpp"
#include "support/temp_dir.hpp"
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

TEST_CASE("tileBorder holds the authored band near and never thins below its pixels far", "[material][geography][adr1976]") {
    using scene::MaterialContext;
    using scene::MaterialOp;
    using scene::MaterialOpKind;
    scene::MaterialProgram program;
    program.name = "border";
    MaterialOp in;
    in.kind = MaterialOpKind::Input;
    in.dst = 0;
    in.input = scene::MaterialInput::LocalPosition;
    MaterialOp border;
    border.kind = MaterialOpKind::TileBorder;
    border.dst = 1;
    border.srcA = 0;
    border.constant = {0.46f, 0.46f, 0.02f, 0.09f};
    border.value = 1.0f;
    program.ops = {in, border};
    program.baseColorRegister = 1;
    REQUIRE(program.validate().has_value());
    const auto run = [&](glm::vec3 local, glm::vec2 fw) {
        MaterialContext ctx;
        ctx.localPosition = local;
        ctx.localFootprint = fw;
        return scene::evaluateMaterialProgram(program, ctx, scene::MaterialResult{}).baseColor;
    };
    // Near (a millimetre a pixel): the authored 2 cm band. Inside it is border; 5 cm in is not.
    CHECK(run({0.45f, 0.09f, 0.0f}, {0.001f, 0.001f}).x == Catch::Approx(1.0f));
    CHECK(run({0.41f, 0.09f, 0.0f}, {0.001f, 0.001f}).x == Catch::Approx(0.0f));
    // Far (5 cm a pixel): the band is one pixel wide, not 2 cm, so 3 cm in is still border.
    CHECK(run({0.43f, 0.09f, 0.0f}, {0.05f, 0.05f}).x > 0.5f);
    CHECK(run({0.20f, 0.09f, 0.0f}, {0.05f, 0.05f}).x == Catch::Approx(0.0f));
    // The top-face mask, and the distance to the edge in pixels.
    CHECK(run({0.0f, 0.09f, 0.0f}, {0.01f, 0.01f}).z == Catch::Approx(1.0f));
    CHECK(run({0.46f, 0.0f, 0.0f}, {0.01f, 0.01f}).z == Catch::Approx(0.0f));
    CHECK(run({0.36f, 0.09f, 0.0f}, {0.01f, 0.01f}).y == Catch::Approx(10.0f).margin(1e-3));
}

TEST_CASE("a tiles ring's slabs and far-tile threshold reach the flattened scene and its uniform",
          "[procedural][composition][geography][adr1959]") {
    // The shape of the art agent's rings (examples/musical-geography/world.py): a "tiles" generator with slabs, a 1x
    // ring with a gap and an explicit threshold, and a 2x ring with no gap and the default.
    const auto json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "rings",
        "nodes": [
            {"name": "mgTiles1", "kind": "procedural", "position": [0, 0, 0], "procedural": {
                "slabs": true, "slabFarPixels": 9.0,
                "source": {"kind": "box", "size": [0.915, 0.3, 0.915]},
                "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1, "cellSize": 1.0,
                    "viewDistance": 40.0, "align": 4, "tiles": {"seed": 3}}},
                "lod": {"cull": true}}},
            {"name": "mgTiles2", "kind": "procedural", "position": [0, 0, 0], "procedural": {
                "slabs": true,
                "source": {"kind": "box", "size": [2.0, 0.36, 2.0]},
                "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1, "cellSize": 2.0,
                    "viewDistance": 80.0, "align": 2, "finePitch": 1.0,
                    "hole": {"cellSize": 1.0, "viewDistance": 40.0, "align": 4}, "tiles": {"seed": 3}}},
                "lod": {"cull": true}}}
        ]})");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    for (int frame = 0; frame < 2; ++frame) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = frame / 30.0;
        (*c)->update(ft);
    }
    int seen = 0;
    for (const auto& p : (*c)->scene().procedurals) {
        CAPTURE(p.name);
        CHECK(p.slabs);
        const glm::vec4 rule = scene::slabFarRule(p);
        CHECK(rule.w == 1.0f);
        if (p.name.find("mgTiles1") != std::string::npos) {
            ++seen;
            CHECK(p.slabFarPixels == 9.0f);
            CHECK(rule.y == 9.0f);
            CHECK(rule.x == Catch::Approx(1.0f / 0.915f));
        } else if (p.name.find("mgTiles2") != std::string::npos) {
            ++seen;
            CHECK(p.slabFarPixels == 6.0f);
            CHECK(rule.y == 6.0f);
            CHECK(rule.x == Catch::Approx(1.0f));
        }
    }
    CHECK(seen == 2);
}

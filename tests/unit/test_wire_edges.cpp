// ADR-1073: the edge list the wire lines draw, and the material's `wire` block and parameters.

#include "params/parameter_set.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/wire_edges.hpp"
#include "scene/wire_lines.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("A cube's feature edges are its twelve, welded across its split corners", "[wire][adr1073]") {
    const scene::MeshData cube = scene::makeCube(0.5f);
    // Every face has its own four vertices (flat normals), so the corners are split three ways.
    REQUIRE(cube.vertices.size() == 24);
    const scene::WireEdgeMesh feature = scene::buildWireEdges(cube, 1, 30.0f);
    CHECK(feature.edgeCount == 12);
    CHECK(feature.vertices.size() == 48);
    CHECK(feature.indices.size() == 72);
    // Every triangle edge: the twelve and one diagonal per face.
    const scene::WireEdgeMesh all = scene::buildWireEdges(cube, 2, 30.0f);
    CHECK(all.edgeCount == 18);
    // Off is nothing.
    CHECK(scene::buildWireEdges(cube, 0, 30.0f).edgeCount == 0);
    // Each quad's two ends name each other.
    const scene::WireEdgeVertex& a = feature.vertices[0];
    const scene::WireEdgeVertex& b = feature.vertices[2];
    CHECK(a.otherPosition == b.position);
    CHECK(b.otherPosition == a.position);
    CHECK(a.corner.y == 1.0f);
    CHECK(b.corner.y == -1.0f);
}

TEST_CASE("The crease angle chooses which of a smooth mesh's edges are features", "[wire][adr1073]") {
    const scene::MeshData sphere = scene::makeIcosphere(1.0f, 1); // 80 faces, 120 edges
    CHECK(scene::buildWireEdges(sphere, 2, 0.0f).edgeCount == 120);
    // Neighbouring faces of a once-subdivided icosphere meet at about 20 degrees.
    CHECK(scene::buildWireEdges(sphere, 1, 10.0f).edgeCount == 120);
    CHECK(scene::buildWireEdges(sphere, 1, 40.0f).edgeCount == 0);
    // A plane's only features are its border.
    const scene::MeshData plane = scene::makePlane(1.0f, 4);
    CHECK(scene::buildWireEdges(plane, 1, 30.0f).edgeCount == 16);
    // The cap holds.
    CHECK(scene::buildWireEdges(sphere, 2, 0.0f, 7).edgeCount == 7);
}

TEST_CASE("A procedural node's material carries wire through its file and its parameters", "[wire][adr1073]") {
    auto g = scene::ProceduralGeometry::fromJson(nlohmann::json::parse(R"({
        "source": {"kind": "box", "size": [1, 1, 1]}, "distribution": {"kind": "single"},
        "material": {"wire": {"mode": "feature", "color": [1, 0, 1], "width": 3, "fill": 0}}})"));
    REQUIRE(g.has_value());
    CHECK(g->material.wire.modeIndex() == 1);
    CHECK(g->material.wire.hidesSurface());
    CHECK(g->toJson().at("material").at("wire").at("width") == 3.0f);
    scene::WireLines w;
    CHECK_FALSE(scene::readWireLines(nlohmann::json::parse(R"({"mode": "dotted"})"), w).has_value());
    CHECK_FALSE(scene::readWireLines(nlohmann::json::parse(R"({"colour": [1, 1, 1]})"), w).has_value());

    params::ParameterSet params;
    auto p = scene::registerProceduralParameters(params, *g, "procedural/cube/");
    for (const char* path : {"procedural/cube/wire/mode", "procedural/cube/wire/crease", "procedural/cube/wire/intensity",
                             "procedural/cube/wire/opacity", "procedural/cube/wire/width", "procedural/cube/wire/fill",
                             "procedural/cube/wire/occlude"}) {
        INFO(path);
        CHECK(params.findAs<float>(path) != nullptr);
    }
    REQUIRE(params.findAs<glm::vec3>("procedural/cube/wire/color") != nullptr);
    params.findAs<float>("procedural/cube/wire/width")->setBase(6.0f);
    params.resetFinals();
    scene::ProceduralGeometry live = *g;
    REQUIRE(scene::applyProceduralParameterValues(p, *g, live));
    CHECK(live.material.wire.width == 6.0f);
    CHECK(live.material.wire.color == glm::vec3(1.0f, 0.0f, 1.0f));
}

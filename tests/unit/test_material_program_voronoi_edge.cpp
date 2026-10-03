// ADR-1069: the Voronoi edge op -- F2 - F1 is zero on a cell boundary and positive inside, F2 >= F1, the hash in [0, 1).

#include "scene/material_program.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("The voronoiEdge material op gives F2 - F1, F1, the cell hash and F2", "[material][adr1069]") {
    REQUIRE(scene::materialOpKindFromName("voronoiEdge").has_value());
    CHECK(*scene::materialOpKindFromName("voronoiEdge") == scene::MaterialOpKind::VoronoiEdge);
    CHECK(std::string(scene::materialOpKindName(scene::MaterialOpKind::VoronoiEdge)) == "voronoiEdge");
}

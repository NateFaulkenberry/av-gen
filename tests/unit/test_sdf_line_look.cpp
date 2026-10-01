// ADR-1047: the line look's settings -- per-surface edge colour, edge width in pixels, threshold and softness.

#include "scene/sdf_object.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("The line look's settings read, round-trip, and stay out of files that do not use them",
          "[sdf][liminal][adr1047]") {
    const auto j = nlohmann::json::parse(R"({"name": "w", "compile": true,
      "tree": {"kind": "box", "size": [1, 1, 1]},
      "surfaces": [{"color": [0.02, 0.02, 0.02], "emission": [0, 0, 0], "edge": [1, 0.2, 0.8]},
                   {"color": [0.02, 0.02, 0.02], "emission": [0, 0, 0]}],
      "look": {"edgeIntensity": 4, "edgePixels": 2.5, "edgeThreshold": 0.05, "edgeSoftness": 0.1}})");
    auto o = scene::SdfObject::fromJson(j);
    REQUIRE(o.has_value());
    CHECK(o->look.edgePixels == 2.5f);
    CHECK(o->look.edgeThreshold == 0.05f);
    CHECK(o->look.edgeSoftness == 0.1f);
    REQUIRE(o->surfaces.size() == 2);
    CHECK(o->surfaces[0].edge == glm::vec3(1.0f, 0.2f, 0.8f));
    CHECK(o->surfaces[1].edge == glm::vec3(1.0f)); // the default: the object's edge colour
    const auto out = o->toJson();
    CHECK(out["look"]["edgePixels"] == 2.5f);
    CHECK(out["surfaces"][0].contains("edge"));
    CHECK_FALSE(out["surfaces"][1].contains("edge"));
    auto again = scene::SdfObject::fromJson(out);
    REQUIRE(again.has_value());
    CHECK(again->surfaces[0].edge == o->surfaces[0].edge);
    CHECK(again->look.edgeSoftness == o->look.edgeSoftness);
    // An object that never set them writes none of the new keys.
    scene::SdfObject plain;
    plain.tree = o->tree;
    const auto p = plain.toJson();
    CHECK_FALSE(p["look"].contains("edgePixels"));
}

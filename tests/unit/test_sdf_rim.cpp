// ADR-1052: the SDF rim (fresnel) emission's settings: read, round-trip, absent from files that do not use it.

#include "scene/sdf_object.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("The SDF rim settings read, round-trip, and stay out of files that do not use them", "[sdf][liminal][adr1052]") {
    const auto j = nlohmann::json::parse(R"({"name": "m", "compile": true,
      "tree": {"kind": "sphere", "radius": 0.5},
      "surfaces": [{"color": [0.02, 0.02, 0.02], "emission": [0, 0, 0], "rim": 0.0},
                   {"color": [0.02, 0.02, 0.02], "emission": [0, 0, 0]}],
      "look": {"rimIntensity": 3, "rimColor": [0.4, 0.8, 1.0], "rimPower": 5}})");
    auto o = scene::SdfObject::fromJson(j);
    REQUIRE(o.has_value());
    CHECK(o->look.rimIntensity == 3.0f);
    CHECK(o->look.rimColor == glm::vec3(0.4f, 0.8f, 1.0f));
    CHECK(o->look.rimPower == 5.0f);
    REQUIRE(o->surfaces.size() == 2);
    CHECK(o->surfaces[0].rim == 0.0f);
    CHECK(o->surfaces[1].rim == 1.0f); // the default: the object's rim
    const auto out = o->toJson();
    CHECK(out["look"]["rimIntensity"] == 3.0f);
    CHECK(out["surfaces"][0].contains("rim"));
    CHECK_FALSE(out["surfaces"][1].contains("rim"));
    auto again = scene::SdfObject::fromJson(out);
    REQUIRE(again.has_value());
    CHECK(again->look.rimPower == 5.0f);

    auto plain = scene::SdfObject::fromJson(nlohmann::json::parse(R"({"name": "p", "tree": {"kind": "box"}})"));
    REQUIRE(plain.has_value());
    CHECK(plain->look.rimIntensity == 0.0f);
    CHECK_FALSE(plain->toJson()["look"].contains("rimIntensity"));
}

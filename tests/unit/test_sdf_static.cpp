// ADR-1054: screen static on SDF surfaces -- the settings read, round-trip and stay out of files that do not use them.

#include "scene/sdf_object.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("Screen static settings read, round-trip, and stay out of files that do not use them", "[sdf][liminal][adr1054]") {
    const auto j = nlohmann::json::parse(R"({"name": "tv", "compile": true,
      "tree": {"kind": "box", "size": [0.4, 0.3, 0.05]},
      "surfaces": [{"color": [0.02, 0.02, 0.02], "emission": [0, 0, 0]},
                   {"color": [0.1, 0.1, 0.1], "emission": [0.5, 0.6, 0.7], "static": 1.0}],
      "look": {"staticCell": 0.02, "staticRate": 12, "staticRoll": 0.5}})");
    auto o = scene::SdfObject::fromJson(j);
    REQUIRE(o.has_value());
    REQUIRE(o->surfaces.size() == 2);
    CHECK(o->surfaces[0].staticAmount == 0.0f);
    CHECK(o->surfaces[1].staticAmount == 1.0f);
    CHECK(o->look.staticCell == 0.02f);
    CHECK(o->look.staticRate == 12.0f);
    CHECK(o->look.staticRoll == 0.5f);
    const auto out = o->toJson();
    CHECK_FALSE(out["surfaces"][0].contains("static"));
    CHECK(out["surfaces"][1]["static"] == 1.0f);
    CHECK(out["look"]["staticRate"] == 12.0f);
    auto plain = scene::SdfObject::fromJson(nlohmann::json::parse(R"({"name": "p", "tree": {"kind": "box"}})"));
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->toJson()["look"].contains("staticCell"));
}

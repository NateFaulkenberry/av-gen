// ADR-1050: the spectrum sweep's parameters and its scene-file keys.

#include "params/parameter_set.hpp"
#include "scene/post_settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("The spectrum sweep is a set of post parameters a scene's post block can set", "[post][adr1050]") {
    params::ParameterSet params;
    scene::PostSettings defaults;
    CHECK(defaults.sweepIntensity == 0.0f);
    CHECK(defaults.sweepWash == 0.0f);
    const auto p = scene::registerPostParameters(params, defaults);
    for (const char* leaf : {"progress", "width", "intensity", "wash", "angle", "span", "hue", "trail"}) {
        INFO(leaf);
        CHECK(params.find(std::string("post/sweep/") + leaf) != nullptr);
    }
    REQUIRE(scene::applyPostJson(nlohmann::json::parse(R"({"sweepProgress": 0.4, "sweepIntensity": 2,
        "sweepWash": 0.5, "sweepAngle": 90})"), p).has_value());
    params.resetFinals();
    scene::PostSettings s;
    scene::applyPostParameters(p, s);
    CHECK(s.sweepProgress == 0.4f);
    CHECK(s.sweepIntensity == 2.0f);
    CHECK(s.sweepWash == 0.5f);
    CHECK(s.sweepAngle == 90.0f);
}

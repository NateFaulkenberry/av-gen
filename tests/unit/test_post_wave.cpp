// ADR-1055: the world wave's settings -- registered as post/wave/*, read from a scene's post block, off by default.

#include "params/parameter_set.hpp"
#include "scene/post_settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("The world wave's parameters register, read a post block, and are off by default", "[liminal][adr1055]") {
    params::ParameterSet set;
    scene::PostSettings defaults;
    CHECK(defaults.waveIntensity == 0.0f);
    CHECK(defaults.waveEdgeTint == 0.0f);
    CHECK(defaults.waveTrail == 0.0f);
    scene::PostParameters p = scene::registerPostParameters(set, defaults);
    for (const char* path : {"post/wave/origin", "post/wave/direction", "post/wave/progress", "post/wave/width",
                             "post/wave/intensity", "post/wave/color", "post/wave/hue", "post/wave/hueSpan",
                             "post/wave/edgeTint", "post/wave/trail", "post/wave/trailColor"}) {
        INFO(path);
        CHECK(set.find(path) != nullptr);
    }
    REQUIRE(scene::applyPostJson(nlohmann::json::parse(R"({"waveProgress": 3.5, "waveIntensity": 2, "waveOrigin": [1, 0, -2],
        "waveTrailColor": [0, 1, 0], "waveTrail": 0.8})"), p).has_value());
    set.resetFinals(); // the frame's modulation pass starts from the base values
    scene::PostSettings s;
    scene::applyPostParameters(p, s);
    CHECK(s.waveProgress == 3.5f);
    CHECK(s.waveIntensity == 2.0f);
    CHECK(s.waveOrigin == glm::vec3(1.0f, 0.0f, -2.0f));
    CHECK(s.waveTrailColor == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(s.waveTrail == 0.8f);
}

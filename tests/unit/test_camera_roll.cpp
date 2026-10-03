// ADR-1075: camera/roll turns the camera's up about its line of sight, and nothing else.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;
using Catch::Approx;

TEST_CASE("camera/roll turns the camera's up about the line of sight", "[camera][roll][adr1075]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "roll",
        "camera": { "mode": 1, "position": [0, 0, 10], "target": [0, 0, 0], "fov": 45.0 },
        "nodes": [ { "name": "box", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] }, "distribution": { "kind": "single" } } } ] })"),
                                             registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    const auto frame = [&] {
        params.resetFinals();
        FrameTime t{};
        t.renderTime = 1.0;
        (*comp)->update(t);
    };
    frame();
    CHECK((*comp)->scene().camera.up.y == Approx(1.0f));
    auto* roll = params.findAs<float>("camera/roll");
    REQUIRE(roll != nullptr);
    roll->setBase(90.0f);
    frame();
    const glm::vec3 up = (*comp)->scene().camera.up;
    CHECK(std::abs(up.x) == Approx(1.0f).margin(1e-4));
    CHECK(up.y == Approx(0.0f).margin(1e-4));
    CHECK((*comp)->scene().camera.position.z == Approx(10.0f));
    roll->setBase(0.0f);
    frame();
    CHECK((*comp)->scene().camera.up == glm::vec3(0.0f, 1.0f, 0.0f));
}

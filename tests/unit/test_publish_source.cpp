// Parameters as signals (ADR-1064): a `publish` source puts chosen parameters' final values on the bus, so an
// effect's modulated intensity can drive another effect.

#include "params/parameter_set.hpp"
#include "signals/publish_source.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <glm/vec3.hpp>

using namespace avgen;

TEST_CASE("A publish source puts a parameter's final value on the bus under its dotted name", "[signals][adr1064]") {
    params::ParameterSet params;
    auto& gain = params.add(params::ParamDesc<float>{.path = "fx/heroGlow/gain", .defaultValue = 1.0f, .hardMin = 0,
                                                     .hardMax = 8, .softMin = 0, .softMax = 4});
    params.add(params::ParamDesc<glm::vec3>{.path = "lights/heart/color", .defaultValue = glm::vec3(1, 0.5f, 0.25f),
                                            .hardMin = glm::vec3(0), .hardMax = glm::vec3(1),
                                            .softMin = glm::vec3(0), .softMax = glm::vec3(1)});
    auto src = signals::SourceRack::create("publish", "fx");
    REQUIRE(src != nullptr);
    REQUIRE(src->settingsFromJson(nlohmann::json::parse(
                     R"({"parameters": ["fx/heroGlow/gain", "lights/heart/color", "fx/notYet/amount"]})"))
                .has_value());
    CHECK(src->settingsToJson()["parameters"].size() == 3);
    signals::SignalBus bus;
    src->attach(bus, params);
    gain.setFinalComponent(0, 2.5f); // what the routes made of it this frame
    src->update(bus, {});
    CHECK(bus.value(*bus.find("fx.heroGlow.gain")) == 2.5f);
    CHECK(bus.value(*bus.find("lights.heart.color")) == 1.0f);
    CHECK(bus.value(*bus.find("lights.heart.color.1")) == 0.5f);
    CHECK(bus.value(*bus.find("lights.heart.color.2")) == 0.25f);
    // A parameter that does not exist (an effect not yet attached) reads 0, and starts when it appears.
    CHECK(bus.value(*bus.find("fx.notYet.amount")) == 0.0f);
    auto& later = params.add(params::ParamDesc<float>{.path = "fx/notYet/amount", .defaultValue = 0.7f, .hardMin = 0,
                                                      .hardMax = 1, .softMin = 0, .softMax = 1});
    later.resetFinal();
    src->update(bus, {});
    CHECK(bus.value(*bus.find("fx.notYet.amount")) == 0.7f);
    CHECK_FALSE(src->settingsFromJson(nlohmann::json::parse(R"({"parameters": [3]})")).has_value());
    CHECK(signals::PublishSource::signalName("post/bloom/intensity") == "post.bloom.intensity");
}

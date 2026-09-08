#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace avgen;
using namespace avgen::params;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {
double d(float v) {
    return static_cast<double>(v);
}

struct Fixture {
    signals::SignalBus bus;
    ParameterSet params;
    Modulator modulator;
    signals::SignalId bass;
    signals::SignalId mid;
    signals::SignalId onset;
    Parameter<float>& scale;
    Parameter<glm::vec3>& color;

    Fixture()
        : bass(bus.declare("audio.bass"))
        , mid(bus.declare("audio.mid"))
        , onset(bus.declare("audio.onset", 0.0f, 1.0f, true))
        , scale(params.add(
              ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 4.0f}))
        , color(params.add(ParamDesc<glm::vec3>{.path = "orb/color",
                                                .defaultValue = glm::vec3(0.5f),
                                                .hardMin = glm::vec3(0.0f),
                                                .hardMax = glm::vec3(1.0f)})) {}

    ModRoute route(const char* source, const char* target, ModOp op, float amount = 1.0f,
                   int component = -1) {
        ModRoute r;
        r.source = source;
        r.target = target;
        r.op = op;
        r.amount = amount;
        r.component = component;
        return r;
    }
};
} // namespace

TEST_CASE("applyModOp", "[modulation]") {
    CHECK(applyModOp(ModOp::Add, 1.0f, 0.5f) == 1.5f);
    CHECK(applyModOp(ModOp::Multiply, 2.0f, 0.5f) == 1.0f);
    CHECK(applyModOp(ModOp::Replace, 2.0f, 0.5f) == 0.5f);
    CHECK(applyModOp(ModOp::Min, 2.0f, 0.5f) == 0.5f);
    CHECK(applyModOp(ModOp::Max, 2.0f, 0.5f) == 2.0f);
}

TEST_CASE("bind resolves signal ids and parameters", "[modulation]") {
    Fixture f;
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
    CHECK_FALSE(f.modulator.bound());
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    CHECK(f.modulator.bound());
    REQUIRE(f.modulator.routes().size() == 1);
    CHECK(f.modulator.routes()[0].sourceId == f.bass);
    CHECK(f.modulator.routes()[0].targetParam == &f.scale);
    CHECK(f.modulator.routes()[0].enabled);
}

TEST_CASE("bind reports unknown sources and targets and disables those routes", "[modulation]") {
    Fixture f;
    f.modulator.addRoute(f.route("audio.nope", "orb/scale", ModOp::Add));
    f.modulator.addRoute(f.route("audio.bass", "orb/missing", ModOp::Add));
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add, 0.5f));
    const auto result = f.modulator.bind(f.bus, f.params);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("audio.nope"));
    CHECK_THAT(result.error().message, ContainsSubstring("orb/missing"));
    CHECK_FALSE(f.modulator.routes()[0].enabled);
    CHECK_FALSE(f.modulator.routes()[1].enabled);
    CHECK(f.modulator.routes()[2].enabled);
    CHECK(f.modulator.bound()); // resolvable routes still evaluate

    f.bus.set(f.bass, 1.0f);
    f.modulator.evaluate(f.bus, f.params, 1.0 / 60.0);
    CHECK_THAT(d(f.scale.value()), WithinAbs(1.5, 1e-6));
}

TEST_CASE("bind rejects a component out of range", "[modulation]") {
    Fixture f;
    f.modulator.addRoute(f.route("audio.bass", "orb/color", ModOp::Add, 1.0f, 3));
    const auto result = f.modulator.bind(f.bus, f.params);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("orb/color"));
    CHECK_FALSE(f.modulator.routes()[0].enabled);
}

TEST_CASE("Each op combines with the base value", "[modulation]") {
    Fixture f;
    f.bus.set(f.bass, 0.5f);
    const double dt = 0.01;

    SECTION("Add") {
        f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.modulator.evaluate(f.bus, f.params, dt);
        CHECK_THAT(d(f.scale.value()), WithinAbs(1.5, 1e-6));
    }
    SECTION("Multiply") {
        f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Multiply, 4.0f)); // x2
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.modulator.evaluate(f.bus, f.params, dt);
        CHECK_THAT(d(f.scale.value()), WithinAbs(2.0, 1e-6));
    }
    SECTION("Replace") {
        f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Replace));
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.modulator.evaluate(f.bus, f.params, dt);
        CHECK_THAT(d(f.scale.value()), WithinAbs(0.5, 1e-6));
    }
    SECTION("Min") {
        f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Min));
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.modulator.evaluate(f.bus, f.params, dt);
        CHECK_THAT(d(f.scale.value()), WithinAbs(0.5, 1e-6));
    }
    SECTION("Max") {
        f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Max, 6.0f)); // 3.0
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.modulator.evaluate(f.bus, f.params, dt);
        CHECK_THAT(d(f.scale.value()), WithinAbs(3.0, 1e-6));
    }
    CHECK(f.scale.base() == 1.0f); // base never touched
}

TEST_CASE("Two Add routes sum onto the base", "[modulation]") {
    Fixture f;
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add, 0.5f));
    f.modulator.addRoute(f.route("audio.mid", "orb/scale", ModOp::Add, 2.0f));
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.bus.set(f.bass, 1.0f);
    f.bus.set(f.mid, 0.25f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(1.0 + 0.5 + 0.5, 1e-6));
    CHECK_THAT(d(f.modulator.routes()[0].lastOutput), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(f.modulator.routes()[1].lastOutput), WithinAbs(0.5, 1e-6));
}

TEST_CASE("Replace is applied before Add regardless of insertion order", "[modulation]") {
    Fixture f;
    f.bus.set(f.bass, 0.25f);
    f.bus.set(f.mid, 2.0f);

    SECTION("Add inserted first") {
        f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
        f.modulator.addRoute(f.route("audio.mid", "orb/scale", ModOp::Replace));
    }
    SECTION("Replace inserted first") {
        f.modulator.addRoute(f.route("audio.mid", "orb/scale", ModOp::Replace));
        f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
    }
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(2.25, 1e-6));
}

TEST_CASE("Multiply runs after Replace and before Add; Min/Max bound the result", "[modulation]") {
    Fixture f;
    f.bus.set(f.bass, 0.5f);
    f.bus.set(f.mid, 1.0f);
    f.modulator.addRoute(f.route("audio.mid", "orb/scale", ModOp::Add, 0.5f));       // +0.5
    f.modulator.addRoute(f.route("audio.mid", "orb/scale", ModOp::Max, 3.0f));       // max(., 3)
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Multiply, 4.0f)); // x2
    f.modulator.addRoute(f.route("audio.mid", "orb/scale", ModOp::Replace, 1.5f));   // = 1.5
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.modulator.evaluate(f.bus, f.params, 0.01);
    // replace 1.5 -> x2 = 3.0 -> +0.5 = 3.5 -> max(3.5, 3) = 3.5
    CHECK_THAT(d(f.scale.value()), WithinAbs(3.5, 1e-6));
}

TEST_CASE("Component targeting on a vec3", "[modulation]") {
    Fixture f;
    f.bus.set(f.bass, 0.25f);
    f.modulator.addRoute(f.route("audio.bass", "orb/color", ModOp::Add, 1.0f, 1));
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.color.value().x), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(f.color.value().y), WithinAbs(0.75, 1e-6));
    CHECK_THAT(d(f.color.value().z), WithinAbs(0.5, 1e-6));

    f.modulator.clearRoutes();
    f.modulator.addRoute(f.route("audio.bass", "orb/color", ModOp::Add, 1.0f, -1));
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.color.value().x), WithinAbs(0.75, 1e-6));
    CHECK_THAT(d(f.color.value().y), WithinAbs(0.75, 1e-6));
    CHECK_THAT(d(f.color.value().z), WithinAbs(0.75, 1e-6));
}

TEST_CASE("masterGain scales every route", "[modulation]") {
    Fixture f;
    f.bus.set(f.bass, 1.0f);
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add, 0.5f));
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.modulator.masterGain = 2.0f;
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(2.0, 1e-6));
    f.modulator.masterGain = 0.0f;
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(1.0, 1e-6));
}

TEST_CASE("Final returns to base when the signal returns to zero", "[modulation]") {
    Fixture f;
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.bus.set(f.bass, 0.8f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(1.8, 1e-6));
    f.bus.set(f.bass, 0.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(1.0, 1e-6));
    // Changing the base is picked up on the next evaluation.
    f.scale.setBase(2.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(2.0, 1e-6));
}

TEST_CASE("Modulated results are clamped to the hard range", "[modulation]") {
    Fixture f;
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add, 100.0f));
    f.modulator.addRoute(f.route("audio.mid", "orb/color", ModOp::Add, -100.0f));
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.bus.set(f.bass, 1.0f);
    f.bus.set(f.mid, 1.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK(f.scale.value() == 4.0f);
    CHECK(f.color.value() == glm::vec3(0.0f));
}

TEST_CASE("Disabled routes and unbound modulators do nothing", "[modulation]") {
    Fixture f;
    f.bus.set(f.bass, 1.0f);
    ModRoute& r = f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
    r.enabled = false;
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.scale.setFinalComponent(0, 3.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK(f.scale.value() == 1.0f); // resetFinals ran, route did not

    Modulator fresh;
    fresh.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
    f.scale.setFinalComponent(0, 3.0f);
    fresh.evaluate(f.bus, f.params, 0.01);
    CHECK(f.scale.value() == 1.0f);
}

TEST_CASE("Events reach the chain envelope", "[modulation]") {
    Fixture f;
    ModRoute r = f.route("audio.onset", "orb/scale", ModOp::Add, 1.0f);
    r.chain.envelope = EnvelopeMode::LinearFall;
    r.chain.envelopeFallPerSecond = 10.0f;
    f.modulator.addRoute(r);
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.bus.setEvent(f.onset, true, 1.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(2.0, 1e-6));
    f.bus.clearEvents();
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK_THAT(d(f.scale.value()), WithinAbs(1.9, 1e-6));
}

TEST_CASE("resetState clears smoothing state", "[modulation]") {
    Fixture f;
    ModRoute r = f.route("audio.bass", "orb/scale", ModOp::Add);
    r.chain.attackMs = 1000.0f;
    r.chain.decayMs = 1000.0f;
    f.modulator.addRoute(r);
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());

    f.bus.set(f.bass, 0.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01); // initialise smoother at 0
    f.bus.set(f.bass, 1.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK(f.scale.value() < 1.1f); // slow attack: barely moved
    CHECK(f.modulator.routes()[0].state.initialised);

    f.modulator.resetState();
    CHECK_FALSE(f.modulator.routes()[0].state.initialised);
    CHECK(f.modulator.routes()[0].state.smoothed == 0.0f);
    CHECK(f.modulator.routes()[0].lastOutput == 0.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01); // first sample re-initialises at 1
    CHECK_THAT(d(f.scale.value()), WithinAbs(2.0, 1e-6));
}

TEST_CASE("addRoute invalidates the binding until bind is called again", "[modulation]") {
    Fixture f;
    f.modulator.addRoute(f.route("audio.bass", "orb/scale", ModOp::Add));
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    f.modulator.addRoute(f.route("audio.mid", "orb/scale", ModOp::Add));
    CHECK_FALSE(f.modulator.bound());
    CHECK(f.modulator.routes()[1].targetParam == nullptr);
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    CHECK(f.modulator.routes()[1].targetParam == &f.scale);
    f.modulator.clearRoutes();
    CHECK(f.modulator.routes().empty());
    CHECK_FALSE(f.modulator.bound());
}

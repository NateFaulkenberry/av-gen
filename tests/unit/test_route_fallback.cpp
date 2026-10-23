// ADR-1570: a route can be optional (an unknown source is the expected state, not a binding problem), and a depth
// signal can have a default that stands in while the bus does not carry it. Together they make a stem route and its
// live fallback: `stem.drums.kick` (optional) and `response.kick` (depth `stem.drums.active`, 1 -> 0, default 0).

#include "params/liveness.hpp"
#include "params/modulation.hpp"
#include "params/serialization.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;
using namespace avgen::params;
using Catch::Matchers::WithinAbs;

namespace {

struct Fixture {
    signals::SignalBus bus;
    ParameterSet params;
    Modulator modulator;
    signals::SignalId mixKick = bus.declare("response.kick");
    Parameter<float>& glow =
        params.add(ParamDesc<float>{.path = "ring/glow", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 10.0f});
};

ModRoute stemRoute() {
    ModRoute r;
    r.source = "stem.drums.kick";
    r.target = "ring/glow";
    r.amount = 2.0f;
    r.optional = true;
    return r;
}

ModRoute fallbackRoute() {
    ModRoute r;
    r.source = "response.kick";
    r.target = "ring/glow";
    r.amount = 1.0f;
    r.depthSource = "stem.drums.active";
    r.depthMin = 1.0f;
    r.depthMax = 0.0f;
    r.depthDefault = 0.0f;
    return r;
}

double v(float x) { return static_cast<double>(x); }

} // namespace

TEST_CASE("With no stem on the bus the optional route is silent and unreported, and the fallback plays at full depth",
          "[modulation][adr1570]") {
    Fixture f;
    f.modulator.addRoute(stemRoute());
    f.modulator.addRoute(fallbackRoute());
    const auto bound = f.modulator.bind(f.bus, f.params);
    CHECK(bound.has_value()); // neither route is a binding problem
    f.bus.set(f.mixKick, 0.7f);
    f.modulator.evaluate(f.bus, f.params, 1.0 / 30.0);
    CHECK_THAT(v(f.glow.value()), WithinAbs(0.7, 1e-6));

    // Control: the same fallback route with no default is rejected, as ADR-900 always did.
    Fixture g;
    ModRoute strict = fallbackRoute();
    strict.depthDefault.reset();
    g.modulator.addRoute(strict);
    CHECK_FALSE(g.modulator.bind(g.bus, g.params).has_value());
    g.bus.set(g.mixKick, 0.7f);
    g.modulator.evaluate(g.bus, g.params, 1.0 / 30.0);
    CHECK(g.glow.value() == 0.0f);

    // Control: a non-optional route on the missing stem is still a binding problem.
    Fixture h;
    ModRoute plain = stemRoute();
    plain.optional = false;
    h.modulator.addRoute(plain);
    CHECK_FALSE(h.modulator.bind(h.bus, h.params).has_value());
}

TEST_CASE("Once the stem is declared and active, the stem route drives and the fallback stands aside",
          "[modulation][adr1570]") {
    Fixture f;
    f.modulator.addRoute(stemRoute());
    f.modulator.addRoute(fallbackRoute());
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
    // The stem appears later (a project's stems analysed, a live input source mapped) and the engine rebinds.
    const signals::SignalId kick = f.bus.declare("stem.drums.kick");
    const signals::SignalId active = f.bus.declare("stem.drums.active");
    REQUIRE(f.modulator.bind(f.bus, f.params).has_value());

    f.bus.set(f.mixKick, 0.7f);
    f.bus.set(kick, 0.4f);
    f.bus.set(active, 1.0f);
    f.modulator.evaluate(f.bus, f.params, 1.0 / 30.0);
    CHECK_THAT(v(f.glow.value()), WithinAbs(0.8, 1e-6)); // 2 x 0.4, the mix's kick at depth 0

    // The stem goes inactive (outside its span, a disconnected channel): the fallback is back at full depth.
    f.bus.set(kick, 0.0f);
    f.bus.set(active, 0.0f);
    f.modulator.evaluate(f.bus, f.params, 1.0 / 30.0);
    CHECK_THAT(v(f.glow.value()), WithinAbs(0.7, 1e-6));
}

TEST_CASE("The liveness rules do not call an absent optional route dead, and still check its target",
          "[modulation][liveness][adr1570]") {
    signals::SignalBus bus;
    ParameterSet params;
    params.add(ParamDesc<float>{.path = "ring/glow", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    const liveness::BusFacts facts(bus, params);
    CHECK(liveness::Registry::standard().checkRoute(stemRoute(), facts).empty());
    ModRoute plain = stemRoute();
    plain.optional = false;
    bool unknown = false;
    for (const auto& finding : liveness::Registry::standard().checkRoute(plain, facts)) {
        unknown = unknown || finding.rule == "unknown-source";
    }
    CHECK(unknown);
    ModRoute badTarget = stemRoute();
    badTarget.target = "ring/nope";
    CHECK_FALSE(liveness::Registry::standard().checkRoute(badTarget, facts).empty());
}

TEST_CASE("optional and depthDefault round-trip, and are written only when set", "[serialization][adr1570]") {
    const nlohmann::json j = routeToJson(fallbackRoute());
    CHECK(j["depthDefault"] == 0.0);
    CHECK_FALSE(j.contains("optional"));
    auto back = routeFromJson(j);
    REQUIRE(back.has_value());
    REQUIRE(back->depthDefault.has_value());
    CHECK(*back->depthDefault == 0.0f);
    CHECK_FALSE(back->optional);

    const nlohmann::json s = routeToJson(stemRoute());
    CHECK(s["optional"] == true);
    CHECK_FALSE(s.contains("depthDefault"));
    auto sb = routeFromJson(s);
    REQUIRE(sb.has_value());
    CHECK(sb->optional);

    nlohmann::json bad = s;
    bad["optional"] = "yes";
    CHECK_FALSE(routeFromJson(bad).has_value());
}

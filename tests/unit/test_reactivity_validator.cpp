// ADR-926: the reactivity validator. Every refusal and every flag has the case it must catch and the
// neighbour it must let through, on the glade (tests/support/reactivity_fixture.hpp):
//
//   * a dead target is refused through the liveness registry (DEAD_TARGET) and not compiled; the same
//     route on a live target is compiled -- including the two rules ADR-926 adds to the registry;
//   * "everything on the kick" is flagged (ONE_SOURCE); a plan with layers owned apart is not;
//   * routes on one source reaching their targets at one instant are flagged (ONE_PHASE); staggered
//     ones are not;
//   * an entity answering four signals, or pushed past its safe range, is flagged (OVER_SATURATED);
//   * a rate whose phase is time x rate is flagged (PHASE_RATE_TRAP); its amplitude neighbour is not;
//   * the reports' traps: a one-frame event or audio.rms as a depth, a hue driven by the audio, a
//     multiply that darkens its target between hits;
//   * a plan source must be pure in time, and a route reading a refused source is blocked with it.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/reactivity.hpp"
#include "params/liveness.hpp"
#include "support/gltf_fixture.hpp"
#include "support/reactivity_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace avgen;
using namespace avgen::directing;

namespace {

struct Glade {
    app::Engine engine{app::EngineMode::Offline};
    Glade() {
        const auto glb = testsupport::writeTriangleGlb("reactivity_validator");
        REQUIRE(testsupport::loadGlade(engine, glb, glb));
    }
    [[nodiscard]] Compilation compile(Plan plan) { return compilePlan(std::move(plan), app::sceneFactsFor(engine)); }
};

PlanRoute route(std::string key, std::string source, std::string target, float amount = 0.3f, float delayMs = 0.0f) {
    PlanRoute r;
    r.key = std::move(key);
    r.route.source = std::move(source);
    r.route.target = std::move(target);
    r.route.op = params::ModOp::Add;
    r.route.amount = amount;
    r.route.chain.delayMs = delayMs;
    r.route.chain.attackMs = 5.0f;
    r.route.chain.decayMs = 300.0f;
    return r;
}

Plan planOf(std::vector<PlanRoute> routes) {
    Plan p;
    p.id = "check";
    p.title = "Check";
    p.routes = std::move(routes);
    return p;
}

std::vector<const Issue*> with(const Validation& v, IssueCode code, std::string_view item = "*") {
    std::vector<const Issue*> out;
    for (const Issue& i : v.issues) {
        if (i.code == code && (item == "*" || i.item == item)) {
            out.push_back(&i);
        }
    }
    return out;
}

bool compiledRoute(const Compilation& c, std::string_view key) {
    const std::string id = planItemId(c.plan.id, key);
    return std::any_of(c.staged.routes.begin(), c.staged.routes.end(), [&](const params::ModRoute& r) { return r.planItem == id; });
}

std::string describe(const Validation& v) {
    std::string out;
    for (const Issue& i : v.issues) {
        out += i.toJson().dump() + "\n";
    }
    return out;
}

} // namespace

TEST_CASE("A route that cannot reach the picture is refused through the liveness registry; its live neighbour compiles",
          "[directing][reactivity][validator][adr926]") {
    Glade glade;
    const Compilation c = glade.compile(planOf({
        route("dark", "audio.onsetLow", "nodes/meadow/scatter/stones/emissionGain"),      // layer emits nothing
        route("lit", "audio.onsetLow", "nodes/meadow/scatter/fungi/emissionGain", 0.3f, 90.0f),
        route("nowave", "section.energy", "nodes/meadow/scatter/shelf/emissiveFieldAmount"), // names no field
        route("wave", "section.energy", "nodes/meadow/scatter/fungi/emissiveFieldAmount"),
        route("typo", "audio.onsetMid", "nodes/elder-gils/emissiveBoost"),                 // no such parameter
        route("gills", "audio.onsetMid", "nodes/elder-gills/emissiveBoost", 0.5f, 40.0f),
        route("nosignal", "audio.kickDrum", "nodes/lantern-gills/emissiveBoost"),          // no such signal
    }));
    INFO(describe(c.validation));
    for (const char* dead : {"dark", "nowave", "typo", "nosignal"}) {
        INFO(dead);
        REQUIRE(with(c.validation, IssueCode::DeadTarget, dead).size() >= 1);
        CHECK(c.validation.isBlocked(dead));
        CHECK_FALSE(compiledRoute(c, dead));
    }
    CHECK(with(c.validation, IssueCode::DeadTarget, "dark").front()->details["rule"] == "layer-emits-nothing");
    CHECK(with(c.validation, IssueCode::DeadTarget, "nowave").front()->details["rule"] == "no-field-named");
    CHECK(with(c.validation, IssueCode::DeadTarget, "typo").front()->details["rule"] == "unknown-target");
    CHECK(with(c.validation, IssueCode::DeadTarget, "nosignal").front()->details["rule"] == "unknown-source");
    // The misspelt target is answered with the catalogue's nearest.
    const auto& suggestions = with(c.validation, IssueCode::DeadTarget, "typo").front()->suggestions;
    CHECK(std::find(suggestions.begin(), suggestions.end(), "nodes/elder-gills/emissiveBoost") != suggestions.end());
    // The neighbours: live, compiled, stamped with the item that made them.
    for (const char* live : {"lit", "wave", "gills"}) {
        INFO(live);
        CHECK(with(c.validation, IssueCode::DeadTarget, live).empty());
        CHECK(compiledRoute(c, live));
    }
}

TEST_CASE("The two rules ADR-926 adds to the liveness registry flag their case and pass their neighbour",
          "[directing][reactivity][liveness][adr926]") {
    Glade glade;
    const scene::SceneLivenessFacts facts(glade.engine.livenessInputs());
    const auto& registry = params::liveness::Registry::standard();
    const auto rules = [&](const char* path) {
        std::vector<std::string> out;
        for (const auto& f : registry.checkTarget(path, -1, facts)) {
            out.push_back(f.rule);
        }
        return out;
    };
    const auto has = [](const std::vector<std::string>& v, const char* rule) {
        return std::find(v.begin(), v.end(), rule) != v.end();
    };
    CHECK(has(rules("nodes/meadow/scatter/stones/emissionGain"), "layer-emits-nothing"));
    CHECK(has(rules("nodes/meadow/scatter/stones/hueOffset"), "layer-emits-nothing"));
    CHECK_FALSE(has(rules("nodes/meadow/scatter/grass/emissionGain"), "layer-emits-nothing")); // faint, but it emits
    CHECK(has(rules("nodes/meadow/scatter/lamps/emissiveFieldAmount"), "no-field-named"));
    CHECK_FALSE(has(rules("nodes/meadow/scatter/fungi/emissiveFieldAmount"), "no-field-named"));
    CHECK(has(rules("procedural/elder-gills/emissiveFieldAmount"), "no-field-named"));
    CHECK_FALSE(has(rules("procedural/moss-gills/emissiveFieldAmount"), "no-field-named"));
    REQUIRE(registry.rule("layer-emits-nothing") != nullptr);
    REQUIRE(registry.rule("no-field-named") != nullptr);
}

TEST_CASE("Everything on the kick is flagged as one source; a plan whose layers are owned apart is not",
          "[directing][reactivity][validator][adr926]") {
    Glade glade;
    const std::vector<std::string> targets{"nodes/elder-gills/emissiveBoost", "nodes/lantern-gills/emissiveBoost",
                                           "nodes/spire-gills/emissiveBoost", "nodes/moss-gills/emissiveBoost",
                                           "nodes/meadow/scatter/fungi/emissionGain"};
    std::vector<PlanRoute> onKick;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        onKick.push_back(route("k" + std::to_string(i), "audio.onsetLow", targets[i], 0.3f, 40.0f * static_cast<float>(i)));
    }
    const Compilation kick = glade.compile(planOf(onKick));
    INFO(describe(kick.validation));
    const auto flagged = with(kick.validation, IssueCode::OneSource);
    REQUIRE(flagged.size() == 1);
    CHECK(flagged.front()->severity == Severity::Warning);
    CHECK(flagged.front()->details["source"] == "audio.onsetLow");
    CHECK(flagged.front()->details["routes"] == 5);
    // The control: the same targets, each on its own layer of the music.
    const std::vector<std::string> sources{"audio.onsetLow", "audio.onsetMid", "music.downbeat", "audio.onsetHigh",
                                           "audio.onsetLow"};
    std::vector<PlanRoute> apart;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        apart.push_back(route("a" + std::to_string(i), sources[i], targets[i], 0.3f, 40.0f * static_cast<float>(i)));
    }
    const Compilation owned = glade.compile(planOf(apart));
    INFO(describe(owned.validation));
    CHECK(with(owned.validation, IssueCode::OneSource).empty());
}

TEST_CASE("Routes on one source that reach their targets at one instant are flagged; staggered ones are not",
          "[directing][reactivity][validator][adr926]") {
    Glade glade;
    const Compilation lockstep = glade.compile(planOf({
        route("a", "music.downbeat", "nodes/elder-gills/emissiveBoost"),
        route("b", "music.downbeat", "nodes/lantern-gills/emissiveBoost"),
        route("c", "music.downbeat", "nodes/spire-gills/emissiveBoost"),
        route("d", "audio.onsetHigh", "nodes/meadow/scatter/shelf/emissionGain", 0.18f, 30.0f),
    }));
    INFO(describe(lockstep.validation));
    const auto flagged = with(lockstep.validation, IssueCode::OnePhase);
    REQUIRE(flagged.size() == 1);
    CHECK(flagged.front()->details["source"] == "music.downbeat");
    const Compilation staggered = glade.compile(planOf({
        route("a", "music.downbeat", "nodes/elder-gills/emissiveBoost", 0.3f, 0.0f),
        route("b", "music.downbeat", "nodes/lantern-gills/emissiveBoost", 0.3f, 120.0f),
        route("c", "music.downbeat", "nodes/spire-gills/emissiveBoost", 0.3f, 240.0f),
        route("d", "audio.onsetHigh", "nodes/meadow/scatter/shelf/emissionGain"),
    }));
    INFO(describe(staggered.validation));
    CHECK(with(staggered.validation, IssueCode::OnePhase).empty());
    // And the plan-wide form: every route of the plan at one instant, whatever its source.
    const Compilation allAtOnce = glade.compile(planOf({
        route("a", "music.downbeat", "nodes/elder-gills/emissiveBoost"),
        route("b", "audio.onsetLow", "nodes/lantern-gills/emissiveBoost"),
        route("c", "audio.onsetHigh", "nodes/meadow/scatter/shelf/emissionGain", 0.18f),
    }));
    INFO(describe(allAtOnce.validation));
    const auto planWide = with(allAtOnce.validation, IssueCode::OnePhase);
    REQUIRE(planWide.size() == 1);
    CHECK_FALSE(planWide.front()->details.contains("source"));
}

TEST_CASE("An entity answering too many signals, or pushed past its safe range, is flagged as over-saturated",
          "[directing][reactivity][validator][adr926]") {
    Glade glade;
    SECTION("four signals on one hero") {
        const Compilation c = glade.compile(planOf({
            route("a", "audio.onsetLow", "nodes/elder-gills/emissiveBoost"),
            route("b", "audio.onsetMid", "nodes/elder-cap/emissiveBoost", 0.3f, 30.0f),
            route("c", "audio.onsetHigh", "particles/elder-spores/emissive", 0.1f, 60.0f),
            route("d", "music.downbeat", "nodes/elder-stem/emissiveBoost", 0.3f, 90.0f),
        }));
        INFO(describe(c.validation));
        const auto flagged = with(c.validation, IssueCode::OverSaturated);
        REQUIRE_FALSE(flagged.empty());
        CHECK(std::any_of(flagged.begin(), flagged.end(), [](const Issue* i) { return i->subject == "elder-cap"; }));
        // Two signals is a hero answering two layers: not flagged.
        const Compilation two = glade.compile(planOf({
            route("a", "audio.onsetLow", "nodes/elder-gills/emissiveBoost"),
            route("b", "music.downbeat", "nodes/elder-cap/emissiveBoost", 0.3f, 30.0f),
        }));
        CHECK(with(two.validation, IssueCode::OverSaturated).empty());
    }
    SECTION("one route past its safe range, and routes stacked past it") {
        const Compilation big = glade.compile(planOf({route("big", "audio.onsetLow", "nodes/meadow/scatter/fungi/emissionGain", 2.0f)}));
        INFO(describe(big.validation));
        CHECK_FALSE(with(big.validation, IssueCode::OverSaturated, "big").empty());
        const Compilation fits = glade.compile(planOf({route("fits", "audio.onsetLow", "nodes/meadow/scatter/fungi/emissionGain", 0.3f)}));
        CHECK(with(fits.validation, IssueCode::OverSaturated, "fits").empty());
        const Compilation stacked = glade.compile(planOf({
            route("s1", "audio.onsetLow", "nodes/meadow/scatter/fungi/emissionGain", 0.3f),
            route("s2", "audio.onsetMid", "nodes/meadow/scatter/fungi/emissionGain", 0.3f, 50.0f),
        }));
        INFO(describe(stacked.validation));
        CHECK_FALSE(with(stacked.validation, IssueCode::OverSaturated).empty());
    }
}

TEST_CASE("A rate whose phase is time x rate is flagged; the amplitude beside it is not",
          "[directing][reactivity][validator][adr926]") {
    Glade glade;
    PlanRoute speed = route("speed", "section.energy", "scene/wind/gustSpeed", 0.5f);
    PlanRoute amount = route("amount", "section.energy", "scene/wind/gustAmount", 0.2f);
    const Compilation c = glade.compile(planOf({speed, amount}));
    INFO(describe(c.validation));
    CHECK_FALSE(with(c.validation, IssueCode::PhaseRateTrap, "speed").empty());
    CHECK(with(c.validation, IssueCode::PhaseRateTrap, "amount").empty());
    CHECK(compiledRoute(c, "speed")); // flagged, not refused: it reaches the picture
}

TEST_CASE("The reports' traps: a one-frame or unsmoothed depth, a hue from the audio, a multiply dark between hits",
          "[directing][reactivity][validator][adr926]") {
    Glade glade;
    PlanRoute eventDepth = route("eventDepth", "audio.onsetLow", "nodes/elder-gills/emissiveBoost");
    eventDepth.route.depthSource = "music.downbeat";
    PlanRoute rmsDepth = route("rmsDepth", "audio.onsetMid", "nodes/lantern-gills/emissiveBoost", 0.3f, 20.0f);
    rmsDepth.route.depthSource = "audio.rms";
    PlanRoute goodDepth = route("goodDepth", "music.downbeat", "nodes/spire-gills/emissiveBoost", 0.3f, 60.0f);
    goodDepth.route.depthSource = "section.energy";
    goodDepth.route.depthMin = 0.3f;
    PlanRoute audioHue = route("audioHue", "audio.onsetHigh", "nodes/meadow/scatter/fungi/hueOffset", 0.05f);
    PlanRoute dark = route("dark", "audio.onsetHigh", "particles/spores/emissive", 1.0f, 40.0f);
    dark.route.op = params::ModOp::Multiply; // no remap: 0 between hats
    PlanRoute lifted = route("lifted", "audio.onsetLow", "nodes/meadow/water/sparkle", 1.0f, 150.0f);
    lifted.route.op = params::ModOp::Multiply;
    lifted.route.chain.remapEnabled = true;
    lifted.route.chain.remapOutMin = 1.0f;
    lifted.route.chain.remapOutMax = 1.3f;
    const Compilation c = glade.compile(planOf({eventDepth, rmsDepth, goodDepth, audioHue, dark, lifted}));
    INFO(describe(c.validation));
    for (const char* trapped : {"eventDepth", "rmsDepth", "audioHue", "dark"}) {
        INFO(trapped);
        CHECK_FALSE(with(c.validation, IssueCode::RouteHazard, trapped).empty());
    }
    for (const char* clean : {"goodDepth", "lifted"}) {
        INFO(clean);
        CHECK(with(c.validation, IssueCode::RouteHazard, clean).empty());
    }
}

TEST_CASE("A plan source must be pure in time, and a route reading a refused source is blocked with it",
          "[directing][reactivity][validator][adr926]") {
    Glade glade;
    Plan plan = planOf({route("fromEnvelope", "env.swell", "nodes/elder-gills/emissiveBoost"),
                        route("fromLfo", "lfo.slow-swell", "nodes/lantern-gills/emissiveBoost", 0.3f, 40.0f)});
    PlanSource envelope;
    envelope.key = "src.env";
    envelope.kind = "envelope";
    envelope.name = "swell";
    PlanSource lfo;
    lfo.key = "src.lfo";
    lfo.kind = "lfo";
    lfo.name = "slow-swell";
    lfo.settings = {{"shape", "sine"}};
    lfo.parameters = {{"beatSync", 1.0f}, {"beatsPerCycle", 8.0f}};
    PlanSource typo = lfo;
    typo.key = "src.typo";
    typo.name = "typo";
    typo.parameters = {{"beatsPerCylce", 8.0f}};
    plan.sources = {envelope, lfo, typo};
    const Compilation c = glade.compile(plan);
    INFO(describe(c.validation));
    CHECK_FALSE(with(c.validation, IssueCode::NonDeterministic, "src.env").empty());
    CHECK_FALSE(with(c.validation, IssueCode::Blocked, "fromEnvelope").empty());
    CHECK_FALSE(compiledRoute(c, "fromEnvelope"));
    // The pure source compiles, with its parameters, and the route that reads it is live.
    CHECK(with(c.validation, IssueCode::DeadTarget, "fromLfo").empty());
    CHECK(compiledRoute(c, "fromLfo"));
    const auto staged = std::find_if(c.staged.sources.begin(), c.staged.sources.end(), [](const nlohmann::json& s) {
        return s.value("name", std::string()) == "slow-swell";
    });
    REQUIRE(staged != c.staged.sources.end());
    CHECK((*staged)["parameters"]["beatsPerCycle"] == 8.0);
    CHECK((*staged)["parameters"]["beatSync"] == 1.0);
    // A misspelt parameter is refused with the one it meant.
    const auto misspelt = with(c.validation, IssueCode::SchemaInvalid, "src.typo");
    REQUIRE_FALSE(misspelt.empty());
    CHECK(std::find(misspelt.front()->suggestions.begin(), misspelt.front()->suggestions.end(), "beatsPerCycle") !=
          misspelt.front()->suggestions.end());
}

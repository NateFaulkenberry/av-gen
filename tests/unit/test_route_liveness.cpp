// ADR-902: the route and parameter liveness rules. Every rule in the registry is held here with a
// case it must flag and a neighbouring case it must not -- a rule that flags everything is as
// useless as one that flags nothing, and only the pair shows which one a rule is.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/liveness.hpp"
#include "params/modulation.hpp"
#include "params/timeline.hpp"
#include "scene/composition.hpp"
#include "scene/route_liveness.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"
#include "support/temp_dir.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/wave_effect.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::params;
using namespace avgen::params::liveness;
using Catch::Matchers::ContainsSubstring;

namespace {

// ---- a world a test can describe exactly -----------------------------------------------------

struct FakeFacts final : Facts {
    const ParameterSet* params = nullptr;
    std::map<std::string, SignalFacts, std::less<>> signals;
    std::map<std::string, Finding, std::less<>> dead;
    std::map<std::string, Finding, std::less<>> rates;
    double fps = 60.0;

    [[nodiscard]] SignalFacts signal(std::string_view name) const override {
        const auto it = signals.find(name);
        return it != signals.end() ? it->second : SignalFacts{};
    }
    [[nodiscard]] const IParameter* parameter(std::string_view path) const override {
        return params != nullptr ? params->find(path) : nullptr;
    }
    [[nodiscard]] std::optional<Finding> deadTarget(std::string_view path, int) const override {
        const auto it = dead.find(path);
        return it != dead.end() ? std::optional<Finding>(it->second) : std::nullopt;
    }
    [[nodiscard]] std::optional<Finding> phaseRate(std::string_view path) const override {
        const auto it = rates.find(path);
        return it != rates.end() ? std::optional<Finding>(it->second) : std::nullopt;
    }
    [[nodiscard]] double frameRate() const override { return fps; }
};

struct World {
    ParameterSet params;
    FakeFacts facts;
    World() {
        facts.params = &params;
        params.add(ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 10.0f});
        params.add(ParamDesc<glm::vec3>{.path = "orb/color", .defaultValue = glm::vec3(0.5f), .hardMin = glm::vec3(0.0f),
                                        .hardMax = glm::vec3(1.0f)});
        params.add(ParamDesc<float>{.path = "orb/fixed",
                                    .defaultValue = 1.0f,
                                    .hardMin = 0.0f,
                                    .hardMax = 2.0f,
                                    .flags = ParamFlags{.exposed = true, .modulatable = false, .serialized = true}});
        facts.signals["audio.bass"] = SignalFacts{.exists = true};
        facts.signals["section.energy"] = SignalFacts{.exists = true};
        facts.signals["beat.pulse"] = SignalFacts{.exists = true, .isEvent = true};
    }
    [[nodiscard]] static ModRoute route(std::string source, std::string target) {
        ModRoute r;
        r.source = std::move(source);
        r.target = std::move(target);
        return r;
    }
    [[nodiscard]] std::vector<Finding> check(const ModRoute& r) const {
        return Registry::standard().checkRoute(r, facts);
    }
};

bool has(const std::vector<Finding>& findings, std::string_view rule) {
    return std::any_of(findings.begin(), findings.end(), [&](const Finding& f) { return f.rule == rule; });
}
// Only on findings that outlive the call: a pointer into a temporary vector dangles.
const Finding* findRule(std::vector<Finding>&& findings, std::string_view rule) = delete;
const Finding* findRule(const std::vector<Finding>& findings, std::string_view rule) {
    const auto it = std::find_if(findings.begin(), findings.end(), [&](const Finding& f) { return f.rule == rule; });
    return it != findings.end() ? &*it : nullptr;
}

} // namespace

// ---- the registry itself --------------------------------------------------------------------

TEST_CASE("The liveness registry lists every rule once, with a verdict and a summary", "[liveness][adr902]") {
    std::set<std::string_view> ids;
    for (const RuleInfo& r : Registry::standard().rules()) {
        INFO(r.id);
        CHECK(ids.insert(r.id).second);
        CHECK_FALSE(r.summary.empty());
        CHECK((r.verdicts == "dead" || r.verdicts == "hazard" || r.verdicts == "dead|hazard"));
        CHECK(Registry::standard().rule(r.id) == &r);
    }
    CHECK(ids.size() >= 20);
    CHECK(Registry::standard().rule("no-such-rule") == nullptr);
    // Verdicts compose dead over hazard over live.
    const std::vector<Finding> mixed{{"a", Verdict::Hazard, ""}, {"b", Verdict::Dead, ""}};
    CHECK(verdictOf(mixed) == Verdict::Dead);
    CHECK(verdictOf(std::vector<Finding>{{"a", Verdict::Hazard, ""}}) == Verdict::Hazard);
    CHECK(verdictOf(std::vector<Finding>{}) == Verdict::Live);
}

TEST_CASE("A healthy route is live", "[liveness][adr902]") {
    World w;
    const auto findings = w.check(World::route("audio.bass", "orb/scale"));
    INFO((findings.empty() ? std::string() : findings.front().rule + ": " + findings.front().reason));
    CHECK(findings.empty());
}

// ---- binding rules ----------------------------------------------------------------------------

TEST_CASE("liveness: disabled", "[liveness][adr902]") {
    World w;
    ModRoute r = World::route("audio.bass", "orb/scale");
    CHECK_FALSE(has(w.check(r), "disabled"));
    r.enabled = false;
    CHECK(has(w.check(r), "disabled"));
    Track t;
    t.target = "orb/scale";
    t.keys.push_back(Key{});
    CHECK_FALSE(has(Registry::standard().checkTrack(t, w.facts), "disabled"));
    t.enabled = false;
    CHECK(has(Registry::standard().checkTrack(t, w.facts), "disabled"));
}

TEST_CASE("liveness: unknown-source and unknown-depth-source", "[liveness][adr902]") {
    World w;
    CHECK(has(w.check(World::route("audio.nope", "orb/scale")), "unknown-source"));
    CHECK_FALSE(has(w.check(World::route("audio.bass", "orb/scale")), "unknown-source"));
    ModRoute r = World::route("audio.bass", "orb/scale");
    r.depthSource = "section.nope";
    CHECK(has(w.check(r), "unknown-depth-source"));
    r.depthSource = "section.energy";
    CHECK_FALSE(has(w.check(r), "unknown-depth-source"));
}

TEST_CASE("liveness: unknown-target, for routes and tracks", "[liveness][adr902]") {
    World w;
    CHECK(has(w.check(World::route("audio.bass", "orb/missing")), "unknown-target"));
    CHECK_FALSE(has(w.check(World::route("audio.bass", "orb/scale")), "unknown-target"));
    Track t;
    t.target = "orb/missing";
    CHECK(has(Registry::standard().checkTrack(t, w.facts), "unknown-target"));
    t.target = "orb/scale";
    CHECK_FALSE(has(Registry::standard().checkTrack(t, w.facts), "unknown-target"));
}

TEST_CASE("liveness: not-modulatable", "[liveness][adr902]") {
    World w;
    CHECK(has(w.check(World::route("audio.bass", "orb/fixed")), "not-modulatable"));
    CHECK_FALSE(has(w.check(World::route("audio.bass", "orb/scale")), "not-modulatable"));
}

TEST_CASE("liveness: component-out-of-range", "[liveness][adr902]") {
    World w;
    ModRoute r = World::route("audio.bass", "orb/color");
    r.component = 3;
    CHECK(has(w.check(r), "component-out-of-range"));
    r.component = 2;
    CHECK_FALSE(has(w.check(r), "component-out-of-range"));
}

// ---- chain rules ------------------------------------------------------------------------------

TEST_CASE("liveness: zero-amount", "[liveness][adr902]") {
    World w;
    ModRoute r = World::route("audio.bass", "orb/scale");
    r.amount = 0.0f;
    CHECK(has(w.check(r), "zero-amount"));
    r.amount = 0.25f;
    CHECK_FALSE(has(w.check(r), "zero-amount"));
    r.depthSource = "section.energy";
    r.depthMin = 0.0f;
    r.depthMax = 0.0f;
    CHECK(has(w.check(r), "zero-amount"));
    r.depthMax = 1.0f;
    CHECK_FALSE(has(w.check(r), "zero-amount"));
}

TEST_CASE("liveness: flat-chain", "[liveness][adr902]") {
    World w;
    ModRoute r = World::route("audio.bass", "orb/scale");
    SECTION("a remap onto one value") {
        r.chain.remapEnabled = true;
        r.chain.remapOutMin = 1.2f;
        r.chain.remapOutMax = 1.2f;
        CHECK(has(w.check(r), "flat-chain"));
        r.chain.remapOutMax = 1.6f;
        CHECK_FALSE(has(w.check(r), "flat-chain"));
    }
    SECTION("a gate above everything the source can reach") {
        r.chain.threshold = ThresholdMode::Gate;
        r.chain.thresholdLevel = 1.5f; // audio.bass is 0..1
        CHECK(has(w.check(r), "flat-chain"));
        r.chain.thresholdLevel = 0.5f;
        CHECK_FALSE(has(w.check(r), "flat-chain"));
    }
    SECTION("a zero gain with no offset") {
        r.chain.gain = 0.0f;
        CHECK(has(w.check(r), "flat-chain"));
        r.chain.gain = 0.5f;
        CHECK_FALSE(has(w.check(r), "flat-chain"));
    }
}

TEST_CASE("liveness: delay-over-ceiling", "[liveness][adr902]") {
    World w;
    ModRoute r = World::route("audio.bass", "orb/scale");
    r.chain.delayMs = 5000.0f;
    CHECK(has(w.check(r), "delay-over-ceiling"));
    r.chain.delayMs = 400.0f;
    CHECK_FALSE(has(w.check(r), "delay-over-ceiling"));
}

TEST_CASE("liveness: event-swallowed, when a threshold gates out the piece's typical events", "[liveness][adr902]") {
    World w;
    // The track's onsets arrive at 0.3 on the bus (the median), full strength is 1.
    SignalFacts onset{.exists = true, .isEvent = true};
    onset.typicalEventStrength = 0.3f;
    w.facts.signals["audio.onset"] = onset;
    ModRoute r = World::route("audio.onset", "orb/scale");
    r.chain.attackMs = 10.0f;
    r.chain.decayMs = 300.0f;
    r.chain.threshold = ThresholdMode::Gate;
    r.chain.thresholdLevel = 0.5f; // answers a full-strength onset, not this piece's
    const auto fFindings = w.check(r);
    const Finding* f = findRule(fFindings, "event-swallowed");
    REQUIRE(f != nullptr);
    CHECK(f->verdict == Verdict::Dead);
    CHECK_THAT(f->reason, ContainsSubstring("typical"));
    CHECK_FALSE(has(w.check(r), "flat-chain")); // it does respond at full strength
    // A gate below the typical strength passes them, and with no typical strength known the rule
    // judges at full strength, where the gate is open.
    r.chain.thresholdLevel = 0.2f;
    CHECK_FALSE(has(w.check(r), "event-swallowed"));
    r.chain.thresholdLevel = 0.5f;
    w.facts.signals["audio.onset"].typicalEventStrength = 0.0f;
    CHECK_FALSE(has(w.check(r), "event-swallowed"));
    // A continuous source is never an event route.
    r.source = "audio.bass";
    CHECK_FALSE(has(w.check(r), "event-swallowed"));
}

TEST_CASE("liveness: under ADR-900 no attack or decay swallows an event, at any frame rate", "[liveness][adr902][adr900]") {
    World w;
    for (const double fps : {24.0, 30.0, 60.0, 120.0}) {
        w.facts.fps = fps;
        for (const float attack : {0.0f, 5.0f, 20.0f, 60.0f, 250.0f, 2400.0f}) {
            for (const float decay : {0.0f, 1.0f, 5.0f, 60.0f, 900.0f}) {
                INFO(fps << " fps, attack " << attack << " ms, decay " << decay << " ms");
                ProcessorChain chain;
                chain.attackMs = attack;
                chain.decayMs = decay;
                const auto pass = eventPassThrough(chain, Polarity::Unipolar, 1.0f, fps);
                REQUIRE(pass.has_value());
                CHECK(pass->ratio > 1.0f - 1e-5f);
                ModRoute r = World::route("beat.pulse", "orb/scale");
                r.chain = chain;
                CHECK_FALSE(has(w.check(r), "event-swallowed"));
            }
        }
    }
}

TEST_CASE("liveness: the event rule measures the new chain -- the old one would have failed GV2's routes",
          "[liveness][adr902]") {
    // GV2's music.build -> scene/windSpeed: attack 2400 ms, decay 2800 ms. The ADR-900 chain lets
    // the whole event through; the old one let 0.7% through (the audit report's number).
    ProcessorChain chain;
    chain.attackMs = 2400.0f;
    chain.decayMs = 2800.0f;
    const auto pass = eventPassThrough(chain, Polarity::Unipolar, 1.0f, 60.0);
    REQUIRE(pass.has_value());
    CHECK(pass->ratio > 0.99f);
    World w;
    w.facts.signals["music.build"] = SignalFacts{.exists = true, .isEvent = true};
    ModRoute r = World::route("music.build", "orb/scale");
    r.chain = chain;
    CHECK_FALSE(has(w.check(r), "event-swallowed"));
}

TEST_CASE("liveness: pulse-between-frames and pulse-swallowed, for value-mode timeline pulses",
          "[liveness][adr902]") {
    World w;
    // Four 16.7 ms pulses starting between frames of a 30 fps grid (GV3's gap dips).
    SignalFacts gap{.exists = true};
    for (const double t : {44.306154, 73.844615, 103.383077, 132.921538}) {
        gap.pulses.emplace_back(t, t + 0.016667);
    }
    w.facts.signals["timeline.gap"] = gap;
    ModRoute r = World::route("timeline.gap", "orb/scale");
    const auto betweenFindings = w.check(r);
    const Finding* between = findRule(betweenFindings, "pulse-between-frames");
    REQUIRE(between != nullptr);
    CHECK(between->verdict == Verdict::Hazard); // caught at 60 fps, missed at 30
    CHECK_THAT(between->reason, ContainsSubstring("30 fps"));

    // Missed at the project's own rate: dead.
    w.facts.fps = 24.0;
    SignalFacts off{.exists = true};
    off.pulses.emplace_back(0.01, 0.02);
    off.pulses.emplace_back(1.01, 1.02);
    w.facts.signals["timeline.off"] = off;
    const auto deadFindings = w.check(World::route("timeline.off", "orb/scale"));
    const Finding* dead = findRule(deadFindings, "pulse-between-frames");
    REQUIRE(dead != nullptr);
    CHECK(dead->verdict == Verdict::Dead);

    // Pulses two frames long at 30 fps are caught at every rate the rule samples.
    w.facts.fps = 60.0;
    SignalFacts wide{.exists = true};
    wide.pulses.emplace_back(1.0, 1.08);
    wide.pulses.emplace_back(2.0, 2.08);
    w.facts.signals["timeline.wide"] = wide;
    ModRoute ok = World::route("timeline.wide", "orb/scale");
    CHECK_FALSE(has(w.check(ok), "pulse-between-frames"));
    CHECK_FALSE(has(w.check(ok), "pulse-swallowed"));
    // A long attack on the same pulses swallows them: the value is up for too short a time.
    ok.chain.attackMs = 1500.0f;
    CHECK(has(w.check(ok), "pulse-swallowed"));
}

// ---- source rules -----------------------------------------------------------------------------

TEST_CASE("liveness: silent-source and live-only-source come from the facts", "[liveness][adr902]") {
    World w;
    SignalFacts silent{.exists = true};
    silent.silentBecause = "no audio";
    w.facts.signals["audio.mid"] = silent;
    CHECK(has(w.check(World::route("audio.mid", "orb/scale")), "silent-source"));
    CHECK_FALSE(has(w.check(World::route("audio.bass", "orb/scale")), "silent-source"));
    SignalFacts live{.exists = true};
    live.liveOnlyBecause = "MIDI only";
    w.facts.signals["control.fader"] = live;
    const auto fFindings = w.check(World::route("control.fader", "orb/scale"));
    const Finding* f = findRule(fFindings, "live-only-source");
    REQUIRE(f != nullptr);
    CHECK(f->verdict == Verdict::Hazard);
    CHECK_FALSE(has(w.check(World::route("audio.bass", "orb/scale")), "live-only-source"));
}

// ---- set rules ----------------------------------------------------------------------------------

TEST_CASE("liveness: overridden-by-replace", "[liveness][adr902]") {
    std::vector<ModRoute> routes{World::route("audio.bass", "orb/scale"), World::route("audio.bass", "orb/scale"),
                                 World::route("audio.bass", "orb/scale")};
    routes[0].op = ModOp::Replace;
    routes[1].op = ModOp::Add;
    routes[2].op = ModOp::Replace;
    std::vector<Track> tracks(2);
    tracks[0].target = "orb/scale";
    tracks[1].target = "orb/color";
    const auto set = Registry::standard().checkSet(routes, tracks);
    REQUIRE(set.routes.size() == 3);
    CHECK(has(set.routes[0], "overridden-by-replace")); // a later Replace wins
    CHECK_FALSE(has(set.routes[1], "overridden-by-replace")); // an Add stacks on it
    CHECK_FALSE(has(set.routes[2], "overridden-by-replace")); // the last one is the one that shows
    CHECK(has(set.tracks[0], "overridden-by-replace"));        // tracks apply before routes
    CHECK_FALSE(has(set.tracks[1], "overridden-by-replace"));
    // A Replace with a depth source only replaces part of the time: a hazard, not dead.
    routes[2].depthSource = "section.energy";
    const auto partial = Registry::standard().checkSet(routes, tracks);
    const Finding* f = findRule(partial.routes[0], "overridden-by-replace");
    REQUIRE(f != nullptr);
    CHECK(f->verdict == Verdict::Hazard);
}

TEST_CASE("liveness: phase-rate reaches routes and varying tracks through the facts", "[liveness][adr902]") {
    World w;
    w.params.add(ParamDesc<float>{.path = "sources/wobble/rate", .defaultValue = 0.5f, .hardMin = 0.0f, .hardMax = 100.0f});
    w.facts.rates["sources/wobble/rate"] = Finding{"phase-rate", Verdict::Hazard, "time x rate"};
    CHECK(has(w.check(World::route("audio.bass", "sources/wobble/rate")), "phase-rate"));
    CHECK_FALSE(has(w.check(World::route("audio.bass", "orb/scale")), "phase-rate"));
    Track t;
    t.target = "sources/wobble/rate";
    t.keys = {Key{.time = 0.0, .value = {0.5f}}, Key{.time = 10.0, .value = {0.5f}}};
    CHECK_FALSE(has(Registry::standard().checkTrack(t, w.facts), "phase-rate")); // held: a constant rate
    t.keys[1].value[0] = 2.0f;
    CHECK(has(Registry::standard().checkTrack(t, w.facts), "phase-rate")); // ramped: the jump
}

// ---- the bind reports each problem once --------------------------------------------------------

TEST_CASE("Modulator::bind logs each liveness problem once, however many times it binds", "[liveness][modulation][adr902]") {
    auto sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    auto logger = spdlog::default_logger();
    logger->sinks().push_back(sink);
    const auto count = [&sink](std::string_view needle) {
        int n = 0;
        for (const std::string& line : sink->last_formatted()) {
            n += line.find(needle) != std::string::npos ? 1 : 0;
        }
        return n;
    };
    signals::SignalBus bus;
    bus.declare("beat.pulse", 0.0f, 1.0f, true);
    ParameterSet params;
    params.add(ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    Modulator modulator;
    ModRoute clamped;
    clamped.source = "beat.pulse";
    clamped.target = "orb/scale";
    clamped.chain.delayMs = 6000.0f; // past the ceiling: a hazard every bind would otherwise repeat
    modulator.addRoute(clamped);
    ModRoute missing = clamped;
    missing.target = "orb/nothing";
    modulator.addRoute(missing);
    for (int i = 0; i < 3; ++i) {
        std::ignore = modulator.bind(bus, params);
    }
    logger->sinks().pop_back();
    CHECK(count("[delay-over-ceiling]") == 1);
    CHECK(count("orb/nothing") == 1); // the unresolved route: once, not once per bind
    // The same key a host would use is spent.
    CHECK_FALSE(modulator.firstReport("delay-over-ceiling|beat.pulse|orb/scale|-1"));
    CHECK(modulator.firstReport("delay-over-ceiling|beat.pulse|orb/other|-1"));
}

// ---- the scene's facts ---------------------------------------------------------------------------

namespace {

std::string sphere(const std::string& name, const std::string& material) {
    return R"({"name": ")" + name + R"(", "kind": "procedural", "procedural": {
        "source": {"kind": "sphere", "radius": 1.0, "segments": 8, "rings": 4},
        "distribution": {"kind": "single"}, "material": )" + material + "}}";
}

struct SceneWorld {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    params::ParameterSet params;
    params::Modulator modulator;
    std::unique_ptr<scene::Composition> composition;
    signals::SignalBus bus;
    signals::SourceRack sources;

    explicit SceneWorld(int extraPrograms = 0) {
        std::string programs = R"(
          {"name": "glowing", "ops": [{"kind": "constant", "dst": 1, "constant": [0.2, 1.0, 0.5, 1.0]}], "emission": 1},
          {"name": "dull", "ops": [{"kind": "constant", "dst": 1, "constant": [0.3, 0.3, 0.3, 1.0]}], "baseColor": 1},
          {"name": "layered", "ops": [{"kind": "constant", "dst": 1, "constant": [0.3, 0.3, 0.3, 1.0]}], "baseColor": 1,
           "layers": [{"name": "sparks", "ops": [{"kind": "constant", "dst": 2, "constant": [1, 1, 0, 1]}], "emission": 2}]},
          {"name": "orphan", "ops": [{"kind": "constant", "dst": 1, "constant": [1, 1, 1, 1]}], "emission": 1})";
        for (int i = 0; i < extraPrograms; ++i) {
            programs += R"(, {"name": "extra)" + std::to_string(i) +
                        R"(", "ops": [{"kind": "constant", "dst": 1, "constant": [1, 1, 1, 1]}], "emission": 1})";
        }
        std::string nodes = sphere("lamp", R"({"program": "glowing"})") + "," +
                            sphere("rock", R"({"program": "dull", "emissiveIntensity": 0.0})") + "," +
                            sphere("ember", R"({"emissiveIntensity": 2.0, "emissiveColor": [1.0, 0.4, 0.1]})") + "," +
                            sphere("moss", R"({"program": "layered"})") + "," +
                            sphere("stone", R"({"emissiveIntensity": 0.0})") + "," +
                            R"({"name": "cluster", "kind": "group"},)" +
                            R"({"name": "pebble", "kind": "procedural", "parent": "cluster", "procedural": {
                                "source": {"kind": "sphere", "radius": 0.2, "segments": 6, "rings": 3},
                                "distribution": {"kind": "single"}, "material": {"emissiveIntensity": 0.0}}},)" +
                            R"({"name": "hive", "kind": "group"},)" +
                            R"({"name": "firefly", "kind": "procedural", "parent": "hive", "procedural": {
                                "source": {"kind": "sphere", "radius": 0.1, "segments": 6, "rings": 3},
                                "distribution": {"kind": "single"},
                                "material": {"emissiveIntensity": 3.0, "emissiveColor": [1.0, 1.0, 0.2]}}})";
        for (int i = 0; i < extraPrograms; ++i) {
            nodes += "," + sphere("extra" + std::to_string(i), R"({"program": "extra)" + std::to_string(i) + R"("})");
        }
        const auto document = nlohmann::json::parse(R"({"format": "avgen-scene", "version": 1, "name": "liveness",
            "materialPrograms": [)" + programs + R"(], "nodes": [)" + nodes + "]}");
        auto loaded = scene::Composition::fromJson(document, registry);
        REQUIRE(loaded.has_value());
        composition = std::move(*loaded);
        composition->attach(params, modulator);
        composition->update(FrameTime{});
        sources.attach(bus, params);
    }

    [[nodiscard]] scene::LivenessInputs inputs() const {
        scene::LivenessInputs in;
        in.bus = &bus;
        in.params = &params;
        in.sources = &sources;
        in.composition = composition.get();
        in.durationSeconds = 60.0;
        in.hasAudio = true;
        return in;
    }
    [[nodiscard]] std::vector<Finding> target(const std::string& path, const scene::LivenessInputs& in) const {
        const scene::SceneLivenessFacts facts(in);
        return Registry::standard().checkTarget(path, -1, facts);
    }
    [[nodiscard]] std::vector<Finding> target(const std::string& path) const { return target(path, inputs()); }
};

} // namespace

TEST_CASE("liveness: program-has-no-emission and emission-lives-in-layer", "[liveness][scene][adr902]") {
    SceneWorld w;
    CHECK(has(w.target("material/dull/emissionIntensity"), "program-has-no-emission"));
    const auto layered = w.target("material/layered/emissionIntensity");
    const Finding* f = findRule(layered, "emission-lives-in-layer");
    REQUIRE(f != nullptr);
    CHECK_THAT(f->reason, ContainsSubstring("sparks"));
    CHECK_FALSE(has(layered, "program-has-no-emission"));
    // The program that writes emission, and a different output of the one that does not.
    CHECK(w.target("material/glowing/emissionIntensity").empty());
    CHECK(w.target("material/dull/op/1/constant/constant").empty());
}

TEST_CASE("liveness: program-unused and program-not-uploaded", "[liveness][scene][adr902]") {
    SceneWorld w;
    CHECK(has(w.target("material/orphan/emissionIntensity"), "program-unused"));
    CHECK_FALSE(has(w.target("material/glowing/emissionIntensity"), "program-unused"));
    // Four programs in the scene, then five more: the ninth and later are past the GPU's table.
    SceneWorld nine(5);
    CHECK(has(nine.target("material/extra4/emissionIntensity"), "program-not-uploaded")); // the ninth
    CHECK_FALSE(has(nine.target("material/extra3/emissionIntensity"), "program-not-uploaded")); // the eighth
    CHECK_FALSE(has(nine.target("material/glowing/emissionIntensity"), "program-not-uploaded"));
}

TEST_CASE("liveness: program-owns-emission (ADR-179)", "[liveness][scene][adr902]") {
    SceneWorld w;
    // A material's own emission on a surface whose program writes emission is replaced by it...
    CHECK(has(w.target("procedural/lamp/material/emissive"), "program-owns-emission"));
    CHECK(has(w.target("procedural/lamp/material/emissiveColor"), "program-owns-emission"));
    // ...and is the whole emission on one with no program, or a program that leaves it alone.
    CHECK_FALSE(has(w.target("procedural/ember/material/emissive"), "program-owns-emission"));
    CHECK_FALSE(has(w.target("procedural/rock/material/emissive"), "program-owns-emission"));
}

TEST_CASE("liveness: node-emits-nothing, to the post-program emissiveBoost semantics", "[liveness][scene][adr902]") {
    SceneWorld w;
    // Dead: nothing the node draws emits -- a program that writes no emission and a zero emissive, a
    // plain zero emissive, and a group whose one child emits nothing.
    CHECK(has(w.target("nodes/rock/emissiveBoost"), "node-emits-nothing"));
    CHECK(has(w.target("nodes/stone/emissiveBoost"), "node-emits-nothing"));
    CHECK(has(w.target("nodes/cluster/emissiveBoost"), "node-emits-nothing"));
    // Live: a program-lit procedural node (the case agent/emission's multiplier makes live), a
    // material emitter, a layer emitter, and a group with an emitting child.
    CHECK_FALSE(has(w.target("nodes/lamp/emissiveBoost"), "node-emits-nothing"));
    CHECK_FALSE(has(w.target("nodes/ember/emissiveBoost"), "node-emits-nothing"));
    CHECK_FALSE(has(w.target("nodes/moss/emissiveBoost"), "node-emits-nothing"));
    CHECK_FALSE(has(w.target("nodes/hive/emissiveBoost"), "node-emits-nothing"));
}

TEST_CASE("liveness: phase-rate from the scene's table, narrowed by source kind and effect type",
          "[liveness][scene][adr902]") {
    SceneWorld w;
    w.sources.add(std::make_unique<signals::LfoSource>("wobble"));
    w.sources.add(std::make_unique<signals::EnvelopeSource>("hit"));
    w.sources.attach(w.bus, w.params);
    CHECK(has(w.target("sources/wobble/rate"), "phase-rate"));
    CHECK_FALSE(has(w.target("sources/hit/attackMs"), "phase-rate")); // integrated over dt: safe

    // fx/<id>/speed is a phase rate for Color Cycling and not for a particle emitter.
    params::ParameterSet& p = w.params;
    p.add(ParamDesc<float>{.path = "fx/hue/speed", .defaultValue = 0.1f, .hardMin = 0.0f, .hardMax = 10.0f});
    p.add(ParamDesc<float>{.path = "fx/sparks/speed", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    std::vector<world::EffectInstance> effects(2);
    effects[0].id = "hue";
    effects[0].kind = world::EffectKind::ColorCycling;
    effects[0].owner = world::EffectOwner::entity("lamp");
    effects[1].id = "sparks";
    effects[1].kind = world::EffectKind::ParticleEmitter;
    effects[1].owner = world::EffectOwner::entity("lamp");
    scene::LivenessInputs in = w.inputs();
    in.effects = effects;
    CHECK(has(w.target("fx/hue/speed", in), "phase-rate"));
    CHECK_FALSE(has(w.target("fx/sparks/speed", in), "phase-rate"));

    // A secondary rate only while its primary moves: a field's wavelength with and without a wave speed.
    p.add(ParamDesc<float>{.path = "field/ring/waveSpeed", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 100.0f});
    p.add(ParamDesc<float>{.path = "field/ring/wavelength", .defaultValue = 5.0f, .hardMin = 0.0f, .hardMax = 100.0f});
    CHECK(has(w.target("field/ring/waveSpeed"), "phase-rate"));
    CHECK_FALSE(has(w.target("field/ring/wavelength"), "phase-rate"));
    p.find("field/ring/waveSpeed")->setBaseComponent(0, 12.0f);
    p.resetFinals();
    CHECK(has(w.target("field/ring/wavelength"), "phase-rate"));

    // windSpeed is a phase rate only through wind-body leaf flutter.
    p.add(ParamDesc<float>{.path = "scene/windSpeed", .defaultValue = 0.7f, .hardMin = 0.0f, .hardMax = 10.0f});
    CHECK_FALSE(has(w.target("scene/windSpeed"), "phase-rate"));
    p.add(ParamDesc<float>{.path = "nodes/oak/wind/strength", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 8.0f});
    CHECK(has(w.target("scene/windSpeed"), "phase-rate"));
    // And a nested composition's copy of a table path is the same parameter.
    p.add(ParamDesc<float>{.path = "nodes/child/scene/volumeNoiseSpeed", .defaultValue = 1.0f, .hardMin = 0.0f,
                           .hardMax = 10.0f});
    CHECK(has(w.target("nodes/child/scene/volumeNoiseSpeed"), "phase-rate"));
}

TEST_CASE("liveness: effect-never-fires, for the effect and every route to it", "[liveness][scene][adr902]") {
    SceneWorld w;
    w.params.add(ParamDesc<float>{.path = "fx/ring/intensity", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    world::EffectInstance ring;
    ring.id = "ring";
    ring.kind = world::EffectKind::GroundPulse;
    ring.owner = world::EffectOwner::entity("lamp");
    ring.wave.source.kind = world::SourceKind::Owner;
    ring.activation = world::Activation::HeroFocus;
    std::vector<world::EffectInstance> effects{ring};
    scene::LivenessInputs in = w.inputs();
    in.effects = effects;

    SECTION("hero focus with no shot spans") {
        const scene::SceneLivenessFacts facts(in);
        const auto never = facts.effectNeverFires(ring);
        REQUIRE(never.has_value());
        CHECK(never->rule == "effect-never-fires");
        CHECK_THAT(never->reason, ContainsSubstring("shot span"));
        CHECK(has(w.target("fx/ring/intensity", in), "effect-never-fires"));
    }
    SECTION("hero focus with a span on another subject, and then on its own") {
        std::vector<world::ShotSpan> spans(1);
        spans[0].start = 0.0;
        spans[0].end = 10.0;
        spans[0].spotlight = true;
        spans[0].subject = "ember";
        in.shots = spans;
        CHECK(scene::SceneLivenessFacts(in).effectNeverFires(ring).has_value());
        spans[0].subject = "lamp";
        CHECK_FALSE(scene::SceneLivenessFacts(in).effectNeverFires(ring).has_value());
        CHECK_FALSE(has(w.target("fx/ring/intensity", in), "effect-never-fires"));
    }
    SECTION("camera travel with spans that never travel") {
        effects[0].activation = world::Activation::CameraTravel;
        std::vector<world::ShotSpan> spans(1);
        spans[0].end = 5.0;
        in.shots = spans;
        CHECK(scene::SceneLivenessFacts(in).effectNeverFires(effects[0]).has_value());
        spans[0].travel = true;
        CHECK_FALSE(scene::SceneLivenessFacts(in).effectNeverFires(effects[0]).has_value());
    }
    SECTION("a window after the piece ends, and one inside it") {
        effects[0].activation = world::Activation::Window;
        effects[0].timing.windowStart = 90.0; // the piece is 60 s
        CHECK(scene::SceneLivenessFacts(in).effectNeverFires(effects[0]).has_value());
        effects[0].timing.windowStart = 20.0;
        CHECK_FALSE(scene::SceneLivenessFacts(in).effectNeverFires(effects[0]).has_value());
    }
    SECTION("a trigger with no events, and an always-on effect") {
        effects[0].activation = world::Activation::Trigger;
        effects[0].timing.trigger.source = world::TriggerSource::MusicEvent;
        effects[0].timing.trigger.name = "drop";
        CHECK(scene::SceneLivenessFacts(in).effectNeverFires(effects[0]).has_value()); // no track: no events
        effects[0].activation = world::Activation::Always;
        CHECK_FALSE(scene::SceneLivenessFacts(in).effectNeverFires(effects[0]).has_value());
    }
    SECTION("an owner the scene does not have") {
        effects[0].activation = world::Activation::Always;
        effects[0].owner = world::EffectOwner::entity("nobody");
        const auto never = scene::SceneLivenessFacts(in).effectNeverFires(effects[0]);
        REQUIRE(never.has_value());
        CHECK_THAT(never->reason, ContainsSubstring("nobody"));
    }
}

TEST_CASE("liveness: silent-source and live-only-source from the scene's facts", "[liveness][scene][adr902]") {
    SceneWorld w;
    w.bus.declare("audio.bass");
    w.bus.declare("control.fader");
    scene::LivenessInputs in = w.inputs();
    in.hasAudio = false;
    {
        const scene::SceneLivenessFacts facts(in);
        CHECK_FALSE(facts.signal("audio.bass").silentBecause.empty());
        CHECK_FALSE(facts.signal("control.fader").liveOnlyBecause.empty());
    }
    in.hasAudio = true;
    in.offline = false;
    {
        const scene::SceneLivenessFacts facts(in);
        CHECK(facts.signal("audio.bass").silentBecause.empty());
        CHECK(facts.signal("control.fader").liveOnlyBecause.empty());
    }
    // A bind asks without judging silence: the audio may not be installed yet.
    in.hasAudio = false;
    in.judgeSilence = false;
    CHECK(scene::SceneLivenessFacts(in).signal("audio.bass").silentBecause.empty());
}

TEST_CASE("The scene's facts see a timeline source's mode and its pulses", "[liveness][scene][adr902]") {
    SceneWorld w;
    auto kick = std::make_unique<signals::TimelineSource>("kick");
    for (const double t : {1.0, 2.0, 3.0}) {
        kick->addKey({t, 1.0f, signals::KeyInterpolation::Step});
        kick->addKey({t + 0.025, 0.0f, signals::KeyInterpolation::Step});
    }
    auto* raw = static_cast<signals::TimelineSource*>(&w.sources.add(std::move(kick)));
    w.sources.attach(w.bus, w.params);
    {
        const scene::SceneLivenessFacts facts(w.inputs());
        const SignalFacts s = facts.signal("timeline.kick");
        CHECK_FALSE(s.isEvent);
        CHECK(s.pulses.size() == 3);
    }
    raw->setMode(signals::TimelineMode::Event);
    w.sources.attach(w.bus, w.params);
    const scene::SceneLivenessFacts facts(w.inputs());
    const SignalFacts s = facts.signal("timeline.kick");
    CHECK(s.isEvent);
    CHECK(s.pulses.empty());
}

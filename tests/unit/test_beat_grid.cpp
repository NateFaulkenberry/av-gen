// ADR-1045: the beat grid -- the author's bar numbering, the tempo map, and authored events.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "signals/beat_grid.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <memory>

using namespace avgen;
using namespace avgen::signals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace {

// All You Got in the owner's numbering: bar 1 is the analysis's bar 2 (2.2018 s), 109 BPM to the
// owner's bar 74 and 111 from the downbeat of bar 75 (165.1376 s in the file).
json songSettings() {
    return json::parse(R"({
      "origin": 2.20183486,
      "beatsPerBar": 4,
      "tempo": [{"bar": 1, "bpm": 109}, {"bar": 75, "bpm": 111}],
      "sections": {"release": 17, "verse1": 25, "bridge2": 75, "bridge3": 83},
      "events": [
        {"at": "release:4:4", "channel": "clap", "release": 1},
        {"at": "verse1:4:4", "channel": "clapB", "release": 2, "curve": "linear", "strength": 0.5},
        {"at": "bridge2:0:4.5", "channel": "is", "attack": 0.25, "hold": 0.5, "release": 0.25},
        {"at": "bridge2:1:1", "channel": "that", "repeat": {"every": 4, "count": 4}, "release": 0.5},
        {"at": "5:1", "until": "17:1", "channel": "eighthGate", "attack": 1, "release": 2}
      ]})");
}

SourceContext at(double seconds) {
    SourceContext ctx;
    ctx.time.renderTime = seconds;
    return ctx;
}

struct Fixture {
    SignalBus bus;
    params::ParameterSet params;
    BeatGridSource source{"song"};

    Fixture() {
        REQUIRE(source.settingsFromJson(songSettings()).has_value());
        source.attach(bus, params);
    }
    float sig(const std::string& name) const {
        const auto id = bus.find(name);
        REQUIRE(id.has_value());
        return bus.value(*id);
    }
};

constexpr double kBar109 = 4.0 * 60.0 / 109.0;
constexpr double kBeat109 = 60.0 / 109.0;
constexpr double kBeat111 = 60.0 / 111.0;

} // namespace

TEST_CASE("The beat grid maps the owner's bars to the song's seconds across the tempo step",
          "[beatgrid][adr1045]") {
    Fixture f;
    const BeatGrid& g = f.source.grid();
    // Bar 1 beat 1 is the origin; the count-in bar before it is at 0 s.
    CHECK_THAT(g.secondsAt(g.beatsOf(1, 1.0)), WithinAbs(2.20183486, 1e-9));
    CHECK_THAT(g.secondsAt(g.beatsOf(0, 1.0)), WithinAbs(0.0, 1e-6));
    // The owner's bar 75 is the analysis's bar 76, the tempo step at 165.1376 s.
    CHECK_THAT(g.secondsAt(g.beatsOf(75, 1.0)), WithinAbs(165.1376, 1e-3));
    // After the step a beat is 60/111 s.
    CHECK_THAT(g.secondsAt(g.beatsOf(75, 2.0)) - g.secondsAt(g.beatsOf(75, 1.0)), WithinAbs(kBeat111, 1e-9));
    // And the two maps invert each other on both sides of it.
    for (const double s : {0.0, 1.0, 50.0, 165.0, 165.2, 240.0}) {
        CHECK_THAT(g.secondsAt(g.beatsAt(s)), WithinAbs(s, 1e-9));
    }
}

TEST_CASE("Grid positions read the owner's way: section:bar:beat, the and of 4, and the bar before",
          "[beatgrid][adr1045]") {
    Fixture f;
    const BeatGrid& g = f.source.grid();
    // Release bar 4 beat 4 = global bar 20 beat 4.
    CHECK_THAT(*g.parsePosition("release:4:4"), WithinAbs(g.beatsOf(20, 4.0), 1e-12));
    CHECK_THAT(*g.parsePosition("20:4"), WithinAbs(g.beatsOf(20, 4.0), 1e-12));
    // "IS" on the and of 4 before bridge 2's downbeat: global bar 74, beat 4.5.
    CHECK_THAT(*g.parsePosition("bridge2:0:4.5"), WithinAbs(g.beatsOf(74, 4.5), 1e-12));
    // Eighth-note words: LET 1, IT 1.5, GO 2.
    CHECK_THAT(*g.parsePosition("verse1:1:1.5") - *g.parsePosition("verse1:1:1"), WithinAbs(0.5, 1e-12));
    CHECK_FALSE(g.parsePosition("chorus:1:1").has_value());
    CHECK_FALSE(g.parsePosition("1").has_value());
    CHECK_FALSE(g.parsePosition("1.5:2").has_value());
}

TEST_CASE("Quarter and eighth pulses peak on their beats and fall to zero before the next",
          "[beatgrid][adr1045]") {
    Fixture f;
    const BeatGrid& g = f.source.grid();
    const double beat = g.secondsAt(g.beatsOf(9, 3.0));
    f.source.update(f.bus, at(beat));
    CHECK_THAT(f.sig("grid.song.quarter"), WithinAbs(1.0, 1e-4));
    CHECK_THAT(f.sig("grid.song.eighth"), WithinAbs(1.0, 1e-4));
    CHECK_THAT(f.sig("grid.song.quarter.wave"), WithinAbs(1.0, 1e-4));
    CHECK(f.sig("grid.song.bar") < 0.2f); // beat 3 is half a bar after the downbeat
    // Half a beat later: the eighth pulses again, the quarter is mid-way and its wave is at its trough.
    f.source.update(f.bus, at(beat + kBeat109 * 0.5));
    CHECK_THAT(f.sig("grid.song.eighth"), WithinAbs(1.0, 1e-4));
    CHECK(f.sig("grid.song.quarter") < 0.25f);
    CHECK_THAT(f.sig("grid.song.quarter.wave"), WithinAbs(0.0, 1e-4));
    CHECK_THAT(f.sig("grid.song.quarter.phase"), WithinAbs(0.5, 1e-4));
    // Just before the next beat the quarter pulse is (almost exactly) zero.
    f.source.update(f.bus, at(beat + kBeat109 * 0.999));
    CHECK(f.sig("grid.song.quarter") < 0.005f);
    // A downbeat: the bar pulse fires.
    f.source.update(f.bus, at(g.secondsAt(g.beatsOf(10, 1.0))));
    CHECK_THAT(f.sig("grid.song.bar"), WithinAbs(1.0, 1e-4));
    // After the tempo step the quarter still lands on the 111 grid.
    f.source.update(f.bus, at(g.secondsAt(g.beatsOf(75, 4.0))));
    CHECK(f.sig("grid.song.quarter") > 0.99f);
}

TEST_CASE("Authored events are envelopes on their own channels: attack anticipates, the peak lands on the beat",
          "[beatgrid][adr1045]") {
    Fixture f;
    const BeatGrid& g = f.source.grid();
    const double clap = g.secondsAt(g.beatsOf(20, 4.0));
    CHECK(f.source.channelAt("clap", clap - 0.01) == 0.0f);
    CHECK_THAT(f.source.channelAt("clap", clap), WithinAbs(1.0, 1e-6));
    CHECK(f.source.channelAt("clap", clap + kBeat109 * 0.5) < 0.2f); // exp release over one beat
    CHECK(f.source.channelAt("clap", clap + kBeat109 * 1.01) == 0.0f);
    // A different clap fires a different channel, with its own strength and curve.
    const double clapB = g.secondsAt(g.beatsOf(28, 4.0));
    CHECK(f.source.channelAt("clap", clapB) == 0.0f);
    CHECK_THAT(f.source.channelAt("clapB", clapB + kBeat109), WithinAbs(0.25, 1e-6)); // linear, half way
    // "IS": rises over a quarter beat into the and of 4, holds half a beat, falls a quarter beat.
    const double is = g.secondsAt(g.beatsOf(74, 4.5));
    CHECK(f.source.channelAt("is", is - kBeat109 * 0.3) == 0.0f);
    CHECK_THAT(f.source.channelAt("is", is - kBeat109 * 0.125), WithinAbs(0.5, 1e-6));
    CHECK_THAT(f.source.channelAt("is", is + kBeat109 * 0.4), WithinAbs(1.0, 1e-6));
    // Repeats every bar, four times, across the 111 grid (bridge 2 is after the step).
    for (int k = 0; k < 4; ++k) {
        CHECK_THAT(f.source.channelAt("that", g.secondsAt(g.beatsOf(75 + k, 1.0))), WithinAbs(1.0, 1e-3));
    }
    CHECK(f.source.channelAt("that", g.secondsAt(g.beatsOf(79, 1.0))) == 0.0f);
    // A span: the gate is up from bar 5 to bar 17, then releases over two beats.
    CHECK_THAT(f.source.channelAt("eighthGate", g.secondsAt(g.beatsOf(10, 1.0))), WithinAbs(1.0, 1e-6));
    CHECK(f.source.channelAt("eighthGate", g.secondsAt(g.beatsOf(17, 3.5))) == 0.0f);
    CHECK(f.source.channelAt("eighthGate", g.secondsAt(g.beatsOf(4, 1.0))) == 0.0f);
    CHECK(f.source.channelAt("eighthGate", g.secondsAt(g.beatsOf(4, 4.5))) > 0.0f); // the attack
    // The bus publishes the same values.
    f.source.update(f.bus, at(clap));
    CHECK_THAT(f.sig("grid.song.clap"), WithinAbs(1.0, 1e-6));
    CHECK(f.sig("grid.song.clapB") == 0.0f);
}

TEST_CASE("The beat grid refuses what it cannot place", "[beatgrid][adr1045]") {
    BeatGridSource s("song");
    CHECK_FALSE(s.settingsFromJson(json::parse(R"({"events": [{"at": "x:1:1", "channel": "a"}]})")).has_value());
    CHECK_FALSE(s.settingsFromJson(json::parse(R"({"events": [{"at": "1:1", "channel": "quarter"}]})")).has_value());
    CHECK_FALSE(s.settingsFromJson(json::parse(R"({"events": [{"at": "1:1"}]})")).has_value());
    CHECK_FALSE(s.settingsFromJson(json::parse(R"({"tempo": []})")).has_value());
    CHECK_FALSE(
        s.settingsFromJson(json::parse(R"({"events": [{"at": "2:1", "until": "1:1", "channel": "a"}]})")).has_value());
    const auto r = s.settingsFromJson(json::parse(R"({"events": [{"at": "3:1", "channel": "a", "attack": -1}]})"));
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error().message, ContainsSubstring("attack"));
}

TEST_CASE("A beat grid round-trips through the rack's JSON and drives a route", "[beatgrid][adr1045]") {
    SourceRack rack;
    json doc = json::array();
    doc.push_back(json{{"kind", "beatgrid"}, {"name", "song"}, {"settings", songSettings()}});
    REQUIRE(rack.fromJson(doc).has_value());
    REQUIRE(rack.find("beatgrid", "song") != nullptr);
    const json out = rack.toJson();
    SourceRack again;
    REQUIRE(again.fromJson(out).has_value());
    auto* g = dynamic_cast<BeatGridSource*>(again.find("beatgrid", "song"));
    REQUIRE(g != nullptr);
    CHECK(g->events().size() == 8);
    CHECK(g->channels().size() == 5);
}

TEST_CASE("Routes from the beat grid land a seek exactly where a play does", "[beatgrid][adr1045][seek]") {
    // A route through a smoothing chain is the hard case: it has state, and ADR-901 replays it from
    // the grid's pure-in-time signals.
    const auto build = [](app::Engine& engine) {
        json doc = json::array();
        doc.push_back(json{{"kind", "beatgrid"}, {"name", "song"}, {"settings", songSettings()}});
        REQUIRE(engine.sources().fromJson(doc).has_value());
        engine.sources().attach(engine.signals(), engine.params());
        params::ModRoute a;
        a.source = "grid.song.clap";
        a.target = "post/grade/saturation";
        a.chain.decayMs = 300.0f;
        engine.modulator().addRoute(std::move(a));
        params::ModRoute b;
        b.source = "grid.song.quarter.wave";
        b.target = "post/grade/contrast";
        engine.modulator().addRoute(std::move(b));
        engine.rebind();
    };
    const auto values = [](app::Engine& e) {
        return std::array<float, 2>{e.params().find("post/grade/saturation")->finalComponent(0),
                                    e.params().find("post/grade/contrast")->finalComponent(0)};
    };
    // Frame 2745 at 60 fps = 45.75 s: just after release bar 4 beat 4's clap (45.69 s).
    const long long target = 2745;
    app::Engine played{app::EngineMode::Offline};
    build(played);
    std::array<float, 2> playedValues{};
    for (long long k = 0; k <= target; ++k) {
        played.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    playedValues = values(played);
    app::Engine seeked{app::EngineMode::Offline};
    build(seeked);
    seeked.seekSeconds(static_cast<double>(target) / 60.0);
    seeked.update(FrameTime{static_cast<double>(target) / 60.0, 0.0, 0});
    const auto seekedValues = values(seeked);
    CHECK(seekedValues[0] == playedValues[0]);
    CHECK(seekedValues[1] == playedValues[1]);
    // And the clap is visibly up there (the route is live, not a pair of zeros agreeing).
    CHECK(playedValues[0] > 1.05f);
}

// Art pass 2 section 19's "environmental object animation": spin, bob, scale-pulse and glow on beat
// sources are routes from the grid into an SDF prop's transform nodes and surfaces -- no new engine
// code, but the recipe the guide gives must actually reach the tree, and land a seek where a play does.
TEST_CASE("Beat-grid routes spin, bob and pulse an SDF prop, seek-exact", "[beatgrid][adr1045][liminal]") {
    const auto build = [](app::Engine& engine) {
        REQUIRE(engine
                    .setCompositionJson(json::parse(R"({"format": "avgen-scene", "version": 1, "name": "props",
          "nodes": [{"kind": "sdf", "name": "props", "sdf": {"compile": true,
            "surfaces": [{"color": [0.02, 0.02, 0.02], "emission": [0, 0, 0]}],
            "tree": {"root": {"kind": "translate", "name": "lampAt", "translation": [0, 1, -3],
              "children": [{"kind": "rotate", "name": "lampSpin", "rotation": [0, 0, 0],
                "children": [{"kind": "scale", "name": "lampPulse", "scale": 1.0,
                  "children": [{"kind": "box", "size": [0.3, 0.6, 0.3]}]}]}]}}}}]})"))
                    .has_value());
        json doc = json::array();
        doc.push_back(json{{"kind", "beatgrid"}, {"name", "song"}, {"settings", songSettings()}});
        REQUIRE(engine.sources().fromJson(doc).has_value());
        engine.sources().attach(engine.signals(), engine.params());
        const auto add = [&](const char* source, const char* target, int component, float amount,
                             params::ModOp op = params::ModOp::Add) {
            params::ModRoute r;
            r.source = source;
            r.target = target;
            r.component = component;
            r.amount = amount;
            r.op = op;
            engine.modulator().addRoute(std::move(r));
        };
        add("grid.song.bar.phase", "sdf/props/node/lampSpin/rotation", 1, 360.0f);         // a turn a bar
        add("grid.song.quarter.wave", "sdf/props/node/lampAt/translation", 1, 0.1f);       // bob
        add("grid.song.quarter", "sdf/props/node/lampPulse/scale", -1, 0.25f);             // scale pulse
        add("grid.song.clap", "sdf/props/surface/0/emission", -1, 4.0f);                   // glow on a clap
        engine.rebind();
    };
    const auto values = [](app::Engine& e) {
        return std::array<float, 4>{e.params().find("sdf/props/node/lampSpin/rotation")->finalComponent(1),
                                    e.params().find("sdf/props/node/lampAt/translation")->finalComponent(1),
                                    e.params().find("sdf/props/node/lampPulse/scale")->finalComponent(0),
                                    e.params().find("sdf/props/surface/0/emission")->finalComponent(0)};
    };
    const long long target = 2745; // 45.75 s: just after release bar 4 beat 4's clap
    app::Engine played{app::EngineMode::Offline};
    build(played);
    for (long long k = 0; k <= target; ++k) {
        played.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    const auto p = values(played);
    // The clap is up, the prop is part-way round its turn, lifted and swollen by the beat it is on.
    CHECK(p[3] > 1.0f);
    CHECK(p[0] > 200.0f); // beat 4 of the bar: three quarters of a turn
    CHECK(p[0] < 360.0f);
    CHECK(p[1] > 1.05f);
    CHECK(p[2] > 1.1f);
    app::Engine seeked{app::EngineMode::Offline};
    build(seeked);
    seeked.seekSeconds(static_cast<double>(target) / 60.0);
    seeked.update(FrameTime{static_cast<double>(target) / 60.0, 0.0, 0});
    const auto s = values(seeked);
    for (std::size_t i = 0; i < s.size(); ++i) {
        INFO("value " << i);
        CHECK(s[i] == p[i]);
    }
}

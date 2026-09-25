// TRIGGER (Effect Library Wave 2, shared-infrastructure.md "TRIGGER"): deterministic event
// activation.
//
// What is asked here is the contract: `TriggerClock::lastTriggers(t, K)` answers each source kind
// from a known event list, it is PURE (the same t gives the same answer whatever was asked before),
// the Proximity source reads only what the checkpointed HistoryBank holds (so a restored bank answers
// what the recorded one did), a trigger survives both files and an unknown source is refused by name,
// and an instance waiting for its first trigger is Dormant with a reason. The seek proofs on a whole
// engine are in test_shockwave.cpp.

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "signals/musical_events.hpp"
#include "support/temp_dir.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/history_bank.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

std::vector<double> ask(const world::TriggerClock& clock, const world::Trigger& trig, double t, std::size_t k = 4,
                        std::string_view owner = {}) {
    std::vector<double> out(k, -1.0);
    out.resize(clock.lastTriggers(trig, owner, t, out));
    return out;
}

world::Trigger beat(int everyN, int offset = 0) {
    world::Trigger t;
    t.source = world::TriggerSource::Beat;
    t.everyN = everyN;
    t.offset = offset;
    return t;
}

// Beats every half second from 0.5 s (0.5, 1.0, ... 10.0): index i is at 0.5 (i + 1).
world::TriggerClock beatClock() {
    world::TriggerClock clock;
    std::vector<double> beats;
    for (int i = 0; i < 20; ++i) {
        beats.push_back(0.5 * (i + 1));
    }
    clock.setBeats(beats);
    return clock;
}

} // namespace

TEST_CASE("TRIGGER: Beat fires on every Nth beat from its offset, newest first", "[trigger]") {
    const world::TriggerClock clock = beatClock();
    // Every beat: at 2.2 s the last three are 2.0, 1.5, 1.0.
    CHECK(ask(clock, beat(1), 2.2, 3) == std::vector<double>{2.0, 1.5, 1.0});
    // A beat exactly at t counts (t0 <= t).
    CHECK(ask(clock, beat(1), 2.0, 1) == std::vector<double>{2.0});
    // Every 4th from beat 0: beats 0, 4, 8 are at 0.5, 2.5, 4.5.
    CHECK(ask(clock, beat(4), 4.9, 4) == std::vector<double>{4.5, 2.5, 0.5});
    // Every 4th from beat 1: 1.0, 3.0, 5.0.
    CHECK(ask(clock, beat(4, 1), 5.2, 4) == std::vector<double>{5.0, 3.0, 1.0});
    // Before the offset beat: nothing.
    CHECK(ask(clock, beat(4, 3), 1.9).empty());
    CHECK(ask(clock, beat(1), 0.4).empty());
}

TEST_CASE("TRIGGER: Onset fires on onsets at or above the threshold only", "[trigger]") {
    world::TriggerClock clock;
    const std::vector<world::TriggerOnset> onsets{{0.3, 1.1f}, {0.9, 2.5f}, {1.4, 0.8f}, {2.0, 1.7f}, {2.6, 3.0f}};
    clock.setOnsets(onsets);
    world::Trigger t;
    t.source = world::TriggerSource::Onset;
    t.threshold = 1.5f;
    CHECK(ask(clock, t, 2.3) == std::vector<double>{2.0, 0.9});
    t.threshold = 0.0f;
    CHECK(ask(clock, t, 1.5, 2) == std::vector<double>{1.4, 0.9});
    t.threshold = 5.0f;
    CHECK(ask(clock, t, 10.0).empty());
    CHECK(clock.silence(t, {}) != nullptr); // "no onset reaches this threshold"
}

TEST_CASE("TRIGGER: MusicEvent fires on its named event only", "[trigger]") {
    world::TriggerClock clock;
    const auto drop = static_cast<std::uint8_t>(signals::MusicalEvent::Drop);
    const auto impact = static_cast<std::uint8_t>(signals::MusicalEvent::Impact);
    const std::vector<world::TriggerMoment> moments{{4.0, impact}, {8.0, drop}, {9.5, impact}, {16.0, drop}};
    clock.setMusicEvents(moments);
    world::Trigger t;
    t.source = world::TriggerSource::MusicEvent;
    t.name = "drop";
    CHECK(ask(clock, t, 20.0) == std::vector<double>{16.0, 8.0});
    CHECK(ask(clock, t, 15.9) == std::vector<double>{8.0});
    t.name = "impact";
    CHECK(ask(clock, t, 10.0) == std::vector<double>{9.5, 4.0});
    t.name = "build";
    CHECK(ask(clock, t, 20.0).empty());
    CHECK(t.validate().has_value());
    t.name = "dropp";
    CHECK_FALSE(t.validate().has_value()); // refused by name, not silently never firing
}

TEST_CASE("TRIGGER: TimelineMarker fires on the markers with its name", "[trigger]") {
    world::TriggerClock clock;
    // Out of order on purpose: the clock sorts.
    const std::vector<world::TriggerMarker> markers{{12.0, "boom"}, {3.0, "boom"}, {7.0, "chorus"}, {9.0, "boom"}};
    clock.setMarkers(markers);
    world::Trigger t;
    t.source = world::TriggerSource::TimelineMarker;
    t.name = "boom";
    CHECK(ask(clock, t, 10.0) == std::vector<double>{9.0, 3.0});
    CHECK(ask(clock, t, 12.0) == std::vector<double>{12.0, 9.0, 3.0});
    t.name = "nope";
    CHECK(ask(clock, t, 20.0).empty());
    CHECK(clock.silence(t, {}) != nullptr);
}

TEST_CASE("TRIGGER: Repeat fires on its schedule", "[trigger]") {
    const world::TriggerClock clock;
    world::Trigger t;
    t.source = world::TriggerSource::Repeat;
    t.period = 1.5;
    t.phase = 2.0;
    CHECK(ask(clock, t, 1.9).empty());
    CHECK(ask(clock, t, 2.0) == std::vector<double>{2.0});
    CHECK(ask(clock, t, 6.6, 3) == std::vector<double>{6.5, 5.0, 3.5});
}

namespace {

// Two nodes on the 60 Hz step grid: the beacon stands at the origin; the craft passes it at 10 m/s
// along x from x = -30, so it enters a 5 m radius at x = -5 (t = 2.5 s), leaves at x = 5, and --
// flying back -- enters again at t = 6.5 s.
void recordPass(world::HistoryBank& bank, double until) {
    const glm::quat id(1.0f, 0.0f, 0.0f, 0.0f);
    for (long long f = 0; static_cast<double>(f) / 60.0 <= until + 1e-9; ++f) {
        const double t = static_cast<double>(f) / 60.0;
        const double x = t <= 4.5 ? -30.0 + 10.0 * t : 15.0 - 10.0 * (t - 4.5);
        bank.record(bank.find("craft"), t, glm::vec3(static_cast<float>(x), 1.0f, 0.0f), id, glm::vec3(1.0f));
        bank.record(bank.find("beacon"), t, glm::vec3(0.0f, 1.0f, 0.0f), id, glm::vec3(1.0f));
    }
}

world::Trigger near(float radius) {
    world::Trigger t;
    t.source = world::TriggerSource::Proximity;
    t.entity = "beacon";
    t.radius = radius;
    return t;
}

} // namespace

TEST_CASE("TRIGGER: Proximity fires on entering the radius, from the recorded history", "[trigger]") {
    world::HistoryBank bank;
    const std::vector<world::HistorySubscription> subs{{"craft", 16.0f}, {"beacon", 16.0f}};
    bank.subscribe(subs);
    recordPass(bank, 8.0);
    world::TriggerClock clock;
    clock.setHistory(&bank);
    // Entries: x = -5 on the way out (t = 2.5 s); on the way back x = 15 - 10 (t - 4.5) = 5 at t = 5.5.
    const auto at8 = ask(clock, near(5.0f), 8.0, 4, "craft");
    REQUIRE(at8.size() == 2);
    CHECK(at8[0] == Approx(5.5).margin(1e-5));
    CHECK(at8[1] == Approx(2.5).margin(1e-5));
    CHECK(ask(clock, near(5.0f), 2.49, 4, "craft").empty());
    CHECK(ask(clock, near(5.0f), 3.0, 4, "craft").size() == 1);
    // Measured from the named owner only.
    CHECK(ask(clock, near(5.0f), 8.0, 4, "beacon").empty());
}

TEST_CASE("TRIGGER: Proximity is exact through a checkpoint round trip of the HistoryBank",
          "[trigger][seek][adr700]") {
    const std::vector<world::HistorySubscription> subs{{"craft", 16.0f}, {"beacon", 16.0f}};
    world::HistoryBank played;
    played.subscribe(subs);
    recordPass(played, 8.0);
    // A seek restores the nearest checkpoint and replays to the target: the checkpoint at 4 s, then
    // the steps after it recorded again.
    world::HistoryBank early;
    early.subscribe(subs);
    recordPass(early, 4.0);
    const world::HistoryBank::Snapshot snap = early.snapshot();
    world::HistoryBank restored;
    restored.subscribe(subs);
    restored.restore(snap);
    const glm::quat id(1.0f, 0.0f, 0.0f, 0.0f);
    for (long long f = 4 * 60 + 1; static_cast<double>(f) / 60.0 <= 8.0 + 1e-9; ++f) {
        const double t = static_cast<double>(f) / 60.0;
        const double x = t <= 4.5 ? -30.0 + 10.0 * t : 15.0 - 10.0 * (t - 4.5);
        restored.record(restored.find("craft"), t, glm::vec3(static_cast<float>(x), 1.0f, 0.0f), id, glm::vec3(1.0f));
        restored.record(restored.find("beacon"), t, glm::vec3(0.0f, 1.0f, 0.0f), id, glm::vec3(1.0f));
    }
    world::TriggerClock a;
    a.setHistory(&played);
    world::TriggerClock b;
    b.setHistory(&restored);
    for (const double t : {2.6, 4.0, 5.6, 7.9}) {
        INFO("t = " << t);
        const auto x = ask(a, near(5.0f), t, 4, "craft");
        const auto y = ask(b, near(5.0f), t, 4, "craft");
        REQUIRE_FALSE(x.empty());
        CHECK(x == y); // bit for bit: the same samples, the same crossing arithmetic
    }
}

TEST_CASE("TRIGGER: lastTriggers is pure -- the answer at t does not depend on what was asked before", "[trigger]") {
    world::TriggerClock clock = beatClock();
    const std::vector<world::TriggerOnset> onsets{{0.3, 1.1f}, {0.9, 2.5f}, {2.0, 1.7f}};
    clock.setOnsets(onsets);
    const std::vector<world::TriggerMarker> markers{{3.0, "boom"}, {9.0, "boom"}};
    clock.setMarkers(markers);
    world::Trigger rep;
    rep.source = world::TriggerSource::Repeat;
    rep.period = 0.7;
    world::Trigger on;
    on.source = world::TriggerSource::Onset;
    world::Trigger mk;
    mk.source = world::TriggerSource::TimelineMarker;
    mk.name = "boom";
    const std::array<world::Trigger, 4> triggers{beat(3, 1), on, mk, rep};
    const std::array<double, 7> times{9.3, 0.2, 4.4, 4.4, 1.0, 7.77, 2.0};

    // The answers of a fresh clock, asked each time once.
    std::vector<std::vector<double>> fresh;
    for (const world::Trigger& t : triggers) {
        for (const double s : times) {
            world::TriggerClock c = clock;
            fresh.push_back(ask(c, t, s));
        }
    }
    // The same clock asked everything, in a scrambled order and twice, with frames bound between
    // (the edge bookkeeping is the one thing `bind` moves).
    std::size_t k = 0;
    for (int pass = 0; pass < 2; ++pass) {
        k = 0;
        for (const world::Trigger& t : triggers) {
            for (const double s : times) {
                clock.setFrame(s * 0.5 + pass);
                CHECK(ask(clock, t, s) == fresh[k++]);
            }
        }
    }
}

TEST_CASE("TRIGGER: the edge is the frame's own interval, and a jump has none", "[trigger][emit]") {
    world::TriggerClock clock = beatClock();
    world::EffectInstance e = world::makeEffect(world::EffectKind::Shockwave, "s");
    e.activation = world::Activation::Trigger;
    e.timing.trigger = beat(1);
    world::EffectContext ctx;
    ctx.triggers = &clock;
    const auto edgeAt = [&](double t) {
        clock.setFrame(t);
        ctx.seconds = t;
        return world::effectTriggerEdge(e, ctx);
    };
    CHECK_FALSE(edgeAt(0.95));        // the first frame has no interval
    CHECK(edgeAt(1.0 + 1.0 / 60.0));  // (0.95, 1.0167] holds the beat at 1.0
    CHECK_FALSE(edgeAt(1.0 + 2.0 / 60.0));
    CHECK_FALSE(edgeAt(1.0 + 2.0 / 60.0)); // the same frame asked again: still no second edge
    CHECK_FALSE(edgeAt(3.02));        // a jump over two beats: no backlog of bursts
    CHECK_FALSE(edgeAt(3.40));        // a 0.38 s step is a jump too
    CHECK(edgeAt(3.51));              // (3.40, 3.51] holds 3.5
}

TEST_CASE("TRIGGER: the activation window opens at the latest trigger and lasts its lifetime", "[trigger]") {
    world::TriggerClock clock = beatClock();
    world::Timing timing;
    timing.trigger = beat(2); // 0.5, 1.5, 2.5, ...
    timing.lifetime = 0.4;
    world::EffectContext ctx;
    ctx.triggers = &clock;
    ctx.seconds = 1.6;
    auto w = world::resolveActivationWindow(world::Activation::Trigger, timing, ctx, true, {}, {});
    REQUIRE(w.has_value());
    CHECK(w->start == 1.5);
    CHECK(w->end == Approx(1.9));
    ctx.seconds = 2.0;
    CHECK_FALSE(world::resolveActivationWindow(world::Activation::Trigger, timing, ctx, true, {}, {}).has_value());
    ctx.seconds = 0.2;
    CHECK_FALSE(world::resolveActivationWindow(world::Activation::Trigger, timing, ctx, true, {}, {}).has_value());
    // With no clock, and through the overload that cannot see events: not active.
    ctx.triggers = nullptr;
    ctx.seconds = 1.6;
    CHECK_FALSE(world::resolveActivationWindow(world::Activation::Trigger, timing, ctx, true, {}, {}).has_value());
    CHECK_FALSE(world::resolveActivationWindow(world::Activation::Trigger, timing, 1.6, {}, true, {}).has_value());
}

TEST_CASE("TRIGGER: a trigger-activated instance with no trigger yet is Dormant, with a reason", "[trigger][effects]") {
    world::TriggerClock clock; // no beats at all: no audio analysed
    world::EffectInstance e = world::makeEffect(world::EffectKind::Shockwave, "s");
    e.id = "s";
    REQUIRE(e.activation == world::Activation::Trigger);
    const std::vector<world::EffectInstance> list{e};
    world::EffectContext ctx;
    ctx.seconds = 3.0;
    ctx.triggers = &clock;
    std::vector<world::EffectStatus> status(1, world::EffectStatus::Drawn);
    std::vector<std::string> reasons(1);
    world::DistortionFrame frame;
    world::buildDistortionFrame(list, ctx, frame, {}, status, reasons);
    CHECK(frame.count == 0);
    CHECK(status[0] == world::EffectStatus::Dormant);
    CHECK(reasons[0].find("No beats") != std::string::npos);

    // Beats exist, but the first is after t: waiting for the first.
    world::TriggerClock later = beatClock();
    ctx.triggers = &later;
    ctx.seconds = 0.2;
    reasons[0].clear();
    world::buildDistortionFrame(list, ctx, frame, {}, status, reasons);
    CHECK(status[0] == world::EffectStatus::Dormant);
    CHECK(reasons[0] == "Waiting for its first trigger.");

    // ...and on the fourth beat it draws.
    ctx.seconds = 0.6;
    world::buildDistortionFrame(list, ctx, frame, {}, status, reasons);
    CHECK(status[0] == world::EffectStatus::Drawn);
    CHECK(frame.count == 1);
}

// ---- the files --------------------------------------------------------------------------------------

namespace {

world::EffectInstance triggered(const char* id, world::Trigger trig, world::EffectOwner owner = {}) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Shockwave, id);
    e.id = id;
    e.owner = std::move(owner);
    e.activation = world::Activation::Trigger;
    e.timing.trigger = std::move(trig);
    e.timing.lifetime = 1.25;
    return e;
}

std::vector<world::EffectInstance> everySource() {
    world::Trigger onset;
    onset.source = world::TriggerSource::Onset;
    onset.threshold = 1.75f;
    world::Trigger music;
    music.source = world::TriggerSource::MusicEvent;
    music.name = "impact";
    world::Trigger marker;
    marker.source = world::TriggerSource::TimelineMarker;
    marker.name = "boom";
    world::Trigger repeat;
    repeat.source = world::TriggerSource::Repeat;
    repeat.period = 0.75;
    repeat.phase = 0.25;
    world::Trigger prox = near(7.5f);
    return {triggered("b", beat(3, 2)), triggered("o", onset), triggered("m", music), triggered("k", marker),
            triggered("r", repeat), triggered("p", prox, world::EffectOwner::entity("craft"))};
}

} // namespace

TEST_CASE("TRIGGER: every source survives the instance's file, and an unknown source is refused by name",
          "[trigger][serialization]") {
    for (const world::EffectInstance& e : everySource()) {
        INFO(e.id);
        REQUIRE(e.validate().has_value());
        const nlohmann::json j = e.toJson();
        CHECK(j.at("activation") == "trigger");
        REQUIRE(j.contains("trigger"));
        auto back = world::EffectInstance::fromJson(j);
        REQUIRE(back.has_value());
        CHECK(back->activation == world::Activation::Trigger);
        CHECK(back->timing.trigger == e.timing.trigger);
        CHECK(back->toJson() == j);
    }
    const nlohmann::json good = everySource().front().toJson();

    nlohmann::json bad = good;
    bad["trigger"]["source"] = "beats"; // an alias nobody wrote down
    auto refused = world::EffectInstance::fromJson(bad);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("unknown trigger source 'beats'") != std::string::npos);

    nlohmann::json missing = good;
    missing.erase("trigger");
    CHECK_FALSE(world::EffectInstance::fromJson(missing).has_value());

    nlohmann::json stray = good;
    stray["activation"] = "always"; // a trigger block nothing fires on
    CHECK_FALSE(world::EffectInstance::fromJson(stray).has_value());

    // A non-trigger instance writes no block.
    world::EffectInstance plain = everySource().front();
    plain.activation = world::Activation::Always;
    CHECK_FALSE(plain.toJson().contains("trigger"));

    // Proximity on a World owner has nothing to measure from.
    world::EffectInstance worldProx = everySource().back();
    worldProx.owner = world::EffectOwner::world();
    CHECK_FALSE(worldProx.validate().has_value());
}

TEST_CASE("TRIGGER: triggers survive the scene file and the project file", "[trigger][serialization]") {
    const auto composition = nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 2, 0] },
                   { "kind": "orb", "name": "beacon", "position": [8, 2, 0] } ] })");
    app::Engine a(app::EngineMode::Offline);
    REQUIRE(a.setCompositionJson(composition).has_value());
    REQUIRE(a.setEffects(everySource()).has_value());
    const auto triggersOf = [](const app::Engine& engine) {
        std::vector<std::pair<std::string, world::Trigger>> out;
        for (const world::EffectInstance& e : engine.effects()) {
            if (e.activation == world::Activation::Trigger) {
                out.emplace_back(e.id, e.timing.trigger);
            }
        }
        return out;
    };
    const auto want = triggersOf(a);
    REQUIRE(want.size() == 6);

    SECTION("the scene serialiser") {
        const nlohmann::json scene = a.composition()->toJson();
        app::Engine b(app::EngineMode::Offline);
        REQUIRE(b.setCompositionJson(scene).has_value());
        CHECK(triggersOf(b) == want);
    }
    SECTION("the project serialiser") {
        const auto path = testsupport::processTempDir() / "trigger_roundtrip.avgen.json";
        REQUIRE(a.saveProject(path).has_value());
        app::Engine b(app::EngineMode::Offline);
        REQUIRE(b.loadProject(path).has_value());
        CHECK(triggersOf(b) == want);
    }
}

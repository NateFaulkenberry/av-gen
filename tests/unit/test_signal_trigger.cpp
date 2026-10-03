// Signal triggers (ADR-1061): the Effect Library's EVENT activation fired by a bus event -- derived from the piece
// where the signal is a function of it (so a seek finds the fronts a play reaches), recorded from the bus live.

#include "analysis/analysis_track.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/signal_events.hpp"
#include "support/groove.hpp"
#include "world/effects/effect_trigger.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>

using namespace avgen;

namespace {

world::Trigger signalTrigger(std::string name, float threshold = 0.0f) {
    world::Trigger t;
    t.source = world::TriggerSource::Signal;
    t.name = std::move(name);
    t.threshold = threshold;
    return t;
}

sonic::NoteTrack notesAt(std::initializer_list<std::pair<double, float>> starts) {
    sonic::NoteTrack track;
    std::uint32_t id = 0;
    for (const auto& [t, v] : starts) {
        sonic::NoteEvent e;
        e.start = t;
        e.duration = 0.2;
        e.velocity = v;
        e.pitch = 60.0f;
        e.key = 60;
        e.id = id++;
        track.notes.push_back(e);
        track.longest = std::max(track.longest, e.duration);
    }
    return track;
}

} // namespace

TEST_CASE("A signal trigger reads and writes its block, and needs a name", "[effects][adr1061]") {
    const auto t = world::triggerFromJson(nlohmann::json{{"source", "signal"}, {"name", "response.kick"}});
    REQUIRE(t);
    CHECK(t->source == world::TriggerSource::Signal);
    CHECK(t->name == "response.kick");
    CHECK(t->threshold == 0.0f); // any event fires unless the file asks for more
    const auto back = world::triggerFromJson(world::triggerToJson(*t));
    REQUIRE(back);
    CHECK(*back == *t);
    const auto strong =
        world::triggerFromJson(nlohmann::json{{"source", "signal"}, {"name", "notes.noteOn"}, {"threshold", 0.3}});
    REQUIRE(strong);
    CHECK(strong->threshold == Catch::Approx(0.3f));
    CHECK_FALSE(world::triggerFromJson(nlohmann::json{{"source", "signal"}}));
}

TEST_CASE("A derived signal answers by time alone, newest first, at its threshold", "[effects][adr1061]") {
    world::TriggerClock clock;
    const world::Trigger trig = signalTrigger("response.kick", 0.5f);
    std::array<double, 4> out{};
    // Asking registers the name; the host then resolves it.
    CHECK(clock.lastTriggers(trig, {}, 1.0, out) == 0);
    REQUIRE(clock.pendingSignals() == std::vector<std::string>{"response.kick"});
    clock.setDerivedSignal("response.kick", {{0.5, 0.9f}, {1.0, 0.2f}, {1.5, 0.7f}, {2.0, 0.6f}});
    CHECK(clock.pendingSignals().empty());
    // Pure: the same second gives the same answer whatever was asked before (a seek is a question).
    REQUIRE(clock.lastTriggers(trig, {}, 1.75, out) == 2);
    CHECK(out[0] == 1.5);
    CHECK(out[1] == 0.5); // 1.0 is below the threshold
    REQUIRE(clock.lastTriggers(trig, {}, 10.0, out) == 3);
    CHECK(out[0] == 2.0);
    REQUIRE(clock.lastTriggers(trig, {}, 1.75, out) == 2);
    CHECK(out[0] == 1.5);
    CHECK(clock.silence(trig, {}) == nullptr);
    CHECK(clock.silence(signalTrigger("response.kick", 0.95f), {}) != nullptr);
}

TEST_CASE("A recorded signal keeps what fired, and a backward jump forgets the future", "[effects][adr1061]") {
    signals::SignalBus bus;
    const signals::SignalId id = bus.declare("notes.noteOn", 0.0f, 1.0f, true);
    world::TriggerClock clock;
    const world::Trigger trig = signalTrigger("notes.noteOn");
    std::array<double, 4> out{};
    static_cast<void>(clock.lastTriggers(trig, {}, 0.0, out));
    clock.setRecordedSignal("notes.noteOn");
    CHECK(clock.silence(trig, {}) != nullptr); // nothing yet, and the panel says why
    for (int f = 0; f < 120; ++f) {
        const double t = f / 60.0;
        bus.setEvent(id, f == 30 || f == 90, f == 30 ? 0.8f : 0.4f);
        clock.recordSignals(bus, t);
        clock.recordSignals(bus, t); // the same frame asked again records nothing twice
        bus.clearEvents();
    }
    REQUIRE(clock.lastTriggers(trig, {}, 2.0, out) == 2);
    CHECK(out[0] == Catch::Approx(1.5));
    CHECK(out[1] == Catch::Approx(0.5));
    CHECK(clock.lastTriggers(signalTrigger("notes.noteOn", 0.5f), {}, 2.0, out) == 1);
    // Back to 1.0 s: the event at 1.5 s has not happened yet.
    clock.recordSignals(bus, 1.0);
    REQUIRE(clock.lastTriggers(trig, {}, 5.0, out) == 1);
    CHECK(out[0] == Catch::Approx(0.5));
}

TEST_CASE("Audio events are derived from the analysis frames with the bus's strengths", "[effects][adr1061]") {
    testsupport::GrooveSpec spec;
    spec.bars = 2;
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    sonic::SignalEventDeriver deriver;
    const auto lows = deriver.derive("audio.onsetLow", &track, nullptr);
    REQUIRE(lows);
    std::size_t expected = 0;
    for (const auto& f : track.frames()) {
        expected += f.lowOnset ? 1 : 0;
    }
    CHECK(lows->size() == expected);
    CHECK_FALSE(lows->empty());
    for (const auto& o : *lows) {
        CHECK(o.strength >= 0.05f);
    }
    CHECK_FALSE(deriver.derive("audio.rms", &track, nullptr)); // continuous, not an event
    CHECK_FALSE(deriver.derive("lfo.wobble", &track, nullptr)); // not a function of the piece: recorded
    CHECK_FALSE(sonic::SignalEventDeriver::derivable("macro.knob"));
}

TEST_CASE("Note events are derived by the Sonic runtime's own publish", "[effects][adr1061]") {
    sonic::SonicSetup setup;
    setup.notes = notesAt({{0.5, 0.9f}, {1.25, 0.4f}, {2.0, 0.7f}});
    sonic::SignalEventDeriver deriver;
    const auto ons = deriver.derive("notes.noteOn", nullptr, &setup);
    REQUIRE(ons);
    REQUIRE(ons->size() == 3);
    CHECK((*ons)[0].t == Catch::Approx(0.5).margin(0.011));
    CHECK((*ons)[1].t == Catch::Approx(1.25).margin(0.011));
    CHECK((*ons)[0].strength == Catch::Approx(0.9f));
    CHECK((*ons)[1].strength == Catch::Approx(0.4f));
    const auto offs = deriver.derive("notes.noteOff", nullptr, &setup);
    REQUIRE(offs);
    CHECK(offs->size() == 3);
    // A continuous Sonic signal is not an event.
    CHECK_FALSE(deriver.derive("notes.velocity", nullptr, &setup));
}

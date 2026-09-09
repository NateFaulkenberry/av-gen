// MIDI clock tracker (ADR-021 follow-up): tempo from jittery ticks, phase and beat count, Start /
// Continue / Stop, missing timestamps, timeouts and glitch rejection.
#include "control/midi_clock.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <random>

using namespace avgen::control;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::uint64_t kNs = 1'000'000'000ull;
constexpr std::uint64_t kInterval120 = 20'833'333; // 60 s / 120 BPM / 24 ticks

MidiMessage rt(MidiKind kind, std::uint64_t stamp) {
    MidiMessage m;
    m.kind = kind;
    m.timestampNs = stamp;
    return m;
}

} // namespace

TEST_CASE("MidiClockTracker measures 120 BPM from ticks with +-1 ms jitter", "[control][midi][clock]") {
    MidiClockTracker t;
    std::mt19937 rng(7);
    std::uniform_int_distribution<std::int64_t> jitter(-1'000'000, 1'000'000);
    const std::uint64_t base = 5 * kNs;
    CHECK(t.feed(rt(MidiKind::Start, base), base));
    CHECK(t.running());
    CHECK_FALSE(t.hasTempo());
    for (int i = 0; i < 96; ++i) {
        const auto stamp = static_cast<std::uint64_t>(static_cast<std::int64_t>(base + static_cast<std::uint64_t>(i) * kInterval120) + jitter(rng));
        t.feed(rt(MidiKind::Clock, stamp), stamp);
        t.advance(stamp);
    }
    CHECK(t.running());
    CHECK_THAT(t.bpm(), WithinAbs(120.0, 0.5));
    CHECK(t.tickCount() == 96);
    CHECK(t.beatCount() == 3); // tick 95 lies in beat 3
    CHECK(t.beatPhase() >= 23.0 / 24.0 - 1e-9);
    CHECK(t.beatPhase() < 1.0);
    // Ordinary messages are not clock messages.
    MidiMessage cc;
    cc.kind = MidiKind::ControlChange;
    CHECK_FALSE(t.feed(cc, base));
}

TEST_CASE("MidiClockTracker phase wraps every 24 ticks and extrapolates between ticks", "[control][midi][clock]") {
    MidiClockTracker t;
    std::uint64_t now = kNs;
    t.feed(rt(MidiKind::Start, now), now);
    for (int i = 0; i < 24; ++i) {
        now += kInterval120;
        t.feed(rt(MidiKind::Clock, now), now);
        t.advance(now);
        CHECK_THAT(t.beatPhase(), WithinAbs(i / 24.0, 1e-6));
        CHECK(t.beatCount() == 0);
        CHECK(t.beatEvent() == (i == 0)); // the first tick after Start is the downbeat
    }
    now += kInterval120;
    t.feed(rt(MidiKind::Clock, now), now);
    t.advance(now);
    CHECK(t.beatCount() == 1);
    CHECK(t.beatEvent());
    CHECK_THAT(t.beatPhase(), WithinAbs(0.0, 1e-6));
    t.advance(now);
    CHECK_FALSE(t.beatEvent()); // latched for one advance only
    // Halfway to the next tick the phase moves on without a message.
    t.advance(now + kInterval120 / 2);
    CHECK_THAT(t.beatPhase(), WithinAbs(0.5 / 24.0, 1e-3));
    // Never past the next tick, even when it is late.
    t.advance(now + 3 * kInterval120);
    CHECK(t.beatPhase() < 1.0 / 24.0);
}

TEST_CASE("MidiClockTracker Stop halts the beat clock, Start resets it, Continue resumes", "[control][midi][clock]") {
    MidiClockTracker t;
    std::uint64_t now = kNs;
    t.feed(rt(MidiKind::Start, now), now);
    for (int i = 0; i < 30; ++i) {
        now += kInterval120;
        t.feed(rt(MidiKind::Clock, now), now);
    }
    t.advance(now);
    CHECK(t.beatCount() == 1);
    CHECK(t.tickCount() == 30);
    t.feed(rt(MidiKind::Stop, now), now);
    CHECK_FALSE(t.running());
    // Clocks while stopped keep the tempo estimate but do not advance the position.
    for (int i = 0; i < 10; ++i) {
        now += kInterval120;
        t.feed(rt(MidiKind::Clock, now), now);
    }
    t.advance(now);
    CHECK(t.tickCount() == 30);
    CHECK_THAT(t.bpm(), WithinAbs(120.0, 0.01));
    t.feed(rt(MidiKind::Continue, now), now);
    CHECK(t.running());
    now += kInterval120;
    t.feed(rt(MidiKind::Clock, now), now);
    CHECK(t.tickCount() == 31);
    t.feed(rt(MidiKind::Start, now), now);
    CHECK(t.running());
    CHECK(t.tickCount() == 0);
    CHECK(t.beatCount() == 0);
    CHECK_THAT(t.beatPhase(), WithinAbs(0.0, 1e-9));
    t.reset();
    CHECK_FALSE(t.running());
    CHECK(t.bpm() == 0.0);
}

TEST_CASE("MidiClockTracker uses the frame time when messages carry no timestamp", "[control][midi][clock]") {
    MidiClockTracker t;
    std::uint64_t frame = 0;
    t.feed(rt(MidiKind::Start, 0), frame);
    for (int i = 0; i < 48; ++i) {
        frame += kInterval120;
        t.feed(rt(MidiKind::Clock, 0), frame); // timestampNs == 0
        t.advance(frame);
    }
    CHECK_THAT(t.bpm(), WithinAbs(120.0, 0.01));
    CHECK(t.beatCount() == 1);
    // A clock without any Start free-runs.
    MidiClockTracker free;
    std::uint64_t now = kNs;
    for (int i = 0; i < 25; ++i) {
        free.feed(rt(MidiKind::Clock, now), now);
        now += kInterval120;
    }
    CHECK(free.running());
    CHECK(free.beatCount() == 1);
}

TEST_CASE("MidiClockTracker ignores dropped ticks, follows tempo changes and times out", "[control][midi][clock]") {
    MidiClockTracker t;
    std::uint64_t now = kNs;
    t.feed(rt(MidiKind::Start, now), now);
    for (int i = 0; i < 48; ++i) {
        now += kInterval120;
        t.feed(rt(MidiKind::Clock, now), now);
    }
    // One dropped tick: a 2x interval must not move the tempo.
    now += 2 * kInterval120;
    t.feed(rt(MidiKind::Clock, now), now);
    CHECK_THAT(t.bpm(), WithinAbs(120.0, 0.01));
    // A real change to 60 BPM is picked up after a few intervals.
    const std::uint64_t interval60 = 2 * kInterval120;
    for (int i = 0; i < 48; ++i) {
        now += interval60;
        t.feed(rt(MidiKind::Clock, now), now);
    }
    CHECK_THAT(t.bpm(), WithinAbs(60.0, 0.05));
    // No ticks for longer than the timeout: the clock is considered stopped.
    t.advance(now + MidiClockTracker::kTimeoutNs + kNs);
    CHECK_FALSE(t.running());
    CHECK(t.hasTempo()); // the last tempo is kept for display
}

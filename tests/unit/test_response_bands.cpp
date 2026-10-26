// ADR-1723: response.mid and response.high, the fast band levels. Each is read against its own adaptive reference,
// so a quiet band still reads across the range, and falls to 0 within a few hundred ms of its band going silent --
// unlike audio.<band>Level (ADR-897), which decays 4.3 dB/s.

#include "analysis/response_feed.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/notes.hpp"
#include "sonic/response.hpp"
#include "sonic/timbre.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>

using namespace avgen;

namespace {

struct Run {
    sonic::ResponseModel model;
    sonic::ResponseSettings settings;
    sonic::ResponseControls controls;
    signals::SignalBus bus;
    sonic::NoteTrack notes;
    Run() { model.declare(bus); }
    // One hop of `dt` with the given band powers (dBFS); the broadband level follows the loudest band.
    void hop(float midDb, float highDb, double dt = 512.0 / 48000.0) {
        analysis::ResponseFeed f;
        f.valid = true;
        f.levelDb = std::max(midDb, highDb);
        f.bassDb = -120.0f;
        f.midDb = midDb;
        f.highDb = highDb;
        model.step(sonic::TimbreFeatures{}, f, dt, controls, settings);
    }
    float value(const char* name) {
        model.publish(bus, notes, 0.0, false, 0.0f, 0.0f, -1.0f, controls, settings);
        return bus.value(*bus.find(name));
    }
};

} // namespace

TEST_CASE("response.mid and response.high are declared and band-selective", "[response][adr1723]") {
    Run r;
    REQUIRE(r.bus.find("response.mid").has_value());
    REQUIRE(r.bus.find("response.high").has_value());
    // Two seconds of a high band 25 dB under full scale, the mid band silent: high reads, mid does not.
    for (int i = 0; i < 188; ++i) {
        r.hop(-120.0f, -25.0f);
    }
    CHECK(r.value("response.high") > 0.3f);
    CHECK(r.value("response.mid") == 0.0f);
    // A quiet band reads against itself: a high band 35 dB under full scale from the start (a dull mix's hats) reads
    // as far up the range as a loud one (its own reference). Below the reference floor (-40 dB, ADR-1510: noise is
    // never normalised up) it reads lower: at -45 dB, 1 - (6.5 + 5) / 15 = 0.23. (A band that DROPS reads quiet for
    // as long as the reference takes to fall: ADR-1510's breakdown rule, the same for every level.)
    Run quiet;
    Run quieter;
    for (int i = 0; i < 188; ++i) {
        quiet.hop(-120.0f, -35.0f);
        quieter.hop(-120.0f, -45.0f);
    }
    CHECK(quiet.value("response.high") > 0.5f);
    const float floored = quieter.value("response.high");
    CHECK(floored > 0.2f);
    CHECK(floored < 0.26f);
}

TEST_CASE("response.high falls to rest within its documented release; audio.*Level is not this", "[response][adr1723]") {
    Run r;
    for (int i = 0; i < 188; ++i) {
        r.hop(-20.0f, -20.0f);
    }
    REQUIRE(r.value("response.high") > 0.3f);
    REQUIRE(r.value("response.mid") > 0.3f);
    // The bands go silent. The dB follower falls from the window's top towards the gate and reads 0 once it crosses
    // the window's bottom: at the -40 dB reference floor that is release x ln(26.5 / 11.5), 0.10 s (high) and
    // 0.125 s (mid); at this louder reference it is shorter. 0.3 s is past both with margin.
    for (int i = 0; i < 28; ++i) { // 0.30 s
        r.hop(-120.0f, -120.0f);
    }
    CHECK(r.value("response.high") == 0.0f);
    CHECK(r.value("response.mid") == 0.0f);
}

TEST_CASE("the mid and high windows and releases are project settings", "[response][adr1723]") {
    sonic::ResponseSettings s;
    REQUIRE(s.applyJson({{"windows", {{"high", {3.0, 20.0}}, {"mid", {4.0, 10.0}}}}, {"highRelease", 0.3},
                         {"midAttack", 0.02}})
                .has_value());
    CHECK(s.highWindow.topDb == 3.0f);
    CHECK(s.highWindow.spanDb == 20.0f);
    CHECK(s.midWindow.spanDb == 10.0f);
    CHECK(s.highRelease == 0.3f);
    CHECK(s.midAttack == 0.02f);
    const auto j = s.toJson();
    CHECK(j["windows"]["high"][1].get<float>() == 20.0f);
}

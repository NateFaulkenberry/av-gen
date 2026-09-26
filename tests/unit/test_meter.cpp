// Musical time, defined once (ADR-896): the meter's arithmetic, the offline clock, and the estimate
// of which tracked beat is beat 1 -- on synthetic grooves whose bar lines were placed, so the answer
// is known rather than remembered.

#include "analysis/analysis_track.hpp"
#include "analysis/meter.hpp"
#include "support/groove.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

analysis::AnalysisTrack analyzed(const testsupport::Groove& g) {
    return analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
}

} // namespace

TEST_CASE("The meter divides musical beats by floor, on both sides of bar 1", "[meter]") {
    analysis::Meter m;
    m.phraseBars = 8;
    m.sectionPhrases = 2;
    m.downbeat = 2;
    // Clock beat 2 is beat 1 of bar 1; the two before it are the pickup's beats 3 and 4 of bar 0.
    CHECK(m.beat(2) == 0);
    CHECK(m.beat(0) == -2);
    CHECK(m.beatInBar(m.beat(2)) == 0);
    CHECK(m.beatInBar(m.beat(0)) == 2);
    CHECK(m.beatInBar(m.beat(1)) == 3);
    CHECK(m.bar(m.beat(1)) == -1);
    CHECK(m.bar(m.beat(5)) == 0);
    CHECK(m.bar(m.beat(6)) == 1);
    CHECK(m.phrase(m.beat(2 + 31)) == 0);
    CHECK(m.phrase(m.beat(2 + 32)) == 1);
    CHECK(m.section(m.beat(2 + 63)) == 0);
    CHECK(m.section(m.beat(2 + 64)) == 1);
    CHECK(m.isDownbeat(m.beat(10)));
    CHECK_FALSE(m.isDownbeat(m.beat(11)));
    // Continuous phases are in 0..1 and wrap on the downbeat, a pickup included.
    CHECK_THAT(m.barPhase(m.beats(2.0)), WithinAbs(0.0, 1e-12));
    CHECK_THAT(m.barPhase(m.beats(3.0)), WithinAbs(0.25, 1e-12));
    CHECK_THAT(m.barPhase(m.beats(1.0)), WithinAbs(0.75, 1e-12)); // beat 4 of the pickup bar
    CHECK_THAT(m.phrasePhase(m.beats(2.0 + 16.0)), WithinAbs(0.5, 1e-12));
    CHECK_THAT(m.sectionPhase(m.beats(2.0 + 32.0)), WithinAbs(0.5, 1e-12));

    // Bar lines on a grid: bar 1 is the downbeat's beat and every fourth after it.
    const std::vector<double> beats{0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0};
    CHECK(m.barTimes(beats) == std::vector<double>{1.0, 3.0, 5.0});
    analysis::Meter first;
    CHECK(first.barTimes(beats) == std::vector<double>{0.0, 2.0, 4.0});
    analysis::Meter before; // bar 1 began a beat before the grid: the first bar line held is bar 2's
    before.downbeat = -1;
    CHECK(before.barTimes(beats) == std::vector<double>{1.5, 3.5});
}

TEST_CASE("The offline clock is a pure function of the second, inverted exactly", "[meter]") {
    const std::vector<double> beats{1.0, 1.5, 2.1, 2.6};
    CHECK_THAT(analysis::clockBeatsAt(beats, 0.5, 1.0), WithinAbs(0.0, 1e-12));
    CHECK_THAT(analysis::clockBeatsAt(beats, 0.5, 1.8), WithinAbs(1.5, 1e-12)); // halfway through a 0.6 s beat
    CHECK_THAT(analysis::clockBeatsAt(beats, 0.5, 0.75), WithinAbs(-0.5, 1e-12)); // before: the period
    CHECK_THAT(analysis::clockBeatsAt(beats, 0.5, 3.6), WithinAbs(5.0, 1e-12));   // after: the period
    for (const double t : {0.2, 1.0, 1.23, 1.5, 2.0, 2.59, 2.6, 4.0}) {
        const double c = analysis::clockBeatsAt(beats, 0.5, t);
        CHECK_THAT(analysis::secondsAtClockBeats(beats, 0.5, c), WithinAbs(t, 1e-9));
    }
    CHECK(analysis::clockBeatsAt({}, 0.5, 3.0) == 0.0);
}

TEST_CASE("The downbeat estimate finds bar 1 behind a pickup of 0 to 3 beats", "[meter][estimate]") {
    for (const int pickup : {0, 1, 2, 3}) {
        testsupport::GrooveSpec spec;
        spec.pickupBeats = pickup;
        spec.bars = 24;
        const testsupport::Groove g = testsupport::makeGroove(spec);
        const analysis::AnalysisTrack track = analyzed(g);
        const auto& beats = track.beats().beatTimes;
        INFO("pickup " << pickup << ", " << beats.size() << " tracked beats, first at "
                       << (beats.empty() ? -1.0 : beats.front()));
        REQUIRE(beats.size() > 90);
        // The tracker starts on the first kick, so the placed pickup is the answer.
        REQUIRE_THAT(beats.front(), WithinAbs(g.truth.beats.front(), 0.03));
        const analysis::MeterEstimate& est = track.meterEstimate();
        REQUIRE(est.valid);
        CHECK(est.downbeat == pickup);
        CHECK(est.downbeatConfidence > 0.5f);

        // Every placed bar line has a predicted one within 30 ms, and within the music there are no
        // extra ones. (The tracker may extrapolate a beat or two into the ring-out after the last
        // kick; a bar line there is past the piece, not off its grid.)
        analysis::Meter meter;
        meter.downbeat = est.downbeat;
        std::vector<double> bars;
        for (const double t : meter.barTimes(beats)) {
            if (t <= g.truth.downbeats.back() + 0.25) {
                bars.push_back(t);
            }
        }
        CHECK(testsupport::worstMiss(g.truth.downbeats, bars) <= 0.030);
        CHECK(testsupport::worstMiss(bars, g.truth.downbeats) <= 0.030);

        // The control: the convention this replaced -- the first tracked beat counted as beat 1 of
        // the count and every fourth count a downbeat, i.e. bar lines from tracked beat 3 -- misses
        // the placed bar lines by at least a beat whenever the pickup is not 3.
        if (pickup != 3) {
            analysis::Meter old;
            old.downbeat = 3;
            CHECK(testsupport::worstMiss(g.truth.downbeats, old.barTimes(beats)) > 0.4);
        }
    }
}

TEST_CASE("The downbeat estimate holds with the backbeat alone and with the bar changes alone",
          "[meter][estimate]") {
    SECTION("claps on 2 and 4, no bass movement, no layers") {
        testsupport::GrooveSpec spec;
        spec.pickupBeats = 1;
        spec.barBass = false;
        spec.layerBars = 0;
        const testsupport::Groove g = testsupport::makeGroove(spec);
        const analysis::AnalysisTrack track = analyzed(g);
        // The backbeat alone decides the phase to within two beats; the prior and whatever else
        // changes decide the rest. What must hold is that beats 2 and 4 are never called beat 1.
        const int d = track.meterEstimate().downbeat;
        CHECK((d == 1 || d == 3));
    }
    SECTION("no claps: the bass changes note on the downbeat") {
        testsupport::GrooveSpec spec;
        spec.pickupBeats = 2;
        spec.backbeat = false;
        const testsupport::Groove g = testsupport::makeGroove(spec);
        const analysis::AnalysisTrack track = analyzed(g);
        CHECK(track.meterEstimate().downbeat == 2);
    }
}

TEST_CASE("The phrase estimate is the length the layers change on, or nothing", "[meter][estimate]") {
    for (const int layer : {4, 8}) {
        testsupport::GrooveSpec spec;
        spec.bars = 48;
        spec.layerBars = layer;
        const testsupport::Groove g = testsupport::makeGroove(spec);
        const analysis::AnalysisTrack track = analyzed(g);
        INFO("layer every " << layer << " bars; evidence " << track.meterEstimate().phraseEvidence);
        CHECK(track.meterEstimate().phraseBars == layer);
    }
    // No layer and a bass that repeats every bar: nothing to hang a phrase on, so no claim.
    testsupport::GrooveSpec flat;
    flat.bars = 32;
    flat.layerBars = 0;
    flat.barBass = false;
    const testsupport::Groove g = testsupport::makeGroove(flat);
    CHECK(analyzed(g).meterEstimate().phraseBars == 0);
}

TEST_CASE("The tracked grid is refined below the analysis hop", "[meter][grid]") {
    // A clean click grid at 128 BPM: a period of 21.97 hops, so the dynamic-programming tracker's
    // beats sit on whole hops and alternate around the truth by up to half a hop (5.3 ms). The
    // refinement fits the grid through them.
    testsupport::GrooveSpec spec;
    spec.bpm = 128.0;
    spec.bars = 24;
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analyzed(g);
    const auto& beats = track.beats().beatTimes;
    REQUIRE(beats.size() >= g.truth.beats.size() - 2);
    double worst = 0.0;
    double sum = 0.0;
    std::size_t n = 0;
    for (const double b : beats) {
        double nearest = 1e9;
        for (const double t : g.truth.beats) {
            nearest = std::min(nearest, std::fabs(b - t));
        }
        worst = std::max(worst, nearest);
        sum += nearest;
        ++n;
    }
    INFO("mean |error| " << 1000.0 * sum / static_cast<double>(n) << " ms, worst " << 1000.0 * worst << " ms");
    CHECK(worst < 0.015);
}

// Causal onsets (ADR-1060): kick, snare, hat, a bass attack and a broadband onset, with no look-ahead, the same
// from a file as live -- scored against a groove whose hits were placed, with the kick's hardest rival in it.

#include "analysis/analysis_runner.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/causal_onsets.hpp"
#include "core/rng.hpp"
#include "support/drum_kit.hpp"
#include "support/groove.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using analysis::HitClass;

namespace {

struct Score {
    std::size_t hits = 0, detected = 0, reference = 0;
    double meanLatency = 0.0;
    [[nodiscard]] double precision() const { return detected ? double(hits) / double(detected) : 0.0; }
    [[nodiscard]] double recall() const { return reference ? double(hits) / double(reference) : 0.0; }
};

Score match(const std::vector<double>& detected, const std::vector<double>& reference, double tolerance = 0.035) {
    Score s;
    s.detected = detected.size();
    s.reference = reference.size();
    std::vector<bool> used(reference.size(), false);
    double latency = 0.0;
    for (const double d : detected) {
        std::size_t best = reference.size();
        double bestError = tolerance;
        for (std::size_t i = 0; i < reference.size(); ++i) {
            const double e = std::fabs(reference[i] - d);
            if (!used[i] && e <= bestError) {
                best = i;
                bestError = e;
            }
        }
        if (best < reference.size()) {
            used[best] = true;
            ++s.hits;
            latency += d - reference[best];
        }
    }
    s.meanLatency = s.hits ? latency / double(s.hits) : 0.0;
    return s;
}

std::vector<double> hitsOf(const analysis::AnalysisTrack& track, HitClass c) {
    std::vector<double> out;
    for (const analysis::AnalysisFrame& f : track.frames()) {
        if (f.causal.valid && f.causal.hit[static_cast<std::size_t>(c)]) {
            out.push_back(f.timeSeconds);
        }
    }
    return out;
}

struct Parts {
    std::vector<double> claps, hats, offbeats;
};

Parts partsOf(const testsupport::Groove& g) {
    Parts p;
    for (std::size_t i = 0; i < g.truth.beats.size(); ++i) {
        if (i % 4 == 1 || i % 4 == 3) {
            p.claps.push_back(g.truth.beats[i]);
        }
        if (i + 1 < g.truth.beats.size()) {
            p.offbeats.push_back(0.5 * (g.truth.beats[i] + g.truth.beats[i + 1]));
        }
    }
    p.hats = p.offbeats;
    return p;
}

} // namespace

TEST_CASE("Causal kick, snare and hat find a drum machine's parts without look-ahead", "[analysis][adr1060]") {
    const testsupport::Kit kit = testsupport::makeKit(16);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});

    const Score kick = match(hitsOf(track, HitClass::Kick), kit.kicks);
    const Score snare = match(hitsOf(track, HitClass::Snare), kit.snares);
    const Score hat = match(hitsOf(track, HitClass::Hat), kit.hats);
    const Score kickOnBass = match(hitsOf(track, HitClass::Kick), kit.bass);
    const Score lowOnBass = match(hitsOf(track, HitClass::Low), kit.bass);
    INFO("kick " << kick.detected << " p " << kick.precision() << " r " << kick.recall() << " lat "
                 << kick.meanLatency << "; snare " << snare.detected << " p " << snare.precision() << " r "
                 << snare.recall() << " lat " << snare.meanLatency << "; hat " << hat.detected << " p "
                 << hat.precision() << " r " << hat.recall() << " lat " << hat.meanLatency << "; kick on bass "
                 << kickOnBass.hits << "/" << kit.bass.size() << ", low on bass " << lowOnBass.recall());
    CHECK(kick.recall() >= 0.95);
    CHECK(kick.precision() >= 0.95);
    CHECK(snare.recall() >= 0.95);
    CHECK(snare.precision() >= 0.9);
    CHECK(hat.recall() >= 0.85);
    CHECK(hat.precision() >= 0.85);
    CHECK(kickOnBass.hits <= kit.bass.size() / 10);
    CHECK(lowOnBass.recall() >= 0.8);
    // Causal: a hit is stamped on its attack (a frame is centred on its window, so up to half a window early),
    // and never more than two hops after it.
    CHECK(kick.meanLatency <= 0.025);
    CHECK(snare.meanLatency <= 0.025);
    CHECK(hat.meanLatency <= 0.025);
}

TEST_CASE("A sustained pad alone fires no drum", "[analysis][adr1060]") {
    const testsupport::Kit kit = testsupport::makeKit(8, false, false, true);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    CHECK(hitsOf(track, HitClass::Kick).empty());
    CHECK(hitsOf(track, HitClass::Snare).empty());
    CHECK(hitsOf(track, HitClass::Hat).empty());
    CHECK(hitsOf(track, HitClass::Low).size() <= 1);
}

TEST_CASE("A bass note is a low attack but not a kick", "[analysis][adr1060]") {
    testsupport::GrooveSpec spec;
    spec.bars = 16;
    spec.offbeatBass = true;    // a percussive bass note on every off-beat
    spec.offbeatHats = false;
    spec.backbeat = false;
    spec.kicklessBars = {6, 7}; // only the bass plays here
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    const Parts p = partsOf(g);

    const std::vector<double> kicks = hitsOf(track, HitClass::Kick);
    const std::vector<double> lows = hitsOf(track, HitClass::Low);
    const Score kick = match(kicks, g.truth.kicks);
    const Score kickOnBass = match(kicks, p.offbeats);
    const Score lowOnBass = match(lows, p.offbeats);
    INFO("kick " << kick.detected << " p " << kick.precision() << " r " << kick.recall() << "; kicks on bass notes "
                 << kickOnBass.hits << " of " << p.offbeats.size() << "; low on bass notes " << lowOnBass.recall());
    CHECK(kick.recall() >= 0.9);
    // This groove's bass is a sine with a 4 ms attack, the clickiest a bass gets: about one in nine reads as a kick.
    CHECK(kickOnBass.hits <= p.offbeats.size() / 8);
    CHECK(lowOnBass.recall() >= 0.8);
}

TEST_CASE("Causal onsets are a function of the frame sequence", "[analysis][adr1060]") {
    testsupport::GrooveSpec spec;
    spec.bars = 4;
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack a = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    const analysis::AnalysisTrack b = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    REQUIRE(a.frames().size() == b.frames().size());
    // A detector walked over the frames again (as the live runner walks them) gives the same answer.
    std::vector<analysis::AnalysisFrame> copy = a.frames();
    analysis::detectCausalOnsets(copy, 48000.0f / 2048.0f, 512.0 / 48000.0);
    for (std::size_t i = 0; i < copy.size(); ++i) {
        for (std::size_t c = 0; c < analysis::kHitClassCount; ++c) {
            REQUIRE(a.frames()[i].causal.ratio[c] == b.frames()[i].causal.ratio[c]);
            REQUIRE(a.frames()[i].causal.ratio[c] == copy[i].causal.ratio[c]);
            REQUIRE(a.frames()[i].causal.hit[c] == copy[i].causal.hit[c]);
        }
    }
}

TEST_CASE("A hit picker fires once per attack, rearms, and respects its refractory time", "[analysis][adr1060]") {
    analysis::HitPicker p;
    p.fire = 2.0f;
    p.refractory = 0.05;
    const double dt = 0.01;
    CHECK(p.step(0.5f, dt) == 0.0f);
    CHECK(p.step(2.5f, dt) > 0.0f);  // fires
    CHECK(p.step(3.0f, dt) == 0.0f); // still over: no second hit
    CHECK(p.step(0.5f, dt) == 0.0f); // rearms below 0.75 x fire
    CHECK(p.step(2.5f, dt) == 0.0f); // too soon (refractory)
    for (int i = 0; i < 5; ++i) {
        p.step(0.5f, dt);
    }
    CHECK(p.step(2.5f, dt) > 0.0f);
}

TEST_CASE("The live latch fires a carried event once", "[analysis][adr1060]") {
    analysis::LiveEventLatch latch;
    analysis::AnalysisFrame f;
    f.liveSerial = 10;
    f.lowOnset = true;
    f.lowStamp = 9; // raised on frame 9, carried onto 10
    latch.apply(f);
    CHECK(f.lowOnset);
    analysis::AnalysisFrame g2;
    g2.liveSerial = 11;
    g2.lowOnset = true;
    g2.lowStamp = 9; // the same event, carried again before the runner saw it consumed
    latch.apply(g2);
    CHECK_FALSE(g2.lowOnset);
    analysis::AnalysisFrame h;
    h.liveSerial = 12;
    h.lowOnset = true;
    h.lowStamp = 12;
    latch.apply(h);
    CHECK(h.lowOnset);
    // A new runner starts its serials again: the latch restarts with it.
    analysis::AnalysisFrame r;
    r.liveSerial = 1;
    r.lowOnset = true;
    r.lowStamp = 1;
    latch.apply(r);
    CHECK(r.lowOnset);
}


#include <chrono>
TEST_CASE("Causal onset detector cost per frame (benchmark, hidden)", "[.][bench][adr1060]") {
    const testsupport::Kit kit = testsupport::makeKit(32);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    std::vector<analysis::AnalysisFrame> frames = track.frames();
    const auto t0 = std::chrono::steady_clock::now();
    for (int rep = 0; rep < 5; ++rep) {
        analysis::detectCausalOnsets(frames, 48000.0f / 2048.0f, 512.0 / 48000.0);
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() /
                      (5.0 * static_cast<double>(frames.size()));
    WARN("causal onsets: " << us << " us per analysis frame over " << frames.size() << " frames");
    CHECK(us < 200.0);
}

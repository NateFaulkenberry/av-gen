// Causal onsets (ADR-1060): a low-register attack and a broadband onset, with no look-ahead, the same from a file as
// live. The detector names no instrument (ADR-1784: nor does any source).

#include "analysis/analysis_runner.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/causal_onsets.hpp"
#include "core/rng.hpp"
#include "support/drum_kit.hpp"
#include "support/groove.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

// One-to-one within -30..+90 ms of the reference (the art agent's window, ADR-1067: a frame is centred on its window,
// so an attack is seen up to half a window early; the kick and the snare are decided some hops late).
Score match(const std::vector<double>& detected, const std::vector<double>& reference, double tolerance = 0.09) {
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
            if (!used[i] && e <= bestError && d >= reference[i] - 0.03) {
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
    std::vector<double> offbeats;
};

Parts partsOf(const testsupport::Groove& g) {
    Parts p;
    for (std::size_t i = 0; i + 1 < g.truth.beats.size(); ++i) {
        p.offbeats.push_back(0.5 * (g.truth.beats[i] + g.truth.beats[i + 1]));
    }
    return p;
}

} // namespace

TEST_CASE("Low attacks find a synth bass's plucks and a kick alike", "[analysis][adr1060]") {
    const testsupport::Kit kit = testsupport::makeKit(16);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    const std::vector<double> lows = hitsOf(track, HitClass::Low);
    const Score onBass = match(lows, kit.bass);
    const Score onKick = match(lows, kit.kicks);
    INFO("low on bass " << onBass.recall() << "; low on kicks " << onKick.recall());
    CHECK(onBass.recall() >= 0.8);
    CHECK(onKick.recall() >= 0.8);
}

TEST_CASE("A sustained pad alone fires almost no low attack", "[analysis][adr1060]") {
    const testsupport::Kit kit = testsupport::makeKit(8, false, false, true);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    CHECK(hitsOf(track, HitClass::Low).size() <= 1);
}

TEST_CASE("A bass note is a low attack", "[analysis][adr1060]") {
    testsupport::GrooveSpec spec;
    spec.bars = 16;
    spec.offbeatBass = true;    // a percussive bass note on every off-beat
    spec.offbeatHats = false;
    spec.backbeat = false;
    spec.kicklessBars = {6, 7}; // only the bass plays here
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    const Parts p = partsOf(g);
    const Score lowOnBass = match(hitsOf(track, HitClass::Low), p.offbeats);
    INFO("low on bass notes " << lowOnBass.recall());
    CHECK(lowOnBass.recall() >= 0.8);
}

TEST_CASE("The onset fires on every part of a drum machine and names none", "[analysis][adr1060]") {
    const testsupport::Kit kit = testsupport::makeKit(16, true, false, true);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    const std::vector<double> onsets = hitsOf(track, HitClass::Onset);
    std::vector<double> all = kit.kicks;
    all.insert(all.end(), kit.snares.begin(), kit.snares.end());
    std::sort(all.begin(), all.end());
    const Score s = match(onsets, all);
    INFO("onset on kicks and snares: r " << s.recall() << " (" << onsets.size() << " onsets)");
    CHECK(s.recall() >= 0.8);
    // Two classes, neither an instrument.
    STATIC_REQUIRE(analysis::kHitClassCount == 2);
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
    f.onset = true;
    f.onsetStamp = 9; // raised on frame 9, carried onto 10
    latch.apply(f);
    CHECK(f.onset);
    analysis::AnalysisFrame g2;
    g2.liveSerial = 11;
    g2.onset = true;
    g2.onsetStamp = 9; // the same event, carried again before the runner saw it consumed
    latch.apply(g2);
    CHECK_FALSE(g2.onset);
    analysis::AnalysisFrame h;
    h.liveSerial = 12;
    h.onset = true;
    h.onsetStamp = 12;
    latch.apply(h);
    CHECK(h.onset);
    // A new runner starts its serials again: the latch restarts with it.
    analysis::AnalysisFrame r;
    r.liveSerial = 1;
    r.onset = true;
    r.onsetStamp = 1;
    latch.apply(r);
    CHECK(r.onset);
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




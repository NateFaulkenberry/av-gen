// Band-limited onsets: kick, snare/clap, hat (ADR-898) -- on a synthetic groove whose kicks were
// placed, with the kick's hardest rival in it: a bass note on every off-beat.

#include "analysis/analysis_track.hpp"
#include "analysis/band_onsets.hpp"
#include "support/groove.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace avgen;

namespace {

struct Score {
    std::size_t hits = 0;
    std::size_t detected = 0;
    std::size_t reference = 0;
    [[nodiscard]] double precision() const { return detected > 0 ? static_cast<double>(hits) / static_cast<double>(detected) : 0.0; }
    [[nodiscard]] double recall() const { return reference > 0 ? static_cast<double>(hits) / static_cast<double>(reference) : 0.0; }
};

// One-to-one matching within `tolerance` seconds.
Score match(const std::vector<double>& detected, const std::vector<double>& reference, double tolerance = 0.03) {
    Score s;
    s.detected = detected.size();
    s.reference = reference.size();
    std::vector<bool> used(reference.size(), false);
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
        }
    }
    return s;
}

std::vector<double> timesOf(const analysis::AnalysisTrack& track, bool analysis::AnalysisFrame::*flag) {
    std::vector<double> out;
    for (const analysis::AnalysisFrame& f : track.frames()) {
        if (f.*flag) {
            out.push_back(f.timeSeconds);
        }
    }
    return out;
}

} // namespace

TEST_CASE("The low onset is the kick, not the off-beat bass", "[analysis][onsets]") {
    testsupport::GrooveSpec spec;
    spec.bars = 24;
    spec.offbeatBass = true;         // a percussive bass note on every off-beat
    spec.kicklessBars = {8, 9};      // a two-bar pull-back
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});

    const std::vector<double> lows = timesOf(track, &analysis::AnalysisFrame::lowOnset);
    const Score kicks = match(lows, g.truth.kicks);
    INFO(lows.size() << " low onsets against " << g.truth.kicks.size() << " kicks: precision "
                     << kicks.precision() << " recall " << kicks.recall());
    CHECK(kicks.precision() >= 0.95);
    CHECK(kicks.recall() >= 0.95);
    // Nothing in the pull-back, where only the bass plays.
    const double pullStart = g.truth.downbeats[8];
    const double pullEnd = g.truth.downbeats[10];
    for (const double t : lows) {
        CHECK_FALSE((t > pullStart + 0.05 && t < pullEnd - 0.05));
    }
    // Every onset carries a strength a route can use.
    for (const analysis::AnalysisFrame& f : track.frames()) {
        if (f.lowOnset) {
            CHECK(f.lowOnsetStrength > 0.0f);
            CHECK(f.lowOnsetStrength <= 1.0f);
        }
    }

    // The control: the broadband onset -- the only onset there was -- fires on the bass notes too,
    // which is why a route could not say "the kick".
    const std::vector<double> broadband = timesOf(track, &analysis::AnalysisFrame::onset);
    std::vector<double> offbeats;
    for (std::size_t i = 0; i + 1 < g.truth.beats.size(); ++i) {
        offbeats.push_back(0.5 * (g.truth.beats[i] + g.truth.beats[i + 1]));
    }
    CHECK(match(broadband, offbeats).hits > offbeats.size() / 2);
    CHECK(match(lows, offbeats).hits < offbeats.size() / 20);
}

TEST_CASE("The mid onset lands on the backbeat and the high onset on the hats", "[analysis][onsets]") {
    testsupport::GrooveSpec spec;
    spec.bars = 16;
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});

    std::vector<double> claps;
    std::vector<double> hats;
    for (std::size_t i = 0; i < g.truth.beats.size(); ++i) {
        if (i % 4 == 1 || i % 4 == 3) {
            claps.push_back(g.truth.beats[i]);
        }
        if (i + 1 < g.truth.beats.size()) {
            hats.push_back(0.5 * (g.truth.beats[i] + g.truth.beats[i + 1]));
        }
    }
    const Score mid = match(timesOf(track, &analysis::AnalysisFrame::midOnset), claps);
    const Score high = match(timesOf(track, &analysis::AnalysisFrame::highOnset), hats);
    INFO("mid: " << mid.detected << " onsets, recall of claps " << mid.recall() << "; high: " << high.detected
                 << " onsets, recall of hats " << high.recall());
    CHECK(mid.recall() >= 0.9);
    CHECK(high.recall() >= 0.9);
}

TEST_CASE("Band onsets derive the percussive onset rate and it counts every band once per hit",
          "[analysis][onsets]") {
    // Stamped by hand: two kicks 0.5 s apart and a clap on the second, so the rate sees two hits.
    analysis::AnalyzerConfig config;
    config.sampleRate = 48000;
    std::vector<analysis::AnalysisFrame> frames(400);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        frames[i].timeSeconds = static_cast<double>(i) * 512.0 / 48000.0;
        frames[i].rms = 0.3f;
    }
    analysis::BandOnsets onsets;
    onsets.low = {{10, 0.9f}, {57, 0.8f}};
    onsets.mid = {{57, 0.6f}};
    analysis::stampBandOnsets(frames, onsets, config);
    CHECK(frames[10].lowOnset);
    CHECK(frames[57].lowOnset);
    CHECK(frames[57].midOnset);
    CHECK(frames[57].midOnsetStrength == 0.6f);
    CHECK_FALSE(frames[11].lowOnset);
    // Leaky over 2 s: two hits, the second 0.5 s after the first, read about (1 + e^-0.25) / 2.
    const double expected = (std::exp(-(57.0 - 10.0) * 512.0 / 48000.0 / 2.0) + 1.0) / 2.0;
    CHECK(std::fabs(frames[57].onsetRate - expected) < 1e-3);
    CHECK(frames[57].onsetRateIsPercussive);
    CHECK(frames[300].onsetRate < frames[57].onsetRate);
}

TEST_CASE("Live playback of a file takes the track's beats and band onsets, none lost or repeated",
          "[analysis][onsets][live]") {
    testsupport::GrooveSpec spec;
    spec.bars = 8;
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    // Live frames on a grid offset from the track's by a third of a hop (a seek lands anywhere),
    // three hops apart -- what a render thread sees at about 30 fps.
    double previous = -1.0;
    double last = 0.0;
    std::size_t lows = 0;
    std::size_t beats = 0;
    for (double t = 0.0035; t <= 12.0; t += 3.0 * 512.0 / 48000.0) {
        last = t;
        analysis::AnalysisFrame live;
        live.timeSeconds = t;
        previous = analysis::overlayTrackFields(live, track, previous);
        lows += live.lowOnset ? 1u : 0u;
        beats += live.beat ? 1u : 0u;
        CHECK(live.beatCount == track.at(t).beatCount);
        CHECK(live.energy == track.at(t).energy);
    }
    std::size_t trackLows = 0;
    std::size_t trackBeats = 0;
    for (const analysis::AnalysisFrame& f : track.frames()) {
        if (f.timeSeconds <= last) {
            trackLows += f.lowOnset ? 1u : 0u;
            trackBeats += f.beat ? 1u : 0u;
        }
    }
    REQUIRE(trackLows > 10);
    CHECK(lows == trackLows);
    CHECK(beats == trackBeats);
    // A jump carries no events: only the continuous fields.
    analysis::AnalysisFrame jumped;
    jumped.timeSeconds = 5.0;
    analysis::overlayTrackFields(jumped, track, 1.0);
    CHECK_FALSE(jumped.beat);
    CHECK_FALSE(jumped.lowOnset);
    CHECK(jumped.beatCount == track.at(5.0).beatCount);
}

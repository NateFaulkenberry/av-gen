// The drum classifier (ADR-1060, ADR-1067; ADR-1630: an isolated drums-role source only): kick, snare and hat with no
// look-ahead, scored against a drum machine whose hits were placed, with the kick's hardest rivals in it. It runs on a
// drum source, never on the mix: the mix's causal onsets have no class that names an instrument
// (test_causal_onsets.cpp).

#include "analysis/analysis_track.hpp"
#include "analysis/causal_onsets.hpp"
#include "analysis/drum_classifier.hpp"
#include "support/drum_kit.hpp"
#include "support/groove.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace avgen;
using analysis::DrumClass;

namespace {

struct Score {
    std::size_t hits = 0, detected = 0, reference = 0;
    double meanLatency = 0.0;
    [[nodiscard]] double precision() const { return detected ? double(hits) / double(detected) : 0.0; }
    [[nodiscard]] double recall() const { return reference ? double(hits) / double(reference) : 0.0; }
};

// One-to-one within -30..+90 ms of the reference (a frame is centred on its window, so an attack is seen up to half a
// window early; the kick and the snare are decided some hops late).
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

// The classifier over a whole track, as a drums source's StemAnalyzer runs it: the causal detector, then the classifier
// on the detector's flux.
std::array<std::vector<double>, analysis::kDrumClassCount> drumHits(const analysis::AnalysisTrack& track) {
    const float binHz =
        static_cast<float>(track.config().sampleRate) / static_cast<float>(track.config().windowSize);
    const double hop = static_cast<double>(track.config().hopSize) / static_cast<double>(track.config().sampleRate);
    analysis::CausalOnsetDetector detector;
    analysis::DrumClassifier classifier;
    std::array<std::vector<double>, analysis::kDrumClassCount> out;
    std::vector<analysis::AnalysisFrame> frames = track.frames();
    for (analysis::AnalysisFrame& f : frames) {
        detector.process(f, binHz, hop);
        const analysis::DrumHits h = classifier.process(f.magnitude, detector.binFlux(), detector.first(), binHz, hop);
        for (std::size_t c = 0; c < analysis::kDrumClassCount; ++c) {
            if (h.hit[c]) {
                out[c].push_back(f.timeSeconds);
            }
        }
    }
    return out;
}

const std::vector<double>& of(const std::array<std::vector<double>, analysis::kDrumClassCount>& h, DrumClass c) {
    return h[static_cast<std::size_t>(c)];
}

} // namespace

TEST_CASE("The drum classifier finds a drum machine's kick, snare and hat without look-ahead",
          "[analysis][adr1060][adr1630]") {
    // The drums and the pad (the bass line is the next case's: ADR-1067).
    const testsupport::Kit kit = testsupport::makeKit(16, true, false, true);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    const auto h = drumHits(track);
    const Score kick = match(of(h, DrumClass::Kick), kit.kicks);
    const Score snare = match(of(h, DrumClass::Snare), kit.snares);
    const Score hat = match(of(h, DrumClass::Hat), kit.hats);
    INFO("kick " << kick.detected << " p " << kick.precision() << " r " << kick.recall() << " lat "
                 << kick.meanLatency << "; snare " << snare.detected << " p " << snare.precision() << " r "
                 << snare.recall() << " lat " << snare.meanLatency << "; hat " << hat.detected << " p "
                 << hat.precision() << " r " << hat.recall() << " lat " << hat.meanLatency);
    CHECK(kick.recall() >= 0.95);
    CHECK(kick.precision() >= 0.95);
    CHECK(snare.recall() >= 0.95);
    // This kit's open hat (one per odd bar) is a second difference of white noise: lasting noise down to 1.5 kHz,
    // which is a snare's signature.
    CHECK(snare.precision() >= 0.75);
    CHECK(hat.recall() >= 0.85);
    CHECK(hat.precision() >= 0.85);
    // Causal: a hit is stamped near its attack. The kick is decided one hop after the low band's rise peaks, the snare
    // four hops after its attack.
    CHECK(kick.meanLatency <= 0.05);
    CHECK(snare.meanLatency <= 0.06);
    CHECK(hat.meanLatency <= 0.025);
}

TEST_CASE("A bright synth bass's plucks read as kicks to the drum classifier (a known limit)",
          "[analysis][adr1060][adr1067]") {
    // An alias-free saw with a filter envelope from 1.2 kHz: its attack is broadband up to 4 kHz, which is what a
    // beater's click looks like to the percussive flux. Why the classifier runs on an isolated drum source only.
    const testsupport::Kit kit = testsupport::makeKit(16);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    const auto h = drumHits(track);
    const Score kick = match(of(h, DrumClass::Kick), kit.kicks);
    const Score kickOnBass = match(of(h, DrumClass::Kick), kit.bass);
    INFO("kick r " << kick.recall() << "; kick on bass " << kickOnBass.hits << "/" << kit.bass.size());
    CHECK(kick.recall() >= 0.9);
    WARN("bright bass plucks read as kicks: " << kickOnBass.hits << " of " << kit.bass.size());
}

TEST_CASE("A sustained pad alone fires no drum", "[analysis][adr1060]") {
    const testsupport::Kit kit = testsupport::makeKit(8, false, false, true);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    const auto h = drumHits(track);
    CHECK(of(h, DrumClass::Kick).empty());
    CHECK(of(h, DrumClass::Snare).empty());
    CHECK(of(h, DrumClass::Hat).empty());
}

TEST_CASE("The drum classifier is a function of the frame sequence", "[analysis][adr1060]") {
    testsupport::GrooveSpec spec;
    spec.bars = 4;
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    CHECK(drumHits(track) == drumHits(track));
}

// Causal onsets (ADR-1060): kick, snare, hat, a bass attack and a broadband onset, with no look-ahead, the same
// from a file as live -- scored against a groove whose hits were placed, with the kick's hardest rival in it.

#include "analysis/analysis_runner.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/causal_onsets.hpp"
#include "core/rng.hpp"
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

// A drum machine with the parts a real mix has, and the bass and pad under them (the groove support's "hat" is white
// noise down to 40 Hz, which no hat is). 16th-note grid at `bpm`: kick on 1 and 3 (and the "and" of 4 in odd bars),
// snare on 2 and 4, a closed hat on every 8th, an open hat on the last 16th of every second bar; a saw bass on the
// off-beat 8ths of the kick's beats; a slow pad chord throughout.
struct Kit {
    audio::AudioFile file = audio::AudioFile::fromInterleaved(std::vector<float>{0.0f, 0.0f}, 2, 48000);
    std::vector<double> kicks, snares, hats, bass;
};

Kit makeKit(int bars, bool drums = true, bool bassLine = true, bool pad = true, double bpm = 124.0) {
    constexpr double kRate = 48000.0;
    constexpr double kTwoPi = 6.283185307179586;
    const double sixteenth = 60.0 / bpm / 4.0;
    const double lead = 0.5;
    const double end = lead + bars * 16 * sixteenth + 1.0;
    const auto n = static_cast<std::size_t>(end * kRate);
    std::vector<float> mono(n, 0.0f);
    Rng rng(20261002);
    Kit k;
    const auto at = [&](double t) { return static_cast<std::size_t>(std::llround(t * kRate)); };
    // One-pole filters for the noise.
    for (int b = 0; b < bars; ++b) {
        for (int s16 = 0; s16 < 16; ++s16) {
            const double t0 = lead + (b * 16 + s16) * sixteenth;
            const std::size_t i0 = at(t0);
            const bool kick = drums && (s16 == 0 || s16 == 8 || (b % 2 == 1 && s16 == 14));
            const bool snare = drums && (s16 == 4 || s16 == 12);
            const bool hat = drums && (s16 % 2 == 0);
            const bool open = drums && (b % 2 == 1 && s16 == 15);
            if (kick) {
                k.kicks.push_back(t0);
                for (std::size_t i = 0; i < at(0.4); ++i) {
                    const double t = static_cast<double>(i) / kRate;
                    const double phase = kTwoPi * (48.0 * t + 140.0 * (1.0 - std::exp(-t * 35.0)) / 35.0);
                    const double click = std::exp(-t * 900.0) * (rng.nextFloat() * 2.0 - 1.0) * 0.25;
                    if (i0 + i < n) mono[i0 + i] += static_cast<float>(0.8 * std::exp(-t * 9.0) * std::sin(phase) + click);
                }
            }
            if (snare) {
                k.snares.push_back(t0);
                double lp = 0.0, prev = 0.0;
                for (std::size_t i = 0; i < at(0.2); ++i) {
                    const double t = static_cast<double>(i) / kRate;
                    const double w = rng.nextFloat() * 2.0 - 1.0;
                    lp += 0.45 * (w - lp);          // low-pass ~ 5 kHz
                    const double band = lp - prev;  // and a high-pass: 1-6 kHz noise
                    prev = lp * 0.6 + prev * 0.4;
                    const double body = std::sin(kTwoPi * 190.0 * t) * std::exp(-t * 30.0);
                    if (i0 + i < n) mono[i0 + i] += static_cast<float>(0.5 * (0.9 * band * std::exp(-t * 22.0) + 0.6 * body));
                }
            }
            if (hat || open) {
                if (!open) {
                    k.hats.push_back(t0);
                }
                const double decay = open ? 9.0 : 60.0;
                double x1 = 0.0, x2 = 0.0;
                for (std::size_t i = 0; i < at(open ? 0.3 : 0.06); ++i) {
                    const double t = static_cast<double>(i) / kRate;
                    const double w = rng.nextFloat() * 2.0 - 1.0;
                    const double hp = w - 2.0 * x1 + x2; // second difference: +12 dB/oct, the hiss above 7 kHz
                    x2 = x1;
                    x1 = w;
                    if (i0 + i < n) mono[i0 + i] += static_cast<float>(0.07 * hp * std::exp(-t * decay));
                }
            }
            // The bass: a plucked saw on the off-beat 8th after each kick beat.
            if (bassLine && (s16 == 2 || s16 == 10)) {
                k.bass.push_back(t0);
                const double f = (b % 4 < 2) ? 55.0 : 49.0;
                double lp = 0.0;
                for (std::size_t i = 0; i < at(0.22); ++i) {
                    const double t = static_cast<double>(i) / kRate;
                    const double saw = 2.0 * (f * t - std::floor(f * t + 0.5));
                    const double cut = 0.02 + 0.25 * std::exp(-t * 18.0);
                    lp += cut * (saw - lp);
                    const double env = std::min(1.0, t / 0.003) * std::exp(-t * 7.0);
                    if (i0 + i < n) mono[i0 + i] += static_cast<float>(0.45 * env * lp);
                }
            }
        }
    }
    if (pad) {
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double env = std::min(1.0, t / 2.0);
            double v = 0.0;
            for (const double f : {220.0, 261.63, 329.63, 392.0}) {
                v += std::sin(kTwoPi * f * t) + 0.3 * std::sin(kTwoPi * 2.0 * f * t + 0.3);
            }
            mono[i] += static_cast<float>(0.025 * env * v);
        }
    }
    std::vector<float> interleaved(2 * n);
    for (std::size_t i = 0; i < n; ++i) {
        interleaved[2 * i] = interleaved[2 * i + 1] = std::tanh(0.8f * mono[i]);
    }
    k.file = audio::AudioFile::fromInterleaved(std::move(interleaved), 2, 48000);
    return k;
}

} // namespace

TEST_CASE("Causal kick, snare and hat find a drum machine's parts without look-ahead", "[analysis][adr1060]") {
    const Kit kit = makeKit(16);
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
    const Kit kit = makeKit(8, false, false, true);
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
    CHECK(kickOnBass.hits <= p.offbeats.size() / 10);
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


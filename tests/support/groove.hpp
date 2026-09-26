#pragma once

// A synthetic 4/4 groove whose bar lines, kicks and sections are placed, not detected (ADR-896..899).
//
// The instruments are tools/make_test_audio.py's -- a pitch-swept kick, a noise-and-body clap, hats,
// a gated bass and a pad -- written in C++ so a test builds the audio in memory. What this adds is
// the thing a downbeat test needs: a known answer. `pickupBeats` puts that many beats of a partial
// bar before bar 1, so the first beat a tracker hears is NOT the downbeat unless it is 0, and the
// downbeat is marked the way produced music marks it:
//
//   * the backbeat -- a clap on beats 2 and 4 (`backbeat`);
//   * the bar line -- the bass changes note on every downbeat and the kick on beat 1 is accented;
//   * the phrase   -- a bright lead layer enters and leaves every `layerBars` bars.
//
// Deterministic: a seeded RNG, no clocks. `truth` carries the placed times so a test compares the
// analysis against what was built rather than against what an earlier run found.

#include "audio/audio_file.hpp"
#include "core/rng.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace avgen::testsupport {

struct GrooveSpec {
    double bpm = 120.0;
    int pickupBeats = 0;         // beats of a partial bar before bar 1 (0..3)
    int bars = 32;               // full bars from bar 1
    double leadInSeconds = 0.5;  // silence before the first beat
    bool backbeat = true;        // clap on 2 and 4
    bool offbeatHats = true;     // hat on every off-beat
    bool offbeatBass = false;    // a bass hit on every off-beat: the kick detector's hard case
    bool barBass = true;         // the bass changes note on every downbeat
    int layerBars = 8;           // the lead layer toggles every this many bars (0 = never)
    std::vector<int> kicklessBars; // 0-based bars from bar 1 with no kick (a pull-back)
    float level = 0.8f;
    std::uint32_t sampleRate = 48000;
};

struct GrooveTruth {
    std::vector<double> beats;     // every beat, the pickup included
    std::vector<double> downbeats; // beat 1 of every bar from bar 1
    std::vector<double> kicks;     // every kick actually played
    double seconds = 0.0;
};

struct Groove {
    audio::AudioFile file = audio::AudioFile::fromInterleaved(std::vector<float>{0.0f, 0.0f}, 2, 48000);
    GrooveTruth truth;
};

inline Groove makeGroove(const GrooveSpec& spec) {
    const double beat = 60.0 / spec.bpm;
    const int totalBeats = spec.pickupBeats + 4 * spec.bars;
    const double end = spec.leadInSeconds + beat * totalBeats + 1.0;
    const auto frames = static_cast<std::size_t>(end * spec.sampleRate);
    const double rate = spec.sampleRate;
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);
    Groove g;
    g.truth.seconds = end;

    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    constexpr double kBassNotes[4] = {55.0, 65.41, 49.0, 73.42}; // A1 C2 G1 D2, one per bar
    Rng rng(20260926);
    const auto add = [&](std::size_t i, float mono, float pan = 0.0f) {
        if (i < frames) {
            left[i] += mono * (1.0f - pan);
            right[i] += mono * (1.0f + pan);
        }
    };
    for (int b = 0; b < totalBeats; ++b) {
        const double t0 = spec.leadInSeconds + beat * b;
        const int musical = b - spec.pickupBeats;    // 0 = bar 1 beat 1
        const int bar = musical >= 0 ? musical / 4 : -1;
        const int inBar = ((musical % 4) + 4) % 4;
        g.truth.beats.push_back(t0);
        if (musical >= 0 && inBar == 0) {
            g.truth.downbeats.push_back(t0);
        }
        const auto start = static_cast<std::size_t>(std::llround(t0 * rate));
        const bool kickless = bar >= 0 && std::find(spec.kicklessBars.begin(), spec.kicklessBars.end(), bar) !=
                                              spec.kicklessBars.end();
        // Kick: a 160 -> 45 Hz sweep with a 250 ms decay; accented on beat 1.
        if (!kickless) {
            g.truth.kicks.push_back(t0);
            const float amp = (musical >= 0 && inBar == 0) ? 1.0f : 0.75f;
            for (std::size_t i = 0; i < static_cast<std::size_t>(0.35 * rate); ++i) {
                const double tt = static_cast<double>(i) / rate;
                const double env = std::exp(-tt * 12.0);
                const double phase = kTwoPi * (45.0 * tt + 115.0 * (1.0 - std::exp(-tt * 40.0)) / 40.0);
                add(start + i, static_cast<float>(amp * 0.9 * env * std::sin(phase)));
            }
        }
        // Clap on beats 2 and 4: noise plus a 180 Hz body, 150 ms.
        if (spec.backbeat && (inBar == 1 || inBar == 3)) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(0.15 * rate); ++i) {
                const double tt = static_cast<double>(i) / rate;
                const double env = std::exp(-tt * 18.0);
                const double noise = static_cast<double>(rng.nextFloat()) * 2.0 - 1.0;
                add(start + i, static_cast<float>(0.45 * env * (0.7 * noise + 0.3 * std::sin(kTwoPi * 180.0 * tt))));
            }
        }
        const auto offStart = static_cast<std::size_t>(std::llround((t0 + 0.5 * beat) * rate));
        // Hat on the off-beat: 30 ms of bright noise.
        if (spec.offbeatHats) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(0.03 * rate); ++i) {
                const double tt = static_cast<double>(i) / rate;
                const double noise = static_cast<double>(rng.nextFloat()) * 2.0 - 1.0;
                // A first difference keeps it bright: most of its power sits above 6 kHz.
                const double bright = noise - 0.9 * (static_cast<double>(rng.nextFloat()) * 2.0 - 1.0);
                add(offStart + i, static_cast<float>(0.12 * std::exp(-tt * 90.0) * bright), 0.2f);
            }
        }
        // The bass: a gated note on the off-beat (the kick's rival) or held through the beat.
        const double note = kBassNotes[static_cast<std::size_t>(spec.barBass && bar >= 0 ? bar % 4 : 0)];
        if (spec.offbeatBass) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(0.2 * rate); ++i) {
                const double tt = static_cast<double>(i) / rate;
                const double env = std::min(1.0, tt / 0.004) * std::exp(-tt * 6.0);
                const double t = (t0 + 0.5 * beat) + tt;
                add(offStart + i, static_cast<float>(0.35 * env * (std::sin(kTwoPi * note * t) +
                                                                   0.3 * std::sin(kTwoPi * 3.0 * note * t))));
            }
        } else {
            for (std::size_t i = 0; i < static_cast<std::size_t>(beat * rate); ++i) {
                const double t = t0 + static_cast<double>(i) / rate;
                add(start + i, static_cast<float>(0.12 * (std::sin(kTwoPi * note * t) + 0.3 * std::sin(kTwoPi * 2.0 * note * t))));
            }
        }
        // The lead layer: a bright saw-ish chord in the phrases where it plays.
        const bool lead = spec.layerBars > 0 && bar >= 0 && (bar / spec.layerBars) % 2 == 1;
        if (lead) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(beat * rate); ++i) {
                const double t = t0 + static_cast<double>(i) / rate;
                double v = 0.0;
                for (const double f : {880.0, 1108.7, 1318.5}) {
                    for (int h = 1; h <= 6; ++h) {
                        v += std::sin(kTwoPi * f * h * t) / h;
                    }
                }
                add(start + i, static_cast<float>(0.035 * v), -0.3f);
            }
        }
    }
    std::vector<float> interleaved(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        interleaved[2 * i] = std::tanh(spec.level * left[i]);
        interleaved[2 * i + 1] = std::tanh(spec.level * right[i]);
    }
    g.file = audio::AudioFile::fromInterleaved(std::move(interleaved), 2, spec.sampleRate);
    return g;
}

// The largest distance from each `truth` time to the nearest `measured` one, in seconds (infinity
// when `measured` is empty) -- "every true bar line has a predicted one within X".
inline double worstMiss(const std::vector<double>& truth, const std::vector<double>& measured) {
    double worst = 0.0;
    for (const double t : truth) {
        double best = 1e30;
        for (const double m : measured) {
            best = std::min(best, std::fabs(m - t));
        }
        worst = std::max(worst, best);
    }
    return worst;
}

} // namespace avgen::testsupport

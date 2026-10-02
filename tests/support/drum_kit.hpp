#pragma once

// A drum machine for the onset and response tests (ADR-1060/1062): a clicked, swept kick, a snare with a body and
// band-limited noise, high-passed hats, a plucked saw bass and a slow pad -- the parts a real mix has, with their times.

#include "audio/audio_file.hpp"
#include "core/rng.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace avgen::testsupport {

// A drum machine with the parts a real mix has, and the bass and pad under them (the groove support's "hat" is white
// noise down to 40 Hz, which no hat is). 16th-note grid at `bpm`: kick on 1 and 3 (and the "and" of 4 in odd bars),
// snare on 2 and 4, a closed hat on every 8th, an open hat on the last 16th of every second bar; a saw bass on the
// off-beat 8ths of the kick's beats; a slow pad chord throughout.
struct Kit {
    audio::AudioFile file = audio::AudioFile::fromInterleaved(std::vector<float>{0.0f, 0.0f}, 2, 48000);
    std::vector<double> kicks, snares, hats, bass;
};

inline Kit makeKit(int bars, bool drums = true, bool bassLine = true, bool pad = true, double bpm = 124.0, float level = 0.8f) {
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
        interleaved[2 * i] = interleaved[2 * i + 1] = std::tanh(level * mono[i]);
    }
    k.file = audio::AudioFile::fromInterleaved(std::move(interleaved), 2, 48000);
    return k;
}


} // namespace avgen::testsupport

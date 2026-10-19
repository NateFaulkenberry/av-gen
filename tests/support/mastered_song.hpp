#pragma once

// A loud-mastered song for the response-range tests (ADR-1510): the drum kit's parts arranged as commercial dance
// music is -- a verse, a breakdown of bass and pad with no drums, and a louder drop with a doubled bass -- then
// mastered as it is: gained to about -8 dBFS RMS and brickwall-limited under full scale. The fixed-window levels of
// ADR-1062 read every second of a signal like this as 1; the tests in test_response_range.cpp pin that they do not.

#include "support/drum_kit.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace avgen::testsupport {

struct MasteredSong {
    audio::AudioFile file = audio::AudioFile::fromInterleaved(std::vector<float>{0.0f, 0.0f}, 2, 48000);
    double verseStart = 0.0, breakdownStart = 0.0, dropStart = 0.0, end = 0.0; // section bounds, seconds
    double rmsDb = 0.0;                                                          // after mastering, before `gainDb`
};

// `gainDb` is applied after the master (a quieter interface channel playing the same record).
inline MasteredSong makeMasteredSong(float gainDb = 0.0f, int verseBars = 12, int breakdownBars = 8, int dropBars = 12) {
    constexpr double kRate = 48000.0;
    constexpr double kBpm = 124.0;
    const double bar = 4.0 * 60.0 / kBpm;
    // makeKit leads every part with 0.5 s and tails it with 1 s; each section is laid at its start minus the lead,
    // so a tail rings into the next section as it would.
    struct Section {
        int bars;
        bool drums, bass, pad;
        float mix;
    };
    const Section sections[] = {{verseBars, true, true, true, 0.75f},
                                {breakdownBars, false, true, true, 0.35f},
                                {dropBars, true, true, true, 1.0f}};
    MasteredSong song;
    song.verseStart = 0.5;
    song.breakdownStart = song.verseStart + verseBars * bar;
    song.dropStart = song.breakdownStart + breakdownBars * bar;
    song.end = song.dropStart + dropBars * bar;
    const auto n = static_cast<std::size_t>((song.end + 1.5) * kRate);
    std::vector<double> mix(n, 0.0);
    double start = song.verseStart;
    int index = 0;
    for (const Section& s : sections) {
        const Kit part = makeKit(s.bars, s.drums, s.bass, s.pad, kBpm, 1.0f);
        const auto mono = part.file.mono();
        const auto at = static_cast<std::size_t>(std::llround((start - 0.5) * kRate));
        for (std::size_t i = 0; i < mono.size() && at + i < n; ++i) {
            mix[at + i] += s.mix * static_cast<double>(mono[i]);
        }
        if (index == 2) {
            // The drop's doubled bass: the line again under the full mix, as a drop's bass is pushed.
            const Kit sub = makeKit(s.bars, false, true, false, kBpm, 1.0f);
            const auto m2 = sub.file.mono();
            for (std::size_t i = 0; i < m2.size() && at + i < n; ++i) {
                mix[at + i] += 0.6 * static_cast<double>(m2[i]);
            }
        }
        // A held sub under the verse and the drop, ducked by the kick (a sidechain): a master's low end is never
        // empty between two hits.
        if (s.drums) {
            for (int b = 0; b < s.bars; ++b) {
                const double f = (b % 4 < 2) ? 55.0 : 49.0;
                for (std::size_t i = 0; i < static_cast<std::size_t>(bar * kRate); ++i) {
                    const double t = static_cast<double>(i) / kRate;
                    const double beat = std::fmod(t, bar / 2.0); // the kick on 1 and 3
                    const double duck = 1.0 - 0.8 * std::exp(-beat * 8.0);
                    const std::size_t j = static_cast<std::size_t>(std::llround((start + b * bar) * kRate)) + i;
                    if (j < n) {
                        mix[j] += s.mix * 0.35 * duck * std::sin(6.283185307179586 * f * (start + b * bar + t));
                    }
                }
            }
        }
        start += s.bars * bar;
        ++index;
    }
    // The master: gain the whole to -8 dBFS RMS over its music, then a brickwall at -0.3 dBFS (a soft knee from
    // -3 dBFS, as a mastering limiter's release smooths it).
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = static_cast<std::size_t>(song.verseStart * kRate); i < static_cast<std::size_t>(song.end * kRate) && i < n; ++i) {
        sum += mix[i] * mix[i];
        ++count;
    }
    const double rms = std::sqrt(sum / static_cast<double>(std::max<std::size_t>(count, 1)));
    const double gain = std::pow(10.0, -8.0 / 20.0) / std::max(rms, 1e-9);
    const double ceiling = std::pow(10.0, -0.3 / 20.0);
    const double knee = std::pow(10.0, -3.0 / 20.0);
    const double post = std::pow(10.0, static_cast<double>(gainDb) / 20.0);
    std::vector<float> interleaved(2 * n);
    double sumAfter = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double v = mix[i] * gain;
        const double a = std::fabs(v);
        if (a > knee) {
            v = std::copysign(knee + (ceiling - knee) * std::tanh((a - knee) / (ceiling - knee)), v);
        }
        if (i >= static_cast<std::size_t>(song.verseStart * kRate) && i < static_cast<std::size_t>(song.end * kRate)) {
            sumAfter += v * v;
        }
        interleaved[2 * i] = interleaved[2 * i + 1] = static_cast<float>(v * post);
    }
    song.rmsDb = 10.0 * std::log10(std::max(sumAfter / static_cast<double>(std::max<std::size_t>(count, 1)), 1e-12));
    song.file = audio::AudioFile::fromInterleaved(std::move(interleaved), 2, 48000);
    return song;
}

} // namespace avgen::testsupport

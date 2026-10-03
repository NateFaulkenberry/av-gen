#pragma once

// The Sonic VFX test mix (ADR-1067): a C++ port of the art agent's `tools/sonic_vfx/make_test_material.py` drum loop
// and the parts that sit under it -- the material behind its finding that kicks and snares vanish under a pad or a
// bass line. Same voices, lines, levels and normalisation; noise and phases from this file's own RNG, so it is the
// same material class, not the same samples.
//
//   drums   8 bars at 120 BPM: four-on-the-floor kicks, snare (even bars) or clap (odd bars) on 2 and 4, 16th hats
//           with an open hat on each bar's 7th 16th, a tom fill in the last bar's second half
//   bass    an 8th-note driven saw-and-sub line round E1
//   pads    five-note pad chords, held a bar each pair, at the full mix's -10 dB (velocity x 0.45)
//   lead    a detuned saw lead phrase from beat 4
//
// Oscillators run by recurrence (a rotating phasor per partial), so a test can make 18 s of it in about a second.

#include "audio/audio_file.hpp"
#include "core/rng.hpp"
#include "sonic/notes.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

namespace avgen::testsupport {

struct SonicMix {
    audio::AudioFile file = audio::AudioFile::fromInterleaved(std::vector<float>{0.0f, 0.0f}, 2, 48000);
    std::vector<double> kicks, snares, hats, bassOffKick; // snares include claps; bassOffKick: bass notes on no kick
};

namespace sonicmix {

constexpr double kRate = 48000.0;
constexpr double kBeat = 0.5;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

inline double midiHz(int p) { return 440.0 * std::pow(2.0, (p - 69) / 12.0); }

inline std::vector<double> adsr(std::size_t count, double a, double d, double s, double r, std::size_t hold) {
    std::vector<double> env(count, 0.0);
    double level = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / kRate;
        if (i < hold) {
            env[i] = t < a ? t / std::max(a, 1e-6) : s + (1.0 - s) * std::exp(-(t - a) / std::max(d, 1e-6));
            level = env[i];
        } else {
            env[i] = level * std::exp(-(t - static_cast<double>(hold) / kRate) / std::max(r, 1e-6));
        }
    }
    return env;
}

// Sum of sin(2 pi f0 k t + phase_k) * amps[k-1], partials under 0.45 x the rate.
inline void additive(std::vector<double>& out, double f0, const std::vector<double>& amps, double cents, Rng& rng,
                     double gain = 1.0) {
    for (std::size_t k = 1; k <= amps.size(); ++k) {
        const double f = f0 * static_cast<double>(k) * std::pow(2.0, cents / 1200.0);
        const double phase = static_cast<double>(rng.nextFloat()) * kTwoPi;
        if (f >= kRate * 0.45 || amps[k - 1] == 0.0) {
            continue;
        }
        const std::complex<double> step = std::polar(1.0, kTwoPi * f / kRate);
        std::complex<double> z = std::polar(1.0, phase);
        const double a = amps[k - 1] * gain;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] += a * z.imag();
            z *= step;
            if ((i & 1023) == 0) {
                z /= std::abs(z); // keep the phasor on the circle
            }
        }
    }
}

// A biquad (RBJ cookbook), direct form I.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    static Biquad make(bool highpass, double hz) {
        const double w = kTwoPi * hz / kRate;
        const double c = std::cos(w), alpha = std::sin(w) / (2.0 * std::sqrt(0.5));
        Biquad q;
        const double a0 = 1.0 + alpha;
        q.b0 = (highpass ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0) / a0;
        q.b1 = (highpass ? -(1.0 + c) : (1.0 - c)) / a0;
        q.b2 = q.b0;
        q.a1 = -2.0 * c / a0;
        q.a2 = (1.0 - alpha) / a0;
        return q;
    }
    double operator()(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// White noise in [lo, hi] (24 dB/octave edges either side), normalised to peak 1: the python's FFT band, near enough.
inline std::vector<double> shapedNoise(std::size_t count, double lo, double hi, Rng& rng) {
    std::vector<double> out(count);
    Biquad h1 = Biquad::make(true, lo), h2 = Biquad::make(true, lo);
    Biquad l1 = Biquad::make(false, std::min(hi, kRate * 0.45)), l2 = Biquad::make(false, std::min(hi, kRate * 0.45));
    double peak = 1e-9;
    for (std::size_t i = 0; i < count; ++i) {
        const double w = static_cast<double>(rng.nextFloat()) * 2.0 - 1.0;
        out[i] = l2(l1(h2(h1(w))));
        peak = std::max(peak, std::abs(out[i]));
    }
    for (double& v : out) {
        v /= peak;
    }
    return out;
}

inline std::vector<double> drum(const std::string& kind, double vel, Rng& rng) {
    if (kind == "kick") {
        const auto n = static_cast<std::size_t>(0.45 * kRate);
        std::vector<double> out(n);
        double phase = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            phase += kTwoPi * (48.0 + 140.0 * std::exp(-t * 35.0)) / kRate;
            const double click = (static_cast<double>(rng.nextFloat()) * 2.0 - 1.0) * std::exp(-t * 900.0) * 0.25;
            out[i] = (0.8 * std::sin(phase) * std::exp(-t * 9.0) + click) * 1.2 * (0.4 + 0.6 * vel);
        }
        return out;
    }
    if (kind == "snare") {
        const auto n = static_cast<std::size_t>(0.35 * kRate);
        auto noise = shapedNoise(n, 1500.0, 9000.0, rng);
        std::vector<double> out(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double tone = (std::sin(kTwoPi * 185.0 * t) + 0.6 * std::sin(kTwoPi * 330.0 * t)) * std::exp(-t / 0.07);
            out[i] = (0.55 * tone + 0.9 * noise[i] * std::exp(-t / 0.13)) * std::min(1.0, t / 0.0005) * (0.35 + 0.65 * vel);
        }
        return out;
    }
    if (kind == "clap") {
        const auto n = static_cast<std::size_t>(0.4 * kRate);
        auto noise = shapedNoise(n, 900.0, 4500.0, rng);
        std::vector<double> out(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            double env = 0.0;
            for (const double o : {0.0, 0.011, 0.022}) {
                env += t >= o ? std::exp(-(t - o) / 0.008) : 0.0;
            }
            env += t >= 0.03 ? 0.6 * std::exp(-(t - 0.03) / 0.12) : 0.0;
            out[i] = noise[i] * env * 0.7 * (0.35 + 0.65 * vel);
        }
        return out;
    }
    if (kind == "hat" || kind == "openhat") {
        const double decay = kind == "hat" ? 0.045 : 0.32;
        const auto n = static_cast<std::size_t>((decay * 5.0 + 0.02) * kRate);
        auto noise = shapedNoise(n, 7000.0, 18000.0, rng);
        std::vector<double> out(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            out[i] = noise[i] * std::exp(-t / decay) * std::min(1.0, t / 0.0003) * 0.22 * (0.3 + 0.7 * vel);
        }
        return out;
    }
    // toms
    const auto n = static_cast<std::size_t>(0.6 * kRate);
    const double f0 = kind == "tomhi" ? 150.0 : 98.0;
    std::vector<double> out(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        phase += kTwoPi * f0 * (1.0 + 0.6 * std::exp(-t / 0.05)) / kRate;
        out[i] = std::sin(phase) * std::exp(-t / 0.25) * (0.4 + 0.6 * vel);
    }
    return out;
}

struct Note {
    double beat, length;
    int pitch, velocity;
    std::string voice;
};

inline std::vector<double> voice(const Note& n, std::size_t count, std::size_t hold, Rng& rng) {
    const double f0 = midiHz(n.pitch);
    const double vel = n.velocity / 127.0;
    std::vector<double> sig(count, 0.0);
    if (n.voice == "pad") {
        std::vector<double> amps(40);
        for (std::size_t k = 1; k <= 40; ++k) {
            amps[k - 1] = (1.0 / static_cast<double>(k)) * std::exp(-static_cast<double>(k) * f0 / 900.0);
        }
        for (const double c : {-7.0, 0.0, 6.0}) {
            additive(sig, f0, amps, c, rng);
        }
        const auto env = adsr(count, 0.45, 0.8, 0.8, 1.2, hold);
        for (std::size_t i = 0; i < count; ++i) {
            sig[i] *= env[i] * (0.35 + 0.65 * vel);
        }
    } else if (n.voice == "bass") {
        std::vector<double> amps(39);
        for (std::size_t k = 1; k <= 39; ++k) {
            amps[k - 1] = (1.0 / static_cast<double>(k)) * std::exp(-static_cast<double>(k) * f0 / 1400.0);
        }
        additive(sig, f0, amps, 0.0, rng);
        const auto env = adsr(count, 0.004, 0.18, 0.75, 0.08, hold);
        for (std::size_t i = 0; i < count; ++i) {
            const double t = static_cast<double>(i) / kRate;
            sig[i] = std::tanh(2.4 * (sig[i] + 0.9 * std::sin(kTwoPi * f0 * 0.5 * t)) * env[i]) * (0.4 + 0.6 * vel);
        }
    } else { // lead: two detuned saws (the vibrato left out)
        std::vector<double> amps(35);
        for (std::size_t k = 1; k <= 35; ++k) {
            amps[k - 1] = (1.0 / static_cast<double>(k)) * std::exp(-static_cast<double>(k) * f0 / 5200.0);
        }
        additive(sig, f0, amps, -5.0, rng);
        additive(sig, f0, amps, 5.0, rng);
        const auto env = adsr(count, 0.012, 0.3, 0.8, 0.18, hold);
        for (std::size_t i = 0; i < count; ++i) {
            sig[i] *= env[i] * (0.35 + 0.65 * vel) * 0.6;
        }
    }
    return sig;
}

} // namespace sonicmix

inline SonicMix makeSonicMix(bool lead, bool bass, bool pads, bool drums = true) {
    using namespace sonicmix;
    std::vector<Note> notes;
    if (pads) {
        const int chords[4][5] = {{45, 57, 60, 64, 71}, {41, 53, 57, 60, 64}, {40, 52, 55, 60, 67}, {43, 55, 59, 62, 64}};
        for (int i = 0; i < 4; ++i) {
            for (const int p : chords[i]) {
                notes.push_back({i * 8.0, 7.6, p, static_cast<int>((66 + 6 * (i % 2)) * 0.45), "pad"});
            }
        }
    }
    const int line[16] = {28, 28, 40, 28, 31, 28, 43, 31, 26, 26, 38, 26, 33, 31, 28, 26};
    std::vector<double> bassTimes;
    if (bass) {
        for (int bar = 0; bar < 6; ++bar) {
            for (int i = 0; i < 8; ++i) {
                notes.push_back({bar * 4.0 + i * 0.5, 0.42, line[(bar % 2) * 8 + i], i % 2 == 0 ? 92 : 72, "bass"});
                bassTimes.push_back((bar * 4.0 + i * 0.5) * kBeat);
            }
        }
    }
    if (lead) {
        const double phrase[20][3] = {{0, 1.5, 69}, {1.5, 0.5, 71}, {2, 1, 72}, {3, 1, 76}, {4, 2, 74}, {6, 1, 72},
                                      {7, 1, 71}, {8, 3, 69}, {12, 0.5, 76}, {12.5, 0.5, 77}, {13, 0.5, 79},
                                      {13.5, 0.5, 81}, {14, 0.25, 83}, {14.25, 0.25, 84}, {14.5, 0.25, 86},
                                      {14.75, 0.25, 88}, {15, 3, 84}, {18, 1, 81}, {19, 1, 79}, {20, 4, 76}};
        for (const auto& p : phrase) {
            if (p[0] + 4.0 < 30.0) {
                notes.push_back({p[0] + 4.0, p[1] * 1.02, static_cast<int>(p[2]), 96, "lead"});
            }
        }
    }
    struct Hit {
        double beat;
        std::string kind;
        int velocity;
    };
    std::vector<Hit> hits;
    for (int bar = 0; bar < (drums ? 8 : 0); ++bar) {
        const double base = bar * 4.0;
        for (int q = 0; q < 4; ++q) {
            hits.push_back({base + q, "kick", 115});
        }
        for (const int q : {1, 3}) {
            hits.push_back({base + q, bar % 2 == 0 ? "snare" : "clap", 105});
        }
        for (int s16 = 0; s16 < 16; ++s16) {
            if (bar == 7 && s16 >= 8) {
                continue;
            }
            hits.push_back({base + s16 * 0.25, s16 % 8 == 6 ? "openhat" : "hat",
                            s16 % 4 == 0 ? 100 : 60 + 10 * (s16 % 2)});
        }
        if (bar == 7) {
            for (const auto& [o, k] : {std::pair{2.0, "tomhi"}, {2.5, "tomhi"}, {3.0, "tomlo"}, {3.5, "tomlo"}}) {
                hits.push_back({base + o, k, 100});
            }
        }
    }
    const auto count = static_cast<std::size_t>((32.0 * kBeat + 2.0) * kRate);
    std::vector<double> mono(count, 0.0);
    Rng rng(20261002);
    const auto panGain = [](double pan) {
        // The python renders stereo with an equal-power pan; the analyzer hears the mean of the two channels.
        return 0.5 * (std::cos(pan * std::numbers::pi / 2) + std::sin(pan * std::numbers::pi / 2)) * std::sqrt(2.0);
    };
    for (const Note& n : notes) {
        const auto start = static_cast<std::size_t>(std::llround(n.beat * kBeat * kRate));
        const auto hold = static_cast<std::size_t>(std::llround(n.length * kBeat * kRate));
        const double tail = n.voice == "pad" ? 3.5 : n.voice == "bass" ? 0.4 : 0.6;
        const std::size_t len = std::min(hold + static_cast<std::size_t>(tail * kRate), count - start);
        const auto sig = voice(n, len, hold, rng);
        const double g = (n.voice == "bass" ? 0.55 : 0.5) * panGain(0.5 + 0.2 * std::sin(n.pitch * 1.3));
        for (std::size_t i = 0; i < len; ++i) {
            mono[start + i] += g * sig[i];
        }
    }
    SonicMix mix;
    for (const Hit& h : hits) {
        const double t = h.beat * kBeat;
        const auto start = static_cast<std::size_t>(std::llround(t * kRate));
        const auto sig = drum(h.kind, h.velocity / 127.0, rng);
        const double pan = h.kind == "hat" ? 0.6 : h.kind == "openhat" ? 0.62 : h.kind == "tomhi" ? 0.42
                           : h.kind == "tomlo"                          ? 0.58
                                                                        : 0.5;
        const double g = 0.8 * panGain(pan);
        for (std::size_t i = 0; i < sig.size() && start + i < count; ++i) {
            mono[start + i] += g * sig[i];
        }
        if (h.kind == "kick") {
            mix.kicks.push_back(t);
        } else if (h.kind == "snare" || h.kind == "clap") {
            mix.snares.push_back(t);
        } else if (h.kind == "hat") {
            mix.hats.push_back(t);
        }
    }
    for (const double b : bassTimes) {
        if (std::none_of(mix.kicks.begin(), mix.kicks.end(), [&](double k) { return std::abs(k - b) < 0.06; })) {
            mix.bassOffKick.push_back(b);
        }
    }
    // Normalise as the python does: active RMS to -17 dBFS, the peak to -1 dBFS at most.
    double sum = 0.0, peak = 0.0;
    std::size_t active = 0;
    for (const double v : mono) {
        if (std::abs(v) > 1e-4) {
            sum += v * v;
            ++active;
        }
        peak = std::max(peak, std::abs(v));
    }
    double gain = std::pow(10.0, -17.0 / 20.0) / std::max(std::sqrt(sum / std::max<std::size_t>(active, 1)), 1e-9);
    gain = std::min(gain, std::pow(10.0, -1.0 / 20.0) / std::max(peak, 1e-9));
    std::vector<float> interleaved(2 * count);
    for (std::size_t i = 0; i < count; ++i) {
        interleaved[2 * i] = interleaved[2 * i + 1] = static_cast<float>(mono[i] * gain);
    }
    mix.file = audio::AudioFile::fromInterleaved(std::move(interleaved), 2, 48000);
    return mix;
}

// ADR-1068: the melodic material the art agent found drums firing on -- a 16th-note pluck arpeggio (88 notes, 11 s)
// and four-note keys stabs (6 bars of five stabs) -- as audio and the MIDI that played it.
struct Melodic {
    audio::AudioFile file = audio::AudioFile::fromInterleaved(std::vector<float>{0.0f, 0.0f}, 2, 48000);
    sonic::NoteTrack notes;
    std::size_t attacks = 0; // distinct note-on instants
};

inline Melodic makeMelodic(bool arp) {
    using namespace sonicmix;
    struct N {
        double beat, length;
        int pitch, velocity;
    };
    std::vector<N> notes;
    if (arp) {
        const int up[8] = {57, 60, 64, 69, 72, 76, 81, 84};
        std::vector<int> pattern(up, up + 8);
        for (int i = 6; i >= 1; --i) {
            pattern.push_back(up[i]);
        }
        for (int i = 0; i < 88; ++i) {
            const int p = pattern[static_cast<std::size_t>(i) % pattern.size()] + ((i / 28) % 2 == 0 ? 0 : -4);
            notes.push_back({i * 0.25, 0.2, p, 70 + 30 * ((i % 4) == 0)});
        }
    } else {
        const int prog[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {55, 59, 62, 67}, {52, 55, 59, 64}};
        for (int bar = 0; bar < 6; ++bar) {
            for (const double r : {0.0, 1.0, 1.5, 2.5, 3.0}) {
                for (const int p : prog[bar % 4]) {
                    notes.push_back({bar * 4.0 + r, 0.4, p, 78 + 20 * (r == 0.0)});
                }
            }
        }
    }
    const double beats = arp ? 22.0 : 24.0;
    const auto count = static_cast<std::size_t>((beats * kBeat + 2.0) * kRate);
    std::vector<double> mono(count, 0.0);
    Rng rng(20261003);
    Melodic m;
    std::vector<double> starts;
    for (const N& n : notes) {
        const auto start = static_cast<std::size_t>(std::llround(n.beat * kBeat * kRate));
        const auto hold = static_cast<std::size_t>(std::llround(n.length * kBeat * kRate));
        const std::size_t len = std::min(hold + static_cast<std::size_t>((arp ? 1.2 : 1.0) * kRate), count - start);
        const double f0 = midiHz(n.pitch);
        const double vel = n.velocity / 127.0;
        std::vector<double> sig(len, 0.0);
        if (arp) { // voice_pluck: decaying 1/k partials, a 1 ms attack
            for (int k = 1; k < 30 && f0 * k <= kRate * 0.45; ++k) {
                const double phase = static_cast<double>(rng.nextFloat()) * kTwoPi;
                for (std::size_t i = 0; i < len; ++i) {
                    const double t = static_cast<double>(i) / kRate;
                    sig[i] += (1.0 / k) * std::exp(-t * (2.5 + 0.9 * k)) * std::sin(kTwoPi * f0 * k * t + phase);
                }
            }
            const auto env = adsr(len, 0.001, 2.0, 1.0, 0.08, hold);
            for (std::size_t i = 0; i < len; ++i) {
                sig[i] *= env[i] * (0.3 + 0.7 * vel);
            }
        } else { // voice_keys: a few harmonics, a 3 ms attack, velocity-dependent brightness
            std::vector<double> amps(23), amps2(8);
            for (std::size_t k = 1; k <= 23; ++k) {
                amps[k - 1] = std::pow(static_cast<double>(k), -1.2) * std::exp(-static_cast<double>(k) * f0 / (2200.0 + 2600.0 * vel));
            }
            for (std::size_t k = 0; k < 8; ++k) {
                amps2[k] = amps[k] * 0.4;
            }
            additive(sig, f0, amps, 0.0, rng);
            additive(sig, f0 * 2.0, amps2, 3.0, rng, 0.5);
            const auto env = adsr(len, 0.003, 0.5, 0.35, 0.25, hold);
            for (std::size_t i = 0; i < len; ++i) {
                sig[i] *= env[i] * (0.25 + 0.75 * vel);
            }
        }
        for (std::size_t i = 0; i < len; ++i) {
            mono[start + i] += 0.5 * sig[i];
        }
        sonic::NoteEvent e;
        e.start = n.beat * kBeat;
        e.duration = n.length * kBeat;
        e.pitch = static_cast<float>(n.pitch);
        e.key = static_cast<std::uint8_t>(n.pitch);
        e.velocity = static_cast<float>(vel);
        m.notes.notes.push_back(e);
        if (starts.empty() || std::abs(starts.back() - e.start) > 1e-6) {
            starts.push_back(e.start);
        }
    }
    m.notes.finish();
    m.attacks = starts.size();
    double sum = 0.0, peak = 0.0;
    std::size_t active = 0;
    for (const double v : mono) {
        if (std::abs(v) > 1e-4) {
            sum += v * v;
            ++active;
        }
        peak = std::max(peak, std::abs(v));
    }
    double gain = std::pow(10.0, -17.0 / 20.0) / std::max(std::sqrt(sum / std::max<std::size_t>(active, 1)), 1e-9);
    gain = std::min(gain, std::pow(10.0, -1.0 / 20.0) / std::max(peak, 1e-9));
    std::vector<float> interleaved(2 * count);
    for (std::size_t i = 0; i < count; ++i) {
        interleaved[2 * i] = interleaved[2 * i + 1] = static_cast<float>(mono[i] * gain);
    }
    m.file = audio::AudioFile::fromInterleaved(std::move(interleaved), 2, 48000);
    return m;
}

} // namespace avgen::testsupport

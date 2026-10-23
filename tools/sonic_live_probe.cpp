// The live Sonic probe (ADR-1025): a stand-in for the owner's keyboard and synth, for measuring and testing the live
// path without them.
//
// It creates a CoreMIDI virtual source ("AV Gen Probe") that AV Gen's MIDI input connects to like any keyboard, and
// plays a small synthesizer into an audio output -- BlackHole on this machine, whose input AV Gen then captures, the
// same loopback a person would use to route a soft synth. Every note it plays is sent as MIDI at the same host
// instant the synth voice starts, like a hardware synth driven from the same keyboard. It logs what it did on the
// host clock (mach_absolute_time in ns, the clock CoreMIDI and AV Gen's live log also use), so the two logs can be
// joined to measure latency.
//
//   avgen_sonic_probe <scenario> [--out <events.csv>] [--wav <out.wav>] [--device BlackHole] [--lead-in <s>]
//
// Scenarios:
//   controller  a performer's controller on Digital Ocean's map: pads 36-40 and CC 1/2 sweeps over a held note.
//   stress      Digital Ocean's reliability run: every pad 36-41 hammered at 4 Hz, palette/night CC sweeps, ~62 s.
//   latency   24 notes, 0.8 s apart, a sharp attack each: MIDI -> bus, audio -> bus.
//   transients  ADR-1605, sound to light: 80 one-shot transients, four families in turn (kick, snare, hat, pluck),
//             0.6031 s apart (the 3.1 ms walks the hop and frame phases). Each logs a `shot` row: the host time its
//             first sample left the probe (the stream's start time plus its sample index), its family as `key`.
//   sweep     one held note; a low-pass cutoff sweeps 120 Hz -> 9 kHz and back (the brief's §17 / PART 17).
//   drive     one held note; the drive rises from clean to hard clipping (§18 / PART 18).
//   demo      a short phrase: low soft notes, high bright notes, chords, an arpeggio, a sweep, distortion.
//   silence   nothing: the device-present, nothing-played baseline.
//   resonator the Resonator scenes' controller (Cymatic World, Resonant Silk, Waveform Dimension; perf/resonator-live):
//             a repeating 30 s pattern for --record-seconds (default 180 s). Two of CC 1-8 and 11 sweep at a time on
//             triangle waves (25 messages a second each, like an encoder), CC 9 steps the palette every 10 s, CC 10
//             toggles the lock every 30 s, and note 36 (Cymatic World's pad strike) plays bursts of four at 4 Hz.
//             Deterministic: every event is a function of its time, so two runs send the same sequence.
// The PART 15 checklist, one gesture each (the live art pass, 01-brief-live.md PARTS 15-16):
//   low       low soft notes on a slow pad             (scale, warmth, movement)
//   high      high bright notes on an open lead         (brightness, geometry, colour, spatial response)
//   chords    a progression of chords on warm keys, then denser voicings (density, polyphony, complexity)
//   arp       rapid 16th-note arpeggios on a pluck      (rhythm, pitch movement, repeated patterns)
//   distorted a bass riff, clean for a bar, then driven hard (roughness, sharpness, deformation, intensity)
//   patches   one phrase through six patches in turn: pad, pluck, lead, FM bell, distorted bass, noise perc
//             (the live form of the brief's §34: the same notes, different sounds)
//   play      a play-through of all of it, about 80 s, from silence back to silence
// The synth has patches (a saw pair, FM, or noise; an ADSR; a filter envelope; drive before or after a
// low-pass or band-pass state-variable filter), so the checklist sounds like the brief's list (PART 22): pad,
// pluck, lead, FM, distorted, filtered, resonant, noisy, sustained, percussive.

#include "audio/capture_coreaudio.hpp"
#include "control/midi.hpp"
#include "sonic/live.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using avgen::sonic::hostNowNs;

constexpr int kVoices = 8;
constexpr float kPi = 3.14159265358979f;

// ---- the synth: an oscillator (saw pair, FM or noise) -> drive -> 2-pole state-variable filter -> ADSR ----------

enum class Osc : int { Saw = 0, Fm = 1, Noise = 2 };

struct Voice {
    std::atomic<int> key{-1};
    std::atomic<float> velocity{0.0f};
    std::atomic<bool> gate{false};
    std::atomic<int> serial{0}; // bumped by every note-on, so a repeated key retriggers
    // audio thread only
    int playingSerial = -1;
    float phase1 = 0.0f, phase2 = 0.0f, env = 0.0f;
    float modEnv = 0.0f, filterEnv = 0.0f, clickEnv = 0.0f;
    bool attacking = false;
    float ic1 = 0.0f, ic2 = 0.0f;
    std::uint32_t noise = 0x9E3779B9u;
    int playingKey = -1;
};

// A patch: everything a scenario may change between notes. The defaults are the first probe's synth (a detuned
// saw pair, 3 ms attack, full sustain, 60 ms release, a plain low-pass), so the older scenarios sound as before.
struct Patch {
    std::atomic<int> osc{static_cast<int>(Osc::Saw)};
    std::atomic<float> detune{1.006f};     // the second saw's ratio
    std::atomic<float> attack{0.003f};     // seconds (one-pole time constants)
    std::atomic<float> decay{0.3f};
    std::atomic<float> sustain{1.0f};      // 0..1 of the velocity
    std::atomic<float> release{0.06f};
    std::atomic<float> filterEnvOct{0.0f}; // the filter envelope's depth, octaves above the cutoff at the attack
    std::atomic<float> filterDecay{0.2f};
    std::atomic<float> fmRatio{1.4f};      // FM: modulator / carrier
    std::atomic<float> fmIndex{0.0f};      // FM: peak index (radians), decaying with fmDecay
    std::atomic<float> fmDecay{1.0f};
    std::atomic<bool> bandPass{false};     // the filter's band-pass output instead of its low-pass
    std::atomic<float> click{0.0f};        // noise: a pitched click's level
};

struct Synth {
    std::array<Voice, kVoices> voices;
    Patch patch;
    std::atomic<float> cutoff{9000.0f};
    std::atomic<float> resonance{0.707f}; // Q: 0.707 = Butterworth, higher rings
    std::atomic<float> drive{1.0f};
    std::atomic<bool> driveAfterFilter{false}; // a drive pedal after a dark patch, rather than into the filter
    std::atomic<float> level{0.35f};
    std::atomic<std::uint64_t> framesOut{0};
    float sampleRate = 48000.0f;
    // A recording of what was played (for the review video's soundtrack): written by the audio thread into a
    // preallocated buffer, read after the device stops.
    std::vector<float> recording;
    std::atomic<std::size_t> recorded{0};
    std::atomic<std::uint64_t> firstCallbackNs{0}; // host time of recording sample 0
    // ADR-1605: one-shot transients. The player sets `shotPending` to a family; the audio thread starts it at the next
    // sample and publishes the stream sample it started at (`shotStartFrame`), so its host time is exact on the
    // stream's own clock.
    std::array<std::vector<float>, 4> shots;
    std::atomic<int> shotPending{-1};
    std::atomic<std::int64_t> shotStartFrame{-1};
    std::uint64_t streamFrame = 0; // audio thread
    int shotPlaying = -1;           // audio thread
    std::size_t shotPos = 0;        // audio thread
};

float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

float coef(float seconds, float sr) {
    return 1.0f - std::exp(-1.0f / (std::max(seconds, 1e-4f) * sr));
}

void render(Synth& s, float* out, ma_uint32 frames, ma_uint32 channels) {
    if (s.firstCallbackNs.load(std::memory_order_relaxed) == 0) {
        s.firstCallbackNs.store(hostNowNs(), std::memory_order_relaxed);
    }
    const float sr = s.sampleRate;
    const Patch& pt = s.patch;
    const auto osc = static_cast<Osc>(pt.osc.load(std::memory_order_relaxed));
    const float cutoff = std::clamp(s.cutoff.load(std::memory_order_relaxed), 20.0f, sr * 0.45f);
    const float k = 1.0f / std::max(s.resonance.load(std::memory_order_relaxed), 0.3f); // 1/Q
    const float drive = std::max(s.drive.load(std::memory_order_relaxed), 1.0f);
    const float norm = 1.0f / std::tanh(drive);
    const bool post = s.driveAfterFilter.load(std::memory_order_relaxed);
    const float level = s.level.load(std::memory_order_relaxed);
    const float detune = pt.detune.load(std::memory_order_relaxed);
    const float attack = coef(pt.attack.load(std::memory_order_relaxed), sr);
    const float decay = coef(pt.decay.load(std::memory_order_relaxed), sr);
    const float sustain = std::clamp(pt.sustain.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float release = coef(pt.release.load(std::memory_order_relaxed), sr);
    const float filterEnvOct = pt.filterEnvOct.load(std::memory_order_relaxed);
    const float filterFall = std::exp(-1.0f / (std::max(pt.filterDecay.load(std::memory_order_relaxed), 1e-3f) * sr));
    const float fmRatio = pt.fmRatio.load(std::memory_order_relaxed);
    const float fmIndex = pt.fmIndex.load(std::memory_order_relaxed);
    const float modFall = std::exp(-1.0f / (std::max(pt.fmDecay.load(std::memory_order_relaxed), 1e-3f) * sr));
    const bool bandPass = pt.bandPass.load(std::memory_order_relaxed);
    const float click = pt.click.load(std::memory_order_relaxed);
    const float clickFall = std::exp(-1.0f / (0.012f * sr));
    const float gFixed = std::tan(kPi * cutoff / sr);
    for (ma_uint32 i = 0; i < frames; ++i) {
        float mix = 0.0f;
        for (Voice& v : s.voices) {
            const bool gate = v.gate.load(std::memory_order_relaxed);
            const int key = v.key.load(std::memory_order_relaxed);
            const int serial = v.serial.load(std::memory_order_acquire);
            if (gate && serial != v.playingSerial) {
                v.playingSerial = serial;
                v.playingKey = key;
                v.env = 0.0f; // a new note: restart the envelopes (a hard, audible attack)
                v.attacking = true;
                v.modEnv = 1.0f;
                v.filterEnv = 1.0f;
                v.clickEnv = 1.0f;
                v.phase1 = v.phase2 = 0.0f;
            }
            const float vel = v.velocity.load(std::memory_order_relaxed);
            if (gate) {
                if (v.attacking) {
                    v.env += (1.25f * vel - v.env) * attack; // aims past the peak, so the attack ends in time
                    if (v.env >= vel) {
                        v.env = vel;
                        v.attacking = false;
                    }
                } else {
                    v.env += (sustain * vel - v.env) * decay;
                }
            } else {
                v.attacking = false;
                v.env += (0.0f - v.env) * release;
            }
            if (v.env < 1e-5f && !gate) {
                continue;
            }
            const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(v.playingKey) - 69.0f) / 12.0f);
            float source = 0.0f;
            if (osc == Osc::Saw) {
                const float dt1 = hz / sr;
                const float dt2 = hz * detune / sr;
                float saw = (2.0f * v.phase1 - 1.0f) - polyBlep(v.phase1, dt1);
                saw += (2.0f * v.phase2 - 1.0f) - polyBlep(v.phase2, dt2);
                source = 0.5f * saw;
                v.phase1 += dt1;
                v.phase1 -= std::floor(v.phase1);
                v.phase2 += dt2;
                v.phase2 -= std::floor(v.phase2);
            } else if (osc == Osc::Fm) {
                // One modulator on one carrier, the index decaying: a struck bell or an electric piano.
                const float mod = std::sin(2.0f * kPi * v.phase2) * fmIndex * v.modEnv;
                source = std::sin(2.0f * kPi * v.phase1 + mod);
                v.phase1 += hz / sr;
                v.phase1 -= std::floor(v.phase1);
                v.phase2 += hz * fmRatio / sr;
                v.phase2 -= std::floor(v.phase2);
                v.modEnv *= modFall;
            } else {
                // Noise with a pitched click on top: a struck, noisy percussion voice.
                v.noise ^= v.noise << 13;
                v.noise ^= v.noise >> 17;
                v.noise ^= v.noise << 5;
                const float white = static_cast<float>(v.noise) / 2147483648.0f - 1.0f;
                source = white + click * v.clickEnv * std::sin(2.0f * kPi * v.phase1);
                v.phase1 += hz / sr;
                v.phase1 -= std::floor(v.phase1);
                v.clickEnv *= clickFall;
            }
            const float driven = post ? source : std::tanh(drive * source) * norm;
            // TPT SVF (Zavalishin); the filter envelope opens the cutoff by up to filterEnvOct octaves.
            float g = gFixed;
            if (filterEnvOct > 0.0f) {
                const float fc = std::clamp(cutoff * std::exp2(filterEnvOct * v.filterEnv), 20.0f, sr * 0.45f);
                g = std::tan(kPi * fc / sr);
                v.filterEnv *= filterFall;
            }
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v3 = driven - v.ic2;
            const float v1 = a1 * v.ic1 + g * a1 * v3;
            const float v2 = v.ic2 + g * v1;
            v.ic1 = 2.0f * v1 - v.ic1;
            v.ic2 = 2.0f * v2 - v.ic2;
            const float filtered = bandPass ? k * v1 : v2;
            mix += (post ? std::tanh(drive * filtered * 1.5f) * norm : filtered) * v.env;
        }
        if (const int pending = s.shotPending.exchange(-1, std::memory_order_acq_rel); pending >= 0) {
            s.shotPlaying = pending;
            s.shotPos = 0;
            s.shotStartFrame.store(static_cast<std::int64_t>(s.streamFrame), std::memory_order_release);
        }
        float shot = 0.0f;
        if (s.shotPlaying >= 0) {
            const auto& buffer = s.shots[static_cast<std::size_t>(s.shotPlaying)];
            if (s.shotPos < buffer.size()) {
                shot = buffer[s.shotPos++];
            } else {
                s.shotPlaying = -1;
            }
        }
        ++s.streamFrame;
        const float sample = std::tanh(mix * level) + shot;
        for (ma_uint32 c = 0; c < channels; ++c) {
            out[i * channels + c] = sample;
        }
        const std::size_t at = s.recorded.load(std::memory_order_relaxed);
        if (at < s.recording.size()) {
            s.recording[at] = sample;
            s.recorded.store(at + 1, std::memory_order_relaxed);
        }
    }
    s.framesOut.fetch_add(frames, std::memory_order_relaxed);
}

void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    render(*static_cast<Synth*>(device->pUserData), static_cast<float*>(output), frames, device->playback.channels);
}

// ---- the event log -------------------------------------------------------------------------------------------

struct Log {
    std::ofstream out;
    void row(const char* kind, int key, float velocity, const Synth& s) {
        out << hostNowNs() << ',' << kind << ',' << key << ',' << velocity << ',' << s.cutoff.load() << ','
            << s.drive.load() << '\n';
    }
};

struct Player {
    Synth& synth;
    avgen::control::MidiVirtualSource& midi;
    Log& log;
    int tourScenes = 17; // `tour`: how many scenes to step through (--scenes)
    double recordSeconds = 180.0; // `resonator`: how long the pattern repeats (--record-seconds)
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    void waitUntil(double seconds) const {
        std::this_thread::sleep_until(start + std::chrono::microseconds(static_cast<std::int64_t>(seconds * 1e6)));
    }
    [[nodiscard]] double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    void on(int key, int velocity) {
        // The MIDI and the voice at the same instant: a keyboard driving a synth and AV Gen at once.
        const std::array<std::uint8_t, 3> bytes{0x90, static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)};
        for (Voice& v : synth.voices) {
            if (!v.gate.load()) {
                v.velocity.store(static_cast<float>(velocity) / 127.0f);
                v.key.store(key);
                v.serial.fetch_add(1, std::memory_order_release);
                v.gate.store(true);
                break;
            }
        }
        static_cast<void>(midi.send(bytes));
        log.row("on", key, static_cast<float>(velocity) / 127.0f, synth);
    }
    void off(int key) {
        const std::array<std::uint8_t, 3> bytes{0x80, static_cast<std::uint8_t>(key), 0};
        for (Voice& v : synth.voices) {
            if (v.gate.load() && v.key.load() == key) {
                v.gate.store(false);
            }
        }
        static_cast<void>(midi.send(bytes));
        log.row("off", key, 0.0f, synth);
    }
    // ADR-1063: a program change (program n opens live scene n mod the list's length).
    void program(int number) {
        const std::array<std::uint8_t, 2> bytes{0xC0, static_cast<std::uint8_t>(number & 0x7F)};
        static_cast<void>(midi.send(bytes));
        log.row("program", number, 0.0f, synth);
    }
    void chordOn(std::initializer_list<int> keys, int velocity) {
        for (int k : keys) {
            on(k, velocity);
        }
    }
    void chordOff(std::initializer_list<int> keys) {
        for (int k : keys) {
            off(k);
        }
    }
    // Plays a note for `len` seconds from `t` (blocking until its end).
    void note(double t, int key, int velocity, double len) {
        waitUntil(t);
        on(key, velocity);
        waitUntil(t + len);
        off(key);
    }
    // Moves a synth parameter along an exponential ramp from a to b over [t0, t1], logging it at 50 Hz.
    void ramp(std::atomic<float>& param, float a, float b, double t0, double t1, const char* name) {
        for (double t = t0; t <= t1 + 1e-9; t += 0.02) {
            waitUntil(t);
            const double u = std::clamp((t - t0) / (t1 - t0), 0.0, 1.0);
            param.store(static_cast<float>(a * std::pow(b / a, u)));
            log.row(name, -1, 0.0f, synth);
        }
    }
    // A control change (a knob or fader of a performer's controller), logged as `cc` with its number as the key.
    void cc(int number, int value) {
        const std::array<std::uint8_t, 3> bytes{0xB0, static_cast<std::uint8_t>(number & 0x7F),
                                                static_cast<std::uint8_t>(std::clamp(value, 0, 127))};
        static_cast<void>(midi.send(bytes));
        log.row("cc", number, static_cast<float>(value) / 127.0f, synth);
    }
    // A knob turned from a to b over [t0, t1] (25 messages a second, like a real encoder).
    void ccRamp(int number, int a, int b, double t0, double t1) {
        for (double t = t0; t <= t1 + 1e-9; t += 0.04) {
            waitUntil(t);
            const double u = std::clamp((t - t0) / (t1 - t0), 0.0, 1.0);
            cc(number, static_cast<int>(std::lround(a + (b - a) * u)));
        }
    }
    // A drum pad: a short note on the pad's key (no synth voice: pads trigger, they do not sound here).
    void pad(double t, int key, int velocity) {
        waitUntil(t);
        const std::array<std::uint8_t, 3> on{0x90, static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)};
        static_cast<void>(midi.send(on));
        log.row("pad", key, static_cast<float>(velocity) / 127.0f, synth);
        waitUntil(t + 0.15);
        const std::array<std::uint8_t, 3> off{0x80, static_cast<std::uint8_t>(key), 0};
        static_cast<void>(midi.send(off));
    }
};

// ---- patches: what a player would reach for on a synth (01-brief-live.md PART 22's list) ----------------------

struct PatchSpec {
    const char* name;
    Osc osc;
    float cutoff, resonance, drive;
    bool post;
    float attack, decay, sustain, release;
    float filterEnvOct, filterDecay;
    float fmRatio, fmIndex, fmDecay;
    bool bandPass;
    float click, detune, level;
};

// name        osc         cutoff   Q     drive post  attack decay sustain release fEnv  fDecay fmR  fmI  fmD   BP     click detune level
constexpr std::array<PatchSpec, 8> kPatches{{
    {"pad",       Osc::Saw,   700.f,  0.8f, 1.f,  false, 0.35f, 0.5f, 1.0f,  1.2f,  0.0f, 0.2f,  1.4f, 0.f, 1.f,  false, 0.f, 1.008f, 0.42f},
    {"keys",      Osc::Saw,   1800.f, 0.7f, 1.f,  false, 0.01f, 0.8f, 0.6f,  0.5f,  0.8f, 0.4f,  1.4f, 0.f, 1.f,  false, 0.f, 1.005f, 0.30f},
    {"pluck",     Osc::Saw,   1400.f, 1.2f, 1.f,  false, 0.002f, 0.25f, 0.0f, 0.15f, 2.2f, 0.12f, 1.4f, 0.f, 1.f,  false, 0.f, 1.004f, 0.45f},
    {"lead",      Osc::Saw,   7000.f, 1.0f, 1.f,  false, 0.004f, 0.4f, 0.8f,  0.12f, 0.0f, 0.2f,  1.4f, 0.f, 1.f,  false, 0.f, 1.003f, 0.35f},
    {"bell",      Osc::Fm,    12000.f, 0.7f, 1.f, false, 0.002f, 2.2f, 0.0f,  1.5f,  0.0f, 0.2f,  1.4f, 7.0f, 1.4f, false, 0.f, 1.0f,   0.50f},
    {"bass",      Osc::Saw,   450.f,  1.4f, 1.f,  false, 0.003f, 0.35f, 0.6f, 0.08f, 1.6f, 0.15f, 1.4f, 0.f, 1.f,  false, 0.f, 1.007f, 0.45f},
    {"distbass",  Osc::Saw,   900.f,  1.2f, 28.f, true,  0.003f, 0.35f, 0.7f, 0.08f, 2.0f, 0.18f, 1.4f, 0.f, 1.f,  false, 0.f, 1.009f, 0.30f},
    {"perc",      Osc::Noise, 2600.f, 1.6f, 1.f,  false, 0.001f, 0.09f, 0.0f, 0.05f, 1.0f, 0.05f, 1.4f, 0.f, 1.f,  true,  0.6f, 1.0f,  0.65f},
}};

void usePatch(Player& p, const char* name) {
    for (const PatchSpec& spec : kPatches) {
        if (std::strcmp(spec.name, name) != 0) {
            continue;
        }
        Synth& s = p.synth;
        Patch& pt = s.patch;
        pt.osc.store(static_cast<int>(spec.osc));
        pt.attack.store(spec.attack);
        pt.decay.store(spec.decay);
        pt.sustain.store(spec.sustain);
        pt.release.store(spec.release);
        pt.filterEnvOct.store(spec.filterEnvOct);
        pt.filterDecay.store(spec.filterDecay);
        pt.fmRatio.store(spec.fmRatio);
        pt.fmIndex.store(spec.fmIndex);
        pt.fmDecay.store(spec.fmDecay);
        pt.bandPass.store(spec.bandPass);
        pt.click.store(spec.click);
        pt.detune.store(spec.detune);
        s.cutoff.store(spec.cutoff);
        s.resonance.store(spec.resonance);
        s.drive.store(spec.drive);
        s.driveAfterFilter.store(spec.post);
        s.level.store(spec.level);
        p.log.row((std::string("patch:") + name).c_str(), -1, 0.0f, s);
        return;
    }
    std::fprintf(stderr, "unknown patch '%s'\n", name);
}

// The PART 15 gestures, each a function of a start time so the play-through can string them together. Each
// returns the time it ends.
double lowSoft(Player& p, double t) {
    usePatch(p, "pad");
    p.waitUntil(t);
    // Low, soft, long: C2, then G2 over it, then E2, then a soft open fifth.
    p.on(36, 46);
    p.waitUntil(t + 2.6);
    p.on(43, 40);
    p.waitUntil(t + 3.4);
    p.off(36);
    p.waitUntil(t + 5.6);
    p.off(43);
    p.waitUntil(t + 5.9);
    p.on(40, 50);
    p.waitUntil(t + 8.4);
    p.off(40);
    p.waitUntil(t + 8.8);
    p.chordOn({36, 43}, 44);
    p.waitUntil(t + 12.0);
    p.chordOff({36, 43});
    return t + 13.2;
}

double highBright(Player& p, double t) {
    usePatch(p, "lead");
    // High and bright, short and separate: a figure around C6-D7, then two held high notes.
    const std::array<int, 8> keys{84, 88, 91, 95, 98, 95, 91, 96};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        p.note(t + 0.6 * static_cast<double>(i), keys[i], 112, 0.32);
    }
    const double u = t + 0.6 * static_cast<double>(keys.size()) + 0.3;
    p.note(u, 93, 118, 1.6);
    p.note(u + 1.9, 100, 118, 1.8);
    return u + 4.2;
}

double chords(Player& p, double t) {
    usePatch(p, "keys");
    // A progression (Cmaj7, Am9, Fmaj7#11, G13), then denser voicings and a cluster: density and complexity grow.
    p.waitUntil(t);
    p.chordOn({48, 52, 55, 59}, 84);
    p.waitUntil(t + 1.8);
    p.chordOff({48, 52, 55, 59});
    p.chordOn({45, 52, 55, 59, 62}, 86);
    p.waitUntil(t + 3.6);
    p.chordOff({45, 52, 55, 59, 62});
    p.chordOn({41, 48, 52, 57, 59, 64}, 90);
    p.waitUntil(t + 5.4);
    p.chordOff({41, 48, 52, 57, 59, 64});
    p.chordOn({43, 50, 53, 57, 59, 64, 69}, 96);
    p.waitUntil(t + 7.4);
    p.chordOff({43, 50, 53, 57, 59, 64, 69});
    // denser and higher: stabs of a cluster, then one wide chord held
    for (int i = 0; i < 4; ++i) {
        const double u = t + 7.7 + 0.45 * i;
        p.waitUntil(u);
        p.chordOn({55, 57, 59, 60, 62, 64}, 100);
        p.waitUntil(u + 0.25);
        p.chordOff({55, 57, 59, 60, 62, 64});
    }
    p.waitUntil(t + 9.7);
    p.chordOn({36, 43, 52, 55, 59, 62, 67, 71}, 92);
    p.waitUntil(t + 12.4);
    p.chordOff({36, 43, 52, 55, 59, 62, 67, 71});
    return t + 13.4;
}

double arpeggio(Player& p, double t) {
    usePatch(p, "pluck");
    // 16ths at 132 BPM over two octaves, up and down: Am, F, C, G (a bar each), twice.
    const double step = 60.0 / 132.0 / 4.0;
    const std::array<std::array<int, 4>, 4> chordsOf{{{57, 60, 64, 69}, {53, 57, 60, 65}, {48, 52, 55, 60},
                                                      {55, 59, 62, 67}}};
    const std::array<int, 16> shape{0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 2, 3};
    int n = 0;
    for (int bar = 0; bar < 8; ++bar) {
        const auto& c = chordsOf[static_cast<std::size_t>(bar % 4)];
        for (int i = 0; i < 16; ++i, ++n) {
            const int idx = shape[static_cast<std::size_t>(i)];
            const int key = c[static_cast<std::size_t>(idx % 4)] + 12 * (idx / 4);
            p.note(t + step * n, key, 92 + (i % 4 == 0 ? 20 : 0), step * 0.8);
        }
    }
    return t + step * n + 0.8;
}

double distorted(Player& p, double t) {
    // A bass riff: one bar clean, then the same riff driven hard (drive before the filter, which is opened a
    // little, as a player would), then harder still.
    usePatch(p, "bass");
    const double step = 60.0 / 110.0 / 4.0;
    const std::array<int, 16> riff{33, 33, 45, 33, 43, 33, 40, 33, 33, 36, 33, 38, 33, 45, 43, 40};
    const std::array<double, 16> len{1.5, 0.8, 0.8, 1.5, 0.8, 0.8, 1.5, 0.8, 1.5, 0.8, 0.8, 1.5, 0.8, 0.8, 0.8, 0.8};
    int n = 0;
    for (int bar = 0; bar < 5; ++bar) {
        if (bar == 1) {
            usePatch(p, "distbass");
        }
        if (bar == 3) {
            p.synth.drive.store(45.0f);
            p.synth.cutoff.store(1800.0f);
            p.log.row("drive", -1, 0.0f, p.synth);
        }
        for (int i = 0; i < 16; ++i, ++n) {
            if (i % 2 == 1 && (i == 5 || i == 11)) {
                continue; // rests: the riff breathes
            }
            p.note(t + step * n, riff[static_cast<std::size_t>(i)], 108, step * len[static_cast<std::size_t>(i)]);
        }
    }
    return t + step * n + 0.8;
}

double sweepGesture(Player& p, double t) {
    usePatch(p, "lead");
    Synth& s = p.synth;
    s.cutoff.store(120.0f);
    s.resonance.store(0.9f);
    s.patch.release.store(0.3f);
    p.log.row("cutoff", -1, 0.0f, s);
    p.waitUntil(t);
    p.on(45, 96);
    p.ramp(s.cutoff, 120.0f, 9000.0f, t + 1.5, t + 9.5, "cutoff");
    p.ramp(s.cutoff, 9000.0f, 120.0f, t + 10.5, t + 15.5, "cutoff");
    p.waitUntil(t + 16.5);
    p.off(45);
    return t + 17.5;
}

double driveGesture(Player& p, double t) {
    usePatch(p, "lead");
    Synth& s = p.synth;
    s.cutoff.store(350.0f);
    s.resonance.store(0.707f);
    s.drive.store(1.0f);
    s.driveAfterFilter.store(true);
    s.patch.release.store(0.3f);
    p.log.row("drive", -1, 0.0f, s);
    p.waitUntil(t);
    p.on(45, 96);
    p.ramp(s.drive, 1.0f, 40.0f, t + 1.5, t + 11.5, "drive");
    p.waitUntil(t + 13.5);
    p.off(45);
    s.drive.store(1.0f);
    s.driveAfterFilter.store(false);
    return t + 14.5;
}

// The same two-bar phrase through each patch: the live form of §34.
double patchesGesture(Player& p, double t) {
    const std::array<const char*, 6> order{"pad", "pluck", "lead", "bell", "distbass", "perc"};
    const double step = 60.0 / 100.0 / 2.0; // 8ths at 100 BPM
    const std::array<int, 12> phrase{57, 60, 64, 67, 64, 60, 62, 65, 69, 72, 69, 65};
    double u = t;
    for (const char* name : order) {
        usePatch(p, name);
        const bool low = std::strcmp(name, "distbass") == 0;
        for (std::size_t i = 0; i < phrase.size(); ++i) {
            const int key = phrase[i] - (low ? 24 : 0);
            const double len = std::strcmp(name, "pad") == 0 ? step * 1.9 : step * 0.8;
            p.note(u + step * static_cast<double>(i), key, 100, len);
        }
        u += step * static_cast<double>(phrase.size()) + 4.0; // room for the world to become the sound's
    }
    return u;
}

// ADR-1605: the four transient families, 48 kHz mono, peak about 0.7.
void buildShots(Synth& s) {
    constexpr float sr = 48000.0f;
    std::uint32_t noise = 0x1234567u;
    const auto white = [&noise]() {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return static_cast<float>(noise) / 2147483648.0f - 1.0f;
    };
    // kick: a 150 -> 50 Hz sine sweep under a 2 ms click
    {
        auto& b = s.shots[0];
        b.resize(static_cast<std::size_t>(0.25f * sr));
        float phase = 0.0f;
        for (std::size_t i = 0; i < b.size(); ++i) {
            const float t = static_cast<float>(i) / sr;
            phase += (50.0f + 100.0f * std::exp(-t / 0.03f)) / sr;
            b[i] = 0.7f * std::sin(2.0f * kPi * phase) * std::exp(-t / 0.12f) + 0.3f * white() * std::exp(-t / 0.002f);
        }
    }
    // snare: high-passed noise and a 190 Hz body
    {
        auto& b = s.shots[1];
        b.resize(static_cast<std::size_t>(0.2f * sr));
        float last = 0.0f;
        for (std::size_t i = 0; i < b.size(); ++i) {
            const float t = static_cast<float>(i) / sr;
            const float w = white();
            b[i] = 0.45f * (w - last) * std::exp(-t / 0.06f) + 0.3f * std::sin(2.0f * kPi * 190.0f * t) * std::exp(-t / 0.05f);
            last = w;
        }
    }
    // hat: second-differenced noise, 8 ms
    {
        auto& b = s.shots[2];
        b.resize(static_cast<std::size_t>(0.05f * sr));
        float l1 = 0.0f, l2 = 0.0f;
        for (std::size_t i = 0; i < b.size(); ++i) {
            const float t = static_cast<float>(i) / sr;
            const float w = white();
            b[i] = 0.35f * (w - 2.0f * l1 + l2) * std::exp(-t / 0.008f);
            l2 = l1;
            l1 = w;
        }
    }
    // pluck: a 220 Hz saw, 2 ms attack, 0.25 s decay
    {
        auto& b = s.shots[3];
        b.resize(static_cast<std::size_t>(0.5f * sr));
        float phase = 0.0f;
        for (std::size_t i = 0; i < b.size(); ++i) {
            const float t = static_cast<float>(i) / sr;
            phase += 220.0f / sr;
            phase -= std::floor(phase);
            b[i] = 0.5f * (2.0f * phase - 1.0f) * std::min(1.0f, t / 0.002f) * std::exp(-t / 0.25f);
        }
    }
}

void scenario(const std::string& name, Player& p) {
    Synth& s = p.synth;
    if (name == "transients") {
        buildShots(s);
        for (int i = 0; i < 80; ++i) {
            const double t = 1.0 + 0.6031 * i;
            const int family = i % 4;
            p.waitUntil(t);
            s.shotStartFrame.store(-1, std::memory_order_release);
            s.shotPending.store(family, std::memory_order_release);
            while (s.shotStartFrame.load(std::memory_order_acquire) < 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            const auto frame = static_cast<double>(s.shotStartFrame.load());
            const std::uint64_t ns = s.firstCallbackNs.load() + static_cast<std::uint64_t>(frame / 48000.0 * 1e9);
            p.log.out << ns << ",shot," << family << ",1,0,0\n";
        }
        p.waitUntil(1.0 + 0.6031 * 80 + 1.0);
    } else if (name == "latency") {
        s.cutoff.store(6000.0f);
        // 0.8 s is exactly 75 analysis hops (512 at 48 kHz) and 48 frames at 60 Hz, so a plain 0.8 s grid would
        // put every note at the same hop and frame phase and measure one offset 24 times. The extra 3.1 ms per note
        // walks both phases across the run.
        for (int i = 0; i < 24; ++i) {
            const double t = 1.0 + 0.8031 * i;
            const int key = 48 + (i * 7) % 24;
            p.waitUntil(t);
            p.on(key, 100);
            p.waitUntil(t + 0.35);
            p.off(key);
        }
        p.waitUntil(1.0 + 0.8031 * 24 + 1.0);
    } else if (name == "sweep") {
        s.cutoff.store(120.0f);
        s.resonance.store(0.9f);
        p.waitUntil(1.0);
        p.on(45, 96); // A2, held throughout
        p.waitUntil(3.0);
        p.ramp(s.cutoff, 120.0f, 9000.0f, 3.0, 11.0, "cutoff");
        p.waitUntil(12.0);
        p.ramp(s.cutoff, 9000.0f, 120.0f, 12.0, 17.0, "cutoff");
        p.waitUntil(18.5);
        p.off(45);
        p.waitUntil(20.0);
    } else if (name == "drive") {
        // A mellow tone (two saws low-passed at 350 Hz: nearly sinusoidal) into a rising drive: clean, then
        // harmonics, then the detuned pair's intermodulation and hard clipping. The top is held for 6 s, long enough
        // for the world's identity (the slow tier, about 2 s) to follow the fast channels.
        s.cutoff.store(350.0f);
        s.resonance.store(0.707f);
        s.drive.store(1.0f);
        s.driveAfterFilter.store(true);
        p.waitUntil(1.0);
        p.on(45, 96);
        p.waitUntil(3.0);
        p.ramp(s.drive, 1.0f, 40.0f, 3.0, 13.0, "drive");
        p.waitUntil(19.0);
        p.off(45);
        p.waitUntil(20.5);
    } else if (name == "demo") {
        // Low soft notes, high bright notes, chords, an arpeggio, a filter sweep on a held chord, distortion.
        s.cutoff.store(500.0f);
        const auto note = [&](double t, int key, int vel, double len) {
            p.waitUntil(t);
            p.on(key, vel);
            p.waitUntil(t + len);
            p.off(key);
        };
        note(1.0, 36, 50, 1.6);
        note(3.0, 43, 50, 1.6);
        p.waitUntil(5.0);
        s.cutoff.store(7000.0f);
        for (int i = 0; i < 6; ++i) {
            note(5.0 + 0.35 * i, 79 + (i % 3) * 3, 110, 0.18);
        }
        s.cutoff.store(2500.0f);
        for (int c = 0; c < 3; ++c) {
            const double t = 8.0 + 1.0 * c;
            p.waitUntil(t);
            for (int k : {48 + c * 2, 52 + c * 2, 55 + c * 2, 59 + c * 2}) {
                p.on(k, 90);
            }
            p.waitUntil(t + 0.8);
            for (int k : {48 + c * 2, 52 + c * 2, 55 + c * 2, 59 + c * 2}) {
                p.off(k);
            }
        }
        for (int i = 0; i < 16; ++i) {
            note(11.5 + 0.125 * i, 60 + std::array<int, 4>{0, 4, 7, 12}[i % 4] + (i / 4) * 2, 100, 0.1);
        }
        p.waitUntil(14.0);
        s.cutoff.store(150.0f);
        s.resonance.store(0.9f);
        p.on(45, 96);
        p.ramp(s.cutoff, 150.0f, 8000.0f, 14.0, 20.0, "cutoff");
        p.ramp(s.drive, 1.0f, 30.0f, 20.0, 25.0, "drive");
        p.waitUntil(26.0);
        p.off(45);
        p.waitUntil(27.5);
    } else if (name == "drivechord") {
        // The drive test on a held minor triad: the notes intermodulate, which a single note cannot.
        usePatch(p, "lead");
        s.cutoff.store(700.0f);
        s.resonance.store(0.707f);
        s.drive.store(1.0f);
        s.driveAfterFilter.store(true);
        p.waitUntil(1.0);
        p.chordOn({45, 48, 52}, 90);
        p.ramp(s.drive, 1.0f, 40.0f, 2.5, 12.5, "drive");
        p.waitUntil(14.5);
        p.chordOff({45, 48, 52});
        p.waitUntil(15.5);
    } else if (name == "low") {
        p.waitUntil(lowSoft(p, 1.0) + 1.0);
    } else if (name == "high") {
        p.waitUntil(highBright(p, 1.0) + 1.0);
    } else if (name == "chords") {
        p.waitUntil(chords(p, 1.0) + 1.0);
    } else if (name == "arp") {
        p.waitUntil(arpeggio(p, 1.0) + 1.0);
    } else if (name == "distorted") {
        p.waitUntil(distorted(p, 1.0) + 1.0);
    } else if (name == "patches") {
        p.waitUntil(patchesGesture(p, 1.0) + 1.0);
    } else if (name == "play") {
        // A play-through: the idle world, then each gesture in turn, and back to silence. The order runs warm to
        // bright to dense to rhythmic to glass, then the two knobs (the filter, the drive) into the heavy riff,
        // the strike field, and a last soft note.
        double t = 3.0;
        t = lowSoft(p, t);
        t = highBright(p, t);
        t = chords(p, t + 0.5);
        t = arpeggio(p, t + 0.5);
        usePatch(p, "bell");
        const std::array<int, 12> bell{72, 79, 76, 84, 83, 79, 88, 91, 86, 84, 79, 96};
        for (std::size_t i = 0; i < bell.size(); ++i) {
            p.note(t + 0.6 * static_cast<double>(i), bell[i], 100, 0.3);
        }
        t += 0.6 * static_cast<double>(bell.size()) + 2.0;
        t = sweepGesture(p, t + 0.5);
        t = driveGesture(p, t + 0.5);
        t = distorted(p, t);
        usePatch(p, "perc");
        for (int i = 0; i < 16; ++i) {
            p.note(t + 0.227 * i, 60 + (i % 3) * 7, i % 4 == 0 ? 127 : 90, 0.05);
        }
        t += 0.227 * 16 + 3.0;
        usePatch(p, "pad");
        p.note(t, 45, 50, 4.0);
        p.waitUntil(t + 8.0);
    } else if (name == "tour") {
        // ADR-1063: the whole set list in one live session. A program change every 20 s (program 0 is Sonic Live, then
        // the Sonic VFX scenes in the index's order), each scene played a 16 s figure: a held chord, an arpeggio and
        // a low riff, so every world shows its sustain, its melody and its low attacks.
        const int scenes = p.tourScenes;
        for (int k = 0; k < scenes; ++k) {
            const double t = 1.0 + 20.0 * k;
            p.waitUntil(t);
            p.program(k);
            usePatch(p, k % 2 == 0 ? "pad" : "pluck");
            p.waitUntil(t + 3.0); // the scene loads
            p.chordOn({48, 55, 60, 64}, 80);
            p.waitUntil(t + 7.0);
            p.chordOff({48, 55, 60, 64});
            for (int i = 0; i < 16; ++i) {
                p.note(t + 7.5 + 0.18 * i, 60 + (i * 7) % 24, 90 + (i % 4 == 0) * 30, 0.14);
            }
            usePatch(p, "distbass");
            for (int i = 0; i < 8; ++i) {
                p.note(t + 11.0 + 0.3 * i, i % 2 == 0 ? 33 : 40, 110, 0.2);
            }
        }
        p.waitUntil(1.0 + 20.0 * scenes);
    } else if (name == "controller") {
        // A performer's controller on Digital Ocean's map (docs/prototypes/digital-ocean/02-report.md): pads 36-40
        // and CC sweeps over a held low pad note, about 46 s.
        usePatch(p, "pad");
        p.waitUntil(1.0);
        p.on(40, 70);
        p.pad(2.0, 36, 110);        // leap
        p.pad(8.0, 37, 110);        // hero wave
        p.ccRamp(2, 0, 127, 12.0, 16.0);   // swell up
        p.ccRamp(2, 127, 0, 19.0, 22.0);   // and down
        p.pad(23.0, 38, 120);       // bloom
        p.pad(27.0, 39, 120);       // flare
        p.pad(29.0, 40, 120);       // spray
        p.ccRamp(1, 0, 127, 31.0, 35.0);   // night falls
        p.pad(37.0, 38, 120);       // a bloom at night
        p.pad(39.0, 36, 110);       // a leap at night
        p.ccRamp(1, 127, 0, 42.0, 45.0);   // dusk again
        p.off(40);
        p.waitUntil(46.0);
    } else if (name == "stress") {
        // Digital Ocean pass 2's reliability check (Pass H): every pad hammered at 4 Hz, the palette and night CCs
        // swept fast and slow, repeated signature events, about 62 s over a held pad note. Nothing may accumulate.
        usePatch(p, "pad");
        p.waitUntil(1.0);
        p.on(40, 70);
        double t = 2.0;
        for (int round = 0; round < 2; ++round) {
            for (int pad = 36; pad <= 41; ++pad) {
                for (int k = 0; k < 8; ++k) {
                    p.pad(t, pad, 100 + (k % 3) * 9);
                    t += 0.25;
                }
            }
        }
        p.ccRamp(1, 0, 127, t, t + 1.0);          // palette: fast up
        p.ccRamp(1, 127, 0, t + 1.0, t + 2.0);    // and down
        p.ccRamp(1, 0, 127, t + 2.0, t + 12.0);   // slowly through all three palettes
        p.ccRamp(2, 0, 127, t + 12.0, t + 15.0);  // night
        for (int k = 0; k < 6; ++k) {
            p.pad(t + 15.5 + 0.4 * k, 37, 120);   // hero waves while it is night
        }
        p.ccRamp(2, 127, 0, t + 18.5, t + 21.0);
        p.ccRamp(1, 127, 0, t + 21.0, t + 31.0);  // back to the sunset
        p.off(40);
        p.waitUntil(t + 32.0);
    } else if (name == "resonator") {
        // 25 ticks a second. Knob k (CC 1-8, then 11) is active in its own 6 s window of the 30 s cycle, overlapping
        // the next by half, so two knobs move at once; inside the window it rides a triangle 0 -> 127 -> 0.
        constexpr std::array<int, 9> knobs{1, 2, 3, 4, 5, 6, 7, 8, 11};
        std::array<int, 128> last{};
        last.fill(-1);
        const double end = std::max(10.0, p.recordSeconds - 2.0);
        int palette = 0;
        bool lock = false;
        const auto send = [&](int number, int value) {
            if (last[static_cast<std::size_t>(number)] != value) {
                last[static_cast<std::size_t>(number)] = value;
                p.cc(number, value);
            }
        };
        for (int tick = 0;; ++tick) {
            const double t = 1.0 + 0.04 * tick;
            if (t > end) {
                break;
            }
            p.waitUntil(t);
            const double cycle = std::fmod(t - 1.0, 30.0);
            for (std::size_t k = 0; k < knobs.size(); ++k) {
                const double w0 = 3.0 * static_cast<double>(k);
                const double u = (cycle - w0) / 6.0;
                if (u >= 0.0 && u <= 1.0) {
                    const double tri = u < 0.5 ? 2.0 * u : 2.0 - 2.0 * u;
                    send(knobs[k], static_cast<int>(std::lround(127.0 * tri)));
                }
            }
            if (tick % 250 == 0) { // every 10 s
                palette = (palette + 1) % 4;
                send(9, palette * 42);
            }
            if (tick % 750 == 375) { // every 30 s, offset
                lock = !lock;
                send(10, lock ? 127 : 0);
            }
            const int beat = tick % 50; // a burst of four pad strikes at 4 Hz every 2 s
            if (beat == 0 || beat == 6 || beat == 12 || beat == 18) {
                const std::array<std::uint8_t, 3> on{0x90, 36, static_cast<std::uint8_t>(96 + (tick / 50) % 3 * 12)};
                static_cast<void>(p.midi.send(on));
                p.log.row("pad", 36, static_cast<float>(on[2]) / 127.0f, p.synth);
            } else if (beat == 3 || beat == 9 || beat == 15 || beat == 21) {
                const std::array<std::uint8_t, 3> off{0x80, 36, 0};
                static_cast<void>(p.midi.send(off));
            }
        }
    } else if (name == "silence") {
        p.waitUntil(5.0);
    } else {
        std::fprintf(stderr, "unknown scenario '%s'\n", name.c_str());
    }
}

bool writeWav(const std::string& path, const std::vector<float>& samples, std::size_t count, std::uint32_t rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    const auto u32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(count * 2);
    f.write("RIFF", 4);
    u32(36 + bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    f.write("data", 4);
    u32(bytes);
    for (std::size_t i = 0; i < count; ++i) {
        const auto v = static_cast<std::int16_t>(std::lround(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f));
        f.write(reinterpret_cast<const char*>(&v), 2);
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: avgen_sonic_probe <transients|latency|sweep|drive|demo|silence|low|high|chords|arp|"
                             "distorted|patches|play|tour|controller|stress|resonator> [--out f.csv] [--scenes n] "
                             "[--wav f.wav] [--device BlackHole] [--lead-in s] [--period frames] [--record-seconds s]\n");
        return 2;
    }
    const std::string name = argv[1];
    std::string outPath = "probe-events.csv";
    int scenes = 17;
    std::string wavPath;
    std::string deviceName = "BlackHole";
    double leadIn = 0.0;
    ma_uint32 period = 128;
    double recordSeconds = 0.0; // 0 = long enough for the scenario (see below)
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--out") outPath = argv[i + 1];
        else if (a == "--wav") wavPath = argv[i + 1];
        else if (a == "--device") deviceName = argv[i + 1];
        else if (a == "--lead-in") leadIn = std::atof(argv[i + 1]);
        else if (a == "--period") period = static_cast<ma_uint32>(std::atoi(argv[i + 1]));
        else if (a == "--scenes") scenes = std::max(1, std::atoi(argv[i + 1]));
        else if (a == "--record-seconds") recordSeconds = std::max(1.0, std::atof(argv[i + 1]));
    }
    // The recording buffer (the --wav soundtrack). It was a fixed 180 s, shorter than a 17-scene tour (1 + 20 x 17
    // = 341 s), so the tour's WAV stopped two thirds of the way through. The default now covers the tour plus the
    // lead-in and a margin, and never less than the old 180 s; --record-seconds overrides it either way.
    if (recordSeconds <= 0.0) {
        recordSeconds = 180.0;
        if (name == "tour") {
            recordSeconds = std::max(recordSeconds, leadIn + 1.0 + 20.0 * scenes + 30.0);
        }
    }

    avgen::control::MidiVirtualSource midi;
    if (auto r = midi.open("AV Gen Probe"); !r) {
        std::fprintf(stderr, "virtual MIDI source: %s\n", r.error().message.c_str());
        return 1;
    }

    // The output device: the first playback device whose name contains `deviceName`.
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::fprintf(stderr, "no audio backend\n");
        return 1;
    }
    ma_device_info* playback = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* capture = nullptr;
    ma_uint32 captureCount = 0;
    ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount);
    const ma_device_id* id = nullptr;
    std::string chosen;
    std::string uid;
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        if (std::strstr(playback[i].name, deviceName.c_str()) != nullptr) {
            id = &playback[i].id;
            chosen = playback[i].name;
#if defined(__APPLE__)
            uid = playback[i].id.coreaudio; // the Core Audio device UID
#endif
            break;
        }
    }
    if (id == nullptr) {
        std::fprintf(stderr, "no playback device matches '%s'\n", deviceName.c_str());
        return 1;
    }
    Synth synth;
    synth.sampleRate = 48000.0f;
    synth.recording.assign(static_cast<std::size_t>(48000.0 * recordSeconds), 0.0f);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.playback.pDeviceID = id;
    config.sampleRate = 48000;
    // ADR-1550: miniaudio sets the device's buffer to the period it is given, and BlackHole may be running inside the
    // owner's Multi-Output Device: then keep its current buffer size rather than resize it under the other client.
    const ma_uint32 wanted = period;
    period = uid.empty() ? period : avgen::audio::courteousPeriodFrames(uid, period);
    if (period != wanted) {
        std::fprintf(stderr, "probe: '%s' is running elsewhere at %u frames; not resizing it to %u\n", chosen.c_str(),
                     period, wanted);
    }
    config.periodSizeInFrames = period;
    config.dataCallback = &callback;
    config.pUserData = &synth;
    ma_device device;
    if (ma_device_init(&context, &config, &device) != MA_SUCCESS || ma_device_start(&device) != MA_SUCCESS) {
        std::fprintf(stderr, "cannot open '%s'\n", chosen.c_str());
        return 1;
    }
    std::fprintf(stderr, "probe: MIDI source 'AV Gen Probe', audio out '%s' (%u-frame period), scenario %s\n",
                 chosen.c_str(), device.playback.internalPeriodSizeInFrames, name.c_str());

    Log log;
    log.out.open(outPath);
    log.out << "hostNs,kind,key,velocity,cutoff,drive\n";
    std::this_thread::sleep_for(std::chrono::duration<double>(leadIn));
    log.row("start", -1, 0.0f, synth);
    Player player{synth, midi, log};
    player.tourScenes = scenes;
    player.recordSeconds = recordSeconds;
    scenario(name, player);
    log.row("end", -1, 0.0f, synth);
    log.out << synth.firstCallbackNs.load() << ",recording,-1,0,0,0\n";
    log.out.flush();
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    if (!wavPath.empty()) {
        writeWav(wavPath, synth.recording, synth.recorded.load(), 48000);
    }
    std::fprintf(stderr, "probe: done (%.1f s of audio)\n", static_cast<double>(synth.recorded.load()) / 48000.0);
    return 0;
}

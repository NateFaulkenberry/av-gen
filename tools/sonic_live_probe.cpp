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
//   latency   24 notes, 0.8 s apart, a sharp attack each: MIDI -> bus, audio -> bus.
//   sweep     one held note; a low-pass cutoff sweeps 120 Hz -> 9 kHz and back (the brief's §17 / PART 17).
//   drive     one held note; the drive rises from clean to hard clipping (§18 / PART 18).
//   demo      a short phrase: low soft notes, high bright notes, chords, an arpeggio, a sweep, distortion.
//   silence   nothing: the device-present, nothing-played baseline.

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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using avgen::sonic::hostNowNs;

constexpr int kVoices = 8;
constexpr float kPi = 3.14159265358979f;

// ---- the synth: saw pair -> drive -> 2-pole low-pass (TPT state variable) -> amplitude envelope ----------------

struct Voice {
    std::atomic<int> key{-1};
    std::atomic<float> velocity{0.0f};
    std::atomic<bool> gate{false};
    // audio thread only
    float phase1 = 0.0f, phase2 = 0.0f, env = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
    int playingKey = -1;
};

struct Synth {
    std::array<Voice, kVoices> voices;
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

void render(Synth& s, float* out, ma_uint32 frames, ma_uint32 channels) {
    if (s.firstCallbackNs.load(std::memory_order_relaxed) == 0) {
        s.firstCallbackNs.store(hostNowNs(), std::memory_order_relaxed);
    }
    const float sr = s.sampleRate;
    const float cutoff = std::clamp(s.cutoff.load(std::memory_order_relaxed), 20.0f, sr * 0.45f);
    const float g = std::tan(kPi * cutoff / sr);
    const float k = 1.0f / std::max(s.resonance.load(std::memory_order_relaxed), 0.3f); // 1/Q
    const float drive = std::max(s.drive.load(std::memory_order_relaxed), 1.0f);
    const float norm = 1.0f / std::tanh(drive);
    const bool post = s.driveAfterFilter.load(std::memory_order_relaxed);
    const float level = s.level.load(std::memory_order_relaxed);
    const float attack = 1.0f - std::exp(-1.0f / (0.003f * sr));
    const float release = 1.0f - std::exp(-1.0f / (0.060f * sr));
    for (ma_uint32 i = 0; i < frames; ++i) {
        float mix = 0.0f;
        for (Voice& v : s.voices) {
            const bool gate = v.gate.load(std::memory_order_relaxed);
            const int key = v.key.load(std::memory_order_relaxed);
            if (gate && key != v.playingKey) {
                v.playingKey = key;
                v.env = 0.0f; // a new note: restart the envelope (a hard, audible attack)
            }
            const float target = gate ? v.velocity.load(std::memory_order_relaxed) : 0.0f;
            v.env += (target - v.env) * (gate ? attack : release);
            if (v.env < 1e-5f && !gate) {
                continue;
            }
            const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(v.playingKey) - 69.0f) / 12.0f);
            const float dt1 = hz / sr;
            const float dt2 = hz * 1.006f / sr;
            float saw = (2.0f * v.phase1 - 1.0f) - polyBlep(v.phase1, dt1);
            saw += (2.0f * v.phase2 - 1.0f) - polyBlep(v.phase2, dt2);
            saw *= 0.5f;
            v.phase1 += dt1;
            v.phase1 -= std::floor(v.phase1);
            v.phase2 += dt2;
            v.phase2 -= std::floor(v.phase2);
            const float driven = post ? saw : std::tanh(drive * saw) * norm;
            // TPT SVF low-pass (Zavalishin).
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v3 = driven - v.ic2;
            const float v1 = a1 * v.ic1 + g * a1 * v3;
            const float v2 = v.ic2 + g * v1;
            v.ic1 = 2.0f * v1 - v.ic1;
            v.ic2 = 2.0f * v2 - v.ic2;
            mix += (post ? std::tanh(drive * v2 * 1.5f) * norm : v2) * v.env;
        }
        const float sample = std::tanh(mix * level);
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
    // Moves a synth parameter along an exponential ramp from a to b over [t0, t1], logging it at 50 Hz.
    void ramp(std::atomic<float>& param, float a, float b, double t0, double t1, const char* name) {
        for (double t = t0; t <= t1 + 1e-9; t += 0.02) {
            waitUntil(t);
            const double u = std::clamp((t - t0) / (t1 - t0), 0.0, 1.0);
            param.store(static_cast<float>(a * std::pow(b / a, u)));
            log.row(name, -1, 0.0f, synth);
        }
    }
};

void scenario(const std::string& name, Player& p) {
    Synth& s = p.synth;
    if (name == "latency") {
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
        // harmonics, then the detuned pair's intermodulation and hard clipping.
        s.cutoff.store(350.0f);
        s.resonance.store(0.707f);
        s.drive.store(1.0f);
        s.driveAfterFilter.store(true);
        p.waitUntil(1.0);
        p.on(45, 96);
        p.waitUntil(3.0);
        p.ramp(s.drive, 1.0f, 40.0f, 3.0, 13.0, "drive");
        p.waitUntil(15.0);
        p.off(45);
        p.waitUntil(16.5);
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
        std::fprintf(stderr, "usage: avgen_sonic_probe <latency|sweep|drive|demo|silence> [--out f.csv] "
                             "[--wav f.wav] [--device BlackHole] [--lead-in s] [--period frames]\n");
        return 2;
    }
    const std::string name = argv[1];
    std::string outPath = "probe-events.csv";
    std::string wavPath;
    std::string deviceName = "BlackHole";
    double leadIn = 0.0;
    ma_uint32 period = 128;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--out") outPath = argv[i + 1];
        else if (a == "--wav") wavPath = argv[i + 1];
        else if (a == "--device") deviceName = argv[i + 1];
        else if (a == "--lead-in") leadIn = std::atof(argv[i + 1]);
        else if (a == "--period") period = static_cast<ma_uint32>(std::atoi(argv[i + 1]));
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
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        if (std::strstr(playback[i].name, deviceName.c_str()) != nullptr) {
            id = &playback[i].id;
            chosen = playback[i].name;
            break;
        }
    }
    if (id == nullptr) {
        std::fprintf(stderr, "no playback device matches '%s'\n", deviceName.c_str());
        return 1;
    }
    Synth synth;
    synth.sampleRate = 48000.0f;
    synth.recording.assign(static_cast<std::size_t>(48000.0 * 90.0), 0.0f);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.playback.pDeviceID = id;
    config.sampleRate = 48000;
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

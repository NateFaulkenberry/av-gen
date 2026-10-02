// ADR-1020: the Sonic Garden subsystem -- timbre measurements, the Sonic Character's normalisation and smoothing,
// the MIDI note track and Musical Context, the Visual Interpreter, and the engine wiring (signals, determinism,
// the subsystem doing nothing when absent).

#include "analysis/analyzer.hpp"
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"
#include "sonic/character.hpp"
#include "sonic/interpret_source.hpp"
#include "sonic/notes.hpp"
#include "sonic/sonic_runtime.hpp"
#include "sonic/timbre.hpp"
#include "support/temp_dir.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_trigger.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <random>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr std::uint32_t kRate = 48000;
constexpr double kTwoPi = 6.283185307179586;

// Mean timbre over the steady middle of a synthesized mono signal.
sonic::TimbreFeatures meanTimbre(const std::vector<float>& mono) {
    analysis::AnalyzerConfig config;
    config.sampleRate = kRate;
    analysis::Analyzer analyzer(config);
    analyzer.push(mono);
    sonic::TimbreAnalyzer timbre(kRate, config.windowSize);
    std::vector<sonic::TimbreFeatures> all;
    analysis::AnalysisFrame frame;
    while (analyzer.pop(frame)) {
        all.push_back(timbre.analyze(frame));
    }
    REQUIRE(all.size() > 20);
    sonic::TimbreFeatures m;
    const std::size_t a = all.size() / 4;
    const std::size_t b = all.size() * 3 / 4;
    for (std::size_t i = a; i < b; ++i) {
        m.harmonicity += all[i].harmonicity;
        m.inharmonicity += all[i].inharmonicity;
        m.flatness += all[i].flatness;
        m.tonalness += all[i].tonalness;
        m.centroidHz += all[i].centroidHz;
        m.f0Hz += all[i].f0Hz;
        m.dissonance += all[i].dissonance;
    }
    const auto n = static_cast<float>(b - a);
    m.harmonicity /= n;
    m.inharmonicity /= n;
    m.flatness /= n;
    m.tonalness /= n;
    m.centroidHz /= n;
    m.f0Hz /= n;
    m.dissonance /= n;
    return m;
}

std::vector<float> saw(const std::vector<float>& f0s, double seconds, int harmonics = 30) {
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        double v = 0.0;
        for (const float f0 : f0s) {
            for (int k = 1; k <= harmonics && f0 * k < 16000.0f; ++k) {
                v += std::sin(kTwoPi * f0 * k * t + 0.37 * k) / k;
            }
        }
        out[i] = static_cast<float>(0.2 * v / static_cast<double>(f0s.size()));
    }
    return out;
}

std::vector<float> inharmonic(float base, double seconds) {
    // Partials at base * {1, 1.41, 2.13, 2.97, 3.62, 4.81}: no shared fundamental in the search range.
    const std::array<double, 6> ratios{1.0, 1.41, 2.13, 2.97, 3.62, 4.81};
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        double v = 0.0;
        for (std::size_t k = 0; k < ratios.size(); ++k) {
            v += std::sin(kTwoPi * base * ratios[k] * t) / (1.0 + 0.4 * k);
        }
        out[i] = static_cast<float>(0.15 * v);
    }
    return out;
}

std::vector<float> noise(double seconds) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate));
    for (float& s : out) {
        s = u(rng);
    }
    return out;
}

// ---- a tiny MIDI writer for the note-track tests -----------------------------------------------------------------

void vlq(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::uint8_t buf[4];
    int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7) != 0) {
        buf[n++] = static_cast<std::uint8_t>(0x80 | (v & 0x7F));
    }
    while (n > 0) {
        out.push_back(buf[--n]);
    }
}

struct Ev {
    std::uint32_t tick;
    std::vector<std::uint8_t> bytes;
};

std::vector<std::uint8_t> midiFile(std::vector<Ev> events, std::uint16_t ppq = 480) {
    std::vector<std::uint8_t> track;
    std::uint32_t last = 0;
    for (const Ev& e : events) {
        vlq(track, e.tick - last);
        last = e.tick;
        track.insert(track.end(), e.bytes.begin(), e.bytes.end());
    }
    vlq(track, 0);
    track.insert(track.end(), {0xFF, 0x2F, 0x00});
    std::vector<std::uint8_t> file{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                                   static_cast<std::uint8_t>(ppq >> 8), static_cast<std::uint8_t>(ppq & 0xFF)};
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    const auto len = static_cast<std::uint32_t>(track.size());
    file.insert(file.end(), {static_cast<std::uint8_t>(len >> 24), static_cast<std::uint8_t>(len >> 16),
                             static_cast<std::uint8_t>(len >> 8), static_cast<std::uint8_t>(len)});
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

sonic::NoteTrack notes(std::vector<std::tuple<double, double, int, float>> list) {
    sonic::NoteTrack t;
    for (const auto& [start, dur, key, vel] : list) {
        sonic::NoteEvent n;
        n.start = start;
        n.duration = dur;
        n.key = static_cast<std::uint8_t>(key);
        n.pitch = static_cast<float>(key);
        n.velocity = vel;
        t.notes.push_back(n);
    }
    t.finish();
    return t;
}

} // namespace

// ---- normalisation and feature conversion -------------------------------------------------------------------------

TEST_CASE("A character term maps a measurement from its fixed range to 0..1", "[sonic][adr1020]") {
    sonic::Term t;
    t.feature = sonic::Feature::Loudness;
    t.lo = -48.0f;
    t.hi = -12.0f;
    CHECK(sonic::mapTerm(t, -48.0f) == Approx(0.0f));
    CHECK(sonic::mapTerm(t, -30.0f) == Approx(0.5f));
    CHECK(sonic::mapTerm(t, 0.0f) == Approx(1.0f));   // clamped, never past 1
    CHECK(sonic::mapTerm(t, -90.0f) == Approx(0.0f)); // or below 0
    t.invert = true;
    CHECK(sonic::mapTerm(t, -30.0f) == Approx(0.5f));
    CHECK(sonic::mapTerm(t, -12.0f) == Approx(0.0f));
    sonic::Term hz;
    hz.feature = sonic::Feature::Centroid;
    hz.lo = 250.0f;
    hz.hi = 4000.0f;
    hz.log = true;
    CHECK(sonic::mapTerm(hz, 1000.0f) == Approx(0.5f)); // 1 kHz is two octaves up a four-octave range
    CHECK(sonic::mapTerm(hz, 0.0f) == Approx(0.0f));    // no centroid is the bottom, not NaN
}

TEST_CASE("The character is absolute: a quieter copy of a sound is less energetic, not renormalised",
          "[sonic][adr1020]") {
    const sonic::CharacterSpec spec = sonic::CharacterSpec::defaults();
    std::array<float, sonic::kFeatureCount> loud{};
    loud[static_cast<std::size_t>(sonic::Feature::Loudness)] = -14.0f;
    loud[static_cast<std::size_t>(sonic::Feature::Centroid)] = 1500.0f;
    auto quiet = loud;
    quiet[static_cast<std::size_t>(sonic::Feature::Loudness)] = -40.0f;
    const auto a = sonic::evaluateCharacter(spec, loud);
    const auto b = sonic::evaluateCharacter(spec, quiet);
    CHECK(a[static_cast<std::size_t>(sonic::Dimension::Energy)] > b[static_cast<std::size_t>(sonic::Dimension::Energy)] + 0.2f);
    // ...and a level change moves nothing that is not about level.
    CHECK(a[static_cast<std::size_t>(sonic::Dimension::Brightness)] ==
          Approx(b[static_cast<std::size_t>(sonic::Dimension::Brightness)]));
}

TEST_CASE("A composite dimension reads the dimensions before it, and a project can override the terms",
          "[sonic][adr1020]") {
    sonic::CharacterSpec spec = sonic::CharacterSpec::defaults();
    const nlohmann::json overrides = nlohmann::json::parse(R"({
        "gateDb": -40,
        "dimensions": {
            "brightness": {"attack": 0.5, "terms": [{"feature": "centroid", "lo": 100, "hi": 200}]},
            "organic": {"terms": [{"dimension": "brightness", "invert": true}]}
        }})");
    REQUIRE(spec.applyJson(overrides).has_value());
    CHECK(spec.gateDb == Approx(-40.0f));
    CHECK(spec.dimensions[static_cast<std::size_t>(sonic::Dimension::Brightness)].attack == Approx(0.5f));
    std::array<float, sonic::kFeatureCount> f{};
    f[static_cast<std::size_t>(sonic::Feature::Centroid)] = 150.0f;
    const auto c = sonic::evaluateCharacter(spec, f);
    CHECK(c[static_cast<std::size_t>(sonic::Dimension::Brightness)] == Approx(0.5f));
    CHECK(c[static_cast<std::size_t>(sonic::Dimension::Organic)] == Approx(0.5f));
    // A dimension may not read one computed after it, and names are checked.
    CHECK_FALSE(spec.applyJson(nlohmann::json::parse(R"({"dimensions": {"energy": {"terms": [{"dimension": "organic"}]}}})")));
    CHECK_FALSE(spec.applyJson(nlohmann::json::parse(R"({"dimensions": {"sparkle": {}}})")));
    CHECK_FALSE(spec.applyJson(nlohmann::json::parse(R"({"dimensions": {"energy": {"terms": [{"feature": "mfcc"}]}}})")));
}

// ---- timbre --------------------------------------------------------------------------------------------------------

TEST_CASE("Timbre tells a harmonic tone, a chord, an inharmonic tone and noise apart", "[sonic][adr1020]") {
    const auto tone = meanTimbre(saw({220.0f}, 1.0));
    const auto chord = meanTimbre(saw({220.0f, 277.18f, 329.63f}, 1.0));
    const auto bell = meanTimbre(inharmonic(311.0f, 1.0));
    const auto hiss = meanTimbre(noise(1.0));
    INFO("tone h " << tone.harmonicity << " i " << tone.inharmonicity << " f0 " << tone.f0Hz);
    INFO("chord h " << chord.harmonicity << " i " << chord.inharmonicity);
    INFO("bell h " << bell.harmonicity << " i " << bell.inharmonicity);
    INFO("noise h " << hiss.harmonicity << " flat " << hiss.flatness << " tonal " << hiss.tonalness);
    CHECK(tone.f0Hz == Approx(220.0f).margin(3.0f));
    CHECK(tone.harmonicity > 0.8f);
    CHECK(tone.inharmonicity < 0.2f);
    // Multi-f0: a chord of harmonic notes is harmonic, not "inharmonic against one fundamental".
    CHECK(chord.harmonicity > 0.7f);
    CHECK(chord.inharmonicity < 0.3f);
    CHECK(bell.inharmonicity > tone.inharmonicity + 0.25f);
    CHECK(bell.harmonicity < tone.harmonicity - 0.2f);
    CHECK(hiss.flatness > 0.2f);
    CHECK(hiss.flatness > 50.0f * tone.flatness);
    CHECK(hiss.tonalness < tone.tonalness - 0.3f);
    CHECK(hiss.harmonicity < 0.3f);
}

TEST_CASE("The timbre of an analysis frame is a pure function of the frame", "[sonic][adr1020]") {
    const auto signal = saw({196.0f, 246.94f}, 0.5);
    analysis::AnalyzerConfig config;
    analysis::Analyzer analyzer(config);
    analyzer.push(signal);
    analysis::AnalysisFrame frame;
    std::vector<analysis::AnalysisFrame> frames;
    while (analyzer.pop(frame)) {
        frames.push_back(frame);
    }
    sonic::TimbreAnalyzer a(kRate, config.windowSize);
    sonic::TimbreAnalyzer b(kRate, config.windowSize);
    // b sees the frames in reverse: no state may carry from one frame to the next.
    std::vector<sonic::TimbreFeatures> fa;
    std::vector<sonic::TimbreFeatures> fb(frames.size());
    for (const auto& f : frames) {
        fa.push_back(a.analyze(f));
    }
    for (std::size_t i = frames.size(); i-- > 0;) {
        fb[i] = b.analyze(frames[i]);
    }
    for (std::size_t i = 0; i < frames.size(); ++i) {
        CHECK(fa[i].harmonicity == fb[i].harmonicity);
        CHECK(fa[i].dissonance == fb[i].dissonance);
        CHECK(fa[i].f0Hz == fb[i].f0Hz);
    }
}

// ---- smoothing and the temporal tiers ------------------------------------------------------------------------------

TEST_CASE("The character tiers: fast attack, slower release, a slow tier, and a hold below the gate",
          "[sonic][adr1020]") {
    sonic::SonicSetup setup;
    sonic::SonicRuntime rt;
    const double dt = 512.0 / 48000.0;
    sonic::TimbreFeatures bright;
    bright.silent = false;
    bright.loudnessDb = -20.0f;
    bright.centroidHz = 4000.0f;
    bright.rolloffHz = 10000.0f;
    bright.highRatioDb = -6.0f;
    sonic::TimbreFeatures dark = bright;
    dark.centroidHz = 250.0f;
    dark.rolloffHz = 600.0f;
    dark.highRatioDb = -36.0f;
    const auto brightness = static_cast<std::size_t>(sonic::Dimension::Brightness);
    for (int i = 0; i < 400; ++i) {
        rt.step(setup, dark, dt);
    }
    CHECK(rt.medium()[brightness] == Approx(0.0f).margin(0.01f));
    // Rising: the medium tier follows at its attack, the slow tier lags far behind.
    for (int i = 0; i < 10; ++i) { // ~107 ms
        rt.step(setup, bright, dt);
    }
    const float risen = rt.medium()[brightness];
    CHECK(risen > 0.5f);
    CHECK(rt.slow()[brightness] < 0.2f);
    for (int i = 0; i < 1000; ++i) {
        rt.step(setup, bright, dt);
    }
    CHECK(rt.medium()[brightness] == Approx(1.0f).margin(0.01f));
    // Falling for the same ~107 ms: the release is slower than the attack.
    for (int i = 0; i < 10; ++i) {
        rt.step(setup, dark, dt);
    }
    CHECK(1.0f - rt.medium()[brightness] < risen);
    // Below the gate the timbre dimensions hold (the sound keeps its identity between notes); energy falls.
    sonic::TimbreFeatures silence;
    const float held = rt.medium()[brightness];
    const float energy = rt.medium()[static_cast<std::size_t>(sonic::Dimension::Energy)];
    for (int i = 0; i < 200; ++i) {
        rt.step(setup, silence, dt);
    }
    CHECK(rt.medium()[brightness] == held);
    CHECK(rt.medium()[static_cast<std::size_t>(sonic::Dimension::Energy)] < energy - 0.3f);
}

TEST_CASE("A transient event fires on a fast rise in level, once, and not on a slow swell", "[sonic][adr1020]") {
    sonic::SonicSetup setup;
    const double dt = 512.0 / 48000.0;
    sonic::TimbreFeatures f;
    f.silent = false;
    f.centroidHz = 1000.0f;
    const auto run = [&](const std::function<float(int)>& level) {
        sonic::SonicRuntime rt;
        signals::SignalBus bus;
        rt.declare(bus);
        int events = 0;
        for (int i = 0; i < 200; ++i) {
            f.loudnessDb = level(i);
            rt.step(setup, f, dt);
            rt.publish(&setup, bus, i * dt);
            events += bus.event(rt.ids().transient) ? 1 : 0;
            bus.clearEvents();
        }
        return events;
    };
    CHECK(run([](int i) { return i < 100 ? -60.0f : -12.0f; }) == 1);         // a hit
    CHECK(run([](int i) { return -60.0f + 48.0f * static_cast<float>(i) / 199.0f; }) == 0); // a 2 s swell
}

// ---- MIDI and the Musical Context ----------------------------------------------------------------------------------

TEST_CASE("A Standard MIDI File becomes a note track in seconds, through tempo changes and running status",
          "[sonic][adr1020]") {
    // 480 ppq. 120 bpm, then at beat 2 a change to 60 bpm. Notes: C4 at 0 for one beat (0.5 s); E4 at beat 2 for one
    // beat (1.0 s at the new tempo), written with running status and ended by a velocity-0 note-on; G4 never ended.
    const auto bytes = midiFile({
        {0, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}},      // 500000 us/quarter
        {0, {0x90, 60, 100}},
        {480, {0x80, 60, 0}},
        {960, {0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40}},    // 1000000 us/quarter
        {960, {0x90, 64, 64}},
        {1440, {64, 0}},                                 // running status: note-on velocity 0 = note-off
        {1440, {67, 127}},                               // running status note-on, never released
        {1920, {0xFF, 0x01, 0x00}},                      // an empty text event
    });
    auto track = sonic::NoteTrack::fromMidiBytes(bytes);
    REQUIRE(track.has_value());
    REQUIRE(track->notes.size() == 3);
    const auto& n = track->notes;
    CHECK(n[0].key == 60);
    CHECK(n[0].start == Approx(0.0));
    CHECK(n[0].duration == Approx(0.5));
    CHECK(n[0].velocity == Approx(100.0f / 127.0f));
    CHECK(n[1].key == 64);
    CHECK(n[1].start == Approx(1.0));  // two beats at 120 bpm
    CHECK(n[1].duration == Approx(1.0)); // one beat at 60 bpm
    CHECK(n[2].key == 67);
    CHECK(n[2].start == Approx(2.0));
    CHECK(n[2].duration == Approx(1.0)); // ended with its track (tick 1920)
    CHECK(track->longest == Approx(1.0));
    // Refusals, not guesses.
    CHECK_FALSE(sonic::NoteTrack::fromMidiBytes(std::vector<std::uint8_t>{'R', 'I', 'F', 'F'}));
    auto smpte = bytes;
    smpte[12] = 0xE7; // a negative (SMPTE) division
    CHECK_FALSE(sonic::NoteTrack::fromMidiBytes(smpte));
}

TEST_CASE("Musical context tells a sustained chord from a rapid arpeggio of the same notes", "[sonic][adr1020]") {
    const auto sustained = notes({{0.0, 4.0, 57, 0.6f}, {0.0, 4.0, 60, 0.6f}, {0.0, 4.0, 64, 0.6f}, {0.0, 4.0, 69, 0.6f}});
    std::vector<std::tuple<double, double, int, float>> arp;
    const int up[] = {57, 60, 64, 69};
    for (int i = 0; i < 32; ++i) {
        arp.emplace_back(i * 0.125, 0.1, up[i % 4], 0.6f);
    }
    const auto arpeggio = notes(arp);
    const auto s = sonic::contextAt(sustained, 3.0);
    const auto a = sonic::contextAt(arpeggio, 3.0);
    CHECK(s.active == 4);
    CHECK(a.active <= 1);
    CHECK(s.chord == Approx(1.0f));
    CHECK(a.chord == Approx(0.0f));
    CHECK(a.rhythm > 6.0f);             // eight onsets a second
    CHECK(s.rhythm < 0.1f);
    CHECK(a.regularity > 0.95f);
    CHECK(s.duration > 2.5f);           // held so far, not the file's four seconds
    CHECK(s.duration < 3.01f);
    CHECK(a.duration < 0.11f);
    CHECK(a.motion > 3.0f);
    CHECK(s.pitch == Approx((57 + 60 + 64 + 69) / 4.0f));
    CHECK(s.range == Approx(12.0f));
    CHECK(s.tension > 0.0f);
    CHECK(s.phrase > 0.7f);             // sounding for three of the last four seconds
    // Direction: a run that only rises reads +1, one that only falls reads -1.
    const auto rising = notes({{0.0, 0.1, 60, 0.5f}, {0.2, 0.1, 62, 0.5f}, {0.4, 0.1, 64, 0.5f}, {0.6, 0.1, 67, 0.5f}});
    const auto falling = notes({{0.0, 0.1, 67, 0.5f}, {0.2, 0.1, 64, 0.5f}, {0.4, 0.1, 62, 0.5f}, {0.6, 0.1, 60, 0.5f}});
    CHECK(sonic::contextAt(rising, 0.7).direction == Approx(1.0f));
    CHECK(sonic::contextAt(falling, 0.7).direction == Approx(-1.0f));
    // The same instant always gives the same answer (a pure function of time).
    const auto again = sonic::contextAt(arpeggio, 3.0);
    CHECK(again.density == a.density);
    CHECK(again.motion == a.motion);
}

TEST_CASE("Note events fire in the interval they fall in, and a phrase starts after a rest", "[sonic][adr1020]") {
    const auto t = notes({{0.0, 0.5, 60, 0.8f}, {0.25, 0.5, 64, 0.4f}, {2.0, 0.5, 67, 0.9f}});
    const auto first = sonic::eventsBetween(t, -0.001, 0.0);
    CHECK(first.noteOn);
    CHECK(first.onVelocity == Approx(0.8f));
    CHECK(first.phraseStart);
    const auto second = sonic::eventsBetween(t, 0.2, 0.3);
    CHECK(second.noteOn);
    CHECK_FALSE(second.phraseStart); // the first note is still sounding
    CHECK_FALSE(sonic::eventsBetween(t, 0.3, 0.45).noteOn);
    CHECK(sonic::eventsBetween(t, 0.45, 0.55).noteOff);
    const auto third = sonic::eventsBetween(t, 1.9, 2.0);
    CHECK(third.noteOn);
    CHECK(third.phraseStart); // 1.25 s of silence before it
    CHECK_FALSE(sonic::eventsBetween(t, 2.0, 2.0).noteOn); // an empty interval
}

// ---- the Visual Interpreter ----------------------------------------------------------------------------------------

TEST_CASE("Interpreter mappings combine, shape and compete deterministically", "[sonic][adr1020]") {
    using sonic::Combine;
    using sonic::InterpretSource;
    const std::vector<std::pair<float, float>> in{{0.2f, 1.0f}, {0.8f, 3.0f}};
    CHECK(InterpretSource::combine(Combine::Mean, in) == Approx(0.65f));
    CHECK(InterpretSource::combine(Combine::Sum, in) == Approx(2.6f));
    CHECK(InterpretSource::combine(Combine::Product, in) == Approx(0.2f * std::pow(0.8f, 3.0f)));
    CHECK(InterpretSource::combine(Combine::Max, in) == Approx(2.4f));
    CHECK(InterpretSource::combine(Combine::Min, in) == Approx(0.2f));
    CHECK(InterpretSource::shape(0.5f, 0.1f, 2.0f, 1.0f) == Approx(1.0f)); // clamped
    CHECK(InterpretSource::shape(0.5f, 0.0f, 1.0f, 2.0f) == Approx(0.25f));
    std::vector<float> family{0.6f, 0.3f, 0.1f};
    InterpretSource::compete(family, 1.0f);
    CHECK(family[0] + family[1] + family[2] == Approx(1.0f));
    CHECK(family[0] == Approx(0.6f));
    std::vector<float> sharp{0.6f, 0.3f, 0.1f};
    InterpretSource::compete(sharp, 4.0f);
    CHECK(sharp[0] > 0.9f); // sharper: the leader takes more
    std::vector<float> none{0.0f, 0.0f};
    InterpretSource::compete(none, 2.0f);
    CHECK(none[0] == 0.0f); // silence stays silence, never an arbitrary split
}

TEST_CASE("An interpret source publishes visual signals from the bus through the source rack", "[sonic][adr1020]") {
    signals::SignalBus bus;
    params::ParameterSet params;
    const auto warm = bus.declare("sonic.warmth.slow");
    const auto bright = bus.declare("sonic.brightness.slow");
    signals::SourceRack rack;
    const nlohmann::json doc = nlohmann::json::parse(R"([{"kind": "interpret", "name": "garden", "settings": {
        "mappings": [
          {"name": "organic", "group": "family", "inputs": [{"signal": "sonic.warmth.slow"}]},
          {"name": "crystal", "group": "family", "inputs": [{"signal": "sonic.brightness.slow"}]},
          {"name": "late", "inputs": ["lfo.later"]},
          {"name": "cool", "inputs": [{"signal": "sonic.warmth.slow", "invert": true, "weight": 2}], "curve": 2}],
        "groups": {"family": {"sharpness": 1}}}}])");
    REQUIRE(rack.fromJson(doc).has_value());
    rack.attach(bus, params);
    const auto organic = bus.find("visual.organic");
    const auto crystal = bus.find("visual.crystal");
    const auto late = bus.find("visual.late");
    const auto cool = bus.find("visual.cool");
    REQUIRE(organic);
    REQUIRE(crystal);
    REQUIRE(late);
    REQUIRE(cool);
    REQUIRE(params.find("sources/garden/organic/in1") != nullptr);
    REQUIRE(params.find("sources/garden/group/family/sharpness") != nullptr);
    bus.set(warm, 0.75f);
    bus.set(bright, 0.25f);
    signals::SourceContext ctx;
    rack.update(bus, ctx);
    CHECK(bus.value(*organic) == Approx(0.75f));
    CHECK(bus.value(*crystal) == Approx(0.25f));
    CHECK(bus.value(*cool) == Approx(0.0625f)); // (1 - 0.75)^2
    CHECK(bus.value(*late) == 0.0f);            // its input does not exist yet: absent, not an error
    // A producer declared after the source attached is found on the next frame.
    const auto lfo = bus.declare("lfo.later");
    bus.set(lfo, 0.4f);
    rack.update(bus, ctx);
    CHECK(bus.value(*late) == Approx(0.4f));
    // The weights are parameters: tuning one moves the output without touching the file.
    auto* sharpness = dynamic_cast<params::Parameter<float>*>(params.find("sources/garden/group/family/sharpness"));
    REQUIRE(sharpness != nullptr);
    sharpness->setBase(3.0f);
    params.resetFinals();
    rack.update(bus, ctx);
    CHECK(bus.value(*organic) == Approx(0.421875f / (0.421875f + 0.015625f)));
    // A replay samples it to the same values (pure given the bus).
    signals::SignalBus copy = bus;
    rack.sample(copy, ctx);
    CHECK(copy.value(*organic) == bus.value(*organic));
    // Settings round-trip.
    const auto saved = rack.toJson();
    signals::SourceRack again;
    REQUIRE(again.fromJson(saved).has_value());
    CHECK(again.toJson() == saved);
}

// ---- the engine: signals, determinism, and nothing when absent -----------------------------------------------------

namespace {

struct SonicProject {
    std::filesystem::path project;
    std::filesystem::path plain;
};

SonicProject writeSonicProject() {
    static const SonicProject paths = [] {
        const auto dir = testsupport::processTempDir() / "sonic";
        std::filesystem::create_directories(dir);
        // Four seconds: a held A-minor chord on a saw, then two seconds of a plucked arpeggio.
        std::vector<float> stereo;
        const auto chord = saw({220.0f, 261.63f, 329.63f}, 2.0);
        const auto pluck = saw({440.0f}, 2.0, 12);
        for (std::size_t i = 0; i < chord.size(); ++i) {
            stereo.push_back(chord[i]);
            stereo.push_back(chord[i]);
        }
        for (std::size_t i = 0; i < pluck.size(); ++i) {
            const double t = std::fmod(static_cast<double>(i) / kRate, 0.25);
            const float s = pluck[i] * static_cast<float>(std::exp(-t / 0.05));
            stereo.push_back(s);
            stereo.push_back(s);
        }
        const auto wav = dir / "sonic.wav";
        REQUIRE(audio::AudioFile::fromInterleaved(stereo, 2, kRate).writeWav(wav).has_value());
        std::vector<Ev> ev{{0, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}}}; // 120 bpm: a beat is 0.5 s
        for (const int k : {57, 60, 64}) {
            ev.push_back({0, {0x90, static_cast<std::uint8_t>(k), 80}});
        }
        for (const int k : {57, 60, 64}) {
            ev.push_back({1920, {0x80, static_cast<std::uint8_t>(k), 0}});
        }
        for (int i = 0; i < 8; ++i) {
            const auto tick = static_cast<std::uint32_t>(1920 + i * 240);
            const auto key = static_cast<std::uint8_t>(69 + (i % 4) * 2);
            ev.push_back({tick, {0x90, key, 100}});
            ev.push_back({tick + 200, {0x80, key, 0}});
        }
        std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick; });
        const auto mid = midiFile(ev);
        std::ofstream(dir / "sonic.mid", std::ios::binary).write(reinterpret_cast<const char*>(mid.data()),
                                                                 static_cast<std::streamsize>(mid.size()));
        const nlohmann::json sources = nlohmann::json::parse(R"([{"kind": "interpret", "name": "garden", "settings": {
            "mappings": [{"name": "glow", "inputs": [{"signal": "sonic.energy"}, {"signal": "notes.velocity"}]}]}}])");
        nlohmann::json p = {{"format", "avgen-project"},
                            {"version", 4},
                            {"assets", {{"audio", {{"path", "sonic.wav"}}}, {"scene", {{"kind", "orb"}}}}},
                            {"sources", sources},
                            {"sonic", {{"notes", "sonic.mid"}}},
                            {"render", {{"fps", 30}}}};
        std::ofstream(dir / "sonic.json") << p.dump(1);
        p.erase("sonic");
        std::ofstream(dir / "plain.json") << p.dump(1);
        return SonicProject{dir / "sonic.json", dir / "plain.json"};
    }();
    return paths;
}

void playTo(app::Engine& engine, double seconds, double fps = 30.0) {
    const auto frames = static_cast<std::uint64_t>(std::llround(seconds * fps));
    for (std::uint64_t f = 0; f <= frames; ++f) {
        FrameTime t;
        t.renderTime = static_cast<double>(f) / fps;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / fps;
        t.frameIndex = f;
        engine.update(t);
    }
}

float value(app::Engine& engine, const char* name) {
    const auto id = engine.signals().find(name);
    REQUIRE(id);
    return engine.signals().value(*id);
}

} // namespace

TEST_CASE("The engine publishes sonic, notes and visual signals from a project's sonic block", "[sonic][adr1020]") {
    const auto paths = writeSonicProject();
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    REQUIRE(engine.loadProject(paths.project).has_value());
    REQUIRE(engine.sonicSetup() != nullptr);
    CHECK(engine.sonicSetup()->notes.notes.size() == 11);
    CHECK(engine.sonicSetup()->timbre.size() == engine.track()->frames().size());
    playTo(engine, 1.5);
    // The held chord: three notes sounding, a harmonic, warm sound.
    CHECK(value(engine, "notes.active") == 3.0f);
    CHECK(value(engine, "notes.chord") == Approx(1.0f));
    CHECK(value(engine, "sonic.harmonicity") > 0.6f);
    CHECK(value(engine, "sonic.energy") > 0.2f);
    CHECK(value(engine, "visual.glow") > 0.2f);
    const float chordSharpness = value(engine, "sonic.sharpness");
    playTo(engine, 3.8);
    // The arpeggio: one note at a time, many onsets, sharper attacks.
    CHECK(value(engine, "notes.active") <= 1.0f);
    CHECK(value(engine, "notes.rhythm") > 0.3f);
    CHECK(value(engine, "sonic.sharpness") > chordSharpness + 0.1f);
    // The project saves its block back, notes path relative.
    const auto saved = testsupport::processTempDir() / "sonic" / "saved.json";
    REQUIRE(engine.saveProject(saved).has_value());
    nlohmann::json doc;
    std::ifstream(saved) >> doc;
    REQUIRE(doc.contains("sonic"));
    CHECK(doc["sonic"]["notes"] == "sonic.mid");
}

TEST_CASE("Sonic signals are deterministic: two engines agree, and a seek lands where a play does",
          "[sonic][adr1020]") {
    const auto paths = writeSonicProject();
    const char* names[] = {"sonic.brightness", "sonic.roughness.slow", "sonic.sharpness", "notes.density",
                           "notes.motion", "visual.glow"};
    app::Engine a(app::EngineMode::Offline);
    app::Engine b(app::EngineMode::Offline);
    a.setLiveControl(false);
    b.setLiveControl(false);
    REQUIRE(a.loadProject(paths.project).has_value());
    REQUIRE(b.loadProject(paths.project).has_value());
    playTo(a, 3.0);
    playTo(b, 3.0);
    for (const char* n : names) {
        INFO(n);
        CHECK(value(a, n) == value(b, n));
    }
    // A third engine seeks straight to 3 s and updates that one frame.
    app::Engine c(app::EngineMode::Offline);
    c.setLiveControl(false);
    REQUIRE(c.loadProject(paths.project).has_value());
    c.seekSeconds(3.0);
    FrameTime t;
    t.renderTime = 3.0;
    t.deltaTime = 1.0 / 30.0;
    t.frameIndex = 90;
    c.update(t);
    for (const char* n : names) {
        INFO(n);
        CHECK(value(c, n) == Approx(value(a, n)).margin(1e-5));
    }
}

TEST_CASE("Without a sonic block the subsystem does nothing: no timbre pass, all zeros", "[sonic][adr1020]") {
    const auto paths = writeSonicProject();
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    REQUIRE(engine.loadProject(paths.plain).has_value());
    CHECK(engine.sonicSetup() == nullptr);
    playTo(engine, 1.0);
    CHECK(value(engine, "sonic.energy") == 0.0f);
    CHECK(value(engine, "notes.active") == 0.0f);
    CHECK(value(engine, "visual.glow") == 0.0f); // the mapping runs, on zeros
    // And loading the sonic project after it, then the plain one again, turns it on and off.
    REQUIRE(engine.loadProject(paths.project).has_value());
    CHECK(engine.sonicSetup() != nullptr);
    REQUIRE(engine.loadProject(paths.plain).has_value());
    CHECK(engine.sonicSetup() == nullptr);
    playTo(engine, 0.5);
    CHECK(value(engine, "sonic.energy") == 0.0f);
}

TEST_CASE("A Signal-triggered effect fires on the piece's note-ons, and a seek finds the same fronts",
          "[sonic][adr1061]") {
    const auto paths = writeSonicProject();
    const auto shock = world::EffectInstance::fromJson(nlohmann::json::parse(R"({
        "id": "noteShock", "type": "shockwave", "owner": {"kind": "world"}, "activation": "trigger",
        "trigger": {"source": "signal", "name": "notes.noteOn", "threshold": 0.7}, "timing": {"lifetime": 1.0}})"));
    REQUIRE(shock.has_value());
    const auto fronts = [](const app::Engine& e, double t) {
        std::array<double, 8> out{};
        const std::size_t n = e.triggerClock().lastTriggers(e.effects().front().timing.trigger, {}, t, out);
        return std::vector<double>(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n));
    };
    app::Engine a(app::EngineMode::Offline);
    a.setLiveControl(false);
    REQUIRE(a.loadProject(paths.project).has_value());
    REQUIRE(a.setEffects({*shock}).has_value());
    playTo(a, 3.0);
    // The arpeggio's eight notes (velocity 100/127 = 0.79) from 2 s, every quarter second; the chord's (80/127 =
    // 0.63) are under the threshold. A note is stamped on the first analysis frame at or after it (the bus's own
    // timing, up to one 10.7 ms hop late), so the one at exactly 3 s is not yet out at 3 s.
    const std::vector<double> played = fronts(a, 3.0);
    REQUIRE(played.size() == 4);
    CHECK(played[0] == Approx(2.75).margin(0.011));
    CHECK(played[3] == Approx(2.0).margin(0.011));
    CHECK(played[0] >= 2.75);
    // A fresh engine that seeks straight to 3 s answers the same, before it has played a frame of the piece.
    app::Engine c(app::EngineMode::Offline);
    c.setLiveControl(false);
    REQUIRE(c.loadProject(paths.project).has_value());
    REQUIRE(c.setEffects({*shock}).has_value());
    c.seekSeconds(3.0);
    FrameTime t;
    t.renderTime = 3.0;
    t.deltaTime = 1.0 / 30.0;
    t.frameIndex = 90;
    c.update(t);
    c.update(t); // the first frame registers the name; the host derives it before the next question
    CHECK(fronts(c, 3.0) == played);
}

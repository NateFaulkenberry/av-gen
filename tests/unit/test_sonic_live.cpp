// Live Sonic input (ADR-1025): the live note model, the streaming timbre and its snapshot handoff, the engine wiring,
// device-absent behaviour, and the equivalence of the live path with the file path where it is meaningful.

#include "analysis/analysis_runner.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "app/engine.hpp"
#include "audio/analysis_stream.hpp"
#include "audio/audio_file.hpp"
#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "core/time.hpp"
#include "sonic/live.hpp"
#include "sonic/notes.hpp"
#include "sonic/sonic_runtime.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr std::uint32_t kRate = 48000;
constexpr double kTwoPi = 6.283185307179586;

// A saw whose brightness sweeps: harmonics fade in over the clip (a filter opening), then a quieter tail.
std::vector<float> sweepingSaw(double seconds) {
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double open = 1.0 + 29.0 * std::min(1.0, t / (seconds * 0.8));
        double v = 0.0;
        for (int k = 1; k <= 30; ++k) {
            const double w = std::clamp(open - k + 1.0, 0.0, 1.0);
            v += w * std::sin(kTwoPi * 110.0 * k * t + 0.3 * k) / k;
        }
        out[i] = static_cast<float>(0.25 * v);
    }
    return out;
}

sonic::NoteEvent note(double start, double duration, int key, float velocity) {
    sonic::NoteEvent n;
    n.start = start;
    n.duration = duration;
    n.key = static_cast<std::uint8_t>(key);
    n.pitch = static_cast<float>(key);
    n.velocity = velocity;
    return n;
}

void checkSameContext(const sonic::MusicalContext& a, const sonic::MusicalContext& b) {
    CHECK(a.active == b.active);
    CHECK(a.density == Approx(b.density).margin(1e-5));
    CHECK(a.rhythm == Approx(b.rhythm).margin(1e-5));
    CHECK(a.velocity == Approx(b.velocity).margin(1e-5));
    CHECK(a.pitch == Approx(b.pitch).margin(1e-5));
    CHECK(a.range == Approx(b.range).margin(1e-5));
    CHECK(a.motion == Approx(b.motion).margin(1e-5));
    CHECK(a.direction == Approx(b.direction).margin(1e-5));
    CHECK(a.duration == Approx(b.duration).margin(1e-5));
    CHECK(a.legato == Approx(b.legato).margin(1e-5));
    CHECK(a.regularity == Approx(b.regularity).margin(1e-5));
    CHECK(a.chord == Approx(b.chord).margin(1e-5));
    CHECK(a.tension == Approx(b.tension).margin(1e-5));
    CHECK(a.repetition == Approx(b.repetition).margin(1e-5));
    CHECK(a.phrase == Approx(b.phrase).margin(1e-5));
}

float value(app::Engine& engine, const char* name) {
    const auto id = engine.signals().find(name);
    REQUIRE(id);
    return engine.signals().value(*id);
}

void frames(app::Engine& engine, FixedStepClock& clock, int n) {
    for (int i = 0; i < n; ++i) {
        engine.update(engine.tick(clock));
    }
}

} // namespace

TEST_CASE("Live notes give the same musical context as the same notes read from a file", "[sonic][adr1025]") {
    // A held chord, a legato line under the pedal, a staccato run and a rest: played live at their times, and as a
    // finished note track (what the MIDI file path reads). During and after every note the context agrees, because a
    // held note counts as "held so far" in both.
    const std::vector<sonic::NoteEvent> played{
        note(0.10, 1.20, 57, 0.6), note(0.11, 1.20, 60, 0.6), note(0.12, 1.20, 64, 0.6),
        note(1.50, 0.45, 69, 0.8), note(1.90, 0.45, 71, 0.8), note(2.30, 0.45, 72, 0.7),
        note(3.00, 0.08, 76, 0.9), note(3.15, 0.08, 74, 0.9), note(3.30, 0.08, 72, 0.9), note(3.45, 0.08, 71, 0.9),
        note(5.20, 0.60, 48, 0.5)};
    sonic::NoteTrack file;
    file.notes = played;
    file.finish();

    sonic::LiveNotes live;
    struct Edge {
        double t;
        bool on;
        std::size_t i;
    };
    std::vector<Edge> edges;
    for (std::size_t i = 0; i < played.size(); ++i) {
        edges.push_back({played[i].start, true, i});
        edges.push_back({played[i].end(), false, i});
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.t < b.t; });
    std::size_t next = 0;
    int compared = 0;
    for (double t = 0.0; t <= 7.0; t += 1.0 / 60.0) {
        while (next < edges.size() && edges[next].t <= t) {
            const auto& n = played[edges[next].i];
            if (edges[next].on) {
                live.noteOn(n.start, 0, n.key, n.velocity);
            } else {
                live.noteOff(n.end(), 0, n.key);
            }
            ++next;
        }
        INFO("t = " << t);
        const sonic::MusicalContext a = sonic::contextAt(live.at(t), t);
        const sonic::MusicalContext b = sonic::contextAt(file, t);
        checkSameContext(a, b);
        ++compared;
    }
    CHECK(compared > 400);
}

TEST_CASE("Live note events fire once, in the frame they fall in", "[sonic][adr1025]") {
    sonic::LiveNotes live;
    live.noteOn(1.00, 0, 60, 0.8);
    auto e = sonic::eventsBetween(live.at(1.01), 0.99, 1.01);
    CHECK(e.noteOn);
    CHECK(e.onVelocity == Approx(0.8f));
    CHECK_FALSE(e.noteOff);
    // Held: no note-off however long it is held.
    for (double t = 1.02; t < 3.0; t += 0.02) {
        CHECK_FALSE(sonic::eventsBetween(live.at(t), t - 0.02, t).noteOff);
    }
    live.noteOff(3.005, 0, 60);
    e = sonic::eventsBetween(live.at(3.02), 3.0, 3.02);
    CHECK(e.noteOff);
    CHECK_FALSE(e.noteOn);
    CHECK_FALSE(sonic::eventsBetween(live.at(3.04), 3.02, 3.04).noteOff);
    CHECK(sonic::contextAt(live.at(3.04), 3.04).active == 0);
    CHECK(sonic::contextAt(live.at(3.04), 3.04).duration == Approx(2.005f).margin(1e-4));
}

TEST_CASE("The sustain pedal holds released notes; all-notes-off ends them; a retrigger ends the first",
          "[sonic][adr1025]") {
    sonic::LiveNotes live;
    live.sustain(0.5, 0, true);
    live.noteOn(1.0, 0, 60, 0.7);
    live.noteOn(1.0, 0, 64, 0.7);
    live.noteOff(1.2, 0, 60);
    live.noteOff(1.2, 0, 64);
    CHECK(sonic::contextAt(live.at(2.0), 2.0).active == 2); // the pedal is down
    live.sustain(2.5, 0, false);
    CHECK(sonic::contextAt(live.at(2.6), 2.6).active == 0);
    // A different channel's pedal does not hold this one.
    live.sustain(3.0, 1, true);
    live.noteOn(3.0, 0, 67, 0.7);
    live.noteOff(3.1, 0, 67);
    CHECK(sonic::contextAt(live.at(3.2), 3.2).active == 0);
    // All notes off.
    live.noteOn(4.0, 2, 50, 0.7);
    live.noteOn(4.0, 2, 53, 0.7);
    live.allOff(4.5);
    CHECK(live.held() == 0);
    CHECK(sonic::contextAt(live.at(4.6), 4.6).active == 0);
    // Two note-ons for one key: the first ends, one note sounds.
    live.noteOn(5.0, 0, 72, 0.5);
    live.noteOn(5.3, 0, 72, 0.9);
    CHECK(live.held() == 1);
    CHECK(sonic::contextAt(live.at(5.4), 5.4).active == 1);
}

TEST_CASE("A long live session keeps the note track small", "[sonic][adr1025]") {
    sonic::LiveNotes live;
    double t = 0.0;
    for (int i = 0; i < 20000; ++i) {
        live.noteOn(t, 0, static_cast<std::uint8_t>(48 + i % 24), 0.7f);
        live.noteOff(t + 0.05, 0, static_cast<std::uint8_t>(48 + i % 24));
        t += 0.1;
        static_cast<void>(live.at(t));
    }
    // Ten notes a second and a horizon of about nine seconds: under a hundred notes are kept.
    CHECK(live.track().notes.size() < 120);
    CHECK(live.received() == 20000);
    // The context in the middle of that is the context of the last few seconds.
    CHECK(sonic::contextAt(live.at(t), t).rhythm > 5.0f);
}

TEST_CASE("The snapshot queue is FIFO, bounded, and safe across two threads", "[sonic][adr1025]") {
    sonic::SpscQueue<int, 8> q;
    for (int i = 0; i < 8; ++i) {
        CHECK(q.push(i));
    }
    CHECK_FALSE(q.push(99)); // full: refused, not blocked
    int v = -1;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(q.pop(v));
        CHECK(v == i);
    }
    CHECK_FALSE(q.pop(v));

    sonic::SpscQueue<std::uint64_t, 64> shared;
    constexpr std::uint64_t kCount = 200000;
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount;) {
            if (shared.push(i)) {
                ++i;
            }
        }
    });
    std::uint64_t expected = 0;
    std::uint64_t got = 0;
    while (expected < kCount) {
        if (shared.pop(got)) {
            REQUIRE(got == expected);
            ++expected;
        }
    }
    producer.join();
    CHECK(expected == kCount);
}

TEST_CASE("Live timbre through the analysis thread reaches exactly the file path's character", "[sonic][adr1025]") {
    // The same samples two ways. File: the whole-track analysis and the load-time timbre pass, walked by
    // SonicRuntime::advance. Live: an AnalysisStream written in device-sized blocks, the AnalysisRunner's own
    // thread, the LiveTimbre tap, the snapshot queue, and LiveSonic stepping on the render side.
    const auto mono = sweepingSaw(3.0);
    analysis::AnalyzerConfig config;
    config.sampleRate = kRate;

    const auto file = audio::AudioFile::fromInterleaved(mono, 1, kRate);
    const auto track = analysis::AnalysisTrack::analyze(file, config);
    sonic::SonicSetup setup;
    setup.analyse(track);
    sonic::SonicRuntime fileRuntime;
    signals::SignalBus fileBus;
    fileRuntime.declare(fileBus);
    fileRuntime.advance(setup, track, 10.0);

    audio::AnalysisStream stream(std::size_t{1} << 16);
    sonic::LiveTimbre tap(config);
    tap.setEnabled(true);
    sonic::LiveSonic live;
    signals::SignalBus liveBus;
    live.declare(liveBus);
    live.begin();
    {
        analysis::AnalysisRunner runner(config, stream);
        runner.setTap(&tap);
        stream.markDiscontinuity(0);
        runner.start();
        // Device-sized blocks, drained on the "render" side while they arrive, as the app does.
        std::size_t written = 0;
        double frameSeconds = 0.0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while ((written < mono.size() || tap.produced() < track.frames().size()) &&
               std::chrono::steady_clock::now() < deadline) {
            if (written < mono.size()) {
                const std::size_t n = std::min<std::size_t>(128, mono.size() - written);
                written += stream.write(std::span<const float>(mono.data() + written, n));
            }
            frameSeconds += 0.001;
            live.frame(setup, &tap, liveBus, frameSeconds, sonic::hostNowNs());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        runner.stop();
    }
    live.frame(setup, &tap, liveBus, 100.0, sonic::hostNowNs());
    // The streaming analyzer keeps its last partial window; the file pass ends the same place.
    REQUIRE(tap.produced() == track.frames().size());
    CHECK(tap.dropped() == 0);
    CHECK(live.runtime().consumed() == 0); // live steps; it never walks a track
    for (std::size_t i = 0; i < sonic::kDimensionCount; ++i) {
        INFO(sonic::dimensionName(static_cast<sonic::Dimension>(i)));
        CHECK(live.runtime().medium()[i] == Approx(fileRuntime.medium()[i]).margin(1e-6));
        CHECK(live.runtime().slow()[i] == Approx(fileRuntime.slow()[i]).margin(1e-6));
    }
    // And the sweep was heard: bright at the end.
    CHECK(live.runtime().medium()[static_cast<std::size_t>(sonic::Dimension::Brightness)] > 0.3f);
}

TEST_CASE("A disabled timbre tap does nothing", "[sonic][adr1025]") {
    analysis::AnalyzerConfig config;
    config.sampleRate = kRate;
    analysis::Analyzer analyzer(config);
    analyzer.push(sweepingSaw(0.5));
    sonic::LiveTimbre tap(config);
    analysis::AnalysisFrame frame;
    int frames = 0;
    while (analyzer.pop(frame)) {
        tap.onFrame(frame);
        ++frames;
    }
    CHECK(frames > 10);
    CHECK(tap.produced() == 0);
    CHECK(tap.queued() == 0);
    CHECK(tap.averageMicros() == 0.0);
}

TEST_CASE("Live MIDI through the engine drives notes.* and the smoothing scales the character", "[sonic][adr1025]") {
    app::Engine engine(app::EngineMode::Live);
    engine.setLiveControl(false); // no device: the injected bytes are the keyboard
    FixedStepClock clock(60.0);
    CHECK_FALSE(engine.liveSonic());
    // Off: a note changes nothing sonic.
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 60, 100});
    frames(engine, clock, 3);
    CHECK(value(engine, "notes.active") == 0.0f);

    REQUIRE(engine.setLiveSonic(true).has_value());
    CHECK(engine.liveSonic());
    CHECK(engine.activeSonicSetup() != nullptr); // the default character, with no sonic block
    engine.control().injectMidi(std::array<std::uint8_t, 9>{0x90, 60, 100, 0x90, 64, 90, 0x91, 67, 127});
    frames(engine, clock, 2);
    CHECK(value(engine, "notes.active") == 3.0f);
    CHECK(value(engine, "notes.velocity") == Approx((100.0f + 90.0f + 127.0f) / 3.0f / 127.0f).margin(1e-3));
    CHECK(value(engine, "notes.chord") == Approx(1.0f));
    const auto st = engine.liveSonicStatus();
    CHECK(st.notes == 3);
    CHECK(st.held == 3);
    CHECK(st.midiReceiving);
    CHECK_FALSE(st.audioReceiving); // no audio input, no file
    // Note off (and a velocity-0 note on, which is a note off).
    engine.control().injectMidi(std::array<std::uint8_t, 6>{0x80, 60, 0, 0x90, 64, 0});
    frames(engine, clock, 2);
    CHECK(value(engine, "notes.active") == 1.0f);
    // Sustain pedal on channel 2 holds its note past its note-off.
    engine.control().injectMidi(std::array<std::uint8_t, 6>{0xB1, 64, 127, 0x81, 67, 0});
    frames(engine, clock, 2);
    CHECK(value(engine, "notes.active") == 1.0f);
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB1, 64, 0});
    frames(engine, clock, 2);
    CHECK(value(engine, "notes.active") == 0.0f);

    engine.setLiveSonicSmoothing(2.0f);
    CHECK(engine.liveSonicSmoothing() == 2.0f);
    CHECK(engine.sonicRuntime().timeScale() == 2.0f);

    // Off again: nothing live is published, and the file path's zeros return.
    REQUIRE(engine.setLiveSonic(false).has_value());
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 72, 100});
    frames(engine, clock, 2);
    CHECK(value(engine, "notes.active") == 0.0f);
}

TEST_CASE("With no devices, live input does nothing and nothing fails", "[sonic][adr1025]") {
    app::Engine engine(app::EngineMode::Live);
    engine.setLiveControl(false);
    FixedStepClock clock(60.0);
    REQUIRE(engine.setLiveSonic(true).has_value());
    CHECK(engine.liveTimbre() == nullptr); // no runner, no tap
    frames(engine, clock, 30);
    CHECK(value(engine, "sonic.energy") == 0.0f);
    CHECK(value(engine, "notes.active") == 0.0f);
    const auto st = engine.liveSonicStatus();
    CHECK_FALSE(st.audioReceiving);
    CHECK_FALSE(st.midiReceiving);
    CHECK(st.audioFrames == 0);
    // An audio input that does not exist is an error, not a crash, and live input keeps running.
    CHECK_FALSE(engine.useAudioInput("no device is called this 7f3c").has_value());
    CHECK_FALSE(engine.hasLiveInput());
    frames(engine, clock, 5);
    CHECK(engine.liveSonic());
    // The offline engine refuses it: a render never sees live input.
    app::Engine offline(app::EngineMode::Offline);
    offline.setLiveControl(false);
    CHECK_FALSE(offline.setLiveSonic(true).has_value());
    CHECK_FALSE(offline.liveSonic());
}

TEST_CASE("A project with sonic.live turns live input on in the live editor, and any other project turns it off",
          "[sonic][adr1025]") {
    const auto dir = testsupport::processTempDir() / "sonic-live";
    std::filesystem::create_directories(dir);
    const auto write = [&](const char* name, nlohmann::json sonic) {
        nlohmann::json p = {{"format", "avgen-project"}, {"version", 4}};
        if (!sonic.is_null()) {
            p["sonic"] = std::move(sonic);
        }
        std::ofstream(dir / name) << p.dump(1);
        return dir / name;
    };
    const auto liveProject = write("live.json", {{"live", true}});
    const auto fileProject = write("file.json", nlohmann::json::object());
    const auto plain = write("plain.json", nullptr);
    const auto bad = write("bad.json", {{"live", "yes"}});

    // The editor's engine (live control on). With live control off -- a render, a trace -- nothing turns on.
    app::Engine editor(app::EngineMode::Live);
    REQUIRE(editor.loadProject(liveProject).has_value());
    CHECK(editor.liveSonic());
    CHECK(editor.sonicSetup()->live);
    REQUIRE(editor.loadProject(fileProject).has_value());
    CHECK_FALSE(editor.liveSonic());
    REQUIRE(editor.loadProject(liveProject).has_value());
    CHECK(editor.liveSonic());
    REQUIRE(editor.loadProject(plain).has_value());
    CHECK_FALSE(editor.liveSonic());
    // A malformed flag is a project warning, and live input stays off.
    static_cast<void>(editor.loadProject(bad));
    CHECK_FALSE(editor.liveSonic());

    app::Engine scratch(app::EngineMode::Live);
    scratch.setLiveControl(false);
    REQUIRE(scratch.loadProject(liveProject).has_value());
    CHECK_FALSE(scratch.liveSonic());
    app::Engine offline(app::EngineMode::Offline);
    REQUIRE(offline.loadProject(liveProject).has_value());
    CHECK_FALSE(offline.liveSonic());

    // The flag survives a save.
    const auto saved = dir / "saved.json";
    REQUIRE(editor.loadProject(liveProject).has_value());
    REQUIRE(editor.saveProject(saved).has_value());
    nlohmann::json doc;
    std::ifstream(saved) >> doc;
    CHECK(doc["sonic"]["live"] == true);
}

TEST_CASE("A real CoreMIDI source reaches notes.* through the editor's MIDI input", "[sonic][adr1025][device]") {
    if (!control::hasMidiBackend()) {
        SKIP("no MIDI backend on this platform");
    }
    control::MidiVirtualSource source;
    const std::string name = "avgen-test-live-sonic-" + std::to_string(::getpid());
    if (auto r = source.open(name); !r) {
        SKIP("cannot create a virtual MIDI source here: " << r.error().message);
    }
    app::Engine engine(app::EngineMode::Live);
    auto map = engine.control().map();
    map.midiEnabled = true;
    map.midiFilter = name;
    engine.control().setMap(map);
    REQUIRE(engine.control().status().midiOpen);
    REQUIRE(engine.setLiveSonic(true).has_value());
    FixedStepClock clock(60.0);
    REQUIRE(source.send(std::array<std::uint8_t, 3>{0x90, 62, 110}).has_value());
    bool heard = false;
    for (int i = 0; i < 400 && !heard; ++i) {
        frames(engine, clock, 1);
        heard = value(engine, "notes.active") == 1.0f;
        if (!heard) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK(heard);
    CHECK(value(engine, "notes.velocity") == Approx(110.0f / 127.0f).margin(1e-3));
    CHECK(engine.liveSonicStatus().lastKey == 62);
}

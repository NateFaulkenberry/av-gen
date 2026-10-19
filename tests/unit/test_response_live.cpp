// ADR-1510 on the live path: the loudness reference is session state. A performer switches scenes mid-song (the
// dropdown, PageDown, a program change); the new scene must not restart the reference's warm-up and read the running
// song as loud. And the first seconds of a set read their first bars high without holding at 1.
//
// Diagnostics: AVGEN_RESPONSE_LIVE_AUDIO=<wav> plays that file instead of the synthetic master, and
// AVGEN_RESPONSE_LIVE_TRACE=<csv> writes the switch run's frames (time, reference dB, the five levels, carried and
// restarted) -- the evidence in docs/development/response-range-report.md.

#include "analysis/analyzer.hpp"
#include "analysis/causal_onsets.hpp"
#include "app/engine.hpp"
#include "control/midi.hpp"
#include "core/time.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/live.hpp"
#include "sonic/sonic_runtime.hpp"
#include "support/mastered_song.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path example(const char* rel) {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / rel;
}

sonic::SonicSetup setupOf(const char* project) {
    std::ifstream f(example(project));
    const auto doc = nlohmann::json::parse(f);
    auto s = sonic::SonicSetup::fromJson(doc.at("sonic"), example(project).parent_path());
    REQUIRE(s.has_value());
    return *s;
}

constexpr std::array<const char*, 5> kLevels{"response.bass", "response.level", "response.sustain",
                                             "response.levelSlow", "response.intensity"};

// A live session fed frame by frame, as the app does: the analyser's frames through the timbre tap, drained once a
// render frame (30 fps).
struct Session {
    sonic::LiveTimbre tap;
    sonic::LiveSonic live;
    signals::SignalBus bus;
    std::array<signals::SignalId, kLevels.size()> ids{};
    explicit Session(const analysis::AnalyzerConfig& config) : tap(config) {
        tap.setEnabled(true);
        live.declare(bus);
        for (std::size_t i = 0; i < kLevels.size(); ++i) {
            ids[i] = *bus.find(kLevels[i]);
        }
        live.begin();
    }
    float value(std::size_t i) const { return bus.value(ids[i]); }
};

audio::AudioFile songAudio() {
    if (const char* path = std::getenv("AVGEN_RESPONSE_LIVE_AUDIO")) {
        auto f = audio::AudioFile::load(path);
        REQUIRE(f.has_value());
        return *f;
    }
    return testsupport::makeMasteredSong(0.0f).file;
}

// The analysis thread's part (AnalysisRunner): the causal onsets and levels each frame carries to the tap.
struct Runner {
    analysis::AnalyzerConfig config;
    analysis::Analyzer analyzer;
    analysis::CausalOnsetDetector causal;
    explicit Runner(const analysis::AnalyzerConfig& c) : config(c), analyzer(c) {}
    template <typename F>
    void push(std::span<const float> samples, F&& each) {
        analyzer.push(samples);
        analysis::AnalysisFrame frame;
        while (analyzer.pop(frame)) {
            causal.process(frame, static_cast<float>(config.sampleRate) / static_cast<float>(config.windowSize),
                           static_cast<double>(config.hopSize) / static_cast<double>(config.sampleRate));
            each(frame);
        }
    }
};

struct SwitchRun {
    float refBefore = 0.0f, refAfter = 0.0f;
    std::array<double, kLevels.size()> before{}, after{}; // the 3 s each side of the switch, carried session
    double restartedLevel = 0.0;                          // response.level, the 3 s after, a session begun there
};

double meanOf(const std::vector<float>& v) {
    double s = 0.0;
    for (const float x : v) {
        s += x;
    }
    return v.empty() ? 0.0 : s / static_cast<double>(v.size());
}

// Plays `file` live from 0, switching from Sonic Live to Bit Ocean at `switchAt`, beside a second session begun at
// the switch (the defect a switch must not have).
SwitchRun playSwitch(const audio::AudioFile& file, double switchAt, std::ofstream* trace) {
    analysis::AnalyzerConfig config;
    config.sampleRate = file.sampleRate();
    Runner runner(config);
    const sonic::SonicSetup sceneA = setupOf("sonic-garden/sonic-live.json");
    const sonic::SonicSetup sceneB = setupOf("sonic-abstract/bit-ocean.json");
    const double endAt = std::min(file.durationSeconds(), switchAt + 20.0);
    Session carried(config);
    std::unique_ptr<Session> restarted;
    const auto mono = file.mono();
    const double frameSeconds = 1.0 / 30.0;
    const auto block = static_cast<std::size_t>(frameSeconds * config.sampleRate);
    std::array<std::vector<float>, kLevels.size()> before, after;
    std::vector<float> fresh;
    SwitchRun r;
    double t = 0.0;
    for (std::size_t at = 0; at < mono.size() && t < endAt; at += block) {
        const bool switched = t >= switchAt;
        if (switched && !restarted) {
            r.refBefore = carried.live.runtime().response().referenceDb();
            restarted = std::make_unique<Session>(config);
        }
        runner.push(mono.subspan(at, std::min(block, mono.size() - at)), [&](const analysis::AnalysisFrame& f) {
            carried.tap.onFrame(f);
            if (restarted) {
                restarted->tap.onFrame(f);
            }
        });
        t += frameSeconds;
        carried.live.frame(switched ? sceneB : sceneA, &carried.tap, carried.bus, t, 0);
        if (restarted) {
            restarted->live.frame(sceneB, &restarted->tap, restarted->bus, t - switchAt, 0);
        }
        if (switched && t < switchAt + 1.0) {
            r.refAfter = carried.live.runtime().response().referenceDb();
        }
        for (std::size_t i = 0; i < kLevels.size(); ++i) {
            if (t >= switchAt - 3.0 && t < switchAt) {
                before[i].push_back(carried.value(i));
            } else if (t >= switchAt && t < switchAt + 3.0) {
                after[i].push_back(carried.value(i));
            }
        }
        if (restarted && t < switchAt + 3.0) {
            fresh.push_back(restarted->value(1));
        }
        if (trace != nullptr) {
            *trace << t << ',' << (switched ? "B" : "A") << ',' << carried.live.runtime().response().referenceDb();
            for (std::size_t i = 0; i < kLevels.size(); ++i) {
                *trace << ',' << carried.value(i);
            }
            *trace << ',' << (restarted ? restarted->live.runtime().response().referenceDb() : 0.0f) << ','
                   << (restarted ? restarted->value(1) : 0.0f) << '\n';
        }
        carried.bus.clearEvents();
        if (restarted) {
            restarted->bus.clearEvents();
        }
    }
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        r.before[i] = meanOf(before[i]);
        r.after[i] = meanOf(after[i]);
    }
    r.restartedLevel = meanOf(fresh);
    return r;
}

} // namespace

TEST_CASE("A live scene switch carries the loudness reference; a restarted session would not", "[sonic][adr1510]") {
    const audio::AudioFile file = songAudio();
    const bool synthetic = std::getenv("AVGEN_RESPONSE_LIVE_AUDIO") == nullptr;
    const testsupport::MasteredSong layout = testsupport::makeMasteredSong(0.0f);
    std::ofstream trace;
    if (const char* out = std::getenv("AVGEN_RESPONSE_LIVE_TRACE")) {
        trace.open(out);
        trace << "time,scene,referenceDb";
        for (const char* n : kLevels) {
            trace << ',' << n;
        }
        trace << ",restartedReferenceDb,restartedLevel\n";
    }
    // In the drop (or a third of the way into a real file): nothing jumps at the switch.
    const double inDrop = synthetic ? layout.dropStart + 8.0 : file.durationSeconds() / 3.0;
    const SwitchRun d = playSwitch(file, inDrop, trace.is_open() ? &trace : nullptr);
    INFO("reference " << d.refBefore << " -> " << d.refAfter << " dB");
    CHECK(std::fabs(d.refAfter - d.refBefore) < 1.0f);
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        INFO(kLevels[i] << ": 3 s before " << d.before[i] << ", 3 s after " << d.after[i]);
        CHECK(std::fabs(d.after[i] - d.before[i]) < 0.15);
    }
    if (synthetic) {
        // In the breakdown: the carried session still reads it as the quiet part of the song; a session begun there
        // would hear it against itself, as ordinary loudness.
        const SwitchRun b = playSwitch(file, 0.5 * (layout.breakdownStart + layout.dropStart), nullptr);
        INFO("breakdown switch: carried level " << b.after[1] << ", restarted " << b.restartedLevel);
        CHECK(b.after[1] < 0.15);
        CHECK(b.restartedLevel > b.after[1] + 0.2);
    }
}

TEST_CASE("The first bars of a live set read high without holding at 1", "[sonic][adr1510]") {
    const testsupport::MasteredSong song = testsupport::makeMasteredSong(0.0f);
    analysis::AnalyzerConfig config;
    config.sampleRate = song.file.sampleRate();
    Runner runner(config);
    const sonic::SonicSetup scene = setupOf("sonic-garden/sonic-live.json");
    Session s(config);
    const auto mono = song.file.mono();
    const double frameSeconds = 1.0 / 30.0;
    const auto block = static_cast<std::size_t>(frameSeconds * config.sampleRate);
    int loudFrames = 0, pinned = 0, framesSeen = 0;
    float firstMax = 0.0f;
    double t = 0.0;
    for (std::size_t at = 0; at < mono.size() && t < song.verseStart + 6.0; at += block) {
        runner.push(mono.subspan(at, std::min(block, mono.size() - at)),
                    [&](const analysis::AnalysisFrame& f) { s.tap.onFrame(f); });
        t += frameSeconds;
        s.live.frame(scene, &s.tap, s.bus, t, 0);
        if (t > song.verseStart + 0.5) {
            ++framesSeen;
            const float level = s.value(1);
            firstMax = std::max(firstMax, level);
            loudFrames += level > 0.5f ? 1 : 0;
            pinned += level > 0.98f ? 1 : 0;
        }
        s.bus.clearEvents();
    }
    INFO("first 6 s: max level " << firstMax << ", " << loudFrames << " of " << framesSeen << " frames above 0.5, "
                                 << pinned << " at 1");
    CHECK(firstMax > 0.6f);                 // the first bars are heard
    CHECK(loudFrames > framesSeen / 4);     // ...often
    CHECK(pinned < framesSeen / 4);         // ...and not held at the top while the reference warms
}

TEST_CASE("Loading another live project keeps the live session (and its reference) running", "[sonic][adr1510]") {
    app::Engine engine(app::EngineMode::Live);
    engine.setLiveControl(false);
    FixedStepClock clock(60.0);
    REQUIRE(engine.loadProject(example("sonic-garden/sonic-live.json")).has_value());
    REQUIRE(engine.setLiveSonic(true).has_value());
    REQUIRE(engine.liveSonic());
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 60, 100});
    for (int i = 0; i < 3; ++i) {
        engine.update(engine.tick(clock));
    }
    REQUIRE(engine.liveSonicStatus().notes == 1);
    // The switcher's open: another live scene. LiveSonic::begin (which resets the runtime, and with it the
    // reference) clears the status; a carried session keeps it.
    REQUIRE(engine.loadProject(example("sonic-abstract/bit-ocean.json")).has_value());
    for (int i = 0; i < 3; ++i) {
        engine.update(engine.tick(clock));
    }
    CHECK(engine.liveSonic());
    CHECK(engine.liveSonicStatus().notes == 1);
}

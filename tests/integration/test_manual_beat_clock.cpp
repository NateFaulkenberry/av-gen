// The manual beat clock (ADR-1783): the tempo override plus one hand-marked beat is the file's beat clock.
//
//   * marked, `beat.source` reads 4 and `beat.pulse` lands on anchor + k x 60 / bpm, whatever the analysis found;
//   * the marked beat is beat 1 of a bar (`beat.count` is a multiple of 4 there);
//   * the mark survives save -> load -> save, inside `transport.tempo`;
//   * clearing the mark, or the override, returns the clock to the analysed grid (`beat.source` 2).

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFps = 60.0;

std::filesystem::path clickWav(const std::filesystem::path& dir, float bpm, double seconds) {
    std::filesystem::create_directories(dir);
    constexpr std::uint32_t kRate = 48000;
    const auto mono = testsupport::clickTrack(bpm, kRate, static_cast<std::size_t>(seconds * kRate));
    const auto file = audio::AudioFile::fromInterleaved(mono, 1, kRate);
    const auto path = dir / "clicks.wav";
    REQUIRE(file.writeWav(path).has_value());
    return path;
}

struct Run {
    std::vector<double> pulses;
    std::vector<float> sources;
    std::vector<float> countAtPulse;
};

Run play(app::Engine& engine, double seconds) {
    FixedStepClock clock(kFps);
    Run out;
    engine.setKeepFrameEvents(true); // an event is cleared by the update that raised it (ADR-1720)
    const auto& ts = engine.timeSignals();
    for (int i = 0; i < static_cast<int>(seconds * kFps); ++i) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        out.sources.push_back(engine.signals().value(ts.clockSource));
        const std::vector<float>& fired = engine.frameEvents();
        if (ts.beatPulse < fired.size() && fired[ts.beatPulse] > 0.0f) {
            out.pulses.push_back(time.renderTime);
            out.countAtPulse.push_back(engine.signals().value(ts.beatCount));
        }
    }
    return out;
}

} // namespace

TEST_CASE("The manual beat clock: a tempo and one marked beat are the file's beat clock",
          "[tempo][engine][adr1783]") {
    const auto dir = testsupport::processTempDir() / "manual-beat-clock";
    const auto wav = clickWav(dir, 120.0f, 8.0);

    {
        app::Engine analysed(app::EngineMode::Offline);
        REQUIRE(analysed.loadAudio(wav).has_value());
        REQUIRE_FALSE(analysed.manualBeatClock());
        CHECK(play(analysed, 1.0).sources.back() == 2.0f); // the analysed grid
    }

    // A deliberately different clock: 100 BPM through 0.3 s. The analysis says 120; the mark wins.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    engine.setTempoOverride(100.0);
    engine.setBeatAnchor(0.3);
    REQUIRE(engine.manualBeatClock());
    const Run manual = play(engine, 6.0);
    CHECK(manual.sources.back() == 4.0f);
    REQUIRE(manual.pulses.size() >= 8);
    for (std::size_t k = 0; k < manual.pulses.size(); ++k) {
        // A pulse is the first frame at or after the beat.
        const double beat = 0.3 + 0.6 * std::round((manual.pulses[k] - 0.3) / 0.6);
        INFO("pulse at " << manual.pulses[k]);
        CHECK(manual.pulses[k] - beat >= -1e-9);
        CHECK(manual.pulses[k] - beat < 1.0 / kFps + 1e-9);
        // The marked beat is beat 1 of a bar: every fourth beat from it is a downbeat.
        const auto index = static_cast<long>(std::llround((beat - 0.3) / 0.6));
        CHECK(static_cast<long>(manual.countAtPulse[k]) % 4 == ((index % 4) + 4) % 4);
    }

    // Save, load, save: the mark is part of the override.
    const auto project = dir / "p.avgen";
    REQUIRE(engine.saveProject(project).has_value());
    nlohmann::json saved;
    {
        std::ifstream in(project);
        saved = nlohmann::json::parse(in);
    }
    CHECK(saved["transport"]["tempo"]["bpm"].get<double>() == 100.0);
    CHECK_THAT(saved["transport"]["tempo"]["anchor"].get<double>(), WithinAbs(0.3, 1e-12));
    {
        app::Engine reloaded(app::EngineMode::Offline);
        REQUIRE(reloaded.loadProject(project).has_value());
        REQUIRE(reloaded.beatAnchor().has_value());
        CHECK_THAT(*reloaded.beatAnchor(), WithinAbs(0.3, 1e-12));
        CHECK(reloaded.manualBeatClock());
    }

    // Clearing the mark returns the analysed grid; the override alone is only the tempo number.
    engine.clearBeatAnchor();
    CHECK_FALSE(engine.manualBeatClock());
    CHECK(engine.tempoOverride().available);
    {
        FixedStepClock clock(kFps);
        engine.update(engine.tick(clock));
        CHECK(engine.signals().value(engine.timeSignals().clockSource) == 2.0f);
    }

    // Clearing the override clears a mark with it.
    engine.setBeatAnchor(0.3);
    REQUIRE(engine.manualBeatClock());
    engine.clearTempoOverride();
    CHECK_FALSE(engine.beatAnchor().has_value());
}

TEST_CASE("Marking a beat with no override keeps the tempo in force", "[tempo][engine][adr1783]") {
    const auto dir = testsupport::processTempDir() / "manual-beat-clock-mark";
    const auto wav = clickWav(dir, 120.0f, 6.0);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    play(engine, 0.5); // the analysed tempo is in force once a frame has been read
    const double inForce = engine.resolvedTempo().second;
    REQUIRE(inForce > 0.0);
    engine.setBeatAnchor(0.5);
    REQUIRE(engine.manualBeatClock());
    CHECK_THAT(engine.tempoOverride().bpm, WithinAbs(inForce, 1e-9));
    engine.setBeatAnchor(-1.0); // refused: not a time
    CHECK_THAT(*engine.beatAnchor(), WithinAbs(0.5, 1e-12));
}

// The ADR-897/898 features on the signal bus: declared under their own names beside the auto-gained
// bands, published every frame, events merged across a render frame's analysis frames so none is lost
// at a low frame rate, and a route on each moving a parameter.

#include "ai/engine_tools.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "support/groove.hpp"
#include "support/temp_dir.hpp"
#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace avgen;

namespace {

std::filesystem::path grooveWav() {
    static const std::filesystem::path path = [] {
        testsupport::GrooveSpec spec;
        spec.bars = 12;
        spec.kicklessBars = {6, 7};
        spec.offbeatHats = true;
        const auto g = testsupport::makeGroove(spec);
        const auto p = testsupport::processTempDir() / "audio_features_bus.wav";
        REQUIRE(g.file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

struct Counts {
    std::size_t low = 0;
    std::size_t mid = 0;
    std::size_t high = 0;
    std::size_t beats = 0;
    double lastPublished = -1.0;
};

// What reached the published frame, render frame by render frame, over `seconds`.
Counts published(double fps, double seconds) {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(grooveWav()).has_value());
    FixedStepClock clock(fps);
    Counts c;
    double lastPublished = -1.0;
    for (int i = 0; i < static_cast<int>(seconds * fps); ++i) {
        engine.update(engine.tick(clock));
        // A render frame that consumed no new analysis frame republishes nothing (the bus drops its
        // events); the published frame is counted once, on the frame its batch arrived.
        const analysis::AnalysisFrame& f = engine.latestFrame();
        if (!engine.hasFrame() || f.timeSeconds == lastPublished) {
            continue;
        }
        lastPublished = f.timeSeconds;
        c.low += f.lowOnset ? 1u : 0u;
        c.mid += f.midOnset ? 1u : 0u;
        c.high += f.highOnset ? 1u : 0u;
        c.beats += f.beat ? 1u : 0u;
    }
    c.lastPublished = lastPublished;
    return c;
}

// Every event on the track's own frames up to `until` -- what the published batches must add up to.
Counts inTrack(double until) {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(grooveWav()).has_value());
    Counts c;
    for (const analysis::AnalysisFrame& f : engine.track()->frames()) {
        if (f.timeSeconds > until) {
            break;
        }
        c.low += f.lowOnset ? 1u : 0u;
        c.mid += f.midOnset ? 1u : 0u;
        c.high += f.highOnset ? 1u : 0u;
        c.beats += f.beat ? 1u : 0u;
    }
    return c;
}

} // namespace

TEST_CASE("The flat-master features and band onsets are on the bus under their own names",
          "[integration][signals][flatmaster]") {
    app::Engine engine(app::EngineMode::Offline);
    for (const char* name : {"audio.bassLevel", "audio.lowMidLevel", "audio.midLevel", "audio.highMidLevel",
                             "audio.trebleLevel", "audio.energy", "audio.onsetRate", "audio.width",
                             "audio.onsetLow", "audio.onsetMid", "audio.onsetHigh"}) {
        INFO(name);
        REQUIRE(engine.signals().find(name).has_value());
    }
    // The auto-gained bands are still there, unchanged in name.
    for (const char* name : {"audio.bass", "audio.lowMid", "audio.mid", "audio.highMid", "audio.treble"}) {
        CHECK(engine.signals().find(name).has_value());
    }
    for (const char* name : {"audio.onsetLow", "audio.onsetMid", "audio.onsetHigh"}) {
        CHECK(engine.signals().info(*engine.signals().find(name)).isEvent);
    }

    REQUIRE(engine.loadAudio(grooveWav()).has_value());
    FixedStepClock clock(60.0);
    float energy = 0.0f;
    float bassLevel = 0.0f;
    float rate = 0.0f;
    float width = -1.0f;
    for (int i = 0; i < 60 * 6; ++i) {
        engine.update(engine.tick(clock));
        energy = std::max(energy, engine.signals().value(*engine.signals().find("audio.energy")));
        bassLevel = std::max(bassLevel, engine.signals().value(*engine.signals().find("audio.bassLevel")));
        rate = std::max(rate, engine.signals().value(*engine.signals().find("audio.onsetRate")));
        width = std::max(width, engine.signals().value(*engine.signals().find("audio.width")));
    }
    CHECK(energy > 0.2f);
    CHECK(bassLevel > 0.5f);
    CHECK(rate > 2.0f);   // kick, clap and hat: more than one hit a beat
    CHECK(width > 0.0f);  // the groove's hats and lead are panned
}

TEST_CASE("No band onset or beat is lost between two render frames, at 30 fps or at 120",
          "[integration][signals][onsets]") {
    constexpr double kSeconds = 20.0;
    for (const double fps : {30.0, 120.0}) {
        const Counts seen = published(fps, kSeconds);
        const Counts truth = inTrack(seen.lastPublished);
        REQUIRE(truth.low > 30);
        REQUIRE(truth.beats > 30);
        INFO(fps << " fps: low " << seen.low << "/" << truth.low << ", mid " << seen.mid << "/" << truth.mid
                 << ", high " << seen.high << "/" << truth.high << ", beats " << seen.beats << "/" << truth.beats);
        CHECK(seen.low == truth.low);
        CHECK(seen.mid == truth.mid);
        CHECK(seen.high == truth.high);
        // Before ADR-898 only the broadband onset was merged, and `audio.beat` kept a beat only when
        // it fell on the batch's last analysis frame -- about one in three at 30 fps.
        CHECK(seen.beats == truth.beats);
    }
}

TEST_CASE("A route on audio.onsetLow pulses a parameter on the kicks and not in the pull-back",
          "[integration][signals][onsets]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(grooveWav()).has_value());
    params::ModRoute route{.source = "audio.onsetLow", .target = "camera/height", .amount = 4.0f};
    route.chain.envelope = params::EnvelopeMode::PeakHold;
    route.chain.envelopeHoldMs = 30.0f;
    route.chain.envelopeFallPerSecond = 20.0f;
    engine.modulator().addRoute(route);
    engine.rebind();
    auto* height = engine.params().find("camera/height");
    REQUIRE(height != nullptr);
    const float base = height->baseComponent(0);
    FixedStepClock clock(60.0);
    float grooving = base;
    float pullBack = base;
    // Bars 6 and 7 (0-based) have no kick: 0.5 s lead-in + 12 s .. + 16 s at 120 BPM.
    for (int i = 0; i < 60 * 20; ++i) {
        const FrameTime t = engine.tick(clock);
        engine.update(t);
        const float h = height->finalComponent(0);
        if (t.renderTime > 2.0 && t.renderTime < 10.0) {
            grooving = std::max(grooving, h);
        }
        if (t.renderTime > 12.8 && t.renderTime < 16.3) {
            pullBack = std::max(pullBack, h);
        }
    }
    CHECK(grooving > base + 0.2f);
    CHECK(pullBack < base + 0.05f);
}

TEST_CASE("audio.get_analysis reports musical time and the flat-master measures", "[integration][signals][ai]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(grooveWav()).has_value());
    FixedStepClock clock(60.0);
    for (int i = 0; i < 60 * 3; ++i) { // three seconds: past the first downbeat
        engine.update(engine.tick(clock));
    }
    ai::ToolRegistry registry;
    ai::registerEngineTools(registry);
    ai::ToolContext ctx(engine);
    const ai::ToolResult r = registry.invoke("audio.get_analysis", nlohmann::json::object(), ctx);
    REQUIRE(r.success);
    REQUIRE(r.value.contains("musical"));
    CHECK(r.value["musical"]["beatsPerBar"] == 4);
    CHECK(r.value["musical"]["downbeatBeat"] == engine.meter().downbeat);
    CHECK(r.value["musical"]["beats"].get<double>() == engine.musicalBeats());
    CHECK(r.value["musical"]["beats"].get<double>() > 3.0); // 2.5 s past a downbeat at 0.5 s, 120 BPM
    CHECK(r.value["energy"].get<float>() > 0.0f);
    CHECK(r.value["bandLevels"].size() == 5);
    CHECK(r.value["onsets"].contains("low"));
}

TEST_CASE("Every new signal is pickable as a route source under a readable name", "[integration][signals][ui]") {
    // UI reach: the Modulation panel's route-source picker lists `ui::routeSourceItems(bus)`; each new
    // signal must appear there with the words a person would look for, not only its code name.
    app::Engine engine(app::EngineMode::Offline);
    const std::vector<std::string> items = ui::routeSourceItems(engine.signals());
    REQUIRE(items.size() == engine.signals().size());
    const std::vector<std::pair<const char*, const char*>> expected{
        {"audio.onsetLow", "kick"},          {"audio.onsetMid", "snare"},
        {"audio.onsetHigh", "hats"},         {"audio.onsetRate", "density"},
        {"audio.energy", "energy"},          {"audio.bassLevel", "bass level"},
        {"audio.trebleLevel", "treble level"}, {"audio.width", "stereo width"},
        {"section.index", "section number"}, {"section.progress", "progress"},
        {"section.energy", "energy of the current section"}, {"section.change", "section change"},
        {"beat.count", "bar 1, beat 1"},     {"beat.bar", "position in the bar"}};
    for (const auto& [name, words] : expected) {
        INFO(name);
        const auto id = engine.signals().find(name);
        REQUIRE(id.has_value());
        const std::string& item = items[*id];
        CHECK(item.starts_with(name)); // typing the name still finds it
        CHECK(item.find(words) != std::string::npos);
    }
    // A signal without a label is listed by its name alone.
    const auto rms = engine.signals().find("audio.rms");
    REQUIRE(rms.has_value());
    CHECK(items[*rms] == "audio.rms");
    // And the assistant's signal search finds them by what they are.
    ai::ToolRegistry registry;
    ai::registerEngineTools(registry);
    ai::ToolContext ctx(engine);
    const ai::ToolResult r = registry.invoke("signal.list", nlohmann::json{{"query", "kick"}}, ctx);
    REQUIRE(r.success);
    REQUIRE(r.value["signals"].size() >= 1);
    CHECK(r.value["signals"][0]["name"] == "audio.onsetLow");
    CHECK(r.value["signals"][0]["label"] == "kick (low-band onset)");
}

// ADR-1168: a seek lands the control layer where a play does -- the scene states, the macros their presets set,
// an interpret source's output and a bounded integrating route driving the camera -- because the seek replays the
// sources, the states and the routes from zero on the play's frame grid.
//
// The control arm is the seek without that replay (the stats show it ran; a project with no states never runs it).

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kRate = 44100;
constexpr double kFps = 30.0;

std::filesystem::path writeProject(bool withStates) {
    const auto dir = testsupport::processTempDir() / (withStates ? "control_seek" : "control_seek_nostates");
    std::filesystem::create_directories(dir);
    {
        const std::size_t frames = 12 * kRate;
        auto tone = testsupport::sine(55.0f, kRate, frames, 0.5f);
        auto clicks = testsupport::clickTrack(120.0f, kRate, frames);
        std::vector<float> mono(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            const float swell = 0.5f + 0.5f * std::sin(static_cast<float>(i) / static_cast<float>(kRate) * 1.1f);
            mono[i] = tone[i] * swell + clicks[i] * 0.6f;
        }
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
        REQUIRE(file.writeWav(dir / "music.wav").has_value());
    }
    {
        std::ofstream scene(dir / "scene.json");
        scene << R"({ "format": "avgen-scene", "version": 1, "name": "control-seek",
  "camera": { "mode": 1, "position": [0, 2, 10], "target": [0, 1, 0], "fov": 45 },
  "nodes": [ { "name": "box", "kind": "procedural", "procedural": {
    "source": { "kind": "box", "size": [1, 1, 1] }, "distribution": { "kind": "single" } } } ] })";
    }
    nlohmann::json p = nlohmann::json::parse(R"({
  "format": "avgen-project", "version": 4,
  "assets": { "scene": { "kind": "composition", "path": "scene.json" }, "audio": { "path": "music.wav" } },
  "parameters": {},
  "sources": [ { "kind": "interpret", "name": "listen", "settings": { "mappings": [
      { "name": "drive", "combine": "mean", "inputs": [ { "signal": "audio.rms", "weight": 1.0 } ],
        "bias": 0.0, "gain": 2.0, "curve": 1.0 } ] } } ],
  "worldMacros": [ { "name": "speed", "label": "SPEED", "default": 0.0, "targets": [] } ],
  "routes": [
    { "source": "macro.speed", "target": "camera/position", "component": 0, "op": "add", "amount": 3.0,
      "chain": { "integrate": true, "integrateMin": -50.0, "integrateMax": 50.0 } },
    { "source": "visual.drive", "target": "camera/target", "component": 1, "op": "add", "amount": 2.0,
      "chain": { "attackMs": 400, "decayMs": 1500 } },
    { "source": "visual.drive", "target": "camera/position", "component": 2, "op": "add", "amount": 4.0,
      "chain": { "integrate": true, "integrateMin": 0.0, "integrateMax": 30.0 } } ],
  "presets": [ { "name": "calm", "values": { "macros/speed": [0.2], "camera/fov": [40] } },
               { "name": "fast", "values": { "macros/speed": [1.0], "camera/fov": [60] } } ],
  "render": { "width": 64, "height": 36, "fps": 30 } })");
    if (withStates) {
        p["states"] = nlohmann::json::parse(R"({ "initial": "Calm", "states": [
      { "name": "Calm", "preset": "calm", "transition": { "seconds": 1.0, "easing": "smooth" },
        "triggers": [ { "kind": "elapsed", "threshold": 2.0, "from": "Fast" } ] },
      { "name": "Fast", "preset": "fast", "transition": { "seconds": 1.5, "easing": "smooth" },
        "triggers": [ { "kind": "signal", "signal": "visual.drive", "threshold": 0.3, "from": "Calm", "hold": true } ] } ] })");
    }
    std::ofstream(dir / "project.json") << p.dump(1);
    return dir / "project.json";
}

struct Pose {
    std::vector<float> values;
    int state = -1;
};

Pose pose(app::Engine& e) {
    Pose p;
    for (const char* path : {"camera/position", "camera/target"}) {
        const params::IParameter* param = e.params().find(path);
        REQUIRE(param != nullptr);
        for (std::size_t i = 0; i < param->componentCount(); ++i) {
            p.values.push_back(param->finalComponent(i));
        }
    }
    p.values.push_back(e.params().find("camera/fov")->finalComponent(0));
    p.state = e.states().currentIndex();
    return p;
}

void frame(app::Engine& e, long long k) {
    e.update(FrameTime{static_cast<double>(k) / kFps, k == 0 ? 0.0 : 1.0 / kFps, static_cast<std::uint64_t>(k)});
}

} // namespace

TEST_CASE("A seek lands the scene states, the macros and an integrating camera route where a play does",
          "[seek][states][integrate][adr1168]") {
    const auto project = writeProject(true);
    for (const long long t : {97LL, 211LL, 299LL}) { // 3.2 s, 7.0 s, 10.0 s: across several state changes
        INFO("frame " << t << " (" << static_cast<double>(t) / kFps << " s)");
        app::Engine played(app::EngineMode::Offline);
        REQUIRE(played.loadProject(project).has_value());
        played.renderSettings().fps = kFps;
        std::vector<int> visited;
        for (long long k = 0; k <= t; ++k) {
            frame(played, k);
            if (visited.empty() || visited.back() != played.states().currentIndex()) {
                visited.push_back(played.states().currentIndex());
            }
        }
        const Pose a = pose(played);

        app::Engine seeked(app::EngineMode::Offline);
        REQUIRE(seeked.loadProject(project).has_value());
        seeked.renderSettings().fps = kFps;
        const auto replaysBefore = seeked.stats().controlReplays;
        seeked.seekSeconds(static_cast<double>(t) / kFps);
        CHECK(seeked.stats().controlReplays == replaysBefore + 1);
        seeked.update(FrameTime{static_cast<double>(t) / kFps, 0.0, 0}); // the landing frame, dt 0
        const Pose b = pose(seeked);
        CHECK(b.state == a.state);
        REQUIRE(a.values.size() == b.values.size());
        for (std::size_t i = 0; i < a.values.size(); ++i) {
            INFO("component " << i << ": played " << a.values[i] << ", seeked " << b.values[i]);
            CHECK(std::abs(a.values[i] - b.values[i]) <= 1e-4f * std::max(1.0f, std::abs(a.values[i])));
        }
        if (t == 299) {
            CHECK(visited.size() >= 3); // the play changed state more than once, so the replay had work to do
        }
        // And the frames after the landing stay with the play.
        frame(played, t + 1);
        frame(seeked, t + 1);
        const Pose a1 = pose(played);
        const Pose b1 = pose(seeked);
        for (std::size_t i = 0; i < a1.values.size(); ++i) {
            CHECK(std::abs(a1.values[i] - b1.values[i]) <= 1e-4f * std::max(1.0f, std::abs(a1.values[i])));
        }
    }
}

TEST_CASE("A project without scene states does not pay for the control replay", "[seek][states][adr1168]") {
    const auto project = writeProject(false);
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(project).has_value());
    e.seekSeconds(4.0);
    e.seekSeconds(2.0);
    CHECK(e.stats().controlReplays == 0);
}

// ADR-1168's cost: a seek late in a real song. `AVGEN_BENCH_PROJECT` names the project (DIGITAL MOSH by default).
TEST_CASE("the cost of a seek that replays the control layer", "[.bench][seek][states][adr1168]") {
    const char* env = std::getenv("AVGEN_BENCH_PROJECT");
    const std::filesystem::path project = env != nullptr ? env : "examples/digital-mosh/digital-mosh.json";
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(project).has_value());
    for (const double t : {30.0, 100.0, 200.0}) {
        const auto start = std::chrono::steady_clock::now();
        e.seekSeconds(t);
        const double withReplay =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        WARN(project.filename().string() << ": seek to " << t << " s: " << withReplay << " ms (control replays "
                                         << e.stats().controlReplays << ")");
    }
}

// Found by THE RIFT (proto/bioluminescent): a second seek to the same second -- a render job always seeks twice, once for
// its warm-up frame and once for the range -- landed in the initial state. StateMachine::reset re-entered the initial
// state at the machine's last updated second (the previous target), so the replay from zero could fire no `elapsed`
// trigger before that target. reset now restarts the machine's clock.
TEST_CASE("A second seek to the same second lands the elapsed-driven states where play does", "[adr1168][states]") {
    const auto dir = testsupport::processTempDir() / "control_seek_twice";
    std::filesystem::create_directories(dir);
    {
        const std::size_t frames = 12 * kRate;
        auto tone = testsupport::sine(110.0f, kRate, frames, 0.4f);
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(tone, 2), 2, kRate);
        REQUIRE(file.writeWav(dir / "music.wav").has_value());
    }
    std::ofstream(dir / "scene.json") << R"({ "format": "avgen-scene", "version": 1, "name": "seek-twice",
  "camera": { "mode": 1, "position": [0, 2, 10], "target": [0, 1, 0], "fov": 45 },
  "nodes": [ { "name": "box", "kind": "procedural", "procedural": {
    "source": { "kind": "box", "size": [1, 1, 1] }, "distribution": { "kind": "single" } } } ] })";
    std::ofstream(dir / "project.json") << R"({ "format": "avgen-project", "version": 4,
  "assets": { "scene": { "kind": "composition", "path": "scene.json" }, "audio": { "path": "music.wav" } },
  "parameters": {}, "routes": [],
  "presets": [ { "name": "a", "values": { "camera/fov": [40] } }, { "name": "b", "values": { "camera/fov": [50] } },
               { "name": "c", "values": { "camera/fov": [60] } } ],
  "states": { "initial": "A", "states": [
      { "name": "A", "preset": "a", "transition": { "seconds": 0.0 }, "triggers": [] },
      { "name": "B", "preset": "b", "transition": { "seconds": 0.0 },
        "triggers": [ { "kind": "elapsed", "threshold": 2.0, "from": "A" } ] },
      { "name": "C", "preset": "c", "transition": { "seconds": 0.0 },
        "triggers": [ { "kind": "elapsed", "threshold": 3.0, "from": "B" } ] } ] },
  "render": { "width": 64, "height": 36, "fps": 30 } })";
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(dir / "project.json").has_value());
    for (int pass = 0; pass < 3; ++pass) {
        engine.seekSeconds(8.0);
        FrameTime t{};
        t.renderTime = 8.0;
        engine.update(t);
        INFO("seek " << pass + 1 << " to 8 s lands in '" << engine.states().current() << "'");
        CHECK(engine.states().current() == "C"); // A until 2 s, B until 5 s, then C
    }
}

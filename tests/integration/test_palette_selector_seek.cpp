// ADR-1420 in the engine: a project palette's selector advances on a bus signal (here the analysed track's onsets),
// and an offline seek replays it from zero on the play's frame grid, so a render started mid-track lands on the
// palette and blend a play from zero shows. The control arm is the same seek with the replay's result thrown away
// (the selector restarted at the selected state): it must land elsewhere, or the case proves nothing.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;

namespace {

constexpr std::uint32_t kRate = 44100;

std::filesystem::path projectWithClicks() {
    const auto dir = testsupport::processTempDir() / "palette_selector_seek";
    std::filesystem::create_directories(dir);
    {
        const std::size_t frames = 8 * kRate;
        auto clicks = testsupport::clickTrack(120.0f, kRate, frames);
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(clicks, 2), 2, kRate);
        REQUIRE(file.writeWav(dir / "clicks.wav").has_value());
    }
    std::ofstream(dir / "room.scene.json") << R"({ "format": "avgen-scene", "version": 1, "name": "room",
      "nodes": [ { "name": "room", "kind": "sdf", "sdf": { "tree": { "root": { "kind": "box", "size": [1, 1, 1] } } } } ] })";
    const nlohmann::json project = nlohmann::json::parse(R"({
      "format": "avgen-project", "version": 4, "app": { "name": "avgen", "version": "0.1.0" },
      "assets": { "scene": { "kind": "composition", "path": "room.scene.json" }, "audio": { "path": "clicks.wav" } },
      "render": { "fps": 60 },
      "palette": {
        "states": [ { "name": "red",   "colors": { "wall": [0.8, 0.05, 0.05] } },
                    { "name": "green", "colors": { "wall": [0.05, 0.7, 0.1] } },
                    { "name": "blue",  "colors": { "wall": [0.05, 0.1, 0.9] } } ],
        "bindings": [ { "role": "wall", "target": "sdf/room/material/baseColor" } ],
        "selector": { "select": 0, "transition": 1.0, "hold": 1.3,
                      "advance": { "signal": "audio.onset", "threshold": 0.05, "order": [0, 2, 1] } } } })");
    std::ofstream(dir / "palette.json") << project.dump(1);
    return dir / "palette.json";
}

glm::vec3 wall(app::Engine& e) {
    const auto* p = e.params().find("sdf/room/material/baseColor");
    REQUIRE(p != nullptr);
    return {p->finalComponent(0), p->finalComponent(1), p->finalComponent(2)};
}

void frame(app::Engine& e, long long k) {
    e.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
}

} // namespace

TEST_CASE("Palette selector in the engine: onsets advance it, and a seek lands where a play does",
          "[palette][seek][adr1420]") {
    const auto project = projectWithClicks();
    app::Engine played{app::EngineMode::Offline};
    REQUIRE(played.loadProject(project).has_value());
    REQUIRE(played.params().find("palette/select") != nullptr);
    const long long t = 250; // 4.17 s: a few advances in, mid-blend or not
    for (long long k = 0; k <= t; ++k) {
        frame(played, k);
    }
    const int playedState = played.palette().selectedState();
    const glm::vec3 playedWall = wall(played);
    CHECK(playedState != 0); // the onsets did advance it
    INFO("played: state " << playedState << ", wall " << playedWall.r << " " << playedWall.g << " " << playedWall.b);

    app::Engine seeked{app::EngineMode::Offline};
    REQUIRE(seeked.loadProject(project).has_value());
    seeked.seekSeconds(static_cast<double>(t) / 60.0);
    seeked.update(FrameTime{static_cast<double>(t) / 60.0, 0.0, 0}); // the landing frame
    CHECK(seeked.palette().selectedState() == playedState);
    CHECK(glm::length(wall(seeked) - playedWall) < 1e-5f);
    // and it plays on in step
    for (long long k = t + 1; k <= t + 90; ++k) {
        frame(played, k);
        frame(seeked, k);
    }
    CHECK(seeked.palette().selectedState() == played.palette().selectedState());
    CHECK(glm::length(wall(seeked) - wall(played)) < 1e-5f);

    // Control: the seek without its replay (the selector restarted at the selected state) lands elsewhere.
    app::Engine reset{app::EngineMode::Offline};
    REQUIRE(reset.loadProject(project).has_value());
    reset.seekSeconds(static_cast<double>(t) / 60.0);
    const_cast<params::Palette&>(reset.palette()).resetSelector();
    reset.update(FrameTime{static_cast<double>(t) / 60.0, 0.0, 0});
    CHECK(reset.palette().selectedState() == 0);
    CHECK(glm::length(wall(reset) - playedWall) > 0.05f);
}

TEST_CASE("Palette selector: a project's palette parameters land on load, without an unknown-parameter warning, "
          "on every load", "[palette][adr1420]") {
    const auto plain = projectWithClicks();
    nlohmann::json doc = nlohmann::json::parse(std::ifstream(plain));
    doc["parameters"] = {{"palette/select", 2.0}, {"palette/lock", 1.0}, {"palette/hold", 9.0},
                         {"palette/transition", 3.0}, {"palette/saturation", 0.8}};
    const auto project = plain.parent_path() / "palette_params.json";
    std::ofstream(project) << doc.dump(1);

    auto sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1024);
    auto logger = spdlog::default_logger();
    logger->sinks().push_back(sink);
    app::Engine engine{app::EngineMode::Offline};
    for (int load = 0; load < 2; ++load) {
        INFO("load " << load);
        REQUIRE(engine.loadProject(project).has_value());
        CHECK(engine.params().findAs<float>("palette/select")->base() == 2.0f);
        CHECK(engine.params().findAs<float>("palette/lock")->base() == 1.0f);
        CHECK(engine.params().findAs<float>("palette/hold")->base() == 9.0f);
        CHECK(engine.params().findAs<float>("palette/transition")->base() == 3.0f);
        CHECK(engine.params().findAs<float>("palette/saturation")->base() == 0.8f);
        engine.update(FrameTime{0.0, 0.0, 0});
        CHECK(engine.palette().selectedState() == 2); // the loaded selection, arrived (no blend from the default)
        CHECK(engine.palette().selectorProgress(0.0) == 1.0f);
    }
    logger->sinks().pop_back();
    int unknown = 0;
    for (const std::string& line : sink->last_formatted()) {
        unknown += line.find("unknown parameter 'palette/") != std::string::npos ? 1 : 0;
    }
    CHECK(unknown == 0);
}

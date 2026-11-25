// ADR-2164: the open project knows which of its references did not resolve -- `Engine::unresolvedReferences()`,
// the structured list `project.validate` and `project.open` report -- and forgets each one when the session puts
// something else in its place.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "audio/audio_file.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path path;
    explicit Dir(const std::string& name)
        : path(fs::temp_directory_path() / ("avgen_unresolved_" + name + "_" + std::to_string(::getpid()))) {
        fs::remove_all(path);
        fs::create_directories(path / "media");
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

const app::Engine::UnresolvedReference* find(const std::vector<app::Engine::UnresolvedReference>& list,
                                             const std::string& pointer) {
    const auto it = std::find_if(list.begin(), list.end(), [&](const auto& r) { return r.pointer == pointer; });
    return it == list.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("Engine::unresolvedReferences names each reference that did not resolve, with its pointer",
          "[integration][project][adr2164]") {
    Dir d("list");
    const fs::path project = d.path / "p.json";
    std::ofstream(project) << json{
        {"format", "avgen-project"},
        {"version", 4},
        {"assets",
         {{"audio", {{"path", "media/gone.wav"}, {"size", 10}, {"sha256", "00"}}},
          {"environment", "media/gone.hdr"},
          {"stems", json::array({{{"key", "bass"}, {"label", "Bass"}, {"file", "media/bass.wav"}}})},
          {"scene", {{"kind", "composition"}, {"path", {{"path", "media/gone.scene.json"}}}}}}},
        {"shaders", json::array({{{"path", "media/gone.wgsl"}, {"stage", "background"}}})}}
                                  .dump(2);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    const auto list = engine.unresolvedReferences();
    REQUIRE(list.size() == 5);
    const auto* scene = find(list, "/assets/scene/path");
    REQUIRE(scene != nullptr);
    CHECK(scene->kind == "scene");
    CHECK(scene->path == "media/gone.scene.json");
    const auto* audio = find(list, "/assets/audio");
    REQUIRE(audio != nullptr);
    CHECK(audio->kind == "audio");
    CHECK(audio->path == "media/gone.wav");
    REQUIRE(find(list, "/assets/environment") != nullptr);
    CHECK(find(list, "/assets/environment")->kind == "environment");
    REQUIRE(find(list, "/assets/stems/0/file") != nullptr);
    CHECK(find(list, "/assets/stems/0/file")->kind == "stem");
    REQUIRE(find(list, "/shaders/0/path") != nullptr);
    CHECK(find(list, "/shaders/0/path")->kind == "shader");
    for (const auto& r : list) {
        CHECK_FALSE(r.reason.empty());
    }

    // The session replaces the audio: that reference is no longer the project's, and the save writes the new one.
    constexpr std::uint32_t rate = 48000;
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(testsupport::sine(200.0f, rate, rate, 0.5f), 2),
                                                  2, rate);
    REQUIRE(file.writeWav(d.path / "media" / "new.wav").has_value());
    REQUIRE(engine.loadAudio(d.path / "media" / "new.wav").has_value());
    CHECK(find(engine.unresolvedReferences(), "/assets/audio") == nullptr);
    CHECK(engine.projectDocument(project)["assets"]["audio"]["path"] == "media/new.wav");
    // The others are still kept.
    CHECK(engine.projectDocument(project)["assets"]["scene"]["path"]["path"] == "media/gone.scene.json");

    // A new project has none.
    engine.newProject();
    CHECK(engine.unresolvedReferences().empty());
}

TEST_CASE("A kept reference saved to another folder still names the same file", "[integration][project][adr2164]") {
    Dir d("rebase");
    const fs::path project = d.path / "p.json";
    std::ofstream(project) << json{{"format", "avgen-project"},
                                   {"version", 4},
                                   {"assets", {{"environment", {{"path", "media/gone.hdr"}, {"size", 3}}}}}}
                                  .dump(2);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    fs::create_directories(d.path / "sub");
    const json moved = engine.projectDocument(d.path / "sub" / "p.json");
    CHECK(moved["assets"]["environment"] == json({{"path", "../media/gone.hdr"}, {"size", 3}}));
}

TEST_CASE("A failed open that falls back says so and leaves an untitled clean project",
          "[integration][project][adr2164]") {
    Dir d("fallback");
    const fs::path project = d.path / "p.json";
    std::ofstream(project) << json{{"format", "avgen-project"},
                                   {"version", 4},
                                   {"parameters", {{"camera/exposure/compensation", "bright"}}}}
                                  .dump(2);
    app::Engine engine(app::EngineMode::Offline);
    const auto before = engine.projectGeneration();
    const auto r = engine.loadProject(project);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("empty untitled project") != std::string::npos);
    CHECK(engine.projectPath().empty());
    CHECK(engine.projectGeneration() != before);
    CHECK_FALSE(engine.revisions().dirty());
}

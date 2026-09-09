// Milestone 0.9 (ADR-019): projects reference their assets relative to the file and restore a
// whole session; bundles copy everything referenced; new project resets.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/gltf_fixture.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    fs::path dir;
    fs::path wav, glb, hdr, sceneFile, shader;
    Fixture() {
        dir = fs::temp_directory_path() / "avgen_project_system";
        fs::remove_all(dir);
        fs::create_directories(dir / "media");
        constexpr std::uint32_t rate = 48000;
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(testsupport::sine(60.0f, rate, rate, 0.5f), 2), 2, rate);
        wav = dir / "media" / "tone.wav";
        REQUIRE(file.writeWav(wav).has_value());
        const auto tmpGlb = testsupport::writeTriangleGlb("project_system");
        glb = dir / "media" / "tri.glb";
        fs::copy_file(tmpGlb, glb, fs::copy_options::overwrite_existing);
        fs::remove(tmpGlb);
        std::vector<float> sky(8 * 4 * 4, 0.5f);
        hdr = dir / "media" / "sky.hdr";
        REQUIRE(assets::writeHdr(hdr, 8, 4, sky).has_value());
        sceneFile = dir / "media" / "stage.json";
        std::ofstream(sceneFile) << R"({"format":"avgen-scene","version":1,"name":"stage","nodes":[
            {"name":"a","kind":"gltf","asset":"tri.glb"},{"name":"b","kind":"gltf","asset":"tri.glb","position":[2,0,0]}]})";
        shader = dir / "media" / "layer.wgsl";
        std::ofstream(shader) << "/* { \"inputs\": [ { \"name\": \"tint\", \"type\": \"float\", \"default\": 0.5 } ] } */\n"
                                 "fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> { return vec4<f32>(inputs.tint, 0.0, 0.0, 1.0); }\n";
    }
    ~Fixture() { fs::remove_all(dir); }
};

nlohmann::json readJson(const fs::path& p) {
    std::ifstream in(p);
    return nlohmann::json::parse(in);
}

} // namespace

TEST_CASE("Projects save asset references relative to the file and restore the session", "[integration][project]") {
    Fixture f;
    const auto project = f.dir / "show.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(f.wav).has_value());
        REQUIRE(engine.loadScene(f.glb).has_value());
        REQUIRE(engine.loadEnvironment(f.hdr).has_value());
        REQUIRE(engine.addShaderLayer(f.shader, shaders::LayerStage::Background).has_value());
        engine.params().find("root/scale")->setBaseComponent(0, 1.5f);
        REQUIRE(engine.saveProject(project).has_value());
        CHECK(engine.projectPath() == project);
        const auto files = engine.referencedFiles();
        CHECK(files.size() == 4); // audio, environment, glb, shader
    }
    const auto doc = readJson(project);
    CHECK(doc["version"].get<int>() >= 4);
    CHECK(doc["app"]["name"] == "avgen");
    CHECK(doc["assets"]["audio"]["path"] == "media/tone.wav");
    CHECK(doc["assets"]["audio"]["size"] == fs::file_size(f.wav));
    CHECK(doc["assets"]["audio"]["sha256"].get<std::string>().size() == 64);
    CHECK(doc["assets"]["environment"]["path"] == "media/sky.hdr");
    CHECK(doc["assets"]["scene"]["kind"] == "gltf");
    CHECK(doc["assets"]["scene"]["path"]["path"] == "media/tri.glb");
    CHECK(doc["shaders"][0]["path"] == "media/layer.wgsl");
    CHECK(doc["shaders"][0]["sha256"].get<std::string>().size() == 64);

    // A fresh engine restores everything from the project alone; the project may be moved as
    // long as its assets move with it.
    const auto moved = f.dir / "moved";
    fs::create_directories(moved);
    fs::copy(f.dir / "media", moved / "media", fs::copy_options::recursive);
    fs::copy_file(project, moved / "show.json");
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(moved / "show.json").has_value());
    CHECK(engine.projectWarnings().empty());
    CHECK(engine.hasAudio());
    CHECK(engine.audioPath() == (moved / "media" / "tone.wav").lexically_normal());
    REQUIRE(engine.gltfScene() != nullptr);
    CHECK(engine.environmentPath() == (moved / "media" / "sky.hdr").lexically_normal());
    CHECK(engine.shaderLayers().layers().size() == 1);
    CHECK(engine.params().find("root/scale")->baseComponent(0) == 1.5f);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
}

TEST_CASE("Missing project assets are warnings and the rest still loads", "[integration][project]") {
    Fixture f;
    const auto project = f.dir / "broken.json";
    nlohmann::json doc = {
        {"format", "avgen-project"}, {"version", 4},
        {"parameters", {{"orb/scale", 1.75}}}, {"routes", nlohmann::json::array()},
        {"assets", {{"audio", "media/nope.wav"}, {"environment", "media/nope.hdr"},
                    {"scene", {{"kind", "gltf"}, {"path", "media/nope.glb"}}}}}};
    std::ofstream(project) << doc.dump(2);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    CHECK(engine.projectWarnings().size() == 3);
    CHECK(engine.orbScene() != nullptr); // the scene load failed, so the orb stayed
    CHECK(engine.params().find("orb/scale")->baseComponent(0) == 1.75f);
    CHECK_FALSE(engine.hasAudio());
}

TEST_CASE("Composition projects reference the scene file or embed an unsaved composition", "[integration][project]") {
    Fixture f;
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(f.sceneFile).has_value());
    const auto project = f.dir / "comp.json";
    REQUIRE(engine.saveProject(project).has_value());
    auto doc = readJson(project);
    CHECK(doc["assets"]["scene"]["kind"] == "composition");
    CHECK(doc["assets"]["scene"]["path"]["path"] == "media/stage.json");
    CHECK(engine.referencedFiles().size() == 2); // scene file + tri.glb (once)

    // Unsaved composition: embedded inline and restored.
    engine.newComposition();
    scene::CompositionNode orb;
    orb.name = "ball";
    orb.kind = scene::NodeKind::Orb;
    REQUIRE(engine.addNode(std::move(orb)).has_value());
    const auto inlineProject = f.dir / "inline.json";
    REQUIRE(engine.saveProject(inlineProject).has_value());
    doc = readJson(inlineProject);
    CHECK(doc["assets"]["scene"].contains("inline"));
    app::Engine other(app::EngineMode::Offline);
    REQUIRE(other.loadProject(inlineProject).has_value());
    REQUIRE(other.composition() != nullptr);
    CHECK(other.composition()->findNode("ball") != nullptr);
    CHECK(other.params().find("nodes/ball/scale") != nullptr);
}

TEST_CASE("Bundles copy every referenced file and reopen from anywhere", "[integration][project]") {
    Fixture f;
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(f.wav).has_value());
    REQUIRE(engine.loadComposition(f.sceneFile).has_value());
    REQUIRE(engine.loadEnvironment(f.hdr).has_value());
    REQUIRE(engine.addShaderLayer(f.shader, shaders::LayerStage::Post).has_value());
    const auto bundle = f.dir / "bundle";
    REQUIRE(engine.exportBundle(bundle).has_value());
    CHECK(fs::exists(bundle / "project.json"));
    CHECK(fs::exists(bundle / "assets" / "tone.wav"));
    CHECK(fs::exists(bundle / "assets" / "sky.hdr"));
    CHECK(fs::exists(bundle / "assets" / "tri.glb"));
    CHECK(fs::exists(bundle / "assets" / "stage.json"));
    CHECK(fs::exists(bundle / "assets" / "layer.wgsl"));
    const auto scene = readJson(bundle / "assets" / "stage.json");
    CHECK(scene["nodes"][0]["asset"] == "tri.glb");
    const auto doc = readJson(bundle / "project.json");
    CHECK(doc["assets"]["scene"]["path"]["path"] == "assets/stage.json");
    CHECK(doc["assets"]["audio"]["path"] == "assets/tone.wav");

    // Delete the originals: the bundle must stand on its own, wherever it is moved.
    const auto elsewhere = fs::temp_directory_path() / "avgen_project_bundle_moved";
    fs::remove_all(elsewhere);
    fs::rename(bundle, elsewhere);
    fs::remove_all(f.dir / "media");
    app::Engine other(app::EngineMode::Offline);
    REQUIRE(other.loadProject(elsewhere / "project.json").has_value());
    CHECK(other.projectWarnings().empty());
    CHECK(other.hasAudio());
    REQUIRE(other.composition() != nullptr);
    CHECK(other.composition()->nodeCount() == 2);
    CHECK(other.shaderLayers().layers().size() == 1);
    fs::remove_all(elsewhere);
}

TEST_CASE("New project resets everything but the audio", "[integration][project]") {
    Fixture f;
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(f.wav).has_value());
    REQUIRE(engine.loadScene(f.glb).has_value());
    REQUIRE(engine.loadEnvironment(f.hdr).has_value());
    engine.addSource("lfo", "wobble");
    engine.storePreset("p");
    engine.timeline().addCue({.time = 1.0, .name = "c"});
    engine.params().find("post/bloom/intensity")->setBaseComponent(0, 3.0f);
    REQUIRE(engine.saveProject(f.dir / "before.json").has_value());
    engine.newProject();
    CHECK(engine.hasAudio());
    CHECK(engine.orbScene() != nullptr);
    CHECK(engine.environmentPath().empty());
    CHECK(engine.sources().find("lfo", "wobble") == nullptr);
    CHECK(engine.presets().presets().empty());
    CHECK(engine.timeline().empty());
    CHECK(engine.projectPath().empty());
    CHECK(engine.params().find("post/bloom/intensity")->baseComponent(0) != 3.0f);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
}

TEST_CASE("Moved assets are relinked by name, size and content hash", "[integration][project][relink]") {
    Fixture f;
    const auto project = f.dir / "relink.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(f.wav).has_value());
        REQUIRE(engine.addShaderLayer(f.shader, shaders::LayerStage::Background).has_value());
        REQUIRE(engine.saveProject(project).has_value());
    }
    const auto doc = readJson(project);
    REQUIRE(doc["assets"]["audio"].is_object());
    CHECK(doc["assets"]["audio"]["size"] == fs::file_size(f.wav));
    const std::string hash = doc["assets"]["audio"]["sha256"];
    CHECK(hash.size() == 64);

    SECTION("a file moved into a subfolder is found and relinked") {
        const auto moved = f.dir / "media" / "moved" / "deeper" / "tone.wav";
        fs::create_directories(moved.parent_path());
        fs::rename(f.wav, moved);
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(project).has_value());
        REQUIRE(engine.projectWarnings().size() == 1);
        CHECK(engine.projectWarnings()[0].rfind("relinked audio: media/tone.wav -> ", 0) == 0);
        CHECK(engine.projectWarnings()[0].find("media/moved/deeper/tone.wav") != std::string::npos);
        CHECK(engine.hasAudio());
        CHECK(engine.audioPath() == moved.lexically_normal());
        CHECK(engine.shaderLayers().layers().size() == 1);
        // Saving again records the new location.
        REQUIRE(engine.saveProject(project).has_value());
        CHECK(readJson(project)["assets"]["audio"]["path"] == "media/moved/deeper/tone.wav");
    }
    SECTION("a same-name file with different content is rejected by the hash") {
        fs::remove(f.wav);
        constexpr std::uint32_t rate = 48000;
        auto other = audio::AudioFile::fromInterleaved(
            testsupport::interleave(testsupport::sine(220.0f, rate, rate, 0.5f), 2), 2, rate);
        const auto impostor = f.dir / "media" / "other" / "tone.wav";
        fs::create_directories(impostor.parent_path());
        REQUIRE(other.writeWav(impostor).has_value());
        CHECK(fs::file_size(impostor) == doc["assets"]["audio"]["size"].get<std::uintmax_t>()); // same size
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(project).has_value());
        REQUIRE(engine.projectWarnings().size() == 1);
        CHECK(engine.projectWarnings()[0].rfind("audio: ", 0) == 0); // still missing
        CHECK_FALSE(engine.hasAudio());
    }
    SECTION("a legacy string reference relinks by name alone; shaders relink too") {
        nlohmann::json legacy = doc;
        legacy["assets"]["audio"] = "media/tone.wav";
        legacy["shaders"][0] = {{"path", "media/layer.wgsl"}, {"stage", doc["shaders"][0]["stage"]}};
        std::ofstream(project) << legacy.dump(2);
        fs::create_directories(f.dir / "media" / "moved");
        fs::rename(f.wav, f.dir / "media" / "moved" / "tone.wav");
        fs::rename(f.shader, f.dir / "media" / "moved" / "layer.wgsl");
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(project).has_value());
        REQUIRE(engine.projectWarnings().size() == 2);
        CHECK(engine.projectWarnings()[0].rfind("relinked audio: ", 0) == 0);
        CHECK(engine.projectWarnings()[1].rfind("relinked shader: ", 0) == 0);
        CHECK(engine.hasAudio());
        CHECK(engine.shaderLayers().layers().size() == 1);
    }
}

// Milestone 0.9 (ADR-019): projects reference their assets relative to the file and restore a
// whole session; bundles copy everything referenced; new project resets.

#include "app/engine.hpp"
#include "app/examples.hpp"
#include "entity/entity.hpp"
#include "world/world_recipe.hpp"
#include "assets/image.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/gltf_fixture.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unistd.h>
#include <fstream>
#include <array>
#include <ranges>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    fs::path dir;
    fs::path wav, glb, hdr, sceneFile, shader;
    Fixture() {
        // Per-process, for the same reason as test_composition: ctest -j runs each test in its
        // own process of one binary, so a fixed directory name is shared mutable state between
        // concurrent tests rather than scratch space.
        dir = fs::temp_directory_path() /
              ("avgen_project_system_" + std::to_string(static_cast<long long>(::getpid())));
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

TEST_CASE("A cue preset that overrides a scene value says so", "[integration][project][cues]") {
    // Cues recall presets into the base values (ADR-018), so from a cue onward the preset's value
    // is what the scene has -- whatever the scene file said. That precedence is right and it was
    // silent, which cost docs/shot-hyperspace.md a dozen material edits that did nothing at all.
    // Loading a project now measures the overlap and says which values the scene will not keep.
    Fixture f;
    const auto sceneFile = f.dir / "media" / "lit.json";
    std::ofstream(sceneFile) << R"({"format":"avgen-scene","version":1,"name":"lit","nodes":[
        {"name":"plate","kind":"procedural","procedural":{
            "source":{"kind":"box","size":[1,1,1]},
            "distribution":{"kind":"single"},
            "material":{"baseColor":[0.1,0.1,0.1],"emissiveIntensity":0.25,"roughness":0.8}}}]})";

    // The author's value for the plate's emission is 0.25. A cue preset names 1.5.
    nlohmann::json doc = {
        {"format", "avgen-project"},
        {"version", 4},
        {"parameters", nlohmann::json::object()},
        {"routes", nlohmann::json::array()},
        {"assets", {{"scene", {{"kind", "composition"}, {"path", "media/lit.json"}}}}},
        {"presets", nlohmann::json::array({nlohmann::json{
             {"name", "big"},
             {"values", {{"procedural/plate/material/emissive", nlohmann::json::array({1.5})}}}}})},
        {"timeline", {{"enabled", true},
                      {"cues", nlohmann::json::array({nlohmann::json{
                           {"time", 0.0}, {"name", "reveal"}, {"preset", "big"}, {"morphSeconds", 0.0}}})}}}};
    const auto project = f.dir / "cued.json";
    std::ofstream(project) << doc.dump(2);

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    REQUIRE(engine.composition() != nullptr);
    // The scene's value is what the parameter starts at, so the conflict is real rather than a
    // report about a value nobody authored.
    const params::IParameter* emissive = engine.params().find("procedural/plate/material/emissive");
    REQUIRE(emissive != nullptr);
    CHECK(emissive->baseComponent(0) == 0.25f);

    // The engine says so, by path, so an author who edits the scene file and sees nothing has
    // somewhere to look. It is not a load fault -- a cue is meant to take a value over -- so it
    // does not join the missing-asset warnings.
    CHECK(std::ranges::find(engine.cuePresetOverrides(), "procedural/plate/material/emissive") !=
          engine.cuePresetOverrides().end());
    CHECK(engine.projectWarnings().empty());

    // A preset that agrees with the scene is not a conflict and must not be reported: a notice
    // that fires on every project is a notice nobody reads.
    doc["presets"][0]["values"]["procedural/plate/material/emissive"] = nlohmann::json::array({0.25});
    const auto agreeing = f.dir / "agreeing.json";
    std::ofstream(agreeing) << doc.dump(2);
    app::Engine quiet(app::EngineMode::Offline);
    REQUIRE(quiet.loadProject(agreeing).has_value());
    CHECK(quiet.cuePresetOverrides().empty());

    // And a project with no timeline at all contests nothing.
    doc.erase("timeline");
    const auto plain = f.dir / "plain.json";
    std::ofstream(plain) << doc.dump(2);
    app::Engine none(app::EngineMode::Offline);
    REQUIRE(none.loadProject(plain).has_value());
    CHECK(none.cuePresetOverrides().empty());
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

TEST_CASE("Scene-owned environments survive project loading and project switches",
          "[integration][project][environment]") {
    Fixture fixture;
    auto sceneDocument = readJson(fixture.sceneFile);
    sceneDocument["environment"] = {{"map", "sky.hdr"}, {"intensity", 0.17}, {"skyIntensity", 0.04}};
    std::ofstream(fixture.sceneFile) << sceneDocument.dump(2);
    nlohmann::json document = {
        {"format", "avgen-project"}, {"version", 4},
        {"assets", {{"scene", {{"kind", "composition"}, {"path", "media/stage.json"}}}}}};
    SECTION("an inline composition owns its environment too") {
        sceneDocument["environment"]["map"] = "media/sky.hdr";
        for (auto& node : sceneDocument["nodes"]) {
            node["asset"] = "media/tri.glb";
        }
        document["assets"]["scene"] = {{"kind", "composition"}, {"inline", sceneDocument}};
    }
    SECTION("a referenced composition owns its environment") {}

    const auto project = fixture.dir / "authored.json";
    std::ofstream(project) << document.dump(2);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project));
    engine.update(FrameTime{});
    CHECK(engine.projectWarnings().empty());
    CHECK(fs::weakly_canonical(engine.environmentPath()) == fs::weakly_canonical(fixture.hdr));
    CHECK(engine.scene().environment.environmentMap != scene::kInvalidTexture);

    const auto alternate = fixture.dir / "media" / "alternate.hdr";
    REQUIRE(assets::writeHdr(alternate, 8, 4, std::vector<float>(8 * 4 * 4, 0.8f)));
    document["assets"]["environment"] = "media/alternate.hdr";
    const auto overrideProject = fixture.dir / "override.json";
    std::ofstream(overrideProject) << document.dump(2);
    REQUIRE(engine.loadProject(overrideProject));
    engine.update(FrameTime{});
    CHECK(fs::weakly_canonical(engine.environmentPath()) == fs::weakly_canonical(alternate));
    REQUIRE(engine.loadProject(project));
    engine.update(FrameTime{});
    CHECK(fs::weakly_canonical(engine.environmentPath()) == fs::weakly_canonical(fixture.hdr));
    CHECK(engine.scene().environment.environmentMap != scene::kInvalidTexture);

    document["assets"]["scene"] = {{"kind", "orb"}};
    document["assets"].erase("environment");
    std::ofstream(project) << document.dump(2);
    REQUIRE(engine.loadProject(project));
    engine.update(FrameTime{});
    CHECK(engine.environmentPath().empty());
    CHECK(engine.scene().environment.environmentMap == scene::kInvalidTexture);
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
    const auto elsewhere = fs::temp_directory_path() /
                           ("avgen_project_bundle_moved_" + std::to_string(static_cast<long long>(::getpid())));
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

TEST_CASE("Glowmere's directed shot stays grounded, bounded and fully connected",
          "[integration][glowmere]") {
    const fs::path root = AVGEN_SOURCE_DIR;
    if (!fs::exists(root / "assets/nature/plants/fern_02/fern_02_1k.gltf")) {
        SKIP("Glowmere's optional nature asset library is not installed");
    }
    app::Engine engine(app::EngineMode::Offline);
    std::string project = "terrain.json";
    bool stylized = false;
    SECTION("current look") {}
    SECTION("painted look") { project = "glowmere-stylized.json"; stylized = true; }
    SECTION("matched painted scene with PBR") { project = "glowmere-stylized-pbr.json"; }
    REQUIRE(engine.loadProject(root / "examples/world" / project));
    REQUIRE(engine.projectWarnings().empty());
    REQUIRE(engine.composition() != nullptr);
    const auto* valley = engine.composition()->findNode("valley");
    REQUIRE(valley != nullptr);
    REQUIRE(engine.composition()->findNode("elder-crown") != nullptr);
    if (project == "terrain.json") {
        REQUIRE(engine.environmentPath().filename() == "kloppenheim_02_puresky_4k.hdr");
    } else {
        REQUIRE(engine.environmentPath().empty());
    }
    // Whatever the project automates, it must bind. An unbound track is silently inert and looks
    // exactly like automation that does nothing.
    for (const auto& track : engine.timeline().tracks()) {
        INFO("track on " << track.target);
        REQUIRE(track.param != nullptr);
    }
    // An *authored* camera path is now optional, and the stylized showcase deliberately has none:
    // its camera is driven by the editor and by the camera director instead, and a timeline that
    // replaces camera/position every frame silently overrides both (a viewport drag writes the base
    // value and the timeline puts it back). So the walk below runs only where a path exists.
    const bool authoredCameraPath =
        std::any_of(engine.timeline().tracks().begin(), engine.timeline().tracks().end(),
                    [](const params::Track& t) { return t.target.starts_with("camera/"); });
    // Every route binds. This is the assertion that matters and it is deliberately made over *all*
    // of them: a route whose source or target does not resolve is silently inert, which looks
    // exactly like a scene that is not reacting, and the loader only warns.
    REQUIRE(!engine.modulator().routes().empty());
    constexpr std::size_t kAmbientWashCount = 2;
    std::size_t slowContinuous = 0;
    for (const auto& route : engine.modulator().routes()) {
        INFO("route " << route.source << " -> " << route.target);
        CHECK(route.targetParam != nullptr);
        CHECK(route.sourceId != signals::kInvalidSignal);
        // A continuous band driving a brightness must be an *envelope*, not a waveform follower:
        // a level that tracks the waveform reads as flicker rather than as response. That is the
        // defect this guards, and the floor below is what forbids it.
        //
        // Two corrections to how it used to ask.
        //
        // It classified by the `audio.` name prefix, and its own comment says events are exempt --
        // "a beat that takes half a second to attack has already missed its beat". `audio.onset`
        // is an event: `signals::AudioSignals` declares it with `setEvent`, and the bus carries an
        // `isEvent` flag for exactly this distinction. Asking the bus is right; guessing from the
        // name was only ever correct because Glowmere happens to route its events through the
        // `music.` namespace instead.
        //
        // And the half-second figure was calibrated on the world's ambient wash -- a valley of
        // bioluminescence brightening with the music, where anything quicker crawls. It is not a
        // statement about every possible target. A hero object's lamp answering the bass is meant
        // to be punchy, and 35 ms is a fast envelope rather than a waveform follower. So the strict
        // figure stays where it was learned, and a named entity's own parts get the floor instead.
        const bool isEvent = route.sourceId != signals::kInvalidSignal &&
                             route.sourceId < engine.signals().size() &&
                             engine.signals().info(route.sourceId).isEvent;
        if (route.source.starts_with("audio.") && !isEvent) {
            // Which of these is the ambient wash? The half-second was learned on exactly two
            // routes -- the valley's luminous crown and its spore field -- and those two are named
            // below rather than derived, because every attempt to derive it has been a guess about
            // node kind rather than about the look. The prefix guess put the UFO's tractor beam in
            // with the spore field, because both are `particles/`. An ownership guess then had to
            // grow an exemption for the water, and then another for the motes that run along it,
            // and a rule that acquires an exemption per feature is a rule that has stopped saying
            // anything.
            //
            // So the claim is split into the part that is universal and the part that is not.
            // Universal: a continuous band driving anything must be an envelope. Not universal: how
            // slow. The two routes where "slow" was actually measured are pinned by name, so an
            // author who speeds the valley up has to argue with this test -- which a count of slow
            // routes would let them past, by adding a slow one somewhere else.
            // All three projects this case loads carry the same pair, and only the crown's material
            // is named differently between them (`glowmereCrown` in the current look,
            // `paintedCrown` in the two painted ones), so the crown is matched by what it is rather
            // than by which look is loaded.
            const bool ambient = route.target == "particles/spores/spawnRate" ||
                                 route.target.ends_with("Crown/emissionIntensity");
            if (ambient) {
                CHECK(route.chain.attackMs >= 500.0f);
                CHECK(route.chain.decayMs >= 1000.0f);
                ++slowContinuous;
            }
            // Every continuous route, ambient or not: smoothing has to exist and be sane. A raw
            // band written straight through is the flicker this whole block is about, and that is
            // true of a river's sparkle at fourteen milliseconds as much as of the valley at six
            // hundred -- the difference between them is a tempo, not whether there is an envelope.
            CHECK(route.chain.attackMs > 0.0f);
            CHECK(route.chain.decayMs >= route.chain.attackMs);
            CHECK(std::isfinite(route.chain.attackMs));
            CHECK(std::isfinite(route.chain.decayMs));
        }
    }
    // Both of the calibrated ambient routes must still be there. Pinning a *total* would pin an
    // authoring decision -- adding a musical reaction should not fail a test about whether routes
    // bind -- but the pair the number was learned on is not an authoring decision, it is the
    // evidence.
    CHECK(slowContinuous == kAmbientWashCount);
    if (!authoredCameraPath) {
        // No path to walk. What must still hold is that the scene is loadable and its camera is
        // somewhere sane, since the editor starts from wherever the scene left it.
        engine.update(FrameTime{0.0, 0.1, 0});
        const auto& camera = engine.scene().camera;
        const float ground = valley->worldMap.height({camera.position.x, camera.position.z});
        INFO("camera at " << camera.position.x << ", " << camera.position.y << ", " << camera.position.z);
        CHECK(camera.position.y - ground >= 1.2f);
        CHECK(glm::length(camera.target - camera.position) > 5.0f);
        return;
    }
    glm::vec3 previousPosition{};
    glm::vec3 previousDirection{};
    for (std::uint64_t frame = 0; frame <= 2700; frame += 3) {
        const double seconds = static_cast<double>(frame) / 30.0;
        engine.update(FrameTime{seconds, 0.1, frame});
        CHECK(engine.scene().environment.stylized == stylized);
        const auto& camera = engine.scene().camera;
        INFO("shot time " << seconds);
        const float ground = valley->worldMap.height({camera.position.x, camera.position.z});
        CHECK(camera.position.y - ground >= 1.2f);
        CHECK(glm::length(camera.target - camera.position) > 5.0f);
        const auto direction = glm::normalize(camera.target - camera.position);
        if (frame > 0) {
            CHECK(glm::length(camera.position - previousPosition) <= 0.4f);
            CHECK(glm::dot(previousDirection, direction) > 0.999f);
        }
        previousPosition = camera.position;
        previousDirection = direction;
    }
    CHECK((engine.scene().environment.environmentMap != scene::kInvalidTexture) == (project == "terrain.json"));
    for (const auto& program : engine.scene().materialPrograms) {
        CHECK(program.validate());
    }
    for (const auto& object : engine.scene().procedurals) {
        CHECK(object.validate());
        if (!object.material.program.empty()) {
            CHECK(std::ranges::any_of(engine.scene().materialPrograms, [&](const auto& program) {
                return program.name == object.material.program;
            }));
        }
    }
}

// Audio is project state like everything else: File > New leaves no piece loaded. It used to keep
// the last one -- the clip list was cleared but the mixed buffer stayed installed, so a brand new
// project opened playing the previous project's song.
TEST_CASE("New project resets everything, the audio included", "[integration][project]") {
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
    CHECK_FALSE(engine.hasAudio());
    CHECK(engine.audioClips().empty());
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

// ADR-059. Ten scene files in this repository carried a `post` block and not one of them did
// anything: nothing read it, so `chromaRetention` and `bloomEmissionWeight` -- the two controls
// that decide whether a scene's own glow reads -- sat at zero in every scene that asked for them.
// Nothing caught it for the life of the feature, which is what this test is for.
TEST_CASE("A composition's post block reaches the post parameters, and the project still wins",
          "[integration][project][post]") {
    Fixture f;
    const auto scenePath = f.dir / "media" / "posty.json";
    std::ofstream(scenePath) << R"({"format":"avgen-scene","version":1,"name":"posty","nodes":[],
      "post":{"chromaRetention":0.6,"bloomEmissionWeight":0.75,"antialias":0.5,"tonemap":3,
              "bloomEnabled":false,"bloomLevels":6}})";

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(scenePath).has_value());
    const auto& params = engine.params();
    auto valueOf = [&](const char* path) {
        const auto* p = dynamic_cast<const params::Parameter<float>*>(params.find(path));
        REQUIRE(p != nullptr);
        return p->base();
    };
    CHECK_THAT(valueOf("post/tonemap/chroma-retention"), Catch::Matchers::WithinAbs(0.6, 1e-5));
    CHECK_THAT(valueOf("post/bloom/emissionWeight"), Catch::Matchers::WithinAbs(0.75, 1e-5));
    CHECK_THAT(valueOf("post/output/antialias"), Catch::Matchers::WithinAbs(0.5, 1e-5));
    {
        const auto* op = dynamic_cast<const params::Parameter<int>*>(params.find("post/tonemap/operator"));
        REQUIRE(op != nullptr);
        CHECK(op->base() == 3);
        const auto* on = dynamic_cast<const params::Parameter<bool>*>(params.find("post/bloom/enabled"));
        REQUIRE(on != nullptr);
        CHECK(on->base() == false);
    }
    // `bloomLevels` is fixed when the pyramid is built, so it has no parameter; naming it must be
    // accepted rather than rejected, because several shipped scenes do.

    // A project that names the same paths is applied after the scene loads and overrides it, which
    // is the whole reason the scene's values go in as base values rather than as a separate path.
    const auto project = f.dir / "posty-project.json";
    std::ofstream(project) << R"({"format":"avgen-project","version":4,
      "assets":{"scene":{"kind":"composition","path":"media/posty.json"}},
      "parameters":{"post/tonemap/chroma-retention":0.2}})";
    app::Engine over(app::EngineMode::Offline);
    REQUIRE(over.loadProject(project).has_value());
    const auto* p = dynamic_cast<const params::Parameter<float>*>(
        over.params().find("post/tonemap/chroma-retention"));
    REQUIRE(p != nullptr);
    CHECK_THAT(p->base(), Catch::Matchers::WithinAbs(0.2, 1e-5));        // the project's value
    const auto* w = dynamic_cast<const params::Parameter<float>*>(
        over.params().find("post/bloom/emissionWeight"));
    REQUIRE(w != nullptr);
    CHECK_THAT(w->base(), Catch::Matchers::WithinAbs(0.75, 1e-5));       // ...and the scene's, where it said nothing
}

// examples/index.json is what the application's example list is built from, and nothing checked
// that its entries point at anything. A renamed folder or a typo in a path produced an entry that
// simply failed to open, at runtime, in front of whoever picked it.
TEST_CASE("Every example in the index exists and loads", "[integration][project][examples]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path examples = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples";
    const auto indexPath = examples / "index.json";
    REQUIRE(std::filesystem::exists(indexPath));
    const auto index = readJson(indexPath);
    REQUIRE(index.contains("examples"));
    REQUIRE(index["examples"].is_array());
    REQUIRE(!index["examples"].empty());

    for (const auto& entry : index["examples"]) {
        REQUIRE(entry.contains("category"));
        REQUIRE(entry.contains("description"));
    }

    // Read the menu with the application's own parser rather than a second copy of the key rules.
    // Checking the raw JSON for a 'project' key passed happily on an index the application then
    // refused to open -- the test agreed with itself while the example list came up empty.
    const auto listed = app::loadExampleIndex(indexPath);
    INFO((listed ? std::string() : listed.error().message));
    REQUIRE(listed.has_value());
    CHECK(listed->size() == index["examples"].size());

    std::set<std::string> names;
    for (const app::ExampleInfo& info : *listed) {
        INFO("example: " << info.name);
        CHECK(names.insert(info.name).second);     // the list is a menu; two identical rows is a bug
        CHECK(!info.description.empty());
        REQUIRE(std::filesystem::exists(info.file));
        // An index entry is a project or a recipe, and they open by different verbs: a project is
        // loaded, a recipe is composed. The routing rule is shared with the application rather
        // than restated here, so the menu and the test cannot disagree about what a file is.
        if (world::isRecipeFile(info.file)) {
            const auto recipe = world::WorldRecipe::loadFile(info.file);
            INFO((recipe ? std::string() : recipe.error().message));
            CHECK(recipe.has_value());
        } else {
            app::Engine engine(app::EngineMode::Offline);
            const auto loaded = engine.loadProject(info.file);
            INFO((loaded ? std::string() : loaded.error().message));
            CHECK(loaded.has_value());
        }
    }
#endif
}

// Opening a project used to be a *merge*: `params::loadProject` applied the values the document
// listed and left every other parameter alone, so anything the engine owns rather than the scene
// -- all of `post/*`, the camera's lens, exposure and focus, the input gain -- carried over from
// whatever was open before. Change the hue shift in one project, open another that says nothing
// about it, and the previous project's grade was still on the picture. `Engine::loadProject` now
// resets every serialised parameter before it applies anything, so silence in the document means
// "the default", not "whatever was there".
TEST_CASE("Opening a project resets the parameters it does not mention",
          "[integration][project][parameters]") {
    Fixture f;
    const auto graded = f.dir / "graded.json";
    std::ofstream(graded) << R"({"format":"avgen-project","version":4,
      "assets":{"scene":{"kind":"composition","path":"media/stage.json"}},
      "parameters":{"post/grade/hueShift":1.1,"post/bloom/intensity":0.9,
                    "camera/lens/focalLength":85.0,"camera/exposure/iso":1600.0,
                    "audio/inputGain":2.5}})";
    const auto plain = f.dir / "plain.json";
    std::ofstream(plain) << R"({"format":"avgen-project","version":4,
      "assets":{"scene":{"kind":"composition","path":"media/stage.json"}}})";

    app::Engine engine(app::EngineMode::Offline);
    auto value = [&](const char* path) {
        const auto* p = engine.params().find(path);
        REQUIRE(p != nullptr);
        return p->baseComponent(0);
    };
    auto engineOwned = [](std::string_view path) {
        return path.starts_with("post/") || path.starts_with("camera/lens/") ||
               path.starts_with("camera/exposure/") || path.starts_with("camera/focus/") ||
               path == "audio/inputGain";
    };
    // Factory values read off an engine that has never opened anything.
    //
    // Not `defaultComponent` on the engine under test: a scene swap re-registers these parameters
    // *from the engine's current settings*, so the leak pollutes the defaults along with the
    // values and comparing a parameter against its own default would agree with itself. This is
    // the negative control for the assertion, not a convenience.
    std::map<std::string, std::vector<float>> factory;
    {
        app::Engine pristine(app::EngineMode::Offline);
        for (const auto* p : pristine.params().ordered()) {
            if (!engineOwned(p->path())) {
                continue;
            }
            std::vector<float> v;
            for (std::size_t i = 0; i < p->componentCount(); ++i) {
                v.push_back(p->defaultComponent(i));
            }
            factory.emplace(std::string(p->path()), std::move(v));
        }
        REQUIRE(factory.size() > 50);  // the whole post chain plus the camera, not a handful
    }
    // Every engine-owned parameter, against factory, so this covers the whole family rather than
    // the five paths the documents happen to name.
    auto engineOwnedAtDefault = [&] {
        std::vector<std::string> off;
        for (const auto* p : engine.params().ordered()) {
            const auto it = factory.find(std::string(p->path()));
            if (it == factory.end()) {
                continue;
            }
            for (std::size_t i = 0; i < it->second.size(); ++i) {
                if (std::abs(p->baseComponent(i) - it->second[i]) > 1e-6f) {
                    off.push_back(std::string(p->path()));
                    break;
                }
            }
        }
        return off;
    };

    REQUIRE(engine.loadProject(graded).has_value());
    // A frame, because that is what makes the leak real: `update` copies the parameter values into
    // the engine's own `PostSettings`/`LensSettings` structs, and those structs are what a scene
    // swap re-registers the parameters *from*. Without a frame in between, the swap alone happened
    // to hide the bug -- which is why this test asserts nothing until one has run.
    engine.update(FrameTime{});
    CHECK_THAT(value("post/grade/hueShift"), Catch::Matchers::WithinAbs(1.1, 1e-5));
    CHECK_THAT(value("camera/lens/focalLength"), Catch::Matchers::WithinAbs(85.0, 1e-4));
    CHECK_THAT(value("camera/exposure/iso"), Catch::Matchers::WithinAbs(1600.0, 1e-2));
    CHECK_THAT(value("audio/inputGain"), Catch::Matchers::WithinAbs(2.5, 1e-5));

    // The other project says nothing about any of them, so all of them go home.
    REQUIRE(engine.loadProject(plain).has_value());
    engine.update(FrameTime{});
    CHECK(engineOwnedAtDefault() == std::vector<std::string>{});

    // ...and so does a value the operator changed by hand, when the same project is reopened: the
    // document is the state, not a set of edits on top of the session.
    REQUIRE(engine.loadProject(graded).has_value());
    engine.update(FrameTime{});
    engine.params().find("post/grade/saturation")->setBaseComponent(0, 1.75f);
    engine.update(FrameTime{});
    REQUIRE(engine.loadProject(graded).has_value());
    engine.update(FrameTime{});
    CHECK_THAT(value("post/grade/saturation"),
               Catch::Matchers::WithinAbs(factory.at("post/grade/saturation").front(), 1e-6));
    CHECK_THAT(value("post/grade/hueShift"), Catch::Matchers::WithinAbs(1.1, 1e-5));  // still the project's

    // A scene's own `post` block is authored state, not a leftover, so it must survive the reset:
    // it is applied after the parameters are cleared and before the project's values land.
    const auto scenePath = f.dir / "media" / "warm.json";
    std::ofstream(scenePath) << R"({"format":"avgen-scene","version":1,"name":"warm","nodes":[],
      "post":{"chromaRetention":0.42}})";
    const auto warm = f.dir / "warm-project.json";
    std::ofstream(warm) << R"({"format":"avgen-project","version":4,
      "assets":{"scene":{"kind":"composition","path":"media/warm.json"}}})";
    REQUIRE(engine.loadProject(warm).has_value());
    engine.update(FrameTime{});
    CHECK_THAT(value("post/tonemap/chroma-retention"), Catch::Matchers::WithinAbs(0.42, 1e-5));
    // ...and it does not leak into the next project either.
    REQUIRE(engine.loadProject(plain).has_value());
    engine.update(FrameTime{});
    CHECK(engineOwnedAtDefault() == std::vector<std::string>{});
}

// File > New is the same promise as opening a project: what you get is factory state. It used to
// reset only `post/*`, leaving the lens, exposure, focus and input gain from the last session.
TEST_CASE("A new project resets the camera as well as the post chain",
          "[integration][project][parameters]") {
    Fixture f;
    app::Engine engine(app::EngineMode::Offline);
    const char* paths[] = {"camera/lens/focalLength", "camera/exposure/iso", "audio/inputGain"};
    const float values[] = {135.0f, 3200.0f, 2.0f};
    for (std::size_t i = 0; i < std::size(paths); ++i) {
        auto* parameter = engine.params().find(paths[i]);
        REQUIRE(parameter != nullptr);
        parameter->setBaseComponent(0, values[i]);
    }

    engine.newProject();

    // Looked up *again* rather than held across the call. `newProject` reloads the orb scene, which
    // clears the parameter set and registers a fresh one, so every pointer taken before it is
    // dangling afterwards -- and reading one is a use-after-free that happens to return the right
    // answer most of the time, which is how this test passed for a while before ASan caught it on a
    // run where the allocator had reused the block. A test that reads freed memory is not evidence
    // about anything, including the thing it is asserting.
    for (std::size_t i = 0; i < std::size(paths); ++i) {
        auto* parameter = engine.params().find(paths[i]);
        REQUIRE(parameter != nullptr);
        INFO(paths[i]);
        CHECK(parameter->baseComponent(0) == parameter->defaultComponent(0));
        // ...and the value really was set, so the equality above is a reset and not a no-op.
        CHECK(parameter->defaultComponent(0) != values[i]);
    }
}

// The same promise as the parameters above, for the audio: what plays is what the project says.
TEST_CASE("A project that names no audio opens silent", "[integration][project][audio]") {
    Fixture f;
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(f.wav).has_value());
    const auto scored = f.dir / "scored.json";
    REQUIRE(engine.saveProject(scored).has_value());

    const auto silent = f.dir / "silent.json";
    std::ofstream(silent) << R"({"format":"avgen-project","version":4,
      "assets":{"scene":{"kind":"composition","path":"media/stage.json"}}})";
    REQUIRE(engine.loadProject(silent).has_value());
    CHECK_FALSE(engine.hasAudio());
    CHECK(engine.audioClips().empty());
    CHECK(engine.durationSeconds() == 0.0);

    // ...and the project that does name one still gets it back.
    REQUIRE(engine.loadProject(scored).has_value());
    CHECK(engine.hasAudio());
    CHECK(engine.durationSeconds() > 0.0);

    // A named file that cannot be loaded is silence too, not the piece that happened to be open:
    // the warning says what went wrong, and nothing plays under it.
    const auto broken = f.dir / "broken.json";
    std::ofstream(broken) << R"({"format":"avgen-project","version":4,
      "assets":{"audio":"media/missing.wav","scene":{"kind":"composition","path":"media/stage.json"}}})";
    REQUIRE(engine.loadProject(broken).has_value());
    CHECK_FALSE(engine.projectWarnings().empty());
    CHECK_FALSE(engine.hasAudio());
}

// ---- the Auto-director's controls travel with the project (ADR-225) ------------------------------
//
// `AutoDirectorSettings` lived on `DirectorState`, which is a member of the running `Application`
// and of nothing that is written anywhere -- so every control in the Auto-director panel reset on
// the next launch, and opening a project did not restore the direction it was cut with. For this
// struct the defaults are the settings the controls exist to move away from: `maxViewRate` and
// `maxCameraSpeed` are 0, which means off, and `dwellShots` is 1, which is the thing ADR-203 was
// written to fix.

TEST_CASE("the Auto-director's controls are saved with the project", "[project][director]") {
    Fixture fx;
    const fs::path project = fx.dir / "directed.json";

    // Every field the panel edits, each moved off its default, so a field the writer forgets is a
    // field this catches rather than one it happens to agree about. The expected values are these
    // literals, written down here and compared against what comes back out of the file -- not
    // recomputed by the same function that wrote them.
    {
        app::Engine engine(app::EngineMode::Offline);
        app::AutoDirectorSettings& d = engine.autoDirector();
        d.mode = app::DirectorMode::EditedSequence;
        d.minShotSeconds = 3.5;
        d.minBuildShotSeconds = 1.25;
        d.maxShotSeconds = 18.0;
        d.wideFocalLength = 21.0f;
        d.heroFocalLength = 85.0f;
        d.maxCameraSpeed = 0.4f;
        d.maxViewRate = 8.0f;
        d.dwellShots = 6;
        d.seed = 4242;
        REQUIRE(d.validate().has_value());
        REQUIRE(engine.saveProject(project).has_value());
    }

    // Read as a document first, so what is asserted is what is in the file rather than what a
    // loader chose to make of it.
    {
        std::ifstream in(project);
        REQUIRE(in.good());
        const nlohmann::json doc = nlohmann::json::parse(in);
        REQUIRE(doc.contains("autoDirector"));
        const nlohmann::json& d = doc.at("autoDirector");
        CHECK(d.at("mode") == "edited");
        CHECK(d.at("minShot").get<double>() == 3.5);
        CHECK(d.at("minBuildShot").get<double>() == 1.25);
        CHECK(d.at("maxShot").get<double>() == 18.0);
        CHECK(d.at("wide").get<float>() == 21.0f);
        CHECK(d.at("hero").get<float>() == 85.0f);
        CHECK(d.at("maxSpeed").get<float>() == 0.4f);
        CHECK(d.at("maxSwing").get<float>() == 8.0f);
        CHECK(d.at("dwell").get<int>() == 6);
        CHECK(d.at("seed").get<std::uint32_t>() == 4242u);
    }

    // And a second engine, which has never seen any of it, comes back with all ten.
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.autoDirector() == app::AutoDirectorSettings{}); // it really did start fresh
        REQUIRE(engine.loadProject(project).has_value());
        const app::AutoDirectorSettings& d = engine.autoDirector();
        CHECK(d.mode == app::DirectorMode::EditedSequence);
        CHECK(d.minShotSeconds == 3.5);
        CHECK(d.minBuildShotSeconds == 1.25);
        CHECK(d.maxShotSeconds == 18.0);
        CHECK(d.wideFocalLength == 21.0f);
        CHECK(d.heroFocalLength == 85.0f);
        CHECK(d.maxCameraSpeed == 0.4f);
        CHECK(d.maxViewRate == 8.0f);
        CHECK(d.dwellShots == 6);
        CHECK(d.seed == 4242u);
    }
}

TEST_CASE("a project with no director block opens on the defaults, silently", "[project][director]") {
    // The compatibility a new block is allowed to assume. A project written before it existed is
    // the ordinary case, not a fault: it must load, it must not warn, and it must not inherit the
    // settings of whatever was open before it.
    Fixture fx;
    const fs::path project = fx.dir / "undirected.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.saveProject(project).has_value());
    }
    // An untouched project writes no block at all, which is what keeps a round trip on an existing
    // file from growing one.
    {
        std::ifstream in(project);
        REQUIRE(in.good());
        const nlohmann::json doc = nlohmann::json::parse(in);
        CHECK_FALSE(doc.contains("autoDirector"));
    }
    // The engine that opens it has a *non-default* director first, so "the defaults came back" is
    // a statement about the load rather than about the engine never having been touched.
    app::Engine engine(app::EngineMode::Offline);
    engine.autoDirector().dwellShots = 9;
    engine.autoDirector().maxViewRate = 40.0f;
    REQUIRE(engine.loadProject(project).has_value());
    CHECK(engine.autoDirector() == app::AutoDirectorSettings{});
    CHECK(engine.autoDirector().dwellShots == 1);
    CHECK(engine.autoDirector().maxViewRate == 0.0f);
}

TEST_CASE("the director block survives a save, a load and a save", "[project][director]") {
    // Stability, not just symmetry: a round trip that drifts by a float each time is a file that
    // changes in version control every time it is opened.
    Fixture fx;
    const fs::path first = fx.dir / "rt1.json";
    const fs::path second = fx.dir / "rt2.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        engine.autoDirector().mode = app::DirectorMode::EditedSequence;
        engine.autoDirector().maxViewRate = 12.5f;
        engine.autoDirector().dwellShots = 4;
        engine.autoDirector().minBuildShotSeconds = 0.75;
        REQUIRE(engine.saveProject(first).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(first).has_value());
    REQUIRE(engine.saveProject(second).has_value());

    const auto blockOf = [](const fs::path& p) {
        std::ifstream in(p);
        REQUIRE(in.good());
        return nlohmann::json::parse(in).at("autoDirector");
    };
    CHECK(blockOf(first) == blockOf(second));
}

TEST_CASE("a director block outside the ranges validate() accepts is refused",
          "[project][director]") {
    // Refused rather than clamped. A project is written by this application, so the only route to
    // one of these is a hand edit or a file from a build that meant something else by the key --
    // and a silently different film is worse than a message.
    Fixture fx;
    const fs::path project = fx.dir / "bad.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        engine.autoDirector().dwellShots = 6;
        REQUIRE(engine.saveProject(project).has_value());
    }
    const auto rewrite = [&](const char* key, const nlohmann::json& value) {
        std::ifstream in(project);
        nlohmann::json doc = nlohmann::json::parse(in);
        in.close();
        doc["autoDirector"][key] = value;
        std::ofstream(project) << doc.dump(2);
    };

    app::Engine engine(app::EngineMode::Offline);
    // ADR-203's control accepts 1..12 and nothing else.
    rewrite("dwell", 40);
    CHECK_FALSE(engine.loadProject(project).has_value());
    rewrite("dwell", 0);
    CHECK_FALSE(engine.loadProject(project).has_value());
    rewrite("mode", "improvised");
    CHECK_FALSE(engine.loadProject(project).has_value());

    // ...and the same document with both fields put right loads, or the three refusals above would
    // have passed for reasons unconnected to the fields they name.
    rewrite("dwell", 12);
    rewrite("mode", "edited");
    REQUIRE(engine.loadProject(project).has_value());
    CHECK(engine.autoDirector().dwellShots == 12);
    CHECK(engine.autoDirector().mode == app::DirectorMode::EditedSequence);
}

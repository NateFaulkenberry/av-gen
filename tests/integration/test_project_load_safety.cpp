// ADR-2164: opening a project is safe for the project that was open, and saving one keeps what did not load.
//
// Two data-safety defects, both reproduced on main before this (113-engine-architecture/05-ui-readiness/
// 02-project-lifecycle.md, runs 1-3):
//
//  1. A failed open was not atomic. `Engine::loadProject` reset the engine before its later readers ran, and a
//     refusal there left a half-reset engine still pointing at the OLD project's path. The next save wrote that
//     over the old file: 5,929 bytes with an override became 1,059 bytes without it.
//  2. A save dropped references that did not resolve. A missing scene was written back as `kind: orb`, a missing
//     audio or environment was removed, and `project.validate` with no path called that project valid.
//
// Every case here is a byte comparison of the files on disk, because that is the person's work: whatever the
// engine says about itself, the test is that the file they had is the file they still have.

#include "ai/tool_api.hpp"
#include "api/session.hpp"
#include "app/engine.hpp"
#include "assets/image.hpp"
#include "audio/audio_file.hpp"
#include "params/parameter_set.hpp"
#include "support/gltf_fixture.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

json readJson(const fs::path& p) {
    std::ifstream in(p);
    return json::parse(in);
}

void writeJson(const fs::path& p, const json& doc) { std::ofstream(p) << doc.dump(2) << '\n'; }

fs::path writeTone(const fs::path& path, float hz, float seconds) {
    constexpr std::uint32_t rate = 48000;
    const auto frames = static_cast<std::size_t>(seconds * rate);
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(testsupport::sine(hz, rate, frames, 0.5f), 2),
                                                  2, rate);
    REQUIRE(file.writeWav(path).has_value());
    return path;
}

constexpr const char* kStage = R"({"format":"avgen-scene","version":1,"name":"stage","nodes":[
    {"name":"a","kind":"gltf","asset":"tri.glb"},{"name":"b","kind":"gltf","asset":"tri.glb","position":[2,0,0]}]})";

constexpr const char* kShader =
    "/* { \"inputs\": [ { \"name\": \"tint\", \"type\": \"float\", \"default\": 0.5 } ] } */\n"
    "fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> { return vec4<f32>(inputs.tint, 0.0, 0.0, 1.0); }\n";

// A project folder `root` with every kind of reference a project can hold, and project A in it: a composition scene,
// one audio file, an environment and an authored override (camera exposure 2.5). `stable` is A's bytes after one
// open and save, which is the canonical form every later save is compared against.
struct Folder {
    fs::path root;
    fs::path media;
    fs::path a;
    std::string stable;

    explicit Folder(const fs::path& where) : root(where), media(where / "media"), a(where / "A.json") {
        fs::remove_all(root);
        fs::create_directories(media);
        writeTone(media / "tone.wav", 220.0f, 1.0f);
        const auto tmpGlb = testsupport::writeTriangleGlb("load_safety_" + std::to_string(::getpid()));
        fs::copy_file(tmpGlb, media / "tri.glb", fs::copy_options::overwrite_existing);
        fs::remove(tmpGlb);
        REQUIRE(assets::writeHdr(media / "sky.hdr", 8, 4, std::vector<float>(8 * 4 * 4, 0.5f)).has_value());
        std::ofstream(media / "stage.scene.json") << kStage;
        {
            app::Engine engine(app::EngineMode::Offline);
            REQUIRE(engine.loadComposition(media / "stage.scene.json").has_value());
            REQUIRE(engine.loadAudio(media / "tone.wav").has_value());
            REQUIRE(engine.loadEnvironment(media / "sky.hdr").has_value());
            params::IParameter* exposure = engine.params().find("camera/exposure/compensation");
            REQUIRE(exposure != nullptr);
            exposure->setBaseComponent(0, 2.5f);
            REQUIRE(engine.saveProject(a).has_value());
        }
        stable = canonicalSave(a);
        REQUIRE(readJson(a).at("parameters").contains("camera/exposure/compensation"));
    }
    ~Folder() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;

    // Opens `file` in a fresh engine and saves it over itself; returns the bytes.
    static std::string canonicalSave(const fs::path& file) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(file).has_value());
        REQUIRE(engine.saveProject(file).has_value());
        return bytesOf(file);
    }

    // A copy of A with one change, written beside it.
    fs::path variant(const std::string& name, const std::function<void(json&)>& change) const {
        json doc = readJson(a);
        change(doc);
        const fs::path out = root / (name + ".json");
        writeJson(out, doc);
        return out;
    }
};

fs::path folderFor(const std::string& name) {
    return fs::temp_directory_path() / ("avgen_load_safety_" + name + "_" + std::to_string(::getpid()));
}

// What Cmd+S does: save over the open project's own file, if it has one.
void saveLikeTheEditor(app::Engine& engine) {
    if (!engine.projectPath().empty()) {
        REQUIRE(engine.saveProject(engine.projectPath()).has_value());
    }
}

float exposureOf(app::Engine& engine) {
    const params::IParameter* p = engine.params().find("camera/exposure/compensation");
    return p != nullptr ? p->baseComponent(0) : -1.0f;
}

// The damaged documents. `atomic` is true where the refusal is found before the engine changes, so the old project
// must still be open and untouched; false where it can only be found after the reset (ADR-2164 lists which), so the
// session must be an empty untitled project rather than a half-reset one under A's name.
struct Damage {
    const char* name;
    bool atomic;
    std::function<void(json&)> change;
};

const std::vector<Damage>& damages() {
    static const std::vector<Damage> list = {
        {"tempo-source", true, [](json& d) { d["control"] = {{"tempoSource", "bogus"}}; }},
        {"transport-loop-type", true,
         [](json& d) { d["transport"] = {{"loop", {{"enabled", true}, {"start", "soon"}, {"end", 4.0}}}}; }},
        {"audio-clip-gain-type", true,
         [](json& d) {
             const json ref = d["assets"]["audio"];
             d["assets"].erase("audio");
             d["assets"]["audioClips"] = json::array({{{"file", ref}, {"gain", "loud"}}});
         }},
        {"shot-span-type", true,
         [](json& d) { d["cameraShotSpans"] = json::array({{{"start", "x"}, {"end", 2.0}}}); }},
        {"palette", true, [](json& d) { d["palette"] = "red"; }},
        {"timeline", true, [](json& d) { d["timeline"] = {{"tracks", 7}}; }},
        {"sequence", true, [](json& d) { d["sequence"] = 5; }},
        {"world-macros", true, [](json& d) { d["worldMacros"] = json::object(); }},
        {"routes", true, [](json& d) { d["routes"] = json::array({{{"source", 3}}}); }},
        {"scene-kind-type", true, [](json& d) { d["assets"]["scene"]["kind"] = 5; }},
        // Only a registered parameter can say its value is the wrong type, and the parameters are registered by the
        // scene this load installs: found after the reset, so this one falls back to an untitled project.
        {"parameter-type", false, [](json& d) { d["parameters"]["camera/exposure/compensation"] = "bright"; }},
    };
    return list;
}

} // namespace

TEST_CASE("A failed open leaves the open project's file byte-identical through the next save",
          "[integration][project][adr2164]") {
    Folder f(folderFor("engine"));
    for (const Damage& damage : damages()) {
        DYNAMIC_SECTION(damage.name) {
            const fs::path bad = f.variant(std::string("B-") + damage.name, damage.change);
            app::Engine engine(app::EngineMode::Offline);
            REQUIRE(engine.loadProject(f.a).has_value());
            const auto generation = engine.projectGeneration();
            const auto opened = engine.loadProject(bad);
            REQUIRE_FALSE(opened.has_value());
            INFO(opened.error().message);
            saveLikeTheEditor(engine);
            CHECK(bytesOf(f.a) == f.stable);
            if (damage.atomic) {
                // Nothing moved: the same project, the same values, no workspace reset in Studio.
                CHECK(engine.projectPath() == f.a);
                CHECK(engine.projectGeneration() == generation);
                CHECK(exposureOf(engine) == 2.5f);
                CHECK(engine.composition() != nullptr);
            } else {
                CHECK(engine.projectPath().empty());
                CHECK(opened.error().message.find("untitled") != std::string::npos);
            }
        }
    }
}

TEST_CASE("A project whose scene file does not parse opens degraded and neither file changes on save",
          "[integration][project][adr2164]") {
    Folder f(folderFor("sceneparse"));
    // B names its own copy of the scene, saved once while it parsed so its bytes are canonical; then the copy breaks.
    fs::copy_file(f.media / "stage.scene.json", f.media / "other.scene.json");
    const fs::path b = f.variant("B", [](json& d) { d["assets"]["scene"]["path"]["path"] = "media/other.scene.json"; });
    const std::string stableB = Folder::canonicalSave(b);
    std::ofstream(f.media / "other.scene.json", std::ios::trunc) << "{ \"format\": \"avgen-scene\", nope";

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(f.a).has_value());
    REQUIRE(engine.loadProject(b).has_value()); // the editor opens it and says the scene is gone
    CHECK_FALSE(engine.projectSceneError().empty());
    // The previous project's world does not stand in for this one's.
    CHECK(engine.composition() == nullptr);
    saveLikeTheEditor(engine);
    CHECK(bytesOf(f.a) == f.stable);
    CHECK(bytesOf(b) == stableB);
}

TEST_CASE("A missing scene, audio and environment survive a save verbatim", "[integration][project][adr2164]") {
    Folder f(folderFor("missing"));
    for (const char* name : {"stage.scene.json", "tone.wav", "sky.hdr"}) {
        fs::rename(f.media / name, f.media / (std::string(name) + ".away"));
    }
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(f.a).has_value());
        CHECK_FALSE(engine.projectWarnings().empty());
        REQUIRE(engine.saveProject(f.a).has_value());
        CHECK(bytesOf(f.a) == f.stable);
        // The in-memory document says the same, so the unsaved-changes check does not see a difference either.
        CHECK(engine.projectDocument(f.a) == json::parse(f.stable));
    }
    // The files come back and the project is whole again.
    for (const char* name : {"stage.scene.json", "tone.wav", "sky.hdr"}) {
        fs::rename(f.media / (std::string(name) + ".away"), f.media / name);
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(f.a).has_value());
    CHECK(engine.projectWarnings().empty());
    CHECK(engine.composition() != nullptr);
    CHECK(engine.hasAudio());
    CHECK_FALSE(engine.environmentPath().empty());
    CHECK(exposureOf(engine) == 2.5f);
}

TEST_CASE("A missing audio clip, stem and shader layer survive a save verbatim", "[integration][project][adr2164]") {
    Folder f(folderFor("missinglists"));
    writeTone(f.media / "second.wav", 330.0f, 1.0f);
    writeTone(f.media / "stem.wav", 110.0f, 1.0f);
    std::ofstream(f.media / "layer.wgsl") << kShader;
    const fs::path p = f.root / "P.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(f.a).has_value());
        REQUIRE(engine.setAudioClips({audio::AudioClip{.file = f.media / "tone.wav"},
                                      audio::AudioClip{.file = f.media / "second.wav", .startSeconds = 0.5}})
                    .has_value());
        engine.setStems({audio::StemSource{.key = "bass", .label = "Bass", .file = f.media / "stem.wav"}});
        REQUIRE(engine.addShaderLayer(f.media / "layer.wgsl", shaders::LayerStage::Background).has_value());
        REQUIRE(engine.saveProject(p).has_value());
    }
    const std::string stable = Folder::canonicalSave(p);
    REQUIRE(readJson(p).at("assets").contains("audioClips"));
    REQUIRE(readJson(p).at("assets").contains("stems"));
    REQUIRE(readJson(p).at("shaders").size() == 1);
    for (const char* name : {"second.wav", "stem.wav", "layer.wgsl"}) {
        fs::remove(f.media / name);
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(p).has_value());
    REQUIRE(engine.saveProject(p).has_value());
    CHECK(bytesOf(p) == stable);
}

TEST_CASE("A project whose files are all present saves as it did", "[integration][project][adr2164]") {
    // The control for the two cases above: nothing about a whole project's save moved.
    Folder f(folderFor("whole"));
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(f.a).has_value());
    CHECK(engine.projectWarnings().empty());
    REQUIRE(engine.saveProject(f.a).has_value());
    CHECK(bytesOf(f.a) == f.stable);
}

// ---- through the Session: project.open, project.save, project.validate --------------------------------------------

namespace {

std::string code(const api::CallResult& r) {
    if (r.error) {
        UNSCOPED_INFO(r.error->message);
    }
    return r.error ? ai::toolErrorCodeName(r.error->code) : "none";
}

struct Rig {
    Folder folder;
    app::Engine engine{app::EngineMode::Offline};
    api::LocalHost host{engine};
    std::unique_ptr<api::Session> s;

    explicit Rig(const std::string& name) : folder(fs::weakly_canonical(fs::temp_directory_path()) /
                                                   ("avgen_load_safety_s_" + name + "_" + std::to_string(::getpid()))) {
        api::SessionOptions o;
        o.client = api::ClientInfo{api::ClientKind::Test, "test", "1"};
        o.permissions.readRoots = {folder.root};
        o.permissions.writeRoots = {folder.root};
        auto made = api::Session::create(std::move(o), host);
        REQUIRE(made.has_value());
        s = std::move(*made);
        s->pump();
    }
    api::CallResult call(const std::string& tool, const json& args) { return s->call(tool, args); }
};

std::set<std::string> missingPointers(const json& report) {
    std::set<std::string> out;
    for (const json& f : report.at("findings")) {
        if (f.at("code") == "asset.missing" && f.at("severity") == "error") {
            out.insert(f.at("pointer").get<std::string>());
        }
    }
    return out;
}

} // namespace

TEST_CASE("Session: a failed project.open and then project.save leave the open project's file byte-identical",
          "[integration][project][api][adr2164]") {
    for (const Damage& damage : damages()) {
        DYNAMIC_SECTION(damage.name) {
            Rig rig(damage.name);
            const fs::path bad = rig.folder.variant("B", damage.change);
            REQUIRE(rig.call("project.open", json{{"path", rig.folder.a.string()}}).success);
            const auto opened = rig.call("project.open", json{{"path", bad.string()}});
            REQUIRE_FALSE(opened.success);
            const auto saved = rig.call("project.save", json::object());
            INFO(code(saved));
            if (damage.atomic) {
                CHECK(saved.success); // A is still the open project, unchanged
            } else {
                CHECK(code(saved) == "FAILED_PRECONDITION"); // an untitled project has no file to write over
            }
            CHECK(bytesOf(rig.folder.a) == rig.folder.stable);
        }
    }
}

TEST_CASE("Session: project.validate reports a missing scene, audio and environment, and project.save keeps them",
          "[integration][project][api][adr2164]") {
    Rig rig("validate");
    for (const char* name : {"stage.scene.json", "tone.wav", "sky.hdr"}) {
        fs::remove(rig.folder.media / name);
    }
    const std::set<std::string> expected = {"/assets/scene/path", "/assets/audio", "/assets/environment"};

    const auto byPath = rig.call("project.validate", json{{"path", rig.folder.a.string()}});
    REQUIRE(byPath.success);
    INFO(byPath.result.dump(2).substr(0, 4000));
    CHECK(byPath.result.at("valid") == false);
    CHECK(missingPointers(byPath.result) == expected);

    REQUIRE(rig.call("project.open", json{{"path", rig.folder.a.string()}}).success);
    const auto session = rig.call("project.validate", json::object());
    REQUIRE(session.success);
    INFO(session.result.dump(2).substr(0, 4000));
    CHECK(session.result.at("valid") == false);
    CHECK(missingPointers(session.result) == expected);

    REQUIRE(rig.call("project.save", json::object()).success);
    CHECK(bytesOf(rig.folder.a) == rig.folder.stable);
}

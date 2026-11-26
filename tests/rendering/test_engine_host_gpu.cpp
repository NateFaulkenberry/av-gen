// ADR-2159 (Engine API stage S6): render and capture jobs through an EngineHost with its real GPU half, driven
// through the session's tools exactly as `avgen call` drives them (the host thread is the caller, and job.wait
// steps the jobs).
//
// The fixture is a stateful, audio-reactive scene, built through the API: a box whose emissive boost an
// `audio.bass` route with an attack/decay envelope drives, from a click track, under a camera the timeline sweeps.
// The route's envelope integrates over time, which is what makes seeking to a frame and playing to it different
// things (ADR-2159's probe).

#include "api/engine_host.hpp"
#include "api/session.hpp"
#include "app/engine.hpp"
#include "app/offline_render.hpp"
#include "assets/image.hpp"
#include "audio/audio_file.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr const char* kScene = R"({
  "format": "avgen-scene", "version": 1, "name": "jobs",
  "camera": { "mode": 1, "position": [-3.0, 1.5, 6.0], "target": [0.0, 0.0, 0.0], "fov": 45.0 },
  "environment": { "background": [0.02, 0.02, 0.05] },
  "lights": [ { "name": "key", "type": "directional", "direction": [-0.5, -0.7, -0.4], "intensity": 3.0 } ],
  "nodes": [
    { "name": "box", "kind": "procedural", "procedural": {
        "source": { "kind": "box", "size": [2, 1, 1] },
        "material": { "emissiveColor": [1, 0.5, 0.2], "emissiveIntensity": 1.5 } } }
  ]
})";

fs::path freshDir(const std::string& name) {
    const fs::path d = testsupport::processTempDir() / ("engine_host_gpu_" + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return fs::weakly_canonical(d);
}

// The project files, once per process: a scene, a project naming it with a camera sweep, and a click track.
fs::path fixtureProject() {
    static const fs::path project = [] {
        const fs::path dir = freshDir("fixture");
        std::ofstream(dir / "jobs.scene.json") << kScene;
        const json doc{
            {"format", "avgen-project"},
            {"version", 4},
            {"assets", {{"scene", {{"kind", "composition"}, {"path", "jobs.scene.json"}}}}},
            {"timeline",
             {{"enabled", true},
              {"cues", json::array()},
              {"tracks",
               {{{"target", "camera/position"}, {"component", -1}, {"enabled", true}, {"timeBase", "seconds"},
                 {"mode", "replace"}, {"loopLength", 0.0},
                 {"keys",
                  {{{"time", 0.0}, {"value", {-3.0, 1.5, 6.0}}, {"interp", "linear"}},
                   {{"time", 4.0}, {"value", {3.0, 1.5, 6.0}}, {"interp", "linear"}}}}}}}}},
            {"render", {{"width", 160}, {"height", 90}, {"fps", 24}, {"output", "png"}}}};
        std::ofstream(dir / "jobs.json") << doc.dump(1);
        const std::uint32_t rate = 48000;
        const auto clicks = testsupport::clickTrack(120.0f, rate, rate * 4);
        REQUIRE(audio::AudioFile::fromInterleaved(clicks, 1, rate).writeWav(dir / "clicks.wav").has_value());
        return dir / "jobs.json";
    }();
    return project;
}

struct GpuRig {
    fs::path root;
    std::unique_ptr<api::EngineHost> host;
    std::unique_ptr<api::Session> session;

    explicit GpuRig(const std::string& name) {
        static bool logInit = false;
        if (!logInit) {
            log::init(log::Level::Warn);
            logInit = true;
        }
        root = freshDir(name);
        const fs::path source = fixtureProject().parent_path();
        for (const char* f : {"jobs.json", "jobs.scene.json", "clicks.wav"}) {
            fs::copy_file(source / f, root / f, fs::copy_options::overwrite_existing);
        }
        api::EngineHostOptions o;
        app::GpuBackendOptions gpuOptions;
        gpuOptions.shaderSearchDirs = {fs::path(AVGEN_SHADER_SOURCE_DIR)};
        gpuOptions.pipelineCache = false;
        o.renderBackend = app::gpuRenderBackendFactory(std::move(gpuOptions));
        o.gpuLockPath = root / "avgen-gpu.lock"; // the suite already runs under the machine's lock
        host = std::make_unique<api::EngineHost>(std::move(o));
        host->bindHostThread();
        api::SessionOptions so;
        so.client = api::ClientInfo{api::ClientKind::Test, "engine-host-gpu", "1"};
        so.permissions.readRoots = {root};
        so.permissions.writeRoots = {root};
        auto s = api::Session::create(std::move(so), *host);
        REQUIRE(s.has_value());
        session = std::move(*s);
        require("project.open", json{{"path", (root / "jobs.json").string()}});
        require("asset.import_audio", json{{"file", (root / "clicks.wav").string()}});
        require("modulation.create", json{{"source", "audio.bass"},
                                          {"target", "nodes/box/emissiveBoost"},
                                          {"amount", 6.0},
                                          {"op", "add"},
                                          {"attackMs", 20.0},
                                          {"decayMs", 400.0}});
    }
    ~GpuRig() {
        if (session) {
            session->close();
        }
    }
    api::CallResult require(const std::string& tool, const json& args) {
        api::CallResult r = session->call(tool, args);
        INFO(tool << ": " << r.toWire().dump());
        REQUIRE(r.success);
        return r;
    }
    json run(const std::string& tool, const json& args) {
        const api::CallResult r = require(tool, args);
        REQUIRE(r.job.has_value());
        const api::CallResult w = session->call("job.wait", json{{"id", *r.job}, {"timeoutMs", 600'000}});
        INFO(w.toWire().dump());
        REQUIRE(w.success);
        return w.result;
    }
    std::string capture(int frame, const std::string& history, const std::string& path) {
        const json done = run("render.capture", json{{"frame", frame}, {"fps", 24}, {"width", 160}, {"height", 90},
                                                     {"history", history}, {"path", path}});
        INFO(done.dump());
        REQUIRE(done.at("state") == "succeeded");
        REQUIRE(done.at("outputs").size() == 1);
        return done.at("result").at("frameHash").get<std::string>();
    }
};

std::size_t pixelsDiffering(const fs::path& a, const fs::path& b) {
    auto x = assets::loadImage(a, false);
    auto y = assets::loadImage(b, false);
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    REQUIRE(x->data.size() == y->data.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < x->data.size(); i += 4) {
        n += (x->data[i] != y->data[i] || x->data[i + 1] != y->data[i + 1] || x->data[i + 2] != y->data[i + 2]) ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("engine host: a capture of frame N is frame N of a sequence from 0, in a fresh host too; seek is not",
          "[gpu][api][jobs][adr2159]") {
    constexpr int kFrame = 12;
    std::string played;
    std::string seeked;
    {
        GpuRig rig("capture");
        CHECK(rig.host->canRender());
        CHECK_FALSE(rig.host->jobStats().deviceCreated); // nothing has rendered yet: no device
        played = rig.capture(kFrame, "play", "a.png");
        CHECK(rig.host->jobStats().deviceCreated);
        const json seq = rig.run("render.start", json{{"range", "0:0.75"}, {"fps", 24}, {"width", 160}, {"height", 90},
                                                       {"output", "png"}, {"path", "seq"}});
        INFO(seq.dump());
        REQUIRE(seq.at("state") == "succeeded");
        const json& hashes = seq.at("result").at("frameHashes");
        REQUIRE(hashes.size() == 18);
        CHECK(hashes[kFrame].get<std::string>() == played);
        // The files agree as well as the hashes: the capture's PNG is the sequence's frame byte for byte.
        CHECK(*sha256File(rig.root / "a.png") == *sha256File(rig.root / "seq" / fmt::format("frame_{:06d}.png", kFrame)));
        seeked = rig.capture(kFrame, "seek", "a-seek.png");
        const std::size_t differing = pixelsDiffering(rig.root / "a.png", rig.root / "a-seek.png");
        WARN("history play " << played << ", seek " << seeked << ": " << differing << " of " << 160 * 90
                             << " pixels differ");
        // The route's attack/decay envelope restarts at a seek: the seeked frame is not the played one.
        CHECK(seeked != played);
        // At frame 0 there is no history, and the two are the same render.
        CHECK(rig.capture(0, "play", "z-play.png") == rig.capture(0, "seek", "z-seek.png"));
    }
    {
        GpuRig fresh("capture_fresh"); // a second host: its own device, its own engine, the same project
        CHECK(fresh.capture(kFrame, "play", "a.png") == played);
    }
}

TEST_CASE("engine host: a render right after parameter.set, unsaved, shows the change", "[gpu][api][jobs][adr2159]") {
    GpuRig rig("unsaved");
    const std::string before = rig.capture(6, "play", "before.png");
    const std::string onDisk = *sha256File(rig.root / "jobs.json");
    rig.require("parameter.set", json{{"path", "camera/fov"}, {"value", 70.0}});
    const std::string after = rig.capture(6, "play", "after.png");
    CHECK(after != before);
    CHECK(pixelsDiffering(rig.root / "before.png", rig.root / "after.png") > 100);
    CHECK(*sha256File(rig.root / "jobs.json") == onDisk); // nothing saved
}

TEST_CASE("engine host: a cancel mid-sequence keeps and lists the frames written; out-of-root output does no GPU work",
          "[gpu][api][jobs][adr2159]") {
    GpuRig rig("cancel");
    const api::CallResult outside =
        rig.session->call("render.start", json{{"range", "0:1"}, {"output", "png"}, {"path", "/tmp/avgen-outside"}});
    REQUIRE_FALSE(outside.success);
    CHECK(outside.error->code == ai::ToolErrorCode::PermissionDenied);
    CHECK_FALSE(rig.host->jobStats().deviceCreated);
    CHECK(rig.session->call("job.list").result.at("jobs").empty());

    const api::CallResult started = rig.require(
        "render.start", json{{"range", "0:4"}, {"fps", 24}, {"width", 160}, {"height", 90}, {"output", "png"}, {"path", "long"}});
    const std::string id = *started.job;
    for (int i = 0; i < 2000; ++i) {
        (void)rig.host->stepJobs();
        const auto r = rig.session->jobs().get(id);
        REQUIRE(r);
        if (r->state == api::JobState::Running && r->framesDone >= 6) {
            break;
        }
    }
    REQUIRE(rig.session->call("job.cancel", json{{"id", id}}).success);
    const json done = rig.session->call("job.wait", json{{"id", id}, {"timeoutMs", 60'000}}).result;
    INFO(done.dump());
    CHECK(done.at("state") == "cancelled");
    const std::size_t listed = done.at("outputs").size();
    CHECK(listed >= 6);
    CHECK(listed < 96);
    for (const json& o : done.at("outputs")) {
        CHECK(fs::exists(o.at("path").get<std::string>()));
        CHECK(o.at("sha256").is_string());
    }
    CHECK(done.at("result").at("frameHashes").size() >= listed);
}

TEST_CASE("engine host: while a show runs no render starts or steps, the show holds the lock, and the render after it "
          "is the render without it",
          "[gpu][api][jobs][show][adr2167]") {
    GpuRig rig("show");
    bool on = false;
    rig.host->local().setShowState([&on] {
        // ADR-2176: off is the owner editing with the transport playing and live input open -- not a show, so the
        // reference render below runs; on is an output window open.
        return on ? api::showStateFor(api::ShowFacts{.outputsOpen = 1, .transportPlaying = true})
                  : api::showStateFor(api::ShowFacts{.transportPlaying = true, .liveInput = true});
    });
    // Long enough that one 50 ms slice cannot finish it (a 160x90 frame takes about a millisecond).
    const json args{{"range", "0:20"}, {"fps", 24}, {"width", 160}, {"height", 90}, {"output", "png"}};
    // The reference, with no show.
    json ref = args;
    ref["path"] = "ref";
    const json reference = rig.run("render.start", ref);
    REQUIRE(reference.at("state") == "succeeded");

    // A refusal costs no GPU work.
    on = true;
    const api::CallResult refused = rig.session->call("render.capture", json{{"frame", 3}, {"path", "x.png"}});
    REQUIRE_FALSE(refused.success);
    CHECK(refused.error->code == ai::ToolErrorCode::Busy);
    CHECK(refused.error->details.at("field") == "show");
    REQUIRE(rig.host->updateShow().held);
    CHECK(api::readLockPid(rig.root / "avgen-gpu.lock") == static_cast<long>(::getpid()));
    on = false;
    rig.host->updateShow();
    CHECK_FALSE(fs::exists(rig.root / "avgen-gpu.lock"));

    // A render that is running when the show starts pauses between frames, and resumes when it stops.
    json run = args;
    run["path"] = "paused";
    const api::CallResult started = rig.require("render.start", run);
    REQUIRE(started.job.has_value());
    std::uint64_t before = 0;
    for (int i = 0; i < 200 && before < 3; ++i) {
        (void)rig.host->stepJobs();
        before = rig.session->call("job.status", json{{"id", *started.job}}).result.value("framesDone", std::uint64_t{0});
    }
    REQUIRE(before >= 3);
    REQUIRE(rig.session->call("job.status", json{{"id", *started.job}}).result.at("state") == "running");
    REQUIRE(before < 480);
    on = true;
    rig.host->updateShow();
    for (int i = 0; i < 30; ++i) {
        (void)rig.host->stepJobs();
    }
    const json paused = rig.session->call("job.status", json{{"id", *started.job}}).result;
    CHECK(paused.at("framesDone").get<std::uint64_t>() == before);
    CHECK(paused.at("phase").get<std::string>().rfind("paused: show active", 0) == 0);
    on = false;
    rig.host->updateShow();
    const api::CallResult w = rig.session->call("job.wait", json{{"id", *started.job}, {"timeoutMs", 600'000}});
    REQUIRE(w.success);
    REQUIRE(w.result.at("state") == "succeeded");
    CHECK(w.result.at("result").at("sequenceHash") == reference.at("result").at("sequenceHash"));
    CHECK(w.result.at("result").at("frameHashes") == reference.at("result").at("frameHashes"));
}

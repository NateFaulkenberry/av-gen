// ADR-1932: the render job pre-rolls the temporal history, so a render of a range starting mid-piece draws its first
// frame as a render from 0 draws that frame.
//
// The fixture is a box under a camera the timeline sweeps (every frame differs; a pure function of time, so a seek
// lands where a play does -- an orbit integrates and would not) with a deep slit scan, rendered through the real
// RenderJob from a project document -- the path a deliverable takes.

#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

constexpr const char* kScene = R"({
  "format": "avgen-scene", "version": 1, "name": "preroll",
  "camera": { "mode": 1, "position": [-3.0, 1.5, 6.0], "target": [0.0, 0.0, 0.0], "fov": 45.0 },
  "environment": { "background": [0.02, 0.02, 0.05] },
  "lights": [ { "name": "key", "type": "directional", "direction": [-0.5, -0.7, -0.4], "intensity": 3.0 } ],
  "nodes": [
    { "name": "box", "kind": "procedural", "procedural": {
        "source": { "kind": "box", "size": [2, 1, 1] },
        "material": { "emissiveColor": [1, 0.5, 0.2], "emissiveIntensity": 1.5 } } }
  ]
})";

void writeProject(const fs::path& file, bool slit, double start, double end, const std::string& out) {
    nlohmann::json params = nlohmann::json::object();
    if (slit) {
        params = {{"temporal/history/capacity", 24.0}, {"temporal/slit/enabled", true}, {"temporal/slit/amount", 1.0},
                  {"temporal/slit/span", 22.0},        {"temporal/slit/angle", 0.0}};
    }
    const nlohmann::json j = {
        {"format", "avgen-project"},
        {"version", 4},
        {"assets", {{"scene", {{"kind", "composition"}, {"path", "preroll.scene.json"}}}}},
        {"parameters", params},
        {"timeline",
         {{"enabled", true},
          {"cues", nlohmann::json::array()},
          {"tracks",
           {{{"target", "camera/position"}, {"component", -1}, {"enabled", true}, {"timeBase", "seconds"},
             {"mode", "replace"}, {"loopLength", 0.0},
             {"keys",
              {{{"time", 0.0}, {"value", {-3.0, 1.5, 6.0}}, {"interp", "linear"}},
               {{"time", 2.0}, {"value", {3.0, 1.5, 6.0}}, {"interp", "linear"}}}}}}}}},
        {"render",
         {{"width", 96}, {"height", 64}, {"fps", 30}, {"start", start}, {"end", end}, {"output", "png"}, {"path", out}}}};
    std::ofstream(file) << j.dump(1);
}

std::vector<std::uint8_t> frame(const fs::path& dir, int i) {
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%06d.png", i);
    auto img = assets::loadImage(dir / name, false);
    REQUIRE(img.has_value());
    return img->data;
}

} // namespace

TEST_CASE("a render of a range opens with the temporal history a render from 0 had", "[gpu][render][temporal][adr1932]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const fs::path dir = testsupport::processTempDir() / "temporal_preroll";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path previous = fs::current_path();
    fs::current_path(dir);
    std::ofstream("preroll.scene.json") << kScene;

    writeProject("full.json", true, 0.0, 2.0, "full");
    writeProject("range.json", true, 1.5, 2.0, "range");
    writeProject("off.json", false, 0.0, 2.0, "off");
    writeProject("offrange.json", false, 1.5, 2.0, "offrange");
    const auto run = [&](const char* project) {
        auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(engine->loadProject(project).has_value());
        app::RenderSettings settings = engine->renderSettings();
        app::RenderJob job(*ctx, shaders, std::move(engine), settings, fs::path("."));
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
    };
    run("full.json");
    run("range.json");
    run("off.json");
    run("offrange.json");
    setenv("AVGEN_NO_TEMPORAL_PREROLL", "1", 1); // the arm: the job as it was before ADR-1932
    writeProject("cold.json", true, 1.5, 2.0, "cold");
    run("cold.json");
    unsetenv("AVGEN_NO_TEMPORAL_PREROLL");

    const auto compare = [](const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
        std::pair<std::size_t, int> r{0, 0};
        for (std::size_t i = 0; i < a.size(); ++i) {
            const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            r.first += d > 0 ? 1 : 0;
            r.second = std::max(r.second, d);
        }
        return r;
    };
    // 1.5 s is the full render's frame 45 and the range's frame 0. The control is the same pair with no temporal
    // effect: a range and a full render differ there by a few LSB in a few pixels for reasons outside the ring, and
    // that is the bar -- the pre-rolled slit must agree as well as the control does.
    const auto control = compare(frame("off", 45), frame("offrange", 0));
    const auto rolled = compare(frame("full", 45), frame("range", 0));
    const auto cold = compare(frame("full", 45), frame("cold", 0));
    const auto rolledLast = compare(frame("full", 59), frame("range", 14));
    INFO("control: " << control.first << " bytes differ, worst " << control.second);
    INFO("pre-rolled: " << rolled.first << " bytes differ, worst " << rolled.second);
    INFO("no pre-roll: " << cold.first << " bytes differ, worst " << cold.second);
    INFO("pre-rolled, last frame: " << rolledLast.first << " bytes differ, worst " << rolledLast.second);
    CHECK(frame("full", 45) != frame("off", 45)); // the slit scan is on and visible
    CHECK(control.second <= 4);
    CHECK(rolled.second <= std::max(control.second, 1));
    CHECK(rolled.first <= control.first * 2 + 16);
    CHECK(rolledLast.second <= std::max(control.second, 1));
    // and without the pre-roll it does not agree, so the checks above can fail
    CHECK(cold.second > 16);
    CHECK(ctx->errorCount() == 0);
    fs::current_path(previous);
    fs::remove_all(dir);
}

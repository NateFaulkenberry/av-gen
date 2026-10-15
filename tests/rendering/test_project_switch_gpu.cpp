// A project's picture must not depend on which project was open before it (ADR-1390).
//
// The owner, 2026-10-15: after the Lighting Lab, Astral Forge drew straight red, green, blue and pale-yellow lines
// through the forge -- the lab's world axes and light arrows, which outlived the lab -- and "other visual anomalies
// carry over too". e3dc8e9e had found one of these (the temporal effects) and fixed that one; this test is the
// general form, so the next one is caught by the class rather than by the owner.
//
// The method is a difference, not a list. For a probe project P:
//
//   fresh     a new engine and a new renderer open P and render frames F0..Fn at a fixed step;
//   switched  ONE engine and ONE renderer -- the editor's -- open every other project X in turn, play a few frames of
//             it, then open P and render the same frames.
//
// The last frame of `switched` must equal the last frame of `fresh` byte for byte. Nothing names what may leak; any
// state that survives a switch and reaches the picture fails it, whoever owns it.
//
// "The editor's" is the load-bearing word. The overlays that leaked live in the editor, not the engine, so the
// switched side runs the editor's own rule for them: `app::ProjectSwitch`, observed once a frame against
// `Engine::projectGeneration()`, the overlays it returns built into the frame with `buildDebugGeometry` exactly as
// `Application::runLive` builds them, and a lab opened the way Engineering Labs opens one (`expectOverlays` with
// `labs::overlaysFor`, then its fixture). The renderer is reset the way `Application::resetProjectViewState` resets it.
//
// **Exact equality, and the control that justifies it.** The first section renders P fresh twice and requires the two
// to be identical; if this machine could not do that, the comparison below would be meaningless and the section says
// so before anything else does. It also renders P with a lab's overlays carried -- main's behaviour -- and requires
// THAT to differ, so a pass below is a statement about the switch and not about a comparison that cannot see lines.
//
// Which "before" projects. The default run takes every Engineering Lab (its fixture and its overlays), every live
// scene (examples/index.json `"live": true`), and the Character Animation Lab example: the projects the owner switches
// between and the ones that own overlays. `[.sweep]` runs the same thing over every project in the index; it is
// 10-20 minutes of GPU per probe, which is too long a lock to take on every suite run on a GPU two agents share
// (docs/development/project-switch-state-2026-10.md has the measured cost). AVGEN_SWITCH_STILLS=<dir> writes the
// fresh and switched frames of every pair that differs, for a person to look at.

#include "app/engine.hpp"
#include "app/project_switch.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "labs/lab.hpp"
#include "labs/overlays.hpp"
#include "rendering/debug_visualizer.hpp"
#include "rendering/scene_renderer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 180;
constexpr double kFps = 30.0;
constexpr double kProbeStart = 3.0; // seconds: past the opening fade of the probes
constexpr int kProbeFrames = 6;     // a short run, so the temporal passes have a history to differ in
constexpr int kBeforeFrames = 4;    // long enough for X's frames to have written their history

fs::path root() { return fs::path(AVGEN_SOURCE_DIR); }

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        return nullptr;
    }
    return std::move(*ctx);
}

// One editor: an engine, a renderer, and the editor's rule for what a project opens with.
struct Editor {
    gpu::Context& ctx;
    gpu::ShaderLibrary shaders;
    rendering::SceneRenderer renderer;
    // Offline by default: a render's engine. AVGEN_SWITCH_LIVE=1 runs the editor's own mode (EngineMode::Live, with
    // live control off so a MIDI device on the machine cannot reach the comparison).
    app::Engine engine{std::getenv("AVGEN_SWITCH_LIVE") != nullptr ? app::EngineMode::Live : app::EngineMode::Offline};
    app::ProjectSwitch projectSwitch;
    rendering::DebugViewOptions overlays;

    explicit Editor(gpu::Context& c) : ctx(c), shaders(c, {fs::path(AVGEN_SHADER_SOURCE_DIR)}), renderer(c, shaders) {
        engine.setLiveControl(false);
    }

    // `lab` set: Engineering Labs > that lab. Otherwise File > Open.
    [[nodiscard]] bool open(const fs::path& file, std::optional<labs::LabId> lab = std::nullopt) {
        if (lab) {
            projectSwitch.expectOverlays(labs::overlaysFor(*lab));
        }
        if (auto r = engine.loadFile(file); !r) {
            projectSwitch.cancelExpected();
            UNSCOPED_INFO("could not open " << file.string() << ": " << r.error().message);
            return false;
        }
        // `Application::serviceProjectSwitch` / `resetProjectViewState`, the parts that reach a frame.
        if (auto o = projectSwitch.observe(engine.projectGeneration())) {
            // AVGEN_SWITCH_AS_MAIN=1 is the control arm: the editor before ADR-1390, where a lab wrote its overlays
            // into the panel and an ordinary open left them there. Pointed at it, the sweep must fail.
            static const bool asMain = std::getenv("AVGEN_SWITCH_AS_MAIN") != nullptr;
            if (!asMain || lab) {
                overlays = std::move(*o);
            }
            renderer.setDiagnosticEntity({});
            renderer.resetTemporalHistory();
        }
        return true;
    }

    // Frames at a fixed step from `start`, overlays built as the live editor builds them; the last one read back.
    [[nodiscard]] std::optional<gpu::Image8> play(double start, int frames) {
        FixedStepClock clock(kFps, start);
        engine.setViewport(kWidth, kHeight);
        std::optional<gpu::Image8> last;
        for (int f = 0; f < frames; ++f) {
            const FrameTime t = engine.tick(clock);
            engine.update(t);
            const rendering::ShaderFrameInputs inputs{&engine.shaderLayers(),
                                                      engine.hasFrame() ? &engine.latestFrame() : nullptr,
                                                      engine.barPhase()};
            rendering::DebugViewOptions options = overlays;
            options.frustumAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
            renderer.setDebugDepthTest(options.depthTest);
            const rendering::ProceduralLodLevels lod =
                rendering::readProceduralLodLevels(renderer.procedurals(), engine.scene(), options);
            rendering::buildDebugGeometry(renderer.debugDraw(), engine.scene(), options, t.renderTime, nullptr, &lod,
                                          renderer.shadows().views());
            if (f + 1 < frames) {
                if (auto r = renderer.renderFrame(engine.scene(), t, kWidth, kHeight, &inputs); !r) {
                    UNSCOPED_INFO("render: " << r.error().message);
                    return std::nullopt;
                }
            } else {
                auto image = renderer.renderToImage(engine.scene(), t, kWidth, kHeight, &inputs);
                if (!image) {
                    UNSCOPED_INFO("render: " << image.error().message);
                    return std::nullopt;
                }
                last = std::move(*image);
            }
        }
        return last;
    }
};

struct Difference {
    std::size_t pixels = 0;
    int maxChannel = 0;
};

Difference compare(const gpu::Image8& a, const gpu::Image8& b) {
    Difference d;
    if (a.width != b.width || a.height != b.height) {
        d.pixels = static_cast<std::size_t>(-1);
        return d;
    }
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        int m = 0;
        for (int c = 0; c < 3; ++c) {
            m = std::max(m, std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c])));
        }
        if (m > 0) {
            ++d.pixels;
            d.maxChannel = std::max(d.maxChannel, m);
        }
    }
    return d;
}

std::string slug(std::string s) {
    for (char& c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '-';
        }
    }
    return s;
}

void writeStill(const std::string& name, const gpu::Image8& image) {
    const char* dir = std::getenv("AVGEN_SWITCH_STILLS");
    if (dir == nullptr || *dir == '\0') {
        return;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    static_cast<void>(assets::writePng(fs::path(dir) / (slug(name) + ".png"), image.width, image.height, image.rgba));
}

struct Before {
    std::string name;
    fs::path file;
    std::optional<labs::LabId> lab;
};

// The projects to open first. `everything` = every project in the index; otherwise the labs, the live scenes and the
// Character Animation Lab.
std::vector<Before> befores(bool everything) {
    std::vector<Before> out;
    for (const labs::LabDescriptor& lab : labs::labs()) {
        if (!lab.fixture.empty()) {
            out.push_back({fmt::format("lab {}", lab.key), root() / std::string(lab.fixture), lab.id});
        }
    }
    std::ifstream in(root() / "examples" / "index.json");
    const nlohmann::json index = nlohmann::json::parse(in, nullptr, false);
    if (index.is_object() && index.contains("examples")) {
        for (const auto& e : index["examples"]) {
            const std::string project = e.value("project", std::string());
            if (project.empty()) {
                continue; // a recipe entry: generated, not opened
            }
            const bool live = e.value("live", false);
            const std::string name = e.value("name", project);
            if (everything || live || name == "Character Animation Lab") {
                out.push_back({name, root() / "examples" / project, std::nullopt});
            }
        }
    }
    return out;
}

struct Probe {
    const char* name;
    fs::path file;
};

void runSweep(gpu::Context& ctx, const Probe& probe, bool everything) {
    REQUIRE(fs::is_regular_file(probe.file));
    std::optional<gpu::Image8> reference;
    {
        Editor fresh(ctx);
        REQUIRE(fresh.renderer.init().has_value());
        REQUIRE(fresh.open(probe.file));
        reference = fresh.play(kProbeStart, kProbeFrames);
        REQUIRE(reference.has_value());
    }
    writeStill(fmt::format("{} fresh", probe.name), *reference);

    Editor editor(ctx);
    REQUIRE(editor.renderer.init().has_value());
    std::vector<std::string> failures;
    std::size_t compared = 0;
    for (const Before& before : befores(everything)) {
        if (!fs::is_regular_file(before.file) || before.file == probe.file) {
            continue;
        }
        if (!editor.open(before.file, before.lab)) {
            continue; // reported by UNSCOPED_INFO; a project that cannot open cannot leak
        }
        static_cast<void>(editor.play(1.0, kBeforeFrames));
        REQUIRE(editor.open(probe.file));
        const auto image = editor.play(kProbeStart, kProbeFrames);
        REQUIRE(image.has_value());
        ++compared;
        const Difference d = compare(*reference, *image);
        if (d.pixels != 0) {
            failures.push_back(fmt::format("{} -> {}: {} pixel(s) differ, up to {} levels", before.name, probe.name,
                                           d.pixels, d.maxChannel));
            writeStill(fmt::format("{} after {}", probe.name, before.name), *image);
        }
    }
    std::string all;
    for (const std::string& f : failures) {
        all += f + "\n";
    }
    INFO(probe.name << ": " << compared << " project(s) opened before it");
    INFO(all);
    CHECK(compared > 0);
    CHECK(failures.empty());
}

const Probe kProbes[] = {
    {"Astral Forge - Trench", root() / "examples/astral-forge/astral-forge-trench.json"},
    {"Hero Metal", root() / "examples/hero/hero.json"},
};

} // namespace

TEST_CASE("The switch comparison is exact and can see a carried overlay", "[gpu][switch]") {
    auto ctx = makeContext();
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    for (const Probe& probe : kProbes) {
        INFO(probe.name);
        std::optional<gpu::Image8> a;
        std::optional<gpu::Image8> b;
        {
            Editor e(*ctx);
            REQUIRE(e.renderer.init().has_value());
            REQUIRE(e.open(probe.file));
            a = e.play(kProbeStart, kProbeFrames);
        }
        {
            Editor e(*ctx);
            REQUIRE(e.renderer.init().has_value());
            REQUIRE(e.open(probe.file));
            b = e.play(kProbeStart, kProbeFrames);
        }
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        // Determinism first: two fresh editors agree to the byte, or exact equality below would be a lie.
        const Difference twice = compare(*a, *b);
        INFO("fresh vs fresh: " << twice.pixels << " pixel(s), up to " << twice.maxChannel);
        REQUIRE(twice.pixels == 0);

        // The control: the Lighting Lab's overlays carried into the probe, which is what the editor did before
        // ADR-1390 (they were written into the panel and never written back). The comparison has to see it.
        Editor carried(*ctx);
        REQUIRE(carried.renderer.init().has_value());
        REQUIRE(carried.open(root() / std::string(labs::lab(labs::LabId::Lighting).fixture), labs::LabId::Lighting));
        static_cast<void>(carried.play(1.0, kBeforeFrames));
        const rendering::DebugViewOptions labOverlays = carried.overlays;
        REQUIRE(carried.open(probe.file));
        carried.overlays = labOverlays; // main's behaviour
        const auto withLines = carried.play(kProbeStart, kProbeFrames);
        REQUIRE(withLines.has_value());
        writeStill(fmt::format("{} control lighting lab overlays carried", probe.name), *withLines);
        const Difference control = compare(*a, *withLines);
        INFO("carried overlays: " << control.pixels << " pixel(s)");
        CHECK(control.pixels > 0);
    }
}

TEST_CASE("A project renders the same whichever lab or live scene was open before it", "[gpu][switch]") {
    auto ctx = makeContext();
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    for (const Probe& probe : kProbes) {
        runSweep(*ctx, probe, false);
    }
}

TEST_CASE("A project renders the same whichever example was open before it", "[.sweep][gpu][switch]") {
    auto ctx = makeContext();
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    for (const Probe& probe : kProbes) {
        runSweep(*ctx, probe, true);
    }
}

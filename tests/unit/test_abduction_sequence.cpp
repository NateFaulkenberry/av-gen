// Phase 1/2 instrument for the UFO abduction sequencing pass.
//
// Loads the PROJECT (ADR-264: a project's parameters are applied over its scene, and every
// `staging/abduction/*` number the director reads is in the project's 5,502) and records, per
// frame, the craft's simulated position, the beat the director is in, and the beam's state -- then
// reports every frame-to-frame discontinuity in the craft's position with the beat boundary it
// landed on.
//
// Hidden by default (`[.abduction-instrument]`) because it prints rather than asserts; the
// assertions built on what it found live in the non-hidden sections below it.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/navigation.hpp"
#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "scene/composition.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path worldDir() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world"; }
fs::path filmProject() { return worldDir() / "glowmere-valley-2-multicam.json"; }
fs::path labScene() { return worldDir() / "tractor-beam-lab.scene.json"; }

struct Sample {
    double t = 0.0;
    std::string beat;
    std::string target;
    glm::vec3 pos{0.0f};
    glm::vec3 drawn{0.0f};
    float yaw = 0.0f;
    float ground = 0.0f;
    float beamVisible = 0.0f;
    float beamRate = 0.0f;
    float step = 0.0f;   // |pos - previous pos|
    float stepY = 0.0f;
};

std::vector<Sample> playFilm(double seconds, double hz,
                             const std::vector<std::pair<std::string, float>>& overrides = {}) {
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(filmProject());
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewport(1600, 900);
    comp->scene().detailLimits.entityDistanceCull = false;

    for (const auto& [path, v] : overrides) {
        params::IParameter* p = engine.params().find(path);
        INFO(path);
        REQUIRE(p != nullptr);
        p->setBaseComponent(0, v);
    }
    const params::IParameter* beamVisible = engine.params().find("nodes/visitor-beam/visible");
    const params::IParameter* beamRate = engine.params().find("particles/visitor-beam/spawnRate");
    REQUIRE(beamVisible != nullptr);
    REQUIRE(beamRate != nullptr);

    std::vector<Sample> out;
    const double step = 1.0 / hz;
    const auto frames = static_cast<int>(std::llround(seconds * hz));
    out.reserve(static_cast<std::size_t>(frames));
    FrameTime time;
    glm::vec3 last(0.0f);
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);

        const entity::Entity* craft = comp->entityWorld().find("visitor");
        REQUIRE(craft != nullptr);
        Sample s;
        s.t = time.renderTime;
        s.beat = std::string(comp->director().beat("abduction"));
        s.target = std::string(comp->director().binding("abduction", "target"));
        s.pos = craft->state().position();
        stage::VisualPlacement vp;
        s.drawn = comp->visualPlacement("visitor", vp) ? vp.origin : s.pos;
        s.yaw = craft->state().yaw;
        const entity::Navigator& nav = comp->entityWorld().navigator();
        s.ground = nav.valid() ? nav.groundHeight(glm::vec2(s.pos.x, s.pos.z)) : 0.0f;
        s.beamVisible = beamVisible->baseComponent(0);
        s.beamRate = beamRate->baseComponent(0);
        if (i > 0) {
            s.step = glm::length(s.pos - last);
            s.stepY = s.pos.y - last.y;
        }
        last = s.pos;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace

namespace {

struct Segment {
    std::string beat;
    double start = 0.0;
    double end = 0.0;
    float simPath = 0.0f;   // path length of state().position()
    float drawnPath = 0.0f; // path length of the node's drawn origin
    float worstStep = 0.0f;
    glm::vec3 simLo{1e9f};
    glm::vec3 simHi{-1e9f};
    glm::vec3 drawnLo{1e9f};
    glm::vec3 drawnHi{-1e9f};
};

std::vector<Segment> segmentsOf(const std::vector<Sample>& run) {
    std::vector<Segment> out;
    for (std::size_t i = 0; i < run.size(); ++i) {
        if (out.empty() || out.back().beat != run[i].beat) {
            out.push_back(Segment{run[i].beat, run[i].t, run[i].t, 0, 0, 0, glm::vec3(1e9f),
                                  glm::vec3(-1e9f), glm::vec3(1e9f), glm::vec3(-1e9f)});
        }
        Segment& s = out.back();
        s.end = run[i].t;
        s.simLo = glm::min(s.simLo, run[i].pos);
        s.simHi = glm::max(s.simHi, run[i].pos);
        s.drawnLo = glm::min(s.drawnLo, run[i].drawn);
        s.drawnHi = glm::max(s.drawnHi, run[i].drawn);
        if (i > 0 && run[i - 1].beat == run[i].beat) {
            s.simPath += run[i].step;
            s.drawnPath += glm::length(run[i].drawn - run[i - 1].drawn);
            s.worstStep = std::max(s.worstStep, run[i].step);
        }
    }
    return out;
}

void reportJumps(const std::vector<Sample>& run, const char* label) {
    float worst = 0.0f;
    std::size_t jumps = 0;
    fmt::print("\n---- {}: craft steps over 1.0 m in one frame ----\n", label);
    for (std::size_t i = 1; i < run.size(); ++i) {
        if (run[i].step <= 1.0f) {
            continue;
        }
        ++jumps;
        worst = std::max(worst, run[i].step);
        fmt::print("t={:7.3f}  {:>9} -> {:<9} step={:7.3f} dy={:+8.3f}  agl {:6.2f} -> {:6.2f}\n",
                   run[i].t, run[i - 1].beat.empty() ? "-" : run[i - 1].beat,
                   run[i].beat.empty() ? "-" : run[i].beat, run[i].step, run[i].stepY,
                   run[i - 1].pos.y - run[i - 1].ground, run[i].pos.y - run[i].ground);
    }
    fmt::print("{}: {} frame(s) over 1.0 m, worst {:.3f} m\n", label, jumps, worst);
}

float worstStepOf(const std::vector<Sample>& run) {
    float worst = 0.0f;
    for (const Sample& s : run) {
        worst = std::max(worst, s.step);
    }
    return worst;
}

} // namespace

TEST_CASE("the film's abduction sequence, frame by frame", "[.abduction-instrument]") {
    const std::vector<Sample> run = playFilm(90.0, 60.0);

    fmt::print("\n---- beats ----\n");
    for (const Segment& s : segmentsOf(run)) {
        const glm::vec3 simBox = s.simHi - s.simLo;
        const glm::vec3 drawnBox = s.drawnHi - s.drawnLo;
        fmt::print("{:>9}  {:7.3f}..{:7.3f} ({:5.2f}s)  simPath={:8.2f}  simBox=({:5.2f},{:5.2f},"
                   "{:5.2f})  drawnPath={:8.2f}  drawnBox=({:5.2f},{:5.2f},{:5.2f})  worst={:6.3f}\n",
                   s.beat.empty() ? "-" : s.beat, s.start, s.end, s.end - s.start, s.simPath,
                   simBox.x, simBox.y, simBox.z, s.drawnPath, drawnBox.x, drawnBox.y, drawnBox.z,
                   s.worstStep);
    }
    reportJumps(run, "shipped");

    // ---- beam visibility against craft motion ----------------------------------------------
    fmt::print("\n---- the beam's visible window against the craft's motion ----\n");
    bool up = false;
    double upAt = 0.0;
    float path = 0.0f;
    for (std::size_t i = 1; i < run.size(); ++i) {
        const bool now = run[i].beamVisible > 0.5f;
        if (now && !up) {
            up = true;
            upAt = run[i].t;
            path = 0.0f;
        }
        if (now) {
            path += run[i].step;
        }
        if (!now && up) {
            up = false;
            fmt::print("beam visible {:7.3f}..{:7.3f} ({:5.2f}s); craft travelled {:7.2f} m in it\n",
                       upAt, run[i].t, run[i].t - upAt, path);
        }
    }
    if (up) {
        fmt::print("beam visible {:7.3f}..end; craft travelled {:7.2f} m in it\n", upAt, path);
    }
}

// The control arm (ADR-182): the same film with `cruiseClearance` lowered to `hoverHeight`. If the
// 11 m teleport is the clearance clamp, this run has no teleport at all -- and if it is anything
// else, the teleport survives. A probe that cannot come out the other way proves nothing.
TEST_CASE("the abduction jump is the clearance clamp, not the beat change",
          "[.abduction-instrument]") {
    const std::vector<Sample> shipped = playFilm(30.0, 60.0);
    const std::vector<Sample> control = playFilm(30.0, 60.0, {{"staging/abduction/cruiseClearance",
                                                               23.0f}});
    reportJumps(shipped, "shipped  (cruiseClearance 34, hoverHeight 23)");
    reportJumps(control, "control  (cruiseClearance 23, hoverHeight 23)");
    fmt::print("\nworst single-frame craft step: shipped {:.3f} m, control {:.3f} m\n",
               worstStepOf(shipped), worstStepOf(control));
}

namespace {

// The same measurement against the Tractor Beam Lab, which is a *scene* with no project over it and
// no Glowmere in it. If the teleport is here too, the mechanism is the director's and not the
// film's data; if it is not, it is the film's data alone. The lab authors the same
// `cruiseClearance` 34 / `hoverHeight` 23 pair, so what this separates is "the numbers" from "the
// place they are written down".
std::vector<Sample> playLab(double seconds, double hz, float cruiseClearance) {
    assets::AssetRegistry registry(labScene().parent_path());
    auto loaded = scene::Composition::loadFile(labScene(), registry);
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp->attach(params, modulator);
    comp->setViewport(1600, 900);
    comp->scene().detailLimits.entityDistanceCull = false;
    if (cruiseClearance > 0.0f) {
        params::IParameter* p = params.find("staging/abduction/cruiseClearance");
        REQUIRE(p != nullptr);
        p->setBaseComponent(0, cruiseClearance);
    }

    std::vector<Sample> out;
    const double step = 1.0 / hz;
    const auto frames = static_cast<int>(std::llround(seconds * hz));
    FrameTime time;
    glm::vec3 last(0.0f);
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);

        const entity::Entity* craft = comp->entityWorld().find("visitor");
        REQUIRE(craft != nullptr);
        Sample s;
        s.t = time.renderTime;
        s.beat = std::string(comp->director().beat("abduction"));
        s.pos = craft->state().position();
        s.drawn = s.pos;
        const entity::Navigator& nav = comp->entityWorld().navigator();
        s.ground = nav.valid() ? nav.groundHeight(glm::vec2(s.pos.x, s.pos.z)) : 0.0f;
        if (i > 0) {
            s.step = glm::length(s.pos - last);
            s.stepY = s.pos.y - last.y;
        }
        last = s.pos;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace

// Phase 3: is it the animation system or the film's data?
TEST_CASE("the same teleport happens in the Tractor Beam Lab", "[.abduction-instrument]") {
    const std::vector<Sample> lab = playLab(40.0, 60.0, 0.0f);
    const std::vector<Sample> labFlat = playLab(40.0, 60.0, 23.0f);
    reportJumps(lab, "lab      (cruiseClearance 34, hoverHeight 23)");
    reportJumps(labFlat, "lab ctrl (cruiseClearance 23, hoverHeight 23)");
    fmt::print("\nworst single-frame craft step: lab {:.3f} m, lab control {:.3f} m\n",
               worstStepOf(lab), worstStepOf(labFlat));
}

// The spec's Phase 2 table: the craft's state at T-1.0, T-0.5, T and T+0.1 around every tractor
// beam activation, where T is the frame `nodes/visitor-beam/visible` goes up.
TEST_CASE("the craft's state around every beam activation", "[.abduction-instrument]") {
    const double hz = 60.0;
    const std::vector<Sample> run = playFilm(90.0, hz);
    const auto at = [&](double t) -> const Sample& {
        auto i = static_cast<std::size_t>(std::llround(t * hz));
        i = std::min(i, run.size() - 1);
        return run[i];
    };
    for (std::size_t i = 1; i < run.size(); ++i) {
        if (!(run[i].beamVisible > 0.5f && run[i - 1].beamVisible <= 0.5f)) {
            continue;
        }
        const double T = run[i].t;
        fmt::print("\n---- beam activation at T = {:.3f} s ----\n", T);
        fmt::print("{:>8}  {:>9}  {:>26}  {:>7}  {:>8}  {:>7}  {:>7}\n", "offset", "beat",
                   "craft world position", "agl", "yaw deg", "speed", "beam");
        for (const double d : {-1.0, -0.5, -1.0 / hz, 0.0, 0.1}) {
            const Sample& s = at(T + d);
            fmt::print("{:+8.3f}  {:>9}  ({:8.2f},{:8.2f},{:8.2f})  {:7.2f}  {:8.2f}  {:7.3f}  "
                       "{:>7}\n",
                       d, s.beat.empty() ? "-" : s.beat, s.pos.x, s.pos.y, s.pos.z,
                       s.pos.y - s.ground, s.yaw * 57.2957795f, s.step * static_cast<float>(hz),
                       s.beamVisible > 0.5f ? "ON" : "off");
        }
    }
}

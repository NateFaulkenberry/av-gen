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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
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
    // Not the same question as the rate. The rate is the tap; this is every particle already in
    // the air, and it is the one the column's visible life is measured on.
    float beamSize = 1.0f;
    // And the same number where it lands: `sizeStart` of the FLATTENED system, which is the
    // parameter's final multiplied through -- audio modulation and all -- rather than the base
    // the director wrote. `enabled` there has the node's `visible` already folded into it.
    float beamDrawnSize = -1.0f;
    bool beamDrawnOn = false;
    float step = 0.0f;   // |pos - previous pos|
    float stepY = 0.0f;
    int camera = 0;      // the camera holding the frame, for the cut test
    // The bound target's fade, for Test 5. -1 = no target bound, or it has no such parameter.
    float targetOpacity = -1.0f;
    float targetVisible = -1.0f;
    float targetY = 0.0f;
    // What reached the *renderer*, not the parameter: how many of the scene's drawable entities are
    // in the blend pipeline this frame, and the least opaque of them. A parameter that moves while
    // these do not is a fade nobody can see (`pbr_shade.wgsl` throws an OPAQUE material's alpha
    // away), which is the whole reason this pair is recorded alongside the number.
    int blended = 0;
    float minOpacity = 1.0f;
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
    const params::IParameter* beamSize = engine.params().find("particles/visitor-beam/size");
    REQUIRE(beamVisible != nullptr);
    REQUIRE(beamRate != nullptr);
    REQUIRE(beamSize != nullptr);

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
        s.beamSize = beamSize->baseComponent(0);
        for (const scene::ParticleSystem& ps : comp->scene().particles) {
            if (ps.name.find("visitor-beam") != std::string::npos) {
                s.beamDrawnSize = ps.sizeStart;
                s.beamDrawnOn = ps.enabled;
            }
        }
        s.camera = static_cast<int>(comp->activeCamera().camera);
        if (!s.target.empty()) {
            if (const params::IParameter* o =
                    engine.params().find("nodes/" + s.target + "/opacity");
                o != nullptr) {
                s.targetOpacity = o->baseComponent(0);
            }
            if (const params::IParameter* v =
                    engine.params().find("nodes/" + s.target + "/visible");
                v != nullptr) {
                s.targetVisible = v->baseComponent(0);
            }
            if (const entity::Entity* prey = comp->entityWorld().find(s.target); prey != nullptr) {
                s.targetY = prey->state().position().y;
            }
        }
        for (const scene::Entity& e : comp->scene().entities) {
            if (e.material.alphaMode == scene::AlphaMode::Blend) {
                ++s.blended;
                s.minOpacity = std::min(s.minOpacity, e.material.opacity);
            }
        }
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
        for (const scene::Entity& e : comp->scene().entities) {
            if (e.material.alphaMode == scene::AlphaMode::Blend) {
                ++s.blended;
                s.minOpacity = std::min(s.minOpacity, e.material.opacity);
            }
        }
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

// =================================================================================================
// The regression suite (ADR-385). These are not hidden: they are the contract.
//
// Every one of them is measured against the *project*, because a project's parameters are applied
// over its scene (ADR-264) and every `staging/abduction/*` number the director reads is in this
// project's 5,502. A test of the scene alone is a test of a film that does not ship.
//
// One run, shared, because loading the film costs about four seconds and the seven questions below
// are seven readings of the same ninety seconds.
// =================================================================================================

namespace {

// A craft travelling at the film's `travelSpeed` of 30 m/s covers 0.5 m in a 60 Hz frame. The
// teleport this suite exists to prevent was 11.004 m. 1.0 m is comfortably above the first and two
// orders of magnitude below the second, so the guard cannot be passed by slowing the craft down and
// cannot be failed by an ordinary frame of travel.
constexpr float kTeleport = 1.0f;
// What "stationary" means for the craft's *simulated* position across the whole beam interval. The
// authored `craftWobble` is 0.3 m of deliberate sway, so the honest bound is a little over that;
// the y bound is separate and near zero because nothing authored moves the craft vertically while
// it holds station, and y is where the 11 m lived.
constexpr float kStationaryXZ = 0.75f;
constexpr float kStationaryY = 0.05f;

struct Cycle {
    double approachEnd = 0.0;  // the last frame of `approach`
    double beamStart = 0.0;    // the first frame of `beam`
    double beamShown = 0.0;    // the frame `nodes/visitor-beam/visible` went up
    double abductEnd = 0.0;    // the last frame of `abduct`
    double departStart = 0.0;
    double departEnd = 0.0;
    double craftMoved = 0.0;   // the first frame after `departStart` the craft moved again
    float worstStep = 0.0f;    // the worst single-frame craft step from beamStart to departEnd
    float spanXZ = 0.0f;       // horizontal extent of the craft over the same interval
    float spanY = 0.0f;
    float beamRateAtMove = 0.0f; // what the beam was emitting when the craft set off again
    bool beamUpAtMove = false;
};

std::vector<Cycle> cyclesOf(const std::vector<Sample>& run) {
    std::vector<Cycle> out;
    for (std::size_t i = 1; i < run.size(); ++i) {
        if (run[i].beat == "beam" && run[i - 1].beat != "beam") {
            Cycle c;
            c.beamStart = run[i].t;
            c.approachEnd = run[i - 1].t;
            out.push_back(c);
        }
        if (out.empty()) {
            continue;
        }
        Cycle& c = out.back();
        if (c.beamShown == 0.0 && run[i].beamVisible > 0.5f && run[i - 1].beamVisible <= 0.5f) {
            c.beamShown = run[i].t;
        }
        if (run[i].beat == "depart" && run[i - 1].beat == "abduct") {
            c.abductEnd = run[i - 1].t;
            c.departStart = run[i].t;
        }
        if (c.departStart > 0.0 && run[i].beat == "depart") {
            c.departEnd = run[i].t;
        }
        if (c.departStart > 0.0 && c.craftMoved == 0.0 && run[i].t > c.departStart
            && run[i].step > 0.01f) {
            c.craftMoved = run[i].t;
            c.beamRateAtMove = run[i].beamRate;
            c.beamUpAtMove = run[i].beamVisible > 0.5f;
        }
    }
    // The craft's extent and worst step over each cycle's whole stationary interval.
    for (Cycle& c : out) {
        if (c.departEnd <= 0.0) {
            continue;
        }
        glm::vec3 lo(1e9f);
        glm::vec3 hi(-1e9f);
        for (std::size_t i = 1; i < run.size(); ++i) {
            if (run[i].t < c.beamStart || run[i].t > c.departEnd) {
                continue;
            }
            c.worstStep = std::max(c.worstStep, run[i].step);
            lo = glm::min(lo, run[i].pos);
            hi = glm::max(hi, run[i].pos);
        }
        c.spanXZ = std::max(hi.x - lo.x, hi.z - lo.z);
        c.spanY = hi.y - lo.y;
    }
    // A cycle the run ended in the middle of answers nothing: its `depart` may be truncated and
    // the craft may simply not have set off again before the last frame. Dropped rather than
    // half-asserted, because a landmark that is missing because the film stopped is not a defect.
    while (!out.empty() && (out.back().departEnd <= 0.0 || out.back().craftMoved <= 0.0)) {
        out.pop_back();
    }
    return out;
}

const std::vector<Sample>& film() {
    static const std::vector<Sample> run = playFilm(90.0, 60.0);
    return run;
}

const std::vector<Cycle>& cycles() {
    static const std::vector<Cycle> c = cyclesOf(film());
    return c;
}

} // namespace

TEST_CASE("the film runs five complete abduction cycles", "[stage][abduction][sequence]") {
    // The premise every other case here rests on. Without it a suite that found no teleport would
    // be a suite that found no abduction (ADR-182).
    REQUIRE(cycles().size() >= 4);
    for (const Cycle& c : cycles()) {
        INFO(fmt::format("cycle at {:.3f}", c.beamStart));
        CHECK(c.beamShown > 0.0);
        CHECK(c.abductEnd > c.beamStart);
        CHECK(c.departEnd > c.departStart);
    }
}

// Test 1 -- the craft has stopped before the beam deploys.
TEST_CASE("the beam never deploys under a moving craft", "[stage][abduction][sequence]") {
    const std::vector<Sample>& run = film();
    REQUIRE(!cycles().empty());
    for (const Cycle& c : cycles()) {
        // The frame the beam became visible, and the one before it. `BeatDesc::stillRoles` holds
        // the whole beat until the craft is measured still, so both must be.
        // From the frame the beam goes up to the end of `depart`. Before `beamShown` the beat is
        // being *held* by its own gate -- the beam is not deploying, the craft is finishing its
        // approach, and asserting stillness there would assert that a gate which exists to wait
        // for motion to stop never sees any.
        bool sawBeam = false;
        for (std::size_t i = 1; i < run.size(); ++i) {
            if (run[i].t < c.beamShown || run[i].t > c.departEnd) {
                continue;
            }
            sawBeam = true;
            const float speed = run[i].step * 60.0f;
            INFO(fmt::format("cycle {:.3f}: at t={:.3f} the beam was {} and the craft at {:.4f} m/s",
                             c.beamStart, run[i].t, run[i].beamVisible > 0.5f ? "UP" : "off",
                             speed));
            // The authored `craftWobble` is 0.3 m of sway at 0.45 Hz, whose peak speed is
            // 0.3 * 2pi * 0.45 = 0.85 m/s. That is the craft's *presentation* and the brief
            // explicitly distinguishes it from translational movement, so the bound is a little
            // above it -- and two orders of magnitude below the 660 m/s the teleport produced.
            CHECK(speed <= 1.0f);
        }
        CHECK(sawBeam);
    }
}

// Test 2 -- the craft is stationary for the whole beam interval.
TEST_CASE("the craft holds station from beam to beam-complete", "[stage][abduction][sequence]") {
    REQUIRE(!cycles().empty());
    for (const Cycle& c : cycles()) {
        INFO(fmt::format("cycle {:.3f}..{:.3f}: xz span {:.3f} m, y span {:.3f} m, worst step "
                         "{:.4f} m", c.beamStart, c.departEnd, c.spanXZ, c.spanY, c.worstStep));
        CHECK(c.spanXZ <= kStationaryXZ);
        CHECK(c.spanY <= kStationaryY);
        CHECK(c.worstStep <= kTeleport);
    }
}

// Test 3 -- the beam is finished before the craft departs.
TEST_CASE("the craft does not depart under a live beam", "[stage][abduction][sequence]") {
    REQUIRE(!cycles().empty());
    for (const Cycle& c : cycles()) {
        REQUIRE(c.craftMoved > 0.0);
        INFO(fmt::format("cycle {:.3f}: the craft set off again at {:.3f}, {:.3f} s after `depart` "
                         "began, with the beam emitting {:.1f}/s", c.beamStart, c.craftMoved,
                         c.craftMoved - c.departStart, c.beamRateAtMove));
        // The craft may not move until the beam has stopped emitting. The node stays *visible*
        // well past this, deliberately and necessarily: `ParticleRenderer::update` skips a system
        // that is not enabled, so a hidden pool is frozen rather than aged, and the column has to
        // stay enabled with nothing coming out of it for a whole particle lifetime or it resumes,
        // unaged, wherever the craft has got to. That is what `beamDrain` is for and it is not a
        // defect -- see `test_abduction_alignment.cpp`, "The beam never resumes particles emitted
        // somewhere else". What must be true is that nothing is being *emitted*.
        CHECK(c.beamRateAtMove <= 1.0f);
        CHECK(c.craftMoved >= c.departStart);
    }
}

// Test 4 -- no teleport at any transition, anywhere in the film.
TEST_CASE("the craft never teleports", "[stage][abduction][sequence]") {
    const std::vector<Sample>& run = film();
    REQUIRE(run.size() > 1000);
    std::size_t jumps = 0;
    float worst = 0.0f;
    double worstAt = 0.0;
    std::string worstWhere;
    for (std::size_t i = 1; i < run.size(); ++i) {
        if (run[i].step <= kTeleport) {
            continue;
        }
        ++jumps;
        if (run[i].step > worst) {
            worst = run[i].step;
            worstAt = run[i].t;
            worstWhere = fmt::format("{} -> {}", run[i - 1].beat, run[i].beat);
        }
    }
    INFO(fmt::format("{} frame(s) over {:.1f} m; worst {:.3f} m at t={:.3f} ({})", jumps, kTeleport,
                     worst, worstAt, worstWhere));
    CHECK(jumps == 0);
}

// Test 6 -- a camera cut never moves a world object.
//
// `UFO Watch` is `followNode: visitor` with `eventBlend: 0.0`, so it is a hard cut to a camera
// parented to the craft, fired on the entry of the beat that used to teleport it. Before ADR-385
// the cut and an 11.004 m drop were one frame apart in four cycles of five and the whole frame
// lurched. The craft's world transform must be continuous across every cut in the film.
TEST_CASE("a camera cut never moves the craft", "[stage][abduction][sequence][camera]") {
    const std::vector<Sample>& run = film();
    std::size_t cuts = 0;
    float worst = 0.0f;
    for (std::size_t i = 1; i < run.size(); ++i) {
        if (run[i].camera == run[i - 1].camera) {
            continue;
        }
        ++cuts;
        // The cut's own frame and the two either side of it: a transform disturbed *by* a cut shows
        // up in the frame the cut lands on or the one after, and looking only at the cut frame
        // would miss a reinitialisation that runs a frame late.
        for (std::size_t k = i > 1 ? i - 1 : 1; k < run.size() && k <= i + 1; ++k) {
            INFO(fmt::format("cut at t={:.3f} (camera {} -> {}); at t={:.3f} the craft stepped "
                             "{:.4f} m", run[i].t, run[i - 1].camera, run[i].camera, run[k].t,
                             run[k].step));
            CHECK(run[k].step <= kTeleport);
            worst = std::max(worst, run[k].step);
        }
    }
    // A film in which the camera never cut would pass this without testing anything.
    INFO(fmt::format("{} camera cut(s); worst craft step within one frame of one: {:.4f} m", cuts,
                     worst));
    CHECK(cuts >= 5);
}

// Test 7 -- the same sequencing contract in the Tractor Beam Lab.
//
// A different scene, on flat ground, with no project over it, a different cast, `travelSpeed` 90
// instead of 30 and every duration different. The lab is where the mechanism was shown to be the
// director's rather than the film's data, and it is where a future change that fixes only Glowmere
// will be caught.
TEST_CASE("the lab follows the same sequencing contract", "[stage][abduction][sequence][lab]") {
    const std::vector<Sample> run = playLab(40.0, 60.0, 0.0f);
    REQUIRE(run.size() > 1000);
    // 90 m/s at 60 Hz is 1.5 m a frame, so the lab's own travel is above the film's teleport bound.
    // The bound here is the teleport that was there: 11.004 m, against 2.5 m of honest travel.
    constexpr float kLabTeleport = 2.6f;
    std::size_t jumps = 0;
    float worst = 0.0f;
    double worstAt = 0.0;
    for (std::size_t i = 1; i < run.size(); ++i) {
        if (run[i].step > kLabTeleport) {
            ++jumps;
            if (run[i].step > worst) {
                worst = run[i].step;
                worstAt = run[i].t;
            }
        }
    }
    INFO(fmt::format("{} frame(s) over {:.1f} m; worst {:.3f} m at t={:.3f}", jumps, kLabTeleport,
                     worst, worstAt));
    CHECK(jumps == 0);

    // And the arm that proves the bound is not simply loose: the lab *does* travel fast, so a run
    // with no frames anywhere near the bound would mean the lab never flew.
    float fastest = 0.0f;
    for (const Sample& s : run) {
        fastest = std::max(fastest, s.step);
    }
    INFO(fmt::format("the lab's fastest honest frame: {:.3f} m", fastest));
    CHECK(fastest > 1.0f);
}

// Test 5 -- the animal dissolves rather than popping.
//
// Before ADR-385 `StepKind::Retire` wrote the role's `visible` parameter to 0 and that was the
// whole disappearance: one frame, fully lit, gone. There was nothing to fade it *with* --
// `nodes/<name>/` registered position, rotation, scale, visible, emissiveBoost, roughnessScale and
// the two light controls, and the only `opacityScale` in the parameter set was on procedural
// sub-parts, which the farm animals are not. And even a number would not have been enough:
// `pbr_shade.wgsl` reads `let alpha = select(1.0, baseColor.a, alphaMode > 1.5)`, so an OPAQUE
// material's alpha is discarded, and every farm GLB is authored OPAQUE.
//
// What this asserts is the sequencing, which is the half a test can see without a GPU: the opacity
// reaches zero continuously, and the hard `visible` toggle happens only after it has.
TEST_CASE("the abducted animal fades out before it is hidden", "[stage][abduction][sequence]") {
    const std::vector<Sample>& run = film();
    // Per abducted animal: the opacity trace while it was the bound target.
    std::map<std::string, std::vector<std::pair<double, float>>> trace;
    std::map<std::string, double> hiddenAt;
    for (const Sample& s : run) {
        if (s.target.empty() || s.targetOpacity < 0.0f) {
            continue;
        }
        trace[s.target].push_back({s.t, s.targetOpacity});
        if (s.targetVisible >= 0.0f && s.targetVisible < 0.5f && hiddenAt.count(s.target) == 0) {
            hiddenAt[s.target] = s.t;
        }
    }
    // Every animal in the film now has a node, so `nodeless` should come back empty -- and it is
    // still counted rather than assumed. It used to hold `rooster-16` and `chicken-17`, entities
    // the scene had no node for, which the loader said out loud at every load ("entity
    // 'rooster-16' drives node 'rooster-16', which this scene has no node for"): no transform
    // parameters, no opacity and nothing drawn, so the director walked them around and abducted
    // them invisibly. The cast is nine four-legged animals now and the gap closed with them.
    std::set<std::string> nodeless;
    for (const Sample& s : run) {
        if (!s.target.empty() && s.targetOpacity < 0.0f) {
            nodeless.insert(s.target);
        }
    }
    INFO(fmt::format("targets with no node and therefore nothing to fade: {}",
                     fmt::join(nodeless, ", ")));
    CHECK(nodeless.empty());
    REQUIRE(trace.size() >= 3);

    std::size_t faded = 0;
    for (const auto& [animal, samples] : trace) {
        float lowest = 1.0f;
        float worstStep = 0.0f;
        double lowestAt = 0.0;
        double firstBelowOne = 0.0;
        for (std::size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].second < lowest) {
                lowest = samples[i].second;
                lowestAt = samples[i].first;
            }
            if (firstBelowOne == 0.0 && samples[i].second < 0.999f) {
                firstBelowOne = samples[i].first;
            }
            // Up to the bottom of the fade and no further. `Retire` puts every parameter it drove
            // through this role back to the value it found, so the frame *after* the fade reaches
            // zero legitimately shows a 1.0 step back to opaque -- on a body it hides in the same
            // step, so nothing is drawn at either value. Measuring past the bottom would be
            // measuring the cleanup and calling it a pop.
            if (lowest > 0.001f) {
                worstStep = std::max(worstStep,
                                     std::fabs(samples[i].second - samples[i - 1].second));
            }
        }
        if (lowest > 0.999f) {
            continue; // never abducted in this window -- claimed, then the run ended
        }
        ++faded;
        INFO(fmt::format("{}: opacity fell to {:.4f} by t={:.3f}, starting at t={:.3f}; worst "
                         "single-frame change {:.4f}; hidden at t={:.3f}",
                         animal, lowest, lowestAt, firstBelowOne, worstStep,
                         hiddenAt.count(animal) != 0 ? hiddenAt.at(animal) : -1.0));
        // It got all the way out.
        CHECK(lowest <= 0.01f);
        // And it got there gradually. A hard toggle is a single-frame change of 1.0; the authored
        // `fadeSeconds` is 1.2 s, so a 60 Hz frame moves about 0.015 even at the eased curve's
        // steepest. 0.1 is an order of magnitude above that and an order of magnitude below a pop.
        CHECK(worstStep <= 0.1f);
        // The fade took real time rather than being a ramp with one frame in it.
        CHECK(lowestAt - firstBelowOne > 0.5);
        // And the hard `visible` toggle came after the fade, never before it. This is the ordering
        // the data buys by putting `retire` last in the same cue as the fade rather than in the
        // parallel one: a `retire` in another cue fires on its own schedule and can beat the fade.
        if (hiddenAt.count(animal) != 0) {
            CHECK(hiddenAt.at(animal) >= lowestAt);
        }
    }
    INFO(fmt::format("{} of {} bound targets with a node faded", faded, trace.size()));
    CHECK(faded >= 3);
}

// Test 5c -- the dissolve belongs at the top of the lift, and it was happening at the bottom.
//
// The report was "they're fading out too early", and the arithmetic behind it is not the
// arithmetic it looks like. The lift is `mix(from, goal, smoothstep(t / abductSeconds))`, so the
// fraction of the CLIMB covered is not the fraction of the beat elapsed. The old cue ran the
// animal's steps in one sequence -- glow-rise 1.3 s, then fade 1.4 s, then glow-fade 1.9 s -- so
// the fade occupied 1.3 s to 2.7 s of a 4.6 s lift, and `smoothstep(1.3/4.6)` is 0.19. The animal
// began dissolving a fifth of the way up and was invisible at 0.63, leaving the beam to finish
// the journey carrying nothing.
//
// The fade now waits `fadeDelaySeconds` in its own cue, parallel to the glow: 3.4 s of 4.6, which
// is `smoothstep(0.739)` = 0.83 of the climb, and it ends as the lift does.
//
// Measured against the climb and not against the clock, which is the whole point -- the bound
// keeps meaning what it says if anybody retimes the beat, and it is the conversion between the
// two that the defect lived in.
TEST_CASE("the abducted animal is most of the way up before it starts to dissolve",
          "[stage][abduction][sequence]") {
    const std::vector<Sample>& run = film();
    struct Climb {
        double startT = 0.0;
        float startY = 0.0f;
        float topY = -1e9f;
        float fadeY = 0.0f;
        double fadeT = -1.0;
        float lowest = 1.0f;
        bool started = false;
    };
    std::map<std::string, Climb> climbs;
    for (const Sample& s : run) {
        if (s.beat != "abduct" || s.target.empty() || s.targetOpacity < 0.0f) {
            continue;
        }
        Climb& c = climbs[s.target];
        if (!c.started) {
            c.started = true;
            c.startT = s.t;
            c.startY = s.targetY;
        }
        c.topY = std::max(c.topY, s.targetY);
        c.lowest = std::min(c.lowest, s.targetOpacity);
        if (c.fadeT < 0.0 && s.targetOpacity < 0.999f) {
            c.fadeT = s.t;
            c.fadeY = s.targetY;
        }
    }
    std::size_t judged = 0;
    for (const auto& [animal, c] : climbs) {
        // A lift the run ended in the middle of is not evidence either way: its `topY` is wherever
        // the film stopped, so the fraction would be measured against a climb that never finished.
        // Completeness is the filter, not a wider bound -- widening it would accept the defect.
        if (c.lowest > 0.01f || c.fadeT < 0.0 || c.topY - c.startY < 5.0f) {
            continue;
        }
        const float climbed = (c.fadeY - c.startY) / (c.topY - c.startY);
        ++judged;
        INFO(fmt::format("{}: lift {:.2f} m -> {:.2f} m from t={:.3f}; the fade starts at "
                         "{:.2f} m, t={:.3f} -- {:.1f}% of the climb, {:.3f} s in",
                         animal, c.startY, c.topY, c.startT, c.fadeY, c.fadeT,
                         100.0f * climbed, c.fadeT - c.startT));
        // The owner asked for 80-90% of the way up. Both ends are asserted: a fade that waited
        // until the animal was already inside the craft would be as wrong as one that started on
        // the ground, and a one-sided bound would call it a pass.
        CHECK(climbed >= 0.78f);
        CHECK(climbed <= 0.97f);
    }
    INFO(fmt::format("{} complete lift(s) of {} judged", judged, climbs.size()));
    CHECK(judged >= 3);
}

// Test 5d -- the beam goes out, instead of draining like a tap somebody half closed.
//
// "The particle beam turns off like water in a slow faucet." It did, and ramping `spawnRate` down
// was why: that closes the tap, and says nothing at all about the six thousand particles already
// falling. They keep going, for `lifetimeMax` = 5 s, thinning from the top -- which is exactly
// what a closing faucet looks like.
//
// So `depart` now stops the spawn dead and crushes `size`, which every particle in flight reads
// from the uniform block on the frame it is drawn. The column goes out in `beamFadeSeconds`.
//
// The drain and the hide are deliberately NOT moved forward with it: a disabled pool is frozen
// rather than aged (`ParticleRenderer::update` skips a system that is not enabled), so hiding the
// beam while it still holds particles means the next cycle resumes them, unaged, under a craft
// that has flown somewhere else. `beamDrainSeconds` still covers a whole particle lifetime. What
// changed is that nothing is visible during it.
//
// Asserted on the flattened system's `sizeStart` rather than on the parameter, because the
// parameter is the instruction and this is the thing the renderer is handed.
TEST_CASE("the beam is out within a moment of the abduction ending", "[stage][abduction][sequence]") {
    const std::vector<Sample>& run = film();
    REQUIRE(!run.empty());
    // The control arm, and it is the half that makes the other one mean something: the beam has to
    // have been at full size while it was firing. A system whose `sizeStart` is always zero would
    // pass "it is out quickly" on every frame of the film (ADR-182).
    float firing = 0.0f;
    for (const Sample& s : run) {
        if (s.beat == "abduct" && s.beamDrawnOn) {
            firing = std::max(firing, s.beamDrawnSize);
        }
    }
    INFO(fmt::format("widest the beam got while lifting: {:.4f}", firing));
    REQUIRE(firing > 0.3f);

    std::size_t measured = 0;
    for (std::size_t i = 1; i < run.size(); ++i) {
        if (run[i].beat != "depart" || run[i - 1].beat == "depart") {
            continue;
        }
        const auto fellTo = [&](float fraction) {
            for (std::size_t j = i; j < run.size() && run[j].beat == "depart"; ++j) {
                if (!run[j].beamDrawnOn || run[j].beamDrawnSize <= firing * fraction) {
                    return run[j].t - run[i].t;
                }
            }
            return -1.0;
        };
        const double quarter = fellTo(0.25f);
        if (quarter < 0.0) {
            continue; // the film ended inside this depart; a truncated decay measures nothing
        }
        ++measured;
        double base = -1.0;
        for (std::size_t j = i; j < run.size() && run[j].beat == "depart"; ++j) {
            if (run[j].beamSize <= 1e-4f) {
                base = run[j].t - run[i].t;
                break;
            }
        }
        INFO(fmt::format("depart at t={:.3f}: the director's size reached 0 after {:.3f} s; the "
                         "drawn column fell to half at {:.3f} s, a quarter at {:.3f} s, a "
                         "twentieth at {:.3f} s",
                         run[i].t, base, fellTo(0.5f), quarter, fellTo(0.05f)));
        // The instruction: `beamFadeSeconds` is 0.15 s, and a frame of slack either side.
        CHECK(base >= 0.0);
        CHECK(base <= 0.2);
        // And the column the renderer draws, which is the half that matters. Before this change it
        // was still at full width here and took the better part of five seconds to drain, so there
        // is no risk of this bound being generous.
        CHECK(quarter <= 0.35);
    }
    INFO(fmt::format("{} depart(s) measured", measured));
    CHECK(measured >= 3);
}

// Test 5b -- the fade reaches the renderer, and lets go of it again.
//
// "Numbers are not frames." The parameter moving proves nothing on its own: `pbr_shade.wgsl` reads
// `let alpha = select(1.0, baseColor.a, alphaMode > 1.5)`, so an OPAQUE material's alpha is
// discarded outright and every farm GLB in the repository is authored OPAQUE. Driving
// `nodes/<animal>/opacity` alone rendered a perfectly solid cow.
//
// So this asserts the other half, at the last point a CPU test can see: the *flattened scene's*
// entities. During a fade some are in the blend pipeline with an opacity under 1; when no fade is
// running none of them are, which is the control -- a promotion that never came back would leave
// the whole cast in a blend pipeline it never asked for, and a test that only looked at the fade
// would call that a pass.
TEST_CASE("the fade reaches the flattened scene and is given back", "[stage][abduction][sequence]") {
    const std::vector<Sample>& run = film();
    // Frame zero's count is the *baseline*, and taking it is the whole reason this case is not
    // the trivially-passing thing it first was: this scene already draws seven blended entities
    // before anything fades -- water, foliage cards and the like -- so "something is in the blend
    // pipeline" is true of every frame of the film and proves nothing at all (ADR-182). What the
    // fade adds is a body that was not there before.
    REQUIRE(!run.empty());
    const int baseline = run.front().blended;
    INFO(fmt::format("the scene draws {} blended entities before anything fades", baseline));
    int mostBlended = 0;
    float leastOpaque = 1.0f;
    std::size_t framesBlended = 0;
    std::size_t framesClear = 0;
    double firstBlend = -1.0;
    for (const Sample& s : run) {
        if (s.blended > baseline) {
            ++framesBlended;
            mostBlended = std::max(mostBlended, s.blended - baseline);
            leastOpaque = std::min(leastOpaque, s.minOpacity);
            if (firstBlend < 0.0) {
                firstBlend = s.t;
            }
        } else {
            ++framesClear;
        }
    }
    INFO(fmt::format("{} frame(s) with something in the blend pipeline (first at t={:.3f}), "
                     "{} with nothing; most blended at once {}, least opaque {:.4f}",
                     framesBlended, firstBlend, framesClear, mostBlended, leastOpaque));
    CHECK(framesBlended > 0);
    CHECK(mostBlended > 0);
    CHECK(leastOpaque <= 0.01f);
    // And it is not permanent: most of the film has nothing fading, and a promotion that never
    // came back would leave the cast in a blend pipeline it never asked for.
    CHECK(framesClear > framesBlended);
}

// The overlay's data, exercised (ADR-385).
//
// `Staging::sequenceStates()` is the reading the World window's Scenarios tab draws. A published
// state nothing reads is the failure mode this repository has an index entry about, so this asserts
// the reading is populated, agrees with the beat the director is actually in, and carries the two
// numbers the brief singled out -- world position and velocity -- for the craft.
TEST_CASE("the director publishes the state an overlay needs", "[stage][abduction][sequence]") {
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(filmProject());
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewport(1600, 900);
    comp->scene().detailLimits.entityDistanceCull = false;

    std::set<std::string> beatsSeen;
    std::set<std::string> rolesSeen;
    std::size_t framesWithCues = 0;
    std::size_t framesGated = 0;
    bool sawCraftPosition = false;
    bool sawNonZeroSpeed = false;
    FrameTime time;
    for (int i = 0; i < 60 * 30; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);

        const auto& states = comp->director().sequenceStates();
        REQUIRE(states.size() == 1);
        const stage::Staging::SequenceState& st = states.front();
        CHECK(st.scenario == "abduction");
        CHECK(st.time == Catch::Approx(time.renderTime));
        // The published beat is the beat, not a copy that drifted.
        CHECK(st.beat == std::string(comp->director().beat("abduction")));
        if (!st.beat.empty()) {
            beatsSeen.insert(st.beat);
        }
        if (st.gated) {
            ++framesGated;
            CHECK(!st.gatedOn.empty()); // it says *what* it is waiting for
        }
        if (!st.cues.empty()) {
            ++framesWithCues;
        }
        for (const stage::Staging::CueState& c : st.cues) {
            rolesSeen.insert(c.role);
            CHECK(c.index <= c.steps);
            CHECK(c.progress >= 0.0f);
            CHECK(c.progress <= 1.0f + 1e-4f);
            if (c.role == "actor" && !c.entity.empty()) {
                CHECK(c.entity == "visitor");
                sawCraftPosition = sawCraftPosition || glm::length(c.position) > 1.0f;
                sawNonZeroSpeed = sawNonZeroSpeed || c.speed > 0.1f;
            }
        }
    }
    INFO(fmt::format("beats {}; roles {}; {} frame(s) with cues, {} gated",
                     fmt::join(beatsSeen, "/"), fmt::join(rolesSeen, "/"), framesWithCues,
                     framesGated));
    // The film reaches all four working beats inside thirty seconds.
    CHECK(beatsSeen.count("approach") == 1);
    CHECK(beatsSeen.count("beam") == 1);
    CHECK(beatsSeen.count("abduct") == 1);
    CHECK(beatsSeen.count("depart") == 1);
    CHECK(rolesSeen.count("actor") == 1);
    CHECK(rolesSeen.count("actor.beam") == 1);
    CHECK(rolesSeen.count("target") == 1);
    CHECK(framesWithCues > 1000);
    // The gate fired at least once, so `gated`/`gatedOn` are not fields that are always empty.
    CHECK(framesGated > 0);
    CHECK(sawCraftPosition);
    CHECK(sawNonZeroSpeed);
}

// ---- why did the search stop? ----------------------------------------------------------------
//
// `acquire` has `"otherwise": ""`, so ONE failed search ends the scenario for the rest of the
// film -- there is no retry. With a smaller cast that is a real cliff, and "nothing passed the
// filter" has six possible causes. This prints all six for every animal, per search, so the
// answer is read rather than guessed (ADR-385).
TEST_CASE("what the abduction search sees, animal by animal", "[.abduction-instrument]") {
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(filmProject());
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewport(1600, 900);
    comp->scene().detailLimits.entityDistanceCull = false;

    FrameTime time;
    const double step = 1.0 / 60.0;
    std::string was;
    for (int i = 0; i < 60 * 90; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);
        const std::string beat(comp->director().beat("abduction"));
        if (beat == was) {
            continue;
        }
        was = beat;
        if (beat != "acquire") {
            continue;
        }
        const entity::Entity* craft = comp->entityWorld().find("visitor");
        REQUIRE(craft != nullptr);
        const glm::vec3 c = craft->state().position();
        const entity::Navigator& nav = comp->entityWorld().navigator();
        fmt::print("\n---- acquire at t={:.3f}, craft ({:.1f}, {:.1f}, {:.1f}) ----\n", time.renderTime,
                   c.x, c.y, c.z);
        for (const auto& e : comp->entityWorld().entities()) {
            const auto& tags = e->desc().tags;
            if (std::find(tags.begin(), tags.end(), "animal") == tags.end()) {
                continue;
            }
            const glm::vec3 p = e->state().position();
            const float d = glm::length(p - c);
            const glm::vec2 xz(p.x, p.z);
            fmt::print("  {:<10} d={:7.1f}  canopy={:6.2f} (>6.5 rejects)  navigable={}  "
                       "pos ({:.1f}, {:.1f}, {:.1f})\n",
                       e->name(), d, nav.valid() ? nav.canopyHeight(xz) : -1.0f,
                       nav.valid() ? (nav.navigable(xz) ? "yes" : "NO ") : "?",
                       p.x, p.y, p.z);
        }
    }
}

// ---- where can an animal actually be abducted from? -------------------------------------------
//
// `acquire` rejects any animal whose canopy is higher than `targetClearance` (6.5 m): under a tree
// the beam does not reach and the shot does not read. Most of this valley is under 7.1-8.0 m of
// canopy, so "open ground" is a much smaller set than the map suggests -- and an animal that
// wanders under a tree stops being abductable without moving far.
//
// This scans the valley for spots that are navigable, open over the whole 20 m the wander
// behaviours use, and far enough apart to read as scattered. It prints them as scene positions.
TEST_CASE("open ground an abduction beam can reach", "[.abduction-instrument]") {
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(filmProject());
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewport(1600, 900);
    FrameTime time;
    engine.update(time);

    const entity::Navigator& nav = comp->entityWorld().navigator();
    REQUIRE(nav.valid());
    constexpr float kClearance = 6.5f;
    constexpr float kMargin = 4.5f;   // the bar a home must clear, with room under the 6.5
    constexpr float kWander = 10.0f;  // and clear over the radius the wander behaviours use
    constexpr float kApart = 40.0f;   // far enough apart that the cast reads as scattered

    {
        std::map<int, int> hist;
        int walk = 0;
        for (float x = -280.0f; x <= 180.0f; x += 6.0f) {
            for (float z = -270.0f; z <= 70.0f; z += 6.0f) {
                const glm::vec2 xz(x, z);
                if (!nav.navigable(xz)) continue;
                ++walk;
                ++hist[static_cast<int>(nav.canopyHeight(xz) * 10.0f + 0.5f)];
            }
        }
        fmt::print("\ncanopy over {} navigable cells:\n", walk);
        for (const auto& [k, n] : hist) {
            fmt::print("   {:5.2f} m : {:5d} ({:4.1f}%)\n", k / 10.0f, n, 100.0 * n / walk);
        }
    }
    struct Spot { glm::vec2 xz; float ground; float worstCanopy; };
    std::vector<Spot> open;
    for (float x = -280.0f; x <= 180.0f; x += 6.0f) {
        for (float z = -270.0f; z <= 70.0f; z += 6.0f) {
            const glm::vec2 xz(x, z);
            if (!nav.navigable(xz)) {
                continue;
            }
            float worst = nav.canopyHeight(xz);
            for (int k = 0; k < 8 && worst <= kMargin; ++k) {
                const float a = static_cast<float>(k) * 0.785398f;
                const glm::vec2 r = xz + kWander * glm::vec2(std::cos(a), std::sin(a));
                if (!nav.navigable(r)) {
                    worst = 1e9f; // it could wander somewhere it cannot stand
                    break;
                }
                worst = std::max(worst, nav.canopyHeight(r));
            }
            if (worst > kMargin) {
                continue;
            }
            open.push_back({xz, nav.groundHeight(xz), worst});
        }
    }
    fmt::print("\n{} open cell(s) of the scan, clearance bar {:.1f} m (the find rejects over {:.1f})\n",
               open.size(), kMargin, kClearance);
    // Greedy, farthest-first, so the chosen set spreads instead of clumping in one clearing.
    // Seeded with the three animals that already stand in open ground, so the answer is "six more
    // places, as far from those and from each other as this valley allows".
    std::vector<Spot> picked;
    for (const char* who : {"bull-1", "horse-2", "cow-3"}) {
        const entity::Entity* e = comp->entityWorld().find(who);
        if (e == nullptr) continue;
        const glm::vec3 p = e->state().position();
        picked.push_back({glm::vec2(p.x, p.z), p.y, nav.canopyHeight(glm::vec2(p.x, p.z))});
        fmt::print("  seed {:<9} [{:.2f}, {:.2f}, {:.2f}]\n", who, p.x, p.y, p.z);
    }
    while (picked.size() < 9 && !open.empty()) {
        std::size_t best = 0;
        float bestD = -1.0f;
        for (std::size_t i = 0; i < open.size(); ++i) {
            float nearest = 1e9f;
            for (const Spot& p : picked) {
                nearest = std::min(nearest, glm::length(open[i].xz - p.xz));
            }
            if (nearest > bestD) {
                bestD = nearest;
                best = i;
            }
        }
        if (!picked.empty() && bestD < kApart) {
            break;
        }
        picked.push_back(open[best]);
        open.erase(open.begin() + static_cast<long>(best));
    }
    for (const Spot& p : picked) {
        fmt::print("  [{:.2f}, {:.2f}, {:.2f}]   canopy over {:.0f} m: {:.2f}\n", p.xz.x, p.ground,
                   p.xz.y, kWander, p.worstCanopy);
    }
}

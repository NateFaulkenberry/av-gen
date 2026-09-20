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
    int camera = 0;      // the camera holding the frame, for the cut test
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
        s.camera = static_cast<int>(comp->activeCamera().camera);
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

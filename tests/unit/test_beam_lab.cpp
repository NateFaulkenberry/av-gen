// The Tractor Beam Lab: does the animal line up with the beam that is *drawn*?
//
// ## Why this file exists when test_abduction_alignment.cpp already did
//
// That file measured, per lift, "how far is the animal's node **origin** from the emitter's world
// point, and is every mesh corner inside `extent.x`". Both numbers were computed correctly and
// both went green, three times, while the owner looked at the render and said nothing had changed.
//
// A probe that cannot fail proves nothing (ADR-182); the corollary this cost is that **a probe
// measuring the wrong quantity cannot fail in the way that matters**. Two quantities were wrong:
//
//  1. *The animal's origin is not the animal.* A farm GLB's mesh is not centred on its own origin:
//     measured from the assets, a cow's bind-pose box centre sits 0.223 model units behind its
//     origin, and ADR-213's 3.6x makes that 0.89 m of world. The scenario then spins the animal at
//     230 deg/s about that origin, so the **body** travels a circle 1.8 m across while the origin
//     the old probe measured does not move at all. The old number was, structurally, blind to the
//     largest single term in what a viewer is looking at.
//
//  2. *`extent.x` is not the beam.* Containment inside a 7.8 m emitter radius is satisfied by
//     almost anything the director can do wrong, and it is satisfied for a body hanging at the rim
//     where the column has no particles a camera can see. "Inside" was a claim so weak it could not
//     distinguish a centred lift from one a metre and a half out.
//
//  3. *And the beam has a bottom.* Every diagnostic here, that one included, treated the beam as its
//     **axis** -- a vertical line, of infinite length -- and asked only how far the animal was from
//     it sideways. The column that is drawn reaches `speed x lifetime`: 30.1 m for the shipped
//     emitter, against a saucer that was holding station 34.0 m up. So for the whole first half of
//     every lift the animal was *underneath* the beam, and no horizontal question could ever have
//     asked about it. That is the largest of the three and the only one visible in a still.
//
// So this file measures the **decomposition**, hop by hop, in one common space (world metres,
// horizontal): every transform between "the director's idea of the craft" and "the box the renderer
// draws the animal in" gets its own column, and every one of them is attributable to exactly one
// piece of code. A number moving is then a fact about a named hop rather than about the abduction
// in general.
//
// ## The coordinate-space audit, as code rather than as a comment
//
//   craft simulated   `Entity::state().position()`    world   entity/navigation + the director tier
//   craft drawn       `Composition::nodeWorldTransform(visitor)`
//                                                     world   parameters, after `applyOffsets`
//   craft "visual"    `Entity::visualPosition()`      world   state + behaviour offsets (ADR-218)
//   beam emitter      `ParticleSystem::position`      world   node-local `(0, -2.05, 0)` through
//                                                             the craft node's world matrix
//   beam axis         a *vertical* line through it    world   `direction` is NOT transformed
//                                                             (`applyParameters`), so the column is
//                                                             world-down however the craft banks
//   animal drawn      `Composition::nodeCorners()`    world   the node's world matrix times the
//                                                             glTF node chain times the mesh box
//
// GPU-free: composition, entities and the director are all CPU, and `Composition::update` is the
// same call the engine makes -- which is what puts the emitter into world space.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/serialization.hpp"
#include "scene/composition.hpp"
#include "world/terrain_query.hpp"
#include "signals/signal_bus.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unistd.h>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path labScene() {
    // `AVGEN_BEAM_LAB_SCENE` points the probe at a single-case lab
    // (`tools/make_tractor_beam_lab.py --case <name>`), which is how a *picture* of one case and
    // the numbers for that same case are taken from the same run rather than from two.
    if (const char* override = std::getenv("AVGEN_BEAM_LAB_SCENE"); override != nullptr) {
        return fs::path(override);
    }
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "tractor-beam-lab.scene.json";
}

// The **project** beside a scene, when one is asked for. `AVGEN_BEAM_LAB_PROJECT` makes the probe
// load what the owner actually opens rather than what the tests have always loaded.
//
// That distinction is not pedantry, it is ADR-264's whole subject. A project's `parameters` block is
// applied *over* the values the scene registers, and Glowmere's projects serialise **every node's
// position, rotation and scale** -- 5,489 parameters in the multicam one. So a scene file is not the
// state that runs; it is the state that runs *before* the project has had its say, and every test in
// this repository has been measuring the former while every render measures the latter.
fs::path labProject() {
    if (const char* override = std::getenv("AVGEN_BEAM_LAB_PROJECT"); override != nullptr) {
        return fs::path(override);
    }
    return {};
}

bool farmAssetsPresent() {
    return fs::is_regular_file(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb");
}

float flatDistance(glm::vec3 a, glm::vec3 b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

const scene::ParticleSystem* beamOf(const scene::Scene& s) {
    for (const auto& ps : s.particles) {
        if (ps.name.find("visitor-beam") != std::string::npos) {
            return &ps;
        }
    }
    return nullptr;
}

// Where a node's meshes actually are, as the renderer will draw them: the horizontal centre of the
// world-space corners, and the farthest corner from that centre.
//
// The centre and not the origin. That distinction is the whole of this file.
struct DrawnBody {
    bool valid = false;
    glm::vec3 centre{0.0f}; // horizontal centre of the drawn box; y is the box centre too
    float reach = 0.0f;     // farthest corner from `centre`, horizontally
    float lowest = 0.0f;
    float highest = 0.0f;
};

DrawnBody drawnBody(scene::Composition& comp, const std::string& node) {
    const std::vector<glm::vec3> corners = comp.nodeCorners(node);
    DrawnBody out;
    if (corners.empty()) {
        return out;
    }
    glm::vec3 lo = corners.front();
    glm::vec3 hi = corners.front();
    for (const glm::vec3& p : corners) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    out.centre = (lo + hi) * 0.5f;
    out.lowest = lo.y;
    out.highest = hi.y;
    for (const glm::vec3& p : corners) {
        out.reach = std::max(out.reach, glm::length(glm::vec2(p.x - out.centre.x, p.z - out.centre.z)));
    }
    out.valid = true;
    return out;
}

// One case's lift, decomposed. Every field is a worst case over the frames of the lift, in metres,
// horizontal, world.
struct Lift {
    std::string animal;
    double startedAt = 0.0;
    int frames = 0;
    float beamRadius = 0.0f;

    // ---- the hops, craft side ----
    float craftSimToDrawn = 0.0f;  // state().position() -> the node the renderer uses
    float craftVisualToDrawn = 0.0f; // visualPosition() -> the same node: is `Visual` the truth?
    // And the same gap vertically. Horizontally the two agree exactly (measured, 0.000 across 23
    // lab lifts) -- but an entity reaction is an ordinary modulation route on the node's *position
    // parameter*, applied before `applyOffsets` adds the entity's own offsets on top, and
    // `visualPosition()` is arithmetic on the entity that structurally cannot see it. Glowmere's
    // saucer carries two audio reactions on `position` component 1.
    float craftVisualToDrawnY = 0.0f;
    float craftDrawnToBeam = 0.0f; // the craft node -> the emitter's world point

    // ---- the hops, animal side ----
    float animalVisualToDrawn = 0.0f; // visualPosition() -> the node the renderer uses
    float originToBody = 0.0f;        // the node origin -> the centre of the box it draws

    // ---- what a viewer is looking at ----
    //
    // Over the **settled** half of the lift, and that qualifier is itself a lesson. An animal is
    // standing where it was standing when the beam lit; the first half of a lift is it travelling
    // from there to the column, and it is *supposed* to be off the axis while it does. A worst case
    // taken over the whole lift is therefore dominated by the start and is nearly insensitive to
    // whether the end is centred -- which is exactly the failure this file exists to stop repeating,
    // in a new place. `late` is the last 50% of the rise; `end` is the final frame.
    float originOffAxisLate = 0.0f; // what the old probe measured, restricted to the settled half
    float bodyOffAxisLate = 0.0f;   // what the owner is looking at
    float bodyOffAxisEnd = 0.0f;    // and at the top, where the animal hangs
    float worstCornerLate = 0.0f;   // farthest mesh corner from the axis, settled half
    float bodyReach = 0.0f;

    float fromY = 0.0f;
    float toY = 0.0f;
    float highestY = 0.0f;   // the top of the drawn body at the top of the lift
    float emitterY = 0.0f;   // where the column *starts*, at that same moment
    float craftAboveGround = 0.0f; // how high the saucer actually holds station, over the TERRAIN
    float columnReach = 0.0f;      // how far down the column's particles actually get
    // What the beam actually is at runtime, as distinct from what the scene file authors. Both are
    // scaled by the beam node's world transform in `applyParameters`, and a project that saved a
    // squashed scale for that node changes both without touching a line of the scene (ADR-264).
    float beamExtent = 0.0f;       // the emitter disc's world radius
    float emitterBelowCraft = 0.0f; // metres from the craft's node origin down to the beam's mouth
};

// How far a particle from this emitter travels before its life runs out, integrated with the
// system's own gravity and drag at its own slowest and shortest.
//
// The conservative end on purpose: `speedMin` and `lifetimeMax` would say how far the *longest*
// lived fastest mote gets, which is a claim about a handful of particles. `speedMin` with
// `lifetimeMin` is the depth by which essentially the whole column has arrived, which is the depth
// a *column* can be said to reach.
//
// This is the quantity nothing in this repository has ever measured, and it is the one the report
// was about. Every previous diagnostic treated the beam as its axis -- a line, of infinite length --
// and asked only how far the animal was from it sideways. The beam that is drawn is a column with
// a bottom, and for the first half of every lift the animal was underneath it.
float columnReach(const scene::ParticleSystem& ps) {
    const float dt = 1.0f / 120.0f;
    float v = ps.speedMin;   // downward, positive
    float travelled = 0.0f;
    for (float t = 0.0f; t < ps.lifetimeMin; t += dt) {
        v += -ps.gravity.y * dt;         // gravity is (0, -g, 0) and the beam fires down
        v *= std::max(0.0f, 1.0f - ps.drag * dt);
        travelled += v * dt;
    }
    return travelled;
}

// One run of the lab through the full production stack.
struct Run {
    assets::AssetRegistry registry;
    std::unique_ptr<scene::Composition> comp;
    std::optional<world::TerrainQuery> terrainOwned;
    fs::path project_;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::vector<Lift> lifts;
    std::vector<std::pair<double, std::string>> beats; // when each beat began, for a render range
    std::string lastBeat;
    std::string loadedProject;
    float beamRadius = 0.0f;
    double liftSeconds = 1.0;

    explicit Run(const fs::path& file, fs::path projectOverride = {})
        : registry(file.parent_path()), project_(std::move(projectOverride)) {
        auto loaded = scene::Composition::loadFile(file, registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        // The project, if one was named, applied exactly where the engine applies it: after the
        // composition has registered its parameters and before a frame has run.
        if (const fs::path project = project_.empty() ? labProject() : project_; !project.empty()) {
            auto ok = params::loadProjectFile(project, params, modulator);
            REQUIRE(ok.has_value());
            loadedProject = project.filename().string();
            // Exactly what `Engine::loadProject` does after the same call, and for the same reason:
            // a project moves nodes, and the entity layer was anchored before it spoke (ADR-264).
            comp->installEntities();
        }
        comp->setViewport(1920, 1080);
        // ADR-186's offline setting. The lab's entities already declare no cull distance; this is
        // the belt to that pair of braces, because a culled animal is not simulated at all and an
        // alignment measured on a frame the entity skipped is a measurement of the cull.
        comp->scene().detailLimits.entityDistanceCull = false;
        terrainOwned = comp->terrainQuery();
        if (const params::IParameter* p = params.find("staging/abduction/abductSeconds");
            p != nullptr) {
            liftSeconds = static_cast<double>(p->baseComponent(0));
        }
    }

    void play(double seconds, double hz = 60.0) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<int>(std::round(seconds * hz));
        std::string liftAnimal;
        for (int i = 0; i < frames; ++i) {
            FrameTime time;
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);

            const world::TerrainQuery& terrain = *terrainOwned;
            const scene::ParticleSystem* beam = beamOf(comp->scene());
            REQUIRE(beam != nullptr);
            beamRadius = beam->extent.x;

            const std::string beat(comp->director().beat("abduction"));
            const std::string_view target = comp->director().binding("abduction", "target");
            if (beat != lastBeat) {
                beats.emplace_back(time.renderTime, beat);
                lastBeat = beat;
            }
            if (beat != "abduct" || target.empty()) {
                liftAnimal.clear();
                continue;
            }
            const entity::Entity* craft = comp->entityWorld().find("visitor");
            const entity::Entity* prey = comp->entityWorld().find(target);
            REQUIRE(craft != nullptr);
            if (prey == nullptr) {
                continue;
            }
            // Only while the director is actually driving the body. The `abduct` beat outlives its
            // own `lift` step -- the animal is retired and released, and the beat runs on until the
            // saucer's `hold` is up -- and on those tail frames the body is no longer being moved
            // at all. Measuring them measures the release, which is a different question with a
            // different answer (and, for a body that was spinning, a much larger one).
            if (!prey->directorMotion().active) {
                continue;
            }
            const DrawnBody body = drawnBody(*comp, std::string(target));
            const DrawnBody hull = drawnBody(*comp, "visitor");
            if (!body.valid) {
                continue;
            }
            const glm::vec3 craftDrawn = craftNodeOrigin("visitor");
            const glm::vec3 animalDrawn = craftNodeOrigin(std::string(target));
            const glm::vec3 axis = beam->position;
            // The ground *under the craft*, from the terrain itself.
            //
            // This was `0.0f` -- true of the lab, whose world is flat by construction, and false of
            // every scene with relief. Run against Glowmere it reported the saucer holding station
            // between 21.8 m and 49.8 m and six of ten columns "stopping short", which is not a
            // fact about the beam at all: it is terrain elevation being read as altitude. A probe
            // carrying an assumption its caller can violate is the same defect as a probe measuring
            // the wrong point, one level up (ADR-182).
            const float groundY =
                terrain.valid() ? terrain.heightAt(glm::vec2(craftDrawn.x, craftDrawn.z)) : 0.0f;

            if (liftAnimal != target) {
                liftAnimal = std::string(target);
                lifts.push_back(Lift{.animal = liftAnimal, .startedAt = time.renderTime});
                lifts.back().fromY = body.centre.y;
            }
            Lift& l = lifts.back();
            ++l.frames;
            l.beamRadius = beam->extent.x;
            l.craftSimToDrawn = std::max(l.craftSimToDrawn, flatDistance(craft->state().position(), craftDrawn));
            l.craftVisualToDrawn = std::max(l.craftVisualToDrawn, flatDistance(craft->visualPosition(), craftDrawn));
            l.craftVisualToDrawnY =
                std::max(l.craftVisualToDrawnY, std::abs(craft->visualPosition().y - craftDrawn.y));
            l.craftDrawnToBeam = std::max(l.craftDrawnToBeam, flatDistance(craftDrawn, axis));
            l.animalVisualToDrawn = std::max(l.animalVisualToDrawn, flatDistance(prey->visualPosition(), animalDrawn));
            l.originToBody = std::max(l.originToBody, flatDistance(animalDrawn, body.centre));
            l.bodyReach = std::max(l.bodyReach, body.reach);
            // The settled half: the animal is off the ground and the rise is more than half done.
            // `abductSeconds` is the step's whole duration and `startedAt` is its first frame, so
            // this is a fact about the schedule rather than about the geometry it is judging.
            const double progress = (time.renderTime - l.startedAt) / std::max(liftSeconds, 1e-3);
            const float bodyGap = flatDistance(body.centre, axis);
            if (progress >= 0.85) {
                l.originOffAxisLate = std::max(l.originOffAxisLate, flatDistance(animalDrawn, axis));
                l.bodyOffAxisLate = std::max(l.bodyOffAxisLate, bodyGap);
                for (const glm::vec3& p : comp->nodeCorners(std::string(target))) {
                    l.worstCornerLate =
                        std::max(l.worstCornerLate, glm::length(glm::vec2(p.x - axis.x, p.z - axis.z)));
                }
            }
            l.bodyOffAxisEnd = bodyGap;

            // ---- and the column, vertically ----
            l.craftAboveGround = std::max(l.craftAboveGround, craftDrawn.y - groundY);
            l.columnReach = columnReach(*beam);
            l.beamExtent = beam->extent.x;
            l.emitterBelowCraft = craftDrawn.y - axis.y;
            if (body.centre.y > l.toY) {
                l.toY = body.centre.y;
                l.highestY = body.highest;
                l.emitterY = axis.y;
            }
        }
    }

    // Where the renderer puts a node: the parent chain and the parameter finals, which is what
    // `applyParameters` fed to the mesh and to the emitter alike.
    [[nodiscard]] glm::vec3 craftNodeOrigin(const std::string& name) const {
        for (const auto& node : comp->nodes()) {
            if (node->name == name) {
                return comp->nodeWorldTransform(*node).position;
            }
        }
        return glm::vec3(0.0f);
    }
};

} // namespace

// ---- the probe ----------------------------------------------------------------------------------
//
// Reports rather than asserts, and prints the decomposition. Hidden from the default run because it
// plays three minutes of the lab through the full composition update.
TEST_CASE("probe: the tractor beam lab, hop by hop", "[.probe][beam][lab]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    Run run(labScene());
    run.play(std::getenv("AVGEN_BEAM_LAB_SECONDS") ? std::atof(std::getenv("AVGEN_BEAM_LAB_SECONDS")) : 230.0);

    fmt::print("\nlifts: {}   beam radius {:.2f} m\n", run.lifts.size(), run.beamRadius);
    fmt::print("{:<18} {:>6} | {:>7} {:>7} {:>8} | {:>7} {:>8} | {:>8} {:>9} {:>8} {:>8} {:>6}\n",
               "case", "t", "sim>drw", "vis>drw", "drw>beam", "vis>drw", "org>body", "org-axis",
               "BODY-AXIS", "at top", "corner", "reach");
    for (const Lift& l : run.lifts) {
        fmt::print("{:<18} {:6.1f} | {:7.3f} {:7.3f} {:8.3f} | {:7.3f} {:8.3f} | {:8.3f} {:9.3f} "
                   "{:8.3f} {:8.3f} {:6.2f}\n",
                   l.animal, l.startedAt, l.craftSimToDrawn, l.craftVisualToDrawn, l.craftDrawnToBeam,
                   l.animalVisualToDrawn, l.originToBody, l.originOffAxisLate, l.bodyOffAxisLate,
                   l.bodyOffAxisEnd, l.worstCornerLate, l.bodyReach);
    }
    fmt::print("\n`Entity::visualPosition()` against the node the renderer places, per lift:\n");
    for (const Lift& l : run.lifts) {
        fmt::print("  {:<18} horizontal {:.3f} m, vertical {:.3f} m\n", l.animal,
                   l.craftVisualToDrawn, l.craftVisualToDrawnY);
    }
    fmt::print("\nwhat the beam actually is at runtime ({}):\n",
               run.loadedProject.empty() ? "scene only, no project" : run.loadedProject);
    for (const Lift& l : run.lifts) {
        fmt::print("  {:<18} emitter radius {:5.2f} m, mouth {:5.2f} m under the craft's origin\n",
                   l.animal, l.beamExtent, l.emitterBelowCraft);
    }

    fmt::print("\nthe column, vertically. Does the drawn beam reach the animal, and does the animal\n"
               "stay under the top of it?\n");
    for (const Lift& l : run.lifts) {
        fmt::print("  {:<18} saucer holds {:5.2f} m up, column reaches {:5.2f} m -> {:<12} | "
                   "body top {:6.2f} m vs emitter {:6.2f} m -> {}\n",
                   l.animal, l.craftAboveGround, l.columnReach,
                   l.columnReach >= l.craftAboveGround ? "reaches" : "STOPS SHORT",
                   l.highestY, l.emitterY,
                   l.highestY <= l.emitterY ? "under" : "OUT OF THE TOP");
    }
    // Temporary: how far each animal has strayed from where the scene put it.
    fmt::print("\nhorizontal travel from the authored spot, per animal:\n");
    for (const auto& e : run.comp->entityWorld().entities()) {
        const auto& tags = e->desc().tags;
        if (std::find(tags.begin(), tags.end(), "animal") == tags.end()) continue;
        fmt::print("  {:<14} {:7.1f} m  director {}\n", e->name(),
                   glm::length(glm::vec2(e->state().travel.x, e->state().travel.z)),
                   e->directorMotion().active ? "ACTIVE" : "-");
    }

    fmt::print("\nbeats:\n");
    for (const auto& [t, name] : run.beats) {
        fmt::print("  {:7.2f}  {}\n", t, name.empty() ? "(idle)" : name);
    }
    CHECK(!run.lifts.empty());
}

// ---- the regression arms -------------------------------------------------------------------------
//
// ## The tolerances, and why they are these numbers
//
// `kOnAxis` (0.25 m) is not a round number chosen to make a run pass. The residual after the fix has
// a floor and the floor is calculable: the director decides before the entity pass writes the
// finals the flattening reads, so the body-centre offset it subtracts is one frame stale in
// *rotation*, and the step spins the animal at 230 deg/s. One frame of that at 60 Hz is 3.8 degrees
// of a 0.89 m radius, or **0.059 m** -- and the measured worst-at-the-top across the four rotation
// cases is 0.056 to 0.065 m. The prediction and the measurement agree to a millimetre, which is the
// strongest thing that can be said about a residual: it is understood rather than merely small.
// 0.25 m is four times that floor, which leaves room for the craft's own 0.3 m wobble inside a
// frame, and is a *third* of the 0.76 - 1.23 m the legacy configuration produces. A tolerance that
// admitted the old behaviour would prove nothing (ADR-182).
//
// `kYawSpread` (0.10 m) is the differential arm and the important one. Four identical cows, aimed
// identically, differing only in facing: whatever a constant error is, it is the same for all four,
// so this number cannot be made to pass by tuning an offset. Legacy: 0.36 m. Fixed: 0.03 m.

namespace {
constexpr float kOnAxis = 0.25f;
constexpr float kYawSpread = 0.10f;

float yawSpread(const std::vector<Lift>& lifts) {
    float lo = 1e9f;
    float hi = -1e9f;
    for (const Lift& l : lifts) {
        if (l.animal.rfind("D-yaw", 0) != 0) {
            continue;
        }
        lo = std::min(lo, l.bodyOffAxisLate);
        hi = std::max(hi, l.bodyOffAxisLate);
    }
    return hi < lo ? 0.0f : hi - lo;
}
} // namespace

TEST_CASE("A lifted animal is drawn on the beam's axis, at every position, facing and asset",
          "[stage][beam][lab][abduction]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    Run run(labScene());
    run.play(190.0);
    REQUIRE(run.lifts.size() >= 20); // the cast is 23; a run that stopped early is not this test

    for (const Lift& l : run.lifts) {
        INFO(l.animal << " at t=" << l.startedAt << ": body centre " << l.bodyOffAxisLate
                      << " m off the axis over the settled part of the lift, " << l.bodyOffAxisEnd
                      << " m at the top; the node origin is " << l.originToBody
                      << " m from the body it draws");
        REQUIRE(l.frames > 100);
        CHECK(l.bodyOffAxisEnd <= kOnAxis);
        CHECK(l.bodyOffAxisLate <= kOnAxis);
    }

    // The arm a magic offset cannot pass. Same animal, same beam, same everything but the facing.
    INFO("the four rotation cases differ by " << yawSpread(run.lifts)
                                              << " m; before the fix they differed by 0.36 m");
    CHECK(yawSpread(run.lifts) <= kYawSpread);

    // And containment, which is the claim ADR-218 made and which is now made about the *body*
    // rather than about the origin: a centred animal reaches its own size from the axis and no more.
    for (const Lift& l : run.lifts) {
        INFO(l.animal << " reaches " << l.worstCornerLate << " m from the axis against a beam of "
                      << l.beamRadius << " m; its own reach from its centre is " << l.bodyReach);
        CHECK(l.worstCornerLate <= l.beamRadius);
        CHECK(l.worstCornerLate <= l.bodyReach + kOnAxis);
    }

    // ---- and the vertical half, in the same run ----
    //
    // One TEST_CASE rather than two because a run of the lab is nineteen seconds of simulation and
    // these are two halves of one question -- "is the animal inside the column that is drawn" has a
    // sideways part and an up-and-down part, and until ADR-262 only the sideways part had ever been
    // asked.
    for (const Lift& l : run.lifts) {
        // Vertical, and nothing in this repository had ever measured it. Every previous diagnostic
        // treated the beam as its axis -- a line, of infinite length -- and asked only how far the
        // animal was from it sideways. The beam that is *drawn* is a column with a bottom and a
        // top, and before ADR-262 the animal was under the one and over the other.
        INFO(l.animal << ": the saucer holds " << l.craftAboveGround
                      << " m up and the column's particles reach " << l.columnReach << " m");
        CHECK(l.columnReach >= l.craftAboveGround);
        INFO(l.animal << ": the top of the drawn body reaches " << l.highestY
                      << " m and the emitter disc is at " << l.emitterY << " m");
        CHECK(l.highestY <= l.emitterY);
    }
}

// ---- the control that fails (ADR-182) ------------------------------------------------------------
//
// `tractor-beam-lab-legacy.scene.json` is the same lab, the same cast, the same camera and the same
// build, carrying the scenario **as it was authored before ADR-262**: `clearance` on the two
// station-holding steps, `liftHeight` measured from the saucer, and a lift aimed at the craft's
// entity with `anchor: visual` and no `place`. Four fields of JSON.
//
// It exists so that the three tests above can be shown capable of failing. A regression suite whose
// arms have never been seen to go red is a suite that has proved nothing about the thing it names;
// this is the configuration the owner was looking at, and every assertion below is one of the
// assertions above with its sense reversed.
//
// If this test ever fails, the control has been fixed and the arms above have stopped meaning
// anything. Re-break it or delete all four together.
TEST_CASE("The pre-ADR-262 scenario fails every one of those checks", "[stage][beam][lab][control]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    Run run(fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "tractor-beam-lab-legacy.scene.json");
    run.play(190.0);
    REQUIRE(run.lifts.size() >= 20);

    // 1. The column stops short of the animal, on every single lift.
    std::size_t short_ = 0;
    std::size_t outOfTheTop = 0;
    float worstOffAxis = 0.0f;
    for (const Lift& l : run.lifts) {
        short_ += l.columnReach < l.craftAboveGround ? 1 : 0;
        outOfTheTop += l.highestY > l.emitterY ? 1 : 0;
        worstOffAxis = std::max(worstOffAxis, l.bodyOffAxisLate);
    }
    INFO(short_ << " of " << run.lifts.size() << " lifts had the column stopping short");
    CHECK(short_ == run.lifts.size());

    // 2. And the tall half of the cast hangs out of the top of it.
    INFO(outOfTheTop << " of " << run.lifts.size() << " lifts put the body above the emitter disc");
    CHECK(outOfTheTop >= 5);

    // 3. And the body -- not the origin -- is off the axis by more than the fixed tolerance.
    INFO("worst body-centre offset over the settled part of a lift: " << worstOffAxis << " m");
    CHECK(worstOffAxis > kOnAxis);

    // 4. And the four rotation cases disagree with each other, which is the signature of an
    //    implementation using the node origin rather than the body: a constant error cannot do it.
    INFO("the four rotation cases spread " << yawSpread(run.lifts) << " m");
    CHECK(yawSpread(run.lifts) > kYawSpread);

    // 5. And the thing the *old* probe measured -- the node's origin against the axis -- looks
    //    fine on the very same frames. This is the whole reason three fixes were reported and none
    //    was visible: the instrument was pointed at a point that was already where it should be.
    float worstOrigin = 0.0f;
    for (const Lift& l : run.lifts) {
        worstOrigin = std::max(worstOrigin, l.originOffAxisLate);
    }
    INFO("worst *origin* offset on those same frames: " << worstOrigin
                                                        << " m -- comfortably inside any tolerance "
                                                           "anybody would have written for it");
    CHECK(worstOrigin < worstOffAxis);
}

// ---- the one that would have caught three wasted rounds ------------------------------------------
//
// A project's `parameters` block is applied **over** the values its scene registers
// (`params::loadDocument`). Nothing warned about that and nothing checked it, and the consequence
// is the whole reason this work exists:
//
//   940232a lowered `liftHeight` from -3.4 to -7.5 and zeroed `animalWobble`, in all four scene
//   files, and touched none of the three project files -- which went on carrying `liftHeight:
//   -3.4` and `animalWobble: 0.4`. The owner opens the *project*. So the edit was real, the file
//   was changed, the test that read the scene went green, and the render was byte-for-byte the
//   behaviour that had just been "fixed".
//
// Three rounds of "I fixed it" / "nothing changed" have exactly this shape, and no measurement of
// the geometry could ever have found it, because the geometry was being measured in the one place
// the override does not reach.
//
// Pure JSON: no scene load, no simulation. It runs in milliseconds and it is the cheapest assertion
// in this file by three orders of magnitude.
TEST_CASE("Every Glowmere project agrees with its scene about the abduction",
          "[stage][beam][abduction][project]") {
    const fs::path world = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world";
    std::size_t checked = 0;
    std::size_t compared = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(world)) {
        const std::string name = entry.path().filename().string();
        if (name.size() < 12 || name.substr(name.size() - 11) != ".scene.json") {
            continue;
        }
        std::ifstream in(entry.path());
        REQUIRE(in.good());
        nlohmann::json scene;
        in >> scene;
        if (!scene.contains("staging")) {
            continue;
        }
        const fs::path project = entry.path().parent_path() / (name.substr(0, name.size() - 11) + ".json");
        if (!fs::is_regular_file(project)) {
            continue;
        }
        std::ifstream pin(project);
        nlohmann::json doc;
        pin >> doc;
        if (!doc.contains("parameters") || !doc["parameters"].is_object()) {
            continue;
        }
        ++checked;
        for (const nlohmann::json& scenario : scene["staging"]["scenarios"]) {
            const std::string prefix = "staging/" + scenario.value("name", std::string{}) + "/";
            for (const nlohmann::json& param : scenario["params"]) {
                const std::string path = prefix + param.value("name", std::string{});
                const auto saved = doc["parameters"].find(path);
                if (saved == doc["parameters"].end()) {
                    continue; // a project need not save every parameter; only agree about the ones it does
                }
                const float want = param.value("value", 0.0f);
                const float have = saved->is_number() ? saved->get<float>()
                                                      : saved->value("value", want);
                ++compared;
                INFO(project.filename().string() << " overrides " << path << " with " << have
                                                 << " while the scene says " << want
                                                 << " -- the project wins at load, so the scene's "
                                                    "value is the one nobody ever sees");
                CHECK(std::fabs(have - want) <= 1e-3f);
            }
        }
    }
    // ADR-182: an arm that examined nothing passes for the wrong reason.
    INFO("checked " << checked << " scene/project pairs, " << compared << " shared parameters");
    CHECK(checked >= 3);
    CHECK(compared >= 50);
}

// ---- production integration (ADR-264) ------------------------------------------------------------
//
// Everything above this line loads a **scene**. What the owner opens, and what `--render` is pointed
// at, is a **project** -- and a project's `parameters` block is applied *over* the values the scene
// registers. Glowmere Valley 2's multicam project carries 5,489 of them, including every node's
// position, rotation, scale and visibility.
//
// So a scene file is not the state that runs. Until this file, no test in this repository had ever
// loaded a project, which is why three rounds of correct measurement sat beside a production render
// that was wrong in ways none of them could see:
//
//   nodes/visitor-beam/scale   [1, 0.061, 1]   -> `cbrt(|sx*sy*sz|)` = 0.394, so the beam's emitter
//                                                 ran at 3.07 m against the 7.8 m the scene authors,
//                                                 and its mouth sat 0.12 m under the hull instead of
//                                                 2.05 m. Every large animal was wider than the beam.
//   nodes/visitor/position     28.66 m away    -> the entity layer was anchored before the project
//                                                 spoke, so `Entity::state().position()` and the node
//                                                 the renderer drew were 28.661 m apart, all run.
//                                                 The lift aims at the beam's *drawn* axis (ADR-262),
//                                                 so the animal was dragged twenty-eight metres
//                                                 sideways as it rose: the reported diagonal.
//
// The first is data and is cleaned by `tools/clean_staged_body_overrides.py`; the second is an engine
// defect and is fixed in `Engine::loadProject` and `Composition::installEntities`. Both are asserted
// here, and the first arm is the cheap one that would have caught the whole thing.

namespace {

// Every node a scenario owns: its actors, their parts, and everything its queries can bind. The same
// derivation `tools/clean_staged_body_overrides.py` makes, from the same declarations.
std::set<std::string> stagedBodies(const nlohmann::json& scene) {
    // The entity list by value, not by pointer into a temporary: `json::value()` returns a *copy*,
    // and a range-for over it keeps that copy alive only for the loop. Storing pointers into it and
    // reading them afterwards is a dangling read that json reports as "cannot use value() with
    // null" -- a confusing message for a plain lifetime mistake.
    std::map<std::string, nlohmann::json> entities;
    if (const auto it = scene.find("entities"); it != scene.end() && it->is_array()) {
        for (const nlohmann::json& e : *it) {
            entities[e.value("name", std::string{})] = e;
        }
    }
    std::set<std::string> owned;
    std::set<std::string> tags;
    const nlohmann::json staging = scene.value("staging", nlohmann::json::object());
    for (const nlohmann::json& actor : staging.value("actors", nlohmann::json::array())) {
        const std::string body = actor.value("body", std::string{});
        owned.insert(body.empty() ? actor.value("name", std::string{}) : body);
        for (const nlohmann::json& part : actor.value("parts", nlohmann::json::array())) {
            owned.insert(part.value("entity", std::string{}));
        }
    }
    for (const nlohmann::json& sc : staging.value("scenarios", nlohmann::json::array())) {
        for (const nlohmann::json& beat : sc.value("beats", nlohmann::json::array())) {
            for (const nlohmann::json& q : beat.value("find", nlohmann::json::array())) {
                if (const std::string t = q.value("tag", std::string{}); !t.empty()) tags.insert(t);
                if (const std::string n = q.value("name", std::string{}); !n.empty()) owned.insert(n);
            }
        }
    }
    for (const auto& [name, e] : entities) {
        if (const auto it = e.find("tags"); it != e.end() && it->is_array()) {
            for (const nlohmann::json& t : *it) {
                if (t.is_string() && tags.count(t.get<std::string>()) != 0) {
                    owned.insert(name);
                }
            }
        }
    }
    std::set<std::string> nodes;
    for (const std::string& name : owned) {
        const auto it = entities.find(name);
        nodes.insert(it == entities.end() ? name : it->second.value("node", name));
    }
    return nodes;
}

} // namespace

TEST_CASE("No project contradicts its scene about a body its scenario owns",
          "[stage][beam][abduction][project]") {
    const fs::path world = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world";
    std::size_t pairs = 0;
    std::size_t compared = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(world)) {
        const std::string name = entry.path().filename().string();
        if (name.size() < 12 || name.substr(name.size() - 11) != ".scene.json") {
            continue;
        }
        std::ifstream in(entry.path());
        nlohmann::json scene;
        in >> scene;
        if (!scene.contains("staging")) {
            continue;
        }
        const fs::path project =
            entry.path().parent_path() / (name.substr(0, name.size() - 11) + ".json");
        if (!fs::is_regular_file(project)) {
            continue;
        }
        std::ifstream pin(project);
        nlohmann::json doc;
        pin >> doc;
        if (!doc.contains("parameters") || !doc["parameters"].is_object()) {
            continue;
        }
        ++pairs;
        const std::set<std::string> bodies = stagedBodies(scene);
        std::map<std::string, nlohmann::json> nodes;
        for (const nlohmann::json& n : scene["nodes"]) {
            nodes[n.value("name", std::string{})] = n;
        }
        for (const auto& [path, saved] : doc["parameters"].items()) {
            // "nodes/<name>/<field>"
            const std::size_t first = path.find('/');
            const std::size_t last = path.rfind('/');
            if (first == std::string::npos || first == last || path.compare(0, 6, "nodes/") != 0) {
                continue;
            }
            const std::string node = path.substr(first + 1, last - first - 1);
            const std::string field = path.substr(last + 1);
            if (bodies.count(node) == 0 || nodes.count(node) == 0) {
                continue;
            }
            const nlohmann::json& n = nodes[node];
            nlohmann::json authored;
            if (field == "position" || field == "rotation") {
                authored = n.value(field, nlohmann::json::array({0.0, 0.0, 0.0}));
            } else if (field == "scale") {
                authored = n.value(field, nlohmann::json::array({1.0, 1.0, 1.0}));
            } else if (field == "visible") {
                authored = n.value(field, nlohmann::json(true));
            } else {
                continue;
            }
            ++compared;
            INFO(project.filename().string()
                 << " overrides " << path << " with " << saved.dump() << " while the scene says "
                 << authored.dump() << ". The project wins at load, and '" << node
                 << "' is a body the scenario drives -- so that value is a photograph of a run, not "
                    "authorship. Run tools/clean_staged_body_overrides.py.");
            if (authored.is_array() && saved.is_array() && authored.size() == saved.size()) {
                for (std::size_t i = 0; i < authored.size(); ++i) {
                    CHECK(std::fabs(saved[i].get<double>() - authored[i].get<double>()) <= 1e-3);
                }
            } else {
                CHECK(saved == authored);
            }
        }
    }
    // ADR-182: an arm that examined nothing passes for the wrong reason.
    INFO("checked " << pairs << " scene/project pairs, " << compared << " staged-body parameters");
    CHECK(pairs >= 3);
    CHECK(compared >= 20);
}

// The emitter's radius as the *parameter stack* holds it: the scene's value with the project's
// applied over it, which is the state that runs (ADR-264 again, one level down). `applyParameters`
// then multiplies it by the beam node's own `cbrt(|sx*sy*sz|)`, so the runtime width and this
// number agree only when nothing has quietly resized the node -- which is the residue class.
namespace {
float beamRadiusFromParameters(const params::ParameterSet& params) {
    const params::IParameter* p = params.find("particles/visitor-beam/extent");
    return p == nullptr ? 0.0f : p->baseComponent(0);
}
} // namespace

TEST_CASE("The shipped Glowmere project abducts the way its scene says it does",
          "[stage][beam][abduction][project][glowmere]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    const fs::path world = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world";
    const fs::path scene = world / "glowmere-valley-2-multicam.scene.json";
    const fs::path project = world / "glowmere-valley-2-multicam.json";
    REQUIRE(fs::is_regular_file(scene));
    REQUIRE(fs::is_regular_file(project));

    // What the scene authors for the beam, read straight out of the file. Reported rather than
    // asserted against, and that change of role is the subject of ADR-271.
    //
    // This used to be the assertion: "the beam's runtime radius equals the radius the scene file
    // authors". It caught the ADR-264 residue on its first live encounter, which is the most a
    // regression test can be asked to do -- and then it caught the owner widening the beam through
    // the panel, reported that as residue, and their tuning was reverted twice on its word.
    //
    // A project's `parameters` are *meant* to sit over its scene; that is what a project is. The
    // invariant that survives is narrower and is the one the residue actually violated: whatever
    // radius the parameter stack ends up holding, **that** is the radius the emitter runs at. The
    // squashed `nodes/visitor-beam/scale` broke it by a hidden `cbrt(0.061)` = 0.394 that no file
    // states and no panel shows; a person typing 0.42 m into the radius field does not.
    std::ifstream in(scene);
    nlohmann::json doc;
    in >> doc;
    float sceneExtent = 0.0f;
    for (const nlohmann::json& n : doc["nodes"]) {
        if (n.value("name", std::string{}) == "visitor-beam") {
            sceneExtent = n["particles"]["extent"][0].get<float>();
        }
    }
    REQUIRE(sceneExtent > 1.0f);

    Run run(scene, project);
    const float authoredExtent = beamRadiusFromParameters(run.params);
    REQUIRE(authoredExtent > 0.0f);
    run.play(60.0);
    REQUIRE(run.lifts.size() >= 2);
    REQUIRE(!run.loadedProject.empty());

    for (const Lift& l : run.lifts) {
        // 1. Nothing resized the beam behind the author's back. `applyParameters` scales `extent`
        //    by the node's own `cbrt(|sx*sy*sz|)`, so a saved node scale is a saved beam width --
        //    and it is a beam width stated nowhere, which is what made it residue rather than a
        //    setting. The control arm below re-introduces exactly that scale and watches this fail.
        INFO(l.animal << ": the beam ran at " << l.beamExtent << " m against the " << authoredExtent
                      << " m the parameters carry (the scene file authors " << sceneExtent << " m)");
        CHECK(std::fabs(l.beamExtent - authoredExtent) <= 0.01f);

        // 2. The craft's entity and the craft's node are the same body. This is the one the 28.661 m
        //    failed, for the whole run, in silence -- and it is the reason the lift went diagonal.
        INFO(l.animal << ": Entity::visualPosition() is " << l.craftVisualToDrawn
                      << " m horizontally and " << l.craftVisualToDrawnY
                      << " m vertically from the node the renderer places");
        CHECK(l.craftVisualToDrawn <= 0.01f);

        // 3. And then the ADR-262 invariants, in production rather than in the lab.
        INFO(l.animal << ": body centre " << l.bodyOffAxisEnd << " m off the axis at the top");
        CHECK(l.bodyOffAxisEnd <= kOnAxis);
        // Centred, not contained. The lab asserts `worstCornerLate <= beamRadius` against the 7.8 m
        // ADR-218 sized from the cast, and that is where a claim about the beam's *width* belongs,
        // because the lab is where the width is fixed. Production's width is the author's: this
        // project now runs a 0.42 m emitter at 0.128 rad, a beam that tapers to the saucer rather
        // than a column, and every animal it lifts is wider than its mouth. Containment there would
        // be a test asserting an art direction the owner has changed. What does not change is that
        // the animal hangs on the axis and no further from it than its own size -- the arm a magic
        // offset cannot pass, and the one the original report was actually about.
        INFO(l.animal << ": reaches " << l.worstCornerLate << " m from the axis; its own reach from "
                      << "its centre is " << l.bodyReach << " and the emitter's radius is "
                      << l.beamRadius);
        CHECK(l.worstCornerLate <= l.bodyReach + kOnAxis);
        CHECK(l.columnReach >= l.craftAboveGround);
        CHECK(l.highestY <= l.emitterY);
    }
}

// ---- and the control for the arm above (ADR-182) --------------------------------------------------
//
// Assertion 1 changed from "equals what the scene authors" to "equals what the parameters carry",
// and the whole question about a change like that is whether it can still fail on the thing it was
// built for. So: the shipped project with `nodes/visitor-beam/scale` put back exactly as ADR-264
// found it, written to a scratch copy, run through the same probe.
//
// Cheap on purpose -- one second, no lifts. The residue is a load-time fact and does not need an
// abduction to be visible; a sixty-second Glowmere run to observe a number that is settled by the
// first frame is a minute of nothing.
TEST_CASE("The beam probe still sees a node scale that resizes the emitter",
          "[stage][beam][abduction][project][glowmere]") {
    const fs::path world = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world";
    const fs::path scene = world / "glowmere-valley-2-multicam.scene.json";
    const fs::path project = world / "glowmere-valley-2-multicam.json";
    REQUIRE(fs::is_regular_file(project));

    std::ifstream in(project);
    nlohmann::json doc;
    in >> doc;
    REQUIRE(doc.contains("parameters"));
    // The shipped file states the beam node's scale and states it as `[1, 1, 1]` -- ADR-264 had
    // `make_abduction_scenario.py` write that transform out explicitly, because "concentric,
    // unrotated, unscaled" is the contract ADR-218 sizes the emitter against and it had been
    // promoted out of a session into a scene file once already. So the two arms below differ by the
    // *value* of one key. If it ever arrives squashed, this case would be comparing a file with
    // itself and would prove nothing about either arm.
    REQUIRE(doc["parameters"].value("nodes/visitor-beam/scale",
                                    nlohmann::json::array({1.0, 1.0, 1.0})) ==
            nlohmann::json::array({1.0, 1.0, 1.0}));

    const fs::path dir = fs::temp_directory_path() /
                         ("avgen_beam_residue_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path squashed = dir / "squashed.json";

    // The clean arm, from the same scratch copy, so the two differ by one key and nothing else --
    // not by a path, a working directory or a re-serialisation.
    const fs::path clean = dir / "clean.json";
    {
        std::ofstream out(clean);
        out << doc.dump(2) << '\n';
    }
    doc["parameters"]["nodes/visitor-beam/scale"] = nlohmann::json::array({1.0, 0.061, 1.0});
    {
        std::ofstream out(squashed);
        out << doc.dump(2) << '\n';
    }

    float cleanRuntime = 0.0f;
    float cleanAuthored = 0.0f;
    {
        Run run(scene, clean);
        cleanAuthored = beamRadiusFromParameters(run.params);
        run.play(1.0);
        cleanRuntime = run.beamRadius;
    }
    float squashedRuntime = 0.0f;
    float squashedAuthored = 0.0f;
    {
        Run run(scene, squashed);
        squashedAuthored = beamRadiusFromParameters(run.params);
        run.play(1.0);
        squashedRuntime = run.beamRadius;
    }
    fs::remove_all(dir);

    // The control: with no node scale the probe reports the parameter's own number, so a failure in
    // the other arm is about the scale and not about the probe.
    INFO("clean: the emitter ran at " << cleanRuntime << " m against the " << cleanAuthored
                                      << " m the parameters carry");
    CHECK(std::fabs(cleanRuntime - cleanAuthored) <= 0.01f);

    // And the arm. `cbrt(|1 * 0.061 * 1|)` is 0.3936, which is a beam width nothing states.
    INFO("squashed: the emitter ran at " << squashedRuntime << " m against the " << squashedAuthored
                                         << " m the parameters carry");
    CHECK(squashedAuthored == cleanAuthored);
    CHECK(std::fabs(squashedRuntime - squashedAuthored) > 0.01f);
    CHECK_THAT(squashedRuntime,
               Catch::Matchers::WithinRel(squashedAuthored * 0.3936f, 0.01f));
}

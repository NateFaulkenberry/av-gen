// Three defects the owner reported in `glowmere-valley-2-multicam`, and the instruments that tell
// them apart (ADR-349).
//
//   1. two aliens "just standing there, not moving"
//   2. farm animals "walking off axis"
//   3. farm animals "still clipping into hills"
//
// **This file loads the PROJECT, and that is the whole reason it exists.**
// `test_glowmere_multicam.cpp` loads the scene, which is what a scene test should do -- but a
// project is applied *over* its scene (ADR-264, ADR-271), and this project carries 5,502
// parameters, of which one moves an alien 68 m. So every measurement taken against the scene alone
// is a measurement of a world that does not ship, and a defect the owner can see is by definition
// in the world that does. `app::Engine` in `Offline` mode is the same loader an offline render
// builds (`test_project_node_set.cpp` §1), so what is stepped here is the film.
//
// GPU-free: `EngineMode::Offline` builds no device, and everything below is entity and nav work.

#include "app/engine.hpp"
#include "entity/behavior.hpp"
#include "entity/character_ai.hpp"
#include "entity/entity.hpp"
#include "entity/locomotion.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "scene/composition.hpp"
#include "world/camera_clearance.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path worldDir() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world"; }
fs::path filmProject() { return worldDir() / "glowmere-valley-2-multicam.json"; }
fs::path filmScene() { return worldDir() / "glowmere-valley-2-multicam.scene.json"; }

const std::array<const char*, 5> kAliens{{"rook", "tide", "sage", "ember", "vane"}};
const std::array<const char*, 16> kFarm{{"bull-1", "horse-2", "cow-3", "sheep-4", "goat-5", "pig-6",
                                         "rooster-7", "chicken-8", "bull-10", "horse-11", "cow-12",
                                         "sheep-13", "goat-14", "pig-15", "rooster-16",
                                         "chicken-17"}};

struct Track {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    float travelled = 0.0f;
    float deepest = 0.0f;
    std::size_t idleFrames = 0;
    std::size_t frames = 0;
    // Defect 1's instrument: how often the winning option was somewhere this body cannot walk to.
    std::size_t ticksWithOptions = 0;
    std::size_t ticksAllUnreachable = 0;
    std::size_t committedUnreachable = 0;
    std::size_t stalls = 0;
    std::size_t decisions = 0;
    std::map<std::string, int> chosen;
    // Defect 2's instrument: the angle between where the body faces and where it is going, sampled
    // only while it is actually moving. A facing measured on a body standing still is noise.
    double yawErrorSum = 0.0;
    double yawErrorMax = 0.0;
    std::size_t yawSamples = 0;
    // Defect 3's instrument: how far the body's own footprint circle sinks below the ground it
    // stands over. A body read at a single point on a slope is level with that point and buried
    // on the uphill side, which is what "clipping into a hill" looks like.
    float deepestSink = 0.0f;
    float radius = 0.0f;
    // Defect 2, sharpened: a turn transient and a body walking backwards look identical in a mean
    // and in a max. What tells them apart is how long it lasts.
    std::size_t backwardsFrames = 0;   // |facing - travel| > 90 degrees
    std::size_t backwardsRunMax = 0;   // the longest unbroken stretch of them
    std::size_t backwardsRun = 0;
    // The abduction scenario lifts an animal off the ground and spins it (`animalSpin` 230 deg/s).
    // A body in a beam is not walking, and a facing error measured on one is not an off-axis walk,
    // so the two are separated here rather than argued about.
    float maxAirborne = 0.0f;
    std::size_t backwardsAirborne = 0;
    // Defect 2's other candidate, and the one a viewer would actually call "off axis": how far the
    // body's own up axis leans from world up. `slopeAlign` 1 makes a walker *part of* the slope,
    // and a quadruped rolled thirty degrees across a hillside reads as walking crooked long before
    // anybody measures a yaw.
    double maxLeanDeg = 0.0;
    double leanSum = 0.0;
    std::size_t leanSamples = 0;
};

// The body's own horizontal half-extent in world metres. `EntityState::radius` is the *crowd
// field's* radius and is 0 on every body that never declared a `bodyRadius`, so using it to size a
// ground query silently measures nothing -- which is what the first cut of this file did for all
// sixteen farm animals. The node's scale times the asset's own bounds is the honest number.
const scene::CompositionNode* nodeOf(const scene::Composition& comp, const entity::Entity& e) {
    for (const auto& n : comp.nodes()) {
        if (n->name == e.desc().driven()) {
            return n.get();
        }
    }
    return nullptr;
}

float bodyHalfExtent(scene::Composition& comp, const entity::Entity& e) {
    const float declared = e.state().radius;
    if (declared > 0.05f) {
        return declared;
    }
    const scene::WorldBounds b = comp.nodeBounds(e.desc().driven());
    if (!b.valid) {
        return 0.0f;
    }
    const glm::vec3 size = b.size();
    return 0.5f * std::max(size.x, size.z);
}

// One run of the film. `seedShift` is added to every entity seed in the *scene* before the project
// is applied over it, so the world, the residue and the project's own 5,502 parameters are all
// exactly the ones that ship and only the per-body random streams differ.
std::map<std::string, Track> playFilm(double seconds, std::uint32_t seedShift, double hz = 40.0,
                                      bool projectOverrides = true) {
    fs::path project = filmProject();
    fs::path scene = filmScene();
    fs::path tmpScene;
    fs::path tmpProject;
    struct Remove {
        std::vector<fs::path> paths;
        ~Remove() {
            std::error_code ec;
            for (const fs::path& p : paths) {
                fs::remove(p, ec);
            }
        }
    } cleanup;

    if (seedShift != 0 || !projectOverrides) {
        std::ifstream in(scene);
        REQUIRE(in.good());
        json sceneDoc;
        in >> sceneDoc;
        if (seedShift != 0) {
            for (json& e : sceneDoc.at("entities")) {
                e["seed"] = e.at("seed").get<std::uint64_t>() + seedShift;
            }
        }
        tmpScene = worldDir() / fmt::format("_defect-{}-{}.scene.json", seedShift,
                                            projectOverrides ? "p" : "s");
        {
            std::ofstream out(tmpScene);
            REQUIRE(out.good());
            out << sceneDoc.dump(1);
        }
        cleanup.paths.push_back(tmpScene);

        std::ifstream pin(project);
        REQUIRE(pin.good());
        json projectDoc;
        pin >> projectDoc;
        projectDoc["assets"]["scene"]["path"]["path"] = tmpScene.filename().string();
        projectDoc["assets"]["scene"]["path"].erase("sha256");
        projectDoc["assets"]["scene"]["path"].erase("size");
        if (!projectOverrides) {
            // **The control arm for defect 2.** Every `nodes/*` parameter removed, so the scene is
            // the only thing placing or turning anything. If an off-axis walk survives this, it is
            // not an override.
            json kept = json::object();
            for (auto it = projectDoc.at("parameters").begin();
                 it != projectDoc.at("parameters").end(); ++it) {
                if (it.key().rfind("nodes/", 0) != 0) {
                    kept[it.key()] = it.value();
                }
            }
            projectDoc["parameters"] = std::move(kept);
        }
        tmpProject = worldDir() / fmt::format("_defect-{}-{}.json", seedShift,
                                              projectOverrides ? "p" : "s");
        {
            std::ofstream out(tmpProject);
            REQUIRE(out.good());
            out << projectDoc.dump(1);
        }
        cleanup.paths.push_back(tmpProject);
        project = tmpProject;
    }

    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewport(1600, 900);
    // ADR-186's offline setting: with the distance cull on, "it travelled 18 m" would be a fact
    // about which level-of-detail band the camera put the body in.
    comp->scene().detailLimits.entityDistanceCull = false;

    const entity::Navigator& nav = comp->entityWorld().navigator();
    const entity::NavGrid* grid = nav.grid();

    std::map<std::string, Track> out;
    const double step = 1.0 / hz;
    const auto frames = static_cast<int>(std::llround(seconds * hz));
    FrameTime time;
    std::map<std::string, glm::vec3> last;
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);

        for (const auto& e : comp->entityWorld().entities()) {
            const std::string name(e->name());
            Track& t = out[name];
            const glm::vec3 p = e->state().position();
            if (t.frames == 0) {
                t.start = p;
                last[name] = p;
                t.radius = bodyHalfExtent(*comp, *e);
            }
            const glm::vec2 delta(p.x - last[name].x, p.z - last[name].z);
            const float moved = glm::length(delta);
            t.travelled += moved;
            t.end = p;
            ++t.frames;
            const entity::NavSample sm = nav.sample(glm::vec2(p.x, p.z));
            t.deepest = std::max(t.deepest, sm.waterDepth);
            t.maxAirborne = std::max(t.maxAirborne, p.y - sm.ground);

            // ---- defect 2: facing against travel -------------------------------------------
            // Only while the body is genuinely moving: 0.02 m in a 25 ms step is 0.8 m/s, which is
            // above the slowest gait here and well above grounding jitter.
            if (moved > 0.02f) {
                const float travelYaw = std::atan2(delta.x, delta.y);
                const float faceYaw = e->state().yaw;
                float d = travelYaw - faceYaw;
                while (d > 3.14159265f) {
                    d -= 6.28318531f;
                }
                while (d < -3.14159265f) {
                    d += 6.28318531f;
                }
                const double deg = std::abs(static_cast<double>(d)) * 57.2957795;
                t.yawErrorSum += deg;
                t.yawErrorMax = std::max(t.yawErrorMax, deg);
                ++t.yawSamples;
                if (deg > 90.0) {
                    ++t.backwardsFrames;
                    ++t.backwardsRun;
                    t.backwardsRunMax = std::max(t.backwardsRunMax, t.backwardsRun);
                    if (p.y - sm.ground > 0.5f) {
                        ++t.backwardsAirborne;
                    }
                } else {
                    t.backwardsRun = 0;
                }
            }

            // ---- defect 3: the body's own footprint against the ground it stands over -------
            // Sampled on a ring at the body's radius: the deepest the terrain rises above the
            // body's feet anywhere under it. Zero on flat ground and on a body read over its own
            // width; positive is geometry the body is inside.
            if (t.radius > 0.05f) {
                for (int k = 0; k < 8; ++k) {
                    const float a = static_cast<float>(k) * 0.7853982f;
                    const glm::vec2 q(p.x + std::cos(a) * t.radius, p.z + std::sin(a) * t.radius);
                    t.deepestSink = std::max(t.deepestSink, nav.groundHeight(q) - p.y);
                }
            }

            // `rooster-16` and `chicken-17` are entities whose nodes the project deletes
            // (ADR-330), so an entity without a node is a legal state here and dereferencing one
            // is a segfault -- which is how the first cut of this ended.
            if (const scene::CompositionNode* node = nodeOf(*comp, *e); node != nullptr) {
                const scene::Transform w = comp->nodeWorldTransform(*node);
                const glm::vec3 up = glm::normalize(w.rotation * glm::vec3(0.0f, 1.0f, 0.0f));
                const double lean =
                    std::acos(static_cast<double>(std::clamp(up.y, -1.0f, 1.0f))) * 57.2957795;
                t.maxLeanDeg = std::max(t.maxLeanDeg, lean);
                t.leanSum += lean;
                ++t.leanSamples;
            }
            if (e->locomotion().activity == entity::Activity::Idle) {
                ++t.idleFrames;
            }
            last[name] = p;

            // ---- defect 1: was the committed option somewhere it could walk to? -------------
            for (const auto& behavior : e->behaviors()) {
                entity::DecisionDebug dbg;
                if (!behavior->decisionDebug(dbg)) {
                    continue;
                }
                t.decisions = dbg.decisions;
                t.stalls = dbg.stalls;
                if (!dbg.chosen.empty()) {
                    t.chosen[std::string(dbg.chosen)] += 1;
                }
            }
        }

        // Region membership, sampled once a second rather than per frame: `regionAt` is O(1) but
        // eight of these per body per frame is noise in a 226-second run.
        if (grid != nullptr && grid->valid() && i % static_cast<int>(hz) == 0) {
            for (const auto& e : comp->entityWorld().entities()) {
                Track& t = out[std::string(e->name())];
                const glm::vec3 p = e->state().position();
                const std::uint16_t mine = grid->regionAt(glm::vec2(p.x, p.z));
                bool any = false;
                bool reachable = false;
                for (const auto& behavior : e->behaviors()) {
                    entity::DecisionDebug dbg;
                    if (!behavior->decisionDebug(dbg) || dbg.options.empty()) {
                        continue;
                    }
                    any = true;
                    // The winner's own destination. `interest` names a place; `holdPost` on its
                    // post and `idle` name none, and a body that chose to stand is not a body
                    // that was refused.
                    for (const entity::ScoredOption& o : dbg.options) {
                        if (!o.chosen) {
                            continue;
                        }
                        (void)o;
                    }
                    if (mine != 0) {
                        reachable = true;
                    }
                }
                if (any) {
                    ++t.ticksWithOptions;
                    if (!reachable) {
                        ++t.ticksAllUnreachable;
                    }
                }
            }
        }
    }
    return out;
}

void report(const char* label, const std::map<std::string, Track>& run,
            std::span<const char* const> who) {
    std::printf("\n--- %s ---\n", label);
    for (const char* name : who) {
        auto it = run.find(name);
        if (it == run.end()) {
            std::printf("  %-11s ABSENT\n", name);
            continue;
        }
        const Track& t = it->second;
        std::string top;
        int best = 0;
        for (const auto& [option, n] : t.chosen) {
            if (n > best) {
                best = n;
                top = option;
            }
        }
        std::printf("  %-11s went %6.1f m  idle %4zu/%zu  yaw mean %4.1f max %5.1f deg  "
                    "backwards %4zu (run %3zu, airborne %3zu)  airborne max %5.1f m  "
                    "lean mean %4.1f max %4.1f deg  sink %5.2f m over r %.2f  '%s'\n",
                    name, static_cast<double>(t.travelled), t.idleFrames, t.frames,
                    t.yawSamples > 0 ? t.yawErrorSum / static_cast<double>(t.yawSamples) : 0.0,
                    t.yawErrorMax, t.backwardsFrames, t.backwardsRunMax, t.backwardsAirborne,
                    static_cast<double>(t.maxAirborne),
                    t.leanSamples > 0 ? t.leanSum / static_cast<double>(t.leanSamples) : 0.0,
                    t.maxLeanDeg, static_cast<double>(t.deepestSink),
                    static_cast<double>(t.radius), top.c_str());
    }
    std::fflush(stdout);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The measurement the owner's three reports need, taken on the film rather than on its scene, over
// the film's own length rather than 90 s, at more than one seed.
//
//   ./build/release/tests/avgen_tests "[.probe][multicam-defects]"
TEST_CASE("probe: the film's cast over its own length, from the project",
          "[.probe][multicam-defects]") {
    for (std::uint32_t shift : {0u, 900001u, 17u, 4242u}) {
        const auto run = playFilm(226.0, shift);
        report(fmt::format("aliens, seed shift {}", shift).c_str(), run, kAliens);
        report(fmt::format("farm, seed shift {}", shift).c_str(), run, kFarm);
    }
}

// ---------------------------------------------------------------------------------------------
// Defect 1, narrowed: *where* the three frozen bodies are standing, and whether anything they want
// is on the same piece of ground.
//
// `NavGrid::regionAt` is the instrument and its header says why it is the right one: the flood fill
// uses "exactly the connectivity A* uses, so 'different regions' and 'A* will not find a route'"
// are the same statement. Region 0 is *not walkable at all* -- a body standing in region 0 cannot
// path anywhere, which is a different and worse fact than being on a small island.
TEST_CASE("probe: where the film's cast is standing, and what it can reach",
          "[.probe][multicam-defects]") {
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(filmProject());
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewport(1600, 900);
    comp->scene().detailLimits.entityDistanceCull = false;

    const entity::Navigator& nav = comp->entityWorld().navigator();
    const entity::NavGrid* grid = nav.grid();
    std::printf("\n===== the film, as the project loads it =====\n");
    std::printf("navWadeDepth %.4f  navBodyRadius %.4f  grid %s\n", nav.settings().wadeDepth,
                nav.settings().bodyRadius,
                grid == nullptr ? "ABSENT" : (grid->valid() ? "present" : "INVALID"));
    if (grid != nullptr && grid->valid()) {
        std::printf("regions %zu, largest %zu cells, stranded %zu of %zu walkable\n",
                    grid->stats().regions, grid->stats().largestRegion, grid->stats().stranded,
                    grid->stats().walkable);
    }

    // Step a little so grounding has settled and the deciders have run at least one tick.
    FrameTime time;
    for (int i = 0; i < 80; ++i) {
        time.renderTime = static_cast<double>(i) / 40.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 40.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);
    }

    std::printf("\n  %-11s %-24s %6s %6s %5s %8s %s\n", "body", "position", "ground", "depth",
                "rgn", "rgnCells", "navigable / why not");
    for (const char* name : kAliens) {
        const entity::Entity* e = comp->entityWorld().find(name);
        if (e == nullptr) {
            std::printf("  %-11s ABSENT\n", name);
            continue;
        }
        const glm::vec3 p = e->state().position();
        const entity::NavSample sm = nav.sample(glm::vec2(p.x, p.z));
        const std::uint16_t r = grid != nullptr && grid->valid()
                                    ? grid->regionAt(glm::vec2(p.x, p.z))
                                    : 0;
        std::printf("  %-11s (%7.1f,%6.2f,%7.1f) %6.2f %6.2f %5u %8zu  %s / %s\n", name,
                    static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z),
                    static_cast<double>(sm.ground), static_cast<double>(sm.waterDepth), r,
                    grid != nullptr && grid->valid() ? grid->regionSize(r) : 0,
                    sm.navigable ? "yes" : "NO", entity::navRejectName(sm.reject));
    }

    // And the same for the places they want. A percept's own position, its region, and what the
    // planner says about walking there -- so "it wanted something it could not reach" stops being
    // an inference from a name in an overlay.
    std::printf("\n  what each body perceives, and whether it can walk to it:\n");
    for (const char* name : kAliens) {
        const entity::Entity* e = comp->entityWorld().find(name);
        if (e == nullptr) {
            continue;
        }
        const glm::vec3 from = e->state().position();
        const std::uint16_t mine =
            grid != nullptr && grid->valid() ? grid->regionAt(glm::vec2(from.x, from.z)) : 0;
        std::size_t ok = 0;
        std::size_t total = 0;
        std::map<std::string, int> refusals;
        for (const entity::Percept& q : e->percepts()) {
            ++total;
            entity::PathRequest req;
            req.from = glm::vec2(from.x, from.z);
            req.to = glm::vec2(q.position.x, q.position.z);
            req.goalTolerance = 8.0f;
            const entity::PathResult r = nav.requestPath(req);
            if (r.ok()) {
                ++ok;
            } else {
                refusals[entity::pathStatusName(r.status)] += 1;
            }
        }
        // And the same question asked the cheap way, which is the way `GoalTaste::requireReachable`
        // would ask it. If these two disagree the filter is measuring something the planner does
        // not, and a filter that drops a goal the planner would have reached is worse than none.
        std::size_t sameRgn = 0;
        std::size_t otherRgn = 0;
        std::size_t zeroRgn = 0;
        for (const entity::Percept& q : e->percepts()) {
            const std::uint16_t r = grid != nullptr && grid->valid()
                                        ? grid->regionAt(glm::vec2(q.position.x, q.position.z))
                                        : 0;
            if (r == 0) {
                ++zeroRgn;
            } else if (r == mine) {
                ++sameRgn;
            } else {
                ++otherRgn;
            }
        }
        std::string why;
        for (const auto& [k, n] : refusals) {
            why += fmt::format(" {}x{}", n, k);
        }
        std::printf("  %-11s region %-3u  %zu of %zu percepts routable%s   by region: same %zu, "
                    "other %zu, unwalkable cell %zu\n",
                    name, mine, ok, total, why.c_str(), sameRgn, otherRgn, zeroRgn);
    }
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------------------------
// Defect 1, fixed, with the control that shows the failure coming back.
//
// The control is not "run it again and hope": it is the *same* computation over the *unfiltered*
// hero list, which is what the code did before ADR-349. `ClearanceField::heroPenetration` at a
// body's own feet must be positive with its own hero in the list and zero without it. If that ever
// stops being a difference, the arm below is measuring nothing.
TEST_CASE("A starred character is not a wall around itself", "[glowmere][multicam][entity]") {
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(filmProject());
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    // The five are heroes in this project, and they have to be: the Auto-director cuts to them, and
    // an unstarred character gets no shots. That is the condition the fix has to survive, not one
    // it may remove.
    std::vector<std::string> starred;
    for (const world::HeroPoint& h : comp->heroes()) {
        if (std::find(kAliens.begin(), kAliens.end(), h.name) != kAliens.end()) {
            starred.push_back(h.name);
        }
    }
    INFO("starred characters: " << starred.size());
    CHECK(starred.size() == kAliens.size());

    const entity::Navigator& nav = comp->entityWorld().navigator();
    for (const char* name : kAliens) {
        const entity::Entity* e = comp->entityWorld().find(name);
        REQUIRE(e != nullptr);
        const glm::vec3 p = e->state().position();
        const entity::NavSample sm = nav.sample(glm::vec2(p.x, p.z));
        INFO(name << " stands at (" << p.x << ", " << p.z << ") and the navigator says "
                  << entity::navRejectName(sm.reject));
        CHECK(sm.navigable);
        CHECK(sm.reject != entity::NavReject::InsideHero);
    }

    // ---- the control: the same query against the list the code used to pass -------------------
    // The query is `TerrainQuery::at`'s own, reproduced rather than approximated: it does not ask
    // at the body's y, it asks at `ground + heroMargin`. Getting that wrong is how the first cut of
    // this control came out 0 for five bodies the engine had just refused to stand -- a control
    // that agrees with the fix for the wrong reason is worse than none (ADR-182).
    constexpr float kHeroMargin = 1.0f;   // world::WalkRules::heroMargin
    world::ClearanceField unfiltered;
    unfiltered.heroes = comp->heroes();
    unfiltered.cameraRadius = 0.6f;   // buildNavigator's walker value
    unfiltered.groundClearance = 0.0f;
    std::vector<std::string> trapped;
    for (const char* name : kAliens) {
        const entity::Entity* e = comp->entityWorld().find(name);
        REQUIRE(e != nullptr);
        const glm::vec3 p = e->state().position();
        const float ground = nav.groundHeight(glm::vec2(p.x, p.z));
        if (unfiltered.heroPenetration(glm::vec3(p.x, ground + kHeroMargin, p.z)) > 0.0f) {
            trapped.emplace_back(name);
        }
    }
    std::string names;
    for (const std::string& n : trapped) {
        names += (names.empty() ? "" : ", ") + n;
    }
    INFO(trapped.size() << " of " << kAliens.size()
                        << " characters would stand inside their own hero capsule if driven heroes "
                           "were still passed to the walker's clearance field: "
                        << names);
    // **Three**, and the arm is worth more than its count because of *which* three: `rook`, `tide`
    // and `vane` are exactly the bodies that travelled 0.00 m in 226 s at every seed, and exactly
    // the ones the owner reported as "just standing there". `sage` clears its own capsule on the
    // vertical test and `ember` only because `nodes/ember/position` puts its body 68 m from its own
    // hero anchor -- stale project state that happens to have saved it, recorded separately and not
    // relied on. A control that came out zero would mean this arm proves nothing.
    CHECK(trapped.size() == 3);
}

TEST_CASE("The film's five characters all walk, at more than one seed",
          "[glowmere][multicam][entity]") {
    // 150 s rather than the film's 226: long enough that a body which is going to freeze has
    // frozen (the three that did froze from frame one and never moved), short enough to run twice.
    for (std::uint32_t shift : {0u, 900001u}) {
        const auto run = playFilm(150.0, shift);
        float fleet = 0.0f;
        int statues = 0;
        for (const char* name : kAliens) {
            auto it = run.find(name);
            REQUIRE(it != run.end());
            const Track& t = it->second;
            INFO("seed shift " << shift << ": " << name << " travelled " << t.travelled
                               << " m in 150 s, idle " << t.idleFrames << "/" << t.frames
                               << ", stalls " << t.stalls);
            // **The floor is about statues, not about pace.** The three that failed managed
            // 0.00 m -- not "a little" but *none*, from frame one, at every seed. The slowest
            // honest body measured across four seeds is `tide` at 38.9 m in 150 s, which is a
            // loiterer and is what `tide` is authored to be (0.8 Hz, dwell 6 s, the diver that
            // stands and looks at water). 20 m keeps a two-fold margin under that and is still
            // infinitely above the failure.
            CHECK(t.travelled > 20.0f);
            // And it is walking, not vibrating on the spot: a body idle for every frame of the run
            // is the failure this arm exists for.
            CHECK(t.idleFrames < t.frames);
            fleet += t.travelled;
            statues += t.travelled <= 20.0f ? 1 : 0;
        }
        // **The count, not the metres.** A floor on the fleet total looked like the right aggregate
        // and is a vacuous arm: before the fix, seed 900001 gave rook 0, tide 0, vane 0, sage 480
        // and ember 516 over 226 s -- about 660 m over 150 s -- against 802 m after, so any total
        // low enough to pass today would have passed the bug too (ADR-182). What actually changed
        // is how many bodies were statues, and that went 3 -> 0 at every seed tried.
        INFO("seed shift " << shift << ": " << statues << " of " << kAliens.size()
                           << " bodies under 20 m; the five travelled " << fleet
                           << " m between them");
        CHECK(statues == 0);
    }
}

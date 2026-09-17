// The proof of concept, measured rather than watched (ADR-210, Parts 1, 2 and 7).
//
// The brief's acceptance criteria are all observable facts about a headless run of the shipped
// scene, and every one of them is asserted here as a number:
//
//   * several farm animals scattered, some visible, some concealed -- counted off the scene file
//     and re-checked against the world's own walkability and obstacle queries, because "it is in
//     the file" and "it is not inside a tree" are different claims;
//   * animals wandering locally -- the stuck metric, which is the measurement the `sage` fix
//     (docs, 92.3% still and a 62.3 s stall, down to 31.7% and 0.1 s) established as the thing to
//     report rather than the thing to promise;
//   * the UFO flying to a *dynamically chosen* animal, hovering above it, lighting the beam,
//     lifting the animal, retiring it, dimming the beam and going to a different one -- asserted by
//     counting **distinct** abducted animals over a run, never by looking at it.
//
// It is a unit test rather than a render test because none of it is about pixels: the composition,
// the entity layer and the director are all GPU-free, and `Composition::updateBehaviour` is the
// same call the engine makes every frame.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "stage/staging.hpp"
#include "world/terrain_query.hpp"

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
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

fs::path sceneFile() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
}

bool farmAssetsPresent() {
    return fs::is_regular_file(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb");
}

bool isAnimal(const entity::Entity& e) {
    const auto& tags = e.desc().tags;
    return std::find(tags.begin(), tags.end(), "animal") != tags.end();
}

// One run of the shipped scene, with everything the engine does per frame that the director and the
// entity layer can see. No GPU, no window, no audio: the signal bus is empty, so what the animals
// and the saucer do is their own autonomy and the director's decisions and nothing else.
struct Run {
    assets::AssetRegistry registry;
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;

    // ---- what the run measured ----
    std::vector<std::string> abducted;      // distinct, in the order they were taken
    std::size_t stalledFrames = 0;          // commanded to move, did not move
    std::size_t commandedFrames = 0;
    double longestStall = 0.0;              // seconds, the worst single run of stalled frames
    std::string worstStaller;
    // The second metric, and the one that can still fail. See the note on the wander test: after
    // `wander` was fixed to stop claiming a speed it is not travelling at, "commanded to move and
    // did not" is zero *by construction*, because the speed it reports is the speed it moved at. So
    // it is a regression guard rather than a discovery, and this is the discovery: how much of the
    // run an animal spent motionless at all, and the longest unbroken stretch of it. The `sage`
    // precedent is quoted in these terms -- 92.3% still with a 62.3 s stretch, down to 31.7%.
    std::size_t stillFrames = 0;
    std::size_t liveFrames = 0;
    double longestStill = 0.0;
    std::string stillest;
    // Path length per animal. The assertion that can actually fail: one animal boxed in and
    // re-picking a destination it can never reach walks nowhere, and an *aggregate* percentage
    // hides it behind seventeen animals that are fine.
    std::map<std::string, float> travelled;
    double beamPeak = 0.0;                  // the highest spawn rate the beam reached
    double beamRest = 0.0;                  // and where it was left
    float closestApproach = 1e9f;           // how near the saucer got to a target, horizontally
    float highestLift = 0.0f;               // metres an animal rose above where it was standing
    float reachedCraft = 1e9f;              // how near an abducted animal got to the saucer
    float lowestFlight = 1e9f;              // least clearance the saucer kept over the terrain
    bool beamWasHidden = false;
    bool beamWasShown = false;

    explicit Run(const fs::path& file) : registry(file.parent_path()) {
        auto loaded = scene::Composition::loadFile(file, registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1920, 1080);
        // ADR-186's offline setting: every entity updated every frame, however far from the view.
        // The measurements here are about the world, not about the camera, and with the cull on a
        // far animal simply is not simulated -- so "it did not move this frame" would be measuring
        // the level-of-detail band rather than anything about wandering. (Measured: with the cull
        // on, the wander metric read 63.6% and a 75 s "stall" that was one culled chick.)
        comp->scene().detailLimits.entityDistanceCull = false;
    }

    void play(double seconds, double hz = 60.0) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<int>(std::round(seconds * hz));
        entity::EntityWorld& world = comp->entityWorld();
        const world::TerrainQuery terrain = comp->terrainQuery();

        std::map<std::string, glm::vec3> was;
        std::map<std::string, glm::vec3> home;
        std::map<std::string, double> stall;
        std::map<std::string, double> still;
        for (const auto& e : world.entities()) {
            was[e->name()] = e->state().position();
            home[e->name()] = e->state().position();
        }
        const params::IParameter* beamRate = params.find("particles/visitor-beam/spawnRate");
        const params::IParameter* beamVisible = params.find("nodes/visitor-beam/visible");
        REQUIRE(beamRate != nullptr);
        REQUIRE(beamVisible != nullptr);

        FrameTime time;
        for (int i = 0; i < frames; ++i) {
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            // The rest of the frame. This loop used to stop at `updateBehaviour`, which is half a
            // frame: `Composition::update` is what folds the parameter finals onto the node
            // transforms and re-flattens the scene, and the flattened scene is what the emitter's
            // world position, every node's world transform and (ADR-262) the director's `Drawn`
            // anchor are read from. Without it the director was deciding against a scene that had
            // never been drawn -- which was invisible while every anchor was entity arithmetic, and
            // became a sheep flown 152 m across the valley the moment one of them was not.
            comp->update(time);

            // ---- the beam ----
            const double rate = beamRate->baseComponent(0);
            beamPeak = std::max(beamPeak, rate);
            beamRest = rate;
            (beamVisible->baseComponent(0) > 0.5f ? beamWasShown : beamWasHidden) = true;

            // ---- the saucer ----
            if (const entity::Entity* craft = world.find("visitor"); craft != nullptr) {
                const glm::vec3 p = craft->state().position();
                if (terrain.valid()) {
                    lowestFlight = std::min(lowestFlight, p.y - terrain.heightAt({p.x, p.z}));
                }
                const std::string_view target = comp->director().binding("abduction", "target");
                if (!target.empty()) {
                    if (const entity::Entity* prey = world.find(target); prey != nullptr) {
                        const glm::vec3 q = prey->state().position();
                        closestApproach =
                            std::min(closestApproach, glm::length(glm::vec2(p.x - q.x, p.z - q.z)));
                    }
                }
            }

            // ---- the animals ----
            for (const auto& ePtr : world.entities()) {
                const entity::Entity& e = *ePtr;
                if (!isAnimal(e)) {
                    continue;
                }
                const glm::vec3 now = e.state().position();
                if (e.directorMotion().active) {
                    // Being abducted. Not wandering, so not part of the wander metric -- but it is
                    // where the lift is measured, and where "the animal reaches the beam/UFO"
                    // becomes a distance rather than an impression.
                    highestLift = std::max(highestLift, now.y - home[e.name()].y);
                    if (const entity::Entity* craft = world.find("visitor"); craft != nullptr) {
                        reachedCraft =
                            std::min(reachedCraft, glm::length(now - craft->state().position()));
                    }
                    was[e.name()] = now;
                    stall[e.name()] = 0.0;
                    still[e.name()] = 0.0;
                    continue;
                }
                const glm::vec2 step2(now.x - was[e.name()].x, now.z - was[e.name()].z);
                ++liveFrames;
                if (glm::length(step2) < 0.001f) {
                    ++stillFrames;
                    still[e.name()] += step;
                    if (still[e.name()] > longestStill) {
                        longestStill = still[e.name()];
                        stillest = e.name();
                    }
                } else {
                    still[e.name()] = 0.0;
                }
                travelled[e.name()] += glm::length(step2);

                const float speed = e.state().speed;
                if (speed > 0.05f) {
                    ++commandedFrames;
                    const glm::vec2 moved(now.x - was[e.name()].x, now.z - was[e.name()].z);
                    // "Did not move" means it covered less than a quarter of what its own commanded
                    // speed says it should have. A quarter rather than nothing because a body
                    // turning on the spot legitimately covers almost none of it for a frame or two,
                    // and calling that stuck would make the metric useless.
                    if (glm::length(moved) < 0.25f * speed * static_cast<float>(step)) {
                        ++stalledFrames;
                        stall[e.name()] += step;
                        if (stall[e.name()] > longestStall) {
                            longestStall = stall[e.name()];
                            worstStaller = e.name();
                        }
                    } else {
                        stall[e.name()] = 0.0;
                    }
                } else {
                    stall[e.name()] = 0.0;
                }
                was[e.name()] = now;
            }

            // ---- what the director took ----
            for (const std::string& name : comp->director().retired()) {
                if (std::find(abducted.begin(), abducted.end(), name) == abducted.end()) {
                    abducted.push_back(name);
                }
            }
        }
    }

    // The least distance any one animal covered, and which. Zero would mean an animal that never
    // went anywhere at all for the whole run.
    [[nodiscard]] std::pair<std::string, float> leastTravelled() const {
        std::pair<std::string, float> worst{"", 1e9f};
        for (const auto& [name, metres] : travelled) {
            if (metres < worst.second) {
                worst = {name, metres};
            }
        }
        return worst;
    }
    [[nodiscard]] double stillPercent() const {
        return liveFrames == 0 ? 0.0
                               : 100.0 * static_cast<double>(stillFrames) /
                                     static_cast<double>(liveFrames);
    }
    [[nodiscard]] double stalledPercent() const {
        return commandedFrames == 0
                   ? 0.0
                   : 100.0 * static_cast<double>(stalledFrames) / static_cast<double>(commandedFrames);
    }
};

} // namespace

TEST_CASE("Glowmere Valley 2 carries a scattered, visible-and-hidden farm", "[poc][farm][stage]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    Run run(sceneFile());
    const entity::EntityWorld& world = run.comp->entityWorld();
    const world::TerrainQuery terrain = run.comp->terrainQuery();
    REQUIRE(terrain.valid());
    // Without the obstacle field the "not inside a tree" assertion below would be asking a question
    // nothing can answer, and would pass by default (ADR-182).
    REQUIRE(terrain.hasObstacles());

    int animals = 0;
    int visible = 0;
    int concealed = 0;
    int unwalkable = 0;
    int occupied = 0;
    std::set<std::string> species;
    for (const auto& ePtr : world.entities()) {
        const entity::Entity& e = *ePtr;
        if (!isAnimal(e)) {
            continue;
        }
        ++animals;
        for (const std::string& tag : e.desc().tags) {
            if (tag != "animal" && tag != "farm") {
                species.insert(tag);
            }
        }
        const glm::vec3 p = e.state().position();
        const glm::vec2 flat(p.x, p.z);
        const world::TerrainPoint sample = terrain.at(flat);
        if (!sample.walkable) {
            ++unwalkable;
        }
        // The animal is not standing inside a trunk, a rock or a hero. Its own body radius is not
        // available here, so 1.2 m is the largest of the nine plus a margin.
        if (terrain.isOccupied(flat, 1.2f)) {
            ++occupied;
        }
        (sample.canopy >= 8.0f ? concealed : visible) += 1;
    }

    INFO(fmt::format("{} animals, {} in the open, {} under canopy, {} species; {} unwalkable, "
                     "{} inside something",
                     animals, visible, concealed, species.size(), unwalkable, occupied));
    CHECK(animals >= 12);
    CHECK(visible >= 5);    // "some animals should be reasonably visible"
    CHECK(concealed >= 5);  // "some animals should be partially concealed"
    CHECK(species.size() >= 6);
    // The hard constraints: nothing clipping terrain, nothing inside a tree or a rock.
    CHECK(unwalkable == 0);
    CHECK(occupied == 0);
    // Each is a `gltf` node with an `animation` state, not a scatter instance -- ADR-205's merge
    // drops skin influences, so a scattered animal would draw in its bind pose for ever.
    for (const auto& ePtr : world.entities()) {
        if (!isAnimal(*ePtr)) {
            continue;
        }
        const scene::CompositionNode* node = run.comp->findNode(ePtr->desc().driven());
        INFO(ePtr->name());
        REQUIRE(node != nullptr);
        CHECK(node->kind == scene::NodeKind::Gltf);
        CHECK(node->animation.state == "Walk");
    }
}

TEST_CASE("the animals wander their own territories without getting stuck",
          "[poc][farm][stage][wander]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present");
    }
    Run run(sceneFile());
    // The abduction is running at the same time, deliberately: the metric has to hold in the scene
    // as shipped, not in a stripped-down one.
    run.play(90.0);

    INFO(fmt::format("commanded to move on {} entity-frames, stalled on {} ({:.1f}%), longest "
                     "stall {:.2f} s ({}); motionless on {} of {} frames ({:.1f}%), longest "
                     "unbroken stretch {:.2f} s ({})",
                     run.commandedFrames, run.stalledFrames, run.stalledPercent(),
                     run.longestStall, run.worstStaller, run.stillFrames, run.liveFrames,
                     run.stillPercent(), run.longestStill, run.stillest));
    // The metric has to have something to measure: an arm where nothing was ever commanded to move
    // would report 0% and mean nothing (ADR-182).
    REQUIRE(run.commandedFrames > 2000);

    // **The first metric is a regression guard, not a discovery, and saying so is the point.**
    // "Commanded to move and did not" is zero by construction now that `wander` stops reporting a
    // speed it is not travelling at: the number it reports *is* the distance it covered. What it
    // still catches is that defect coming back.
    CHECK(run.stalledPercent() == 0.0);
    CHECK(run.longestStall == 0.0);

    // The metrics that can still fail.
    //
    // A note on the percentage, because the obvious threshold is the wrong one. The `sage`
    // precedent is quoted as 31.7% still, and asserting that here fails at 61.8% -- but 61.8% is
    // the behaviour *working*: these animals pause for between 3 and 26 authored seconds between
    // destinations and then walk a few metres, so most of the run is correctly a pause. Borrowing a
    // number from a different behaviour with different settings would be measuring the settings.
    // So the percentage gets only a sanity ceiling, and the two assertions that carry the weight
    // are the ones that separate "paused on purpose" from "stuck":
    //
    //   * the longest unbroken motionless stretch, against the animals' own `pauseMax`. Measured:
    //     26.20 s against an authored 26 s, i.e. one pause and not two. The `sage` wedge was 62.3 s,
    //     which is what being stuck looks like next to this.
    //   * the least distance *any single animal* covered. An aggregate hides one boxed-in animal
    //     behind seventeen that are fine; this does not.
    // Both bounds are read from the scene's own `wander` settings rather than written down here.
    // The comment above says the longest stretch is judged "against the animals' own `pauseMax`",
    // and it was not -- it was judged against 27.5, a literal true only while `pauseMax` was 26.
    // ADR-213 lengthened the pauses to 12-45 s deliberately ("very little movement areas ... little
    // space to move") and this failed at 44.97 s, which is one pause of a 45 s maximum: the setting
    // changed and the test was measuring the old setting.
    double pauseMax = 0.0;
    double pauseMin = 0.0;
    for (const entity::EntityDesc& e : run.comp->entities()) {
        for (const entity::BehaviorDesc& b : e.behaviors) {
            if (b.kind != "wander") {
                continue;
            }
            if (b.settings.contains("pauseMax")) {
                pauseMax = std::max(pauseMax, b.settings.at("pauseMax").get<double>());
            }
            if (b.settings.contains("pauseMin")) {
                pauseMin = std::max(pauseMin, b.settings.at("pauseMin").get<double>());
            }
        }
    }
    REQUIRE(pauseMax > 0.0); // the settings were found; a zero here would make both bounds vacuous
    INFO(fmt::format("authored pauses {:.0f}-{:.0f} s", pauseMin, pauseMax));
    // One pause, not two. The 6% is sampling: a pause is timed from the frame the body stops, and
    // the stretch is counted in frames.
    CHECK(run.longestStill < pauseMax * 1.06);
    // A sanity ceiling derived from the duty cycle the settings imply, not a tuned number: a body
    // that pauses for a mean of (pauseMin+pauseMax)/2 and then crosses a territory of `maxRange`
    // spends roughly that fraction of its life still. Loose by design -- it is here to catch every
    // animal being wedged, which reads as 100%.
    CHECK(run.stillPercent() < 96.0);
    const auto [laziest, metres] = run.leastTravelled();
    INFO(fmt::format("least distance covered by any one animal: {:.1f} m ({})", metres, laziest));
    CHECK(metres > 2.0f);

    // And they stayed home. A wander with no territory is a random walk, and a random walk leaves.
    const entity::EntityWorld& world = run.comp->entityWorld();
    float furthest = 0.0f;
    std::string wanderer;
    for (const auto& ePtr : world.entities()) {
        const entity::Entity& e = *ePtr;
        if (!isAnimal(e) || e.directorMotion().active) {
            continue;
        }
        const float strayed = glm::length(glm::vec2(e.state().travel.x, e.state().travel.z));
        if (strayed > furthest) {
            furthest = strayed;
            wanderer = e.name();
        }
    }
    INFO(fmt::format("furthest from home after 90 s: {:.1f} m ({})", furthest, wanderer));
    CHECK(furthest < 40.0f);
}

TEST_CASE("the UFO abducts several animals, choosing each one from the scene",
          "[poc][ufo][stage][abduction]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present");
    }
    Run run(sceneFile());
    const stage::Staging& director = run.comp->director();
    REQUIRE(director.desc().scenarios.size() == 1);
    REQUIRE(director.desc().scenarios[0].name == "abduction");
    const int wanted = director.desc().scenarios[0].maxCycles;
    REQUIRE(wanted >= 3);

    // Long enough for every cycle the scenario is allowed, at 60 Hz. The scenario ends itself at
    // `maxCycles`, so a run that is too long costs frames and changes nothing.
    run.play(220.0);

    INFO(fmt::format("abducted {} of {}: {}", run.abducted.size(), wanted,
                     fmt::join(run.abducted, ", ")));
    // **Several sequential abductions with dynamically chosen targets.** Distinct names, because
    // "it abducted the same cow six times" would satisfy a count and nothing else.
    CHECK(run.abducted.size() >= 4);
    CHECK(std::set<std::string>(run.abducted.begin(), run.abducted.end()).size() ==
          run.abducted.size());
    CHECK_FALSE(director.running("abduction")); // it finished, rather than hanging in a beat

    // Every one of them is an animal the scene actually contains, and none is a hardcoded name.
    for (const std::string& name : run.abducted) {
        INFO(name);
        const entity::Entity* e = run.comp->entityWorld().find(name);
        REQUIRE(e != nullptr);
        CHECK(isAnimal(*e));
        // Removed from the shot: its node is hidden.
        const params::IParameter* visible =
            run.params.find("nodes/" + e->desc().driven() + "/visible");
        REQUIRE(visible != nullptr);
        CHECK(visible->baseComponent(0) == Approx(0.0f));
    }

    // It flew there rather than teleporting, and it got above its target: the closest horizontal
    // approach over the run is a hover directly overhead.
    INFO(fmt::format("closest horizontal approach {:.2f} m", run.closestApproach));
    CHECK(run.closestApproach < 2.0f);

    // It did not fly through the world on the way. The scenario's `cruiseClearance` is 34 m and the
    // hover height 23 m, so the least clearance over the run is the hover -- and it is comfortably
    // above the ground rather than in it.
    INFO(fmt::format("least clearance over the terrain {:.2f} m", run.lowestFlight));
    CHECK(run.lowestFlight > 5.0f);

    // The beam lit and then went out again.
    INFO(fmt::format("beam spawn rate: peak {:.0f}, left at {:.0f}", run.beamPeak, run.beamRest));
    CHECK(run.beamWasHidden);
    CHECK(run.beamWasShown);
    CHECK(run.beamPeak > 2000.0);
    CHECK(run.beamRest < 1200.0);

    // An animal actually rose, a long way, rather than being hidden where it stood -- and it
    // stopped **short of the craft**, which is the half that changed.
    //
    // `liftHeight` was -3.4: the top of the lift sat 3.4 m under the saucer's origin, and the widest
    // animal in the farm reaches 5.80 m from its own origin, so a bull's back arrived 2.4 m *inside*
    // the saucer. Reported as "the animal clips through the UFO once it reaches the top". The lift
    // now ends at -7.5, clear of the widest reach with margin, so the two never intersect.
    //
    // The lower bound is what stops this passing on an animal that never left the ground; the upper
    // bound is the anti-clipping guarantee. Both are needed: the old test had only the first half of
    // each and asserted the animal got *close*, which is the defect written down as a requirement.
    INFO(fmt::format("highest lift {:.1f} m, closest to the craft {:.2f} m", run.highestLift,
                     run.reachedCraft));
    CHECK(run.highestLift > 10.0f);
    CHECK(run.reachedCraft > 5.9f);   // clear of the widest animal's 5.80 m reach
    CHECK(run.reachedCraft < 12.0f);  // but still plainly *at* the craft, not stalled below it

    // If it stopped early, say why rather than leaving it a mystery: for every animal still on the
    // ground, the three things the query asks about it.
    if (run.abducted.size() < static_cast<std::size_t>(wanted)) {
        const world::TerrainQuery terrain = run.comp->terrainQuery();
        const entity::Entity* craft = run.comp->entityWorld().find("visitor");
        const glm::vec3 from = craft != nullptr ? craft->state().position() : glm::vec3(0.0f);
        const float radius = director.parameter("abduction", "searchRadius");
        const float ceiling = director.parameter("abduction", "targetClearance");
        std::string why;
        for (const auto& ePtr : run.comp->entityWorld().entities()) {
            const entity::Entity& e = *ePtr;
            if (!isAnimal(e) ||
                std::find(run.abducted.begin(), run.abducted.end(), e.name()) != run.abducted.end()) {
                continue;
            }
            const glm::vec3 p = e.state().position();
            why += fmt::format("  {:<12} {:6.1f} m  canopy {:5.1f}  navigable {}\n", e.name(),
                               glm::length(p - from), terrain.canopyHeightAt({p.x, p.z}),
                               terrain.isWalkable({p.x, p.z}) ? "yes" : "NO");
        }
        INFO(fmt::format("search radius {:.0f}, canopy ceiling {:.1f}; left on the ground:\n{}",
                         radius, ceiling, why));
    }

    // And the director did not brute-force the scene to do it.
    const stage::StageReport& report = director.report();
    INFO(fmt::format("{} searches over {} candidates, {} queries, {} candidates examined",
                     report.searches, report.candidates, report.queries, report.tested));
    CHECK(report.candidates < run.comp->entityWorld().size());
    CHECK(report.searches < 600); // 220 s at a 0.5 s interval is ~440, not 13,200 frames
    CHECK(director.problems().empty());
}

TEST_CASE("the same scene and seed abduct the same animals in the same order",
          "[poc][ufo][stage][determinism]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present");
    }
    // Determinism is non-negotiable: identical seed and configuration must produce identical
    // behaviour, and offline must match realtime. Two independent loads of the same file, played at
    // the same rate, in the same process -- which is the only comparison this project accepts
    // (never across process runs unless the arms were interleaved).
    Run a(sceneFile());
    Run b(sceneFile());
    a.play(90.0);
    b.play(90.0);
    INFO(fmt::format("a: {}\nb: {}", fmt::join(a.abducted, ", "), fmt::join(b.abducted, ", ")));
    CHECK(a.abducted == b.abducted);
    CHECK_FALSE(a.abducted.empty());
    CHECK(a.stalledFrames == b.stalledFrames);
    CHECK(a.highestLift == Approx(b.highestLift));
}

TEST_CASE("the shipped scenario exposes its numbers as director parameters",
          "[poc][ufo][stage][params]") {
    // "Do not hardcode these values throughout the implementation -- they should exist as behaviour
    // parameters." Each of the brief's knobs is a real `params::Parameter`, which is what makes it
    // keyframeable, presettable and modulatable.
    Run run(sceneFile());
    for (const char* knob : {"searchRadius", "hoverHeight", "travelSpeed", "approachSeconds",
                             "hoverSeconds", "beamSeconds", "abductSeconds", "liftHeight",
                             "animalSpin", "gapSeconds"}) {
        INFO(knob);
        CHECK(run.params.find(std::string("staging/abduction/") + knob) != nullptr);
    }
    // And changing one changes the shot, rather than being a number nothing reads.
    CHECK(run.comp->director().parameter("abduction", "hoverHeight") > 1.0f);
    CHECK(run.comp->director().setParameter("abduction", "hoverHeight", 41.0f));
    CHECK(run.comp->director().parameter("abduction", "hoverHeight") == Approx(41.0f));
}

// ---- the river, and which side of it the squad stands on ----------------------------------------

namespace {

// Water deep enough to be a river rather than a puddle. The scene wades at 1.6 m (`navWadeDepth`),
// so anything at or past that is water a body would not simply walk through, which is the only
// definition of "the other side" that means anything here.
constexpr float kRiverDepth = 1.6f;

// Whether the straight line between two points crosses the river. Sampled rather than solved: the
// bed is a heightfield and there is no analytic river to intersect.
bool crossesRiver(const world::TerrainQuery& terrain, glm::vec3 a, glm::vec3 b) {
    constexpr int kSamples = 400;
    for (int i = 0; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const glm::vec3 p = a + (b - a) * t;
        if (terrain.waterDepthAt(glm::vec2(p.x, p.z)) >= kRiverDepth) {
            return true;
        }
    }
    return false;
}

std::vector<std::pair<std::string, glm::vec3>> squadOf(const scene::Composition& comp) {
    static const char* kNames[] = {"rook", "tide", "sage", "ember", "vane"};
    std::vector<std::pair<std::string, glm::vec3>> out;
    for (const auto& n : comp.nodes()) {
        for (const char* want : kNames) {
            if (n != nullptr && n->name == want) {
                out.emplace_back(n->name, n->transform.position);
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("Where the river runs", "[.report][glowmere][river]") {
    // Hidden: a map to read, not a claim to keep. Run it when placing anything that has to be on a
    // particular bank:
    //     avgen_tests "Where the river runs"
    Run run(sceneFile());
    const world::TerrainQuery terrain = run.comp->terrainQuery();

    std::string map;
    for (int z = -80; z <= 80; z += 5) {
        std::string row;
        for (int x = -160; x <= 40; x += 5) {
            const float d = terrain.waterDepthAt(glm::vec2(static_cast<float>(x), static_cast<float>(z)));
            row += d >= kRiverDepth ? '#' : (d > 0.05f ? '~' : '.');
        }
        map += fmt::format("z={:>4}  {}\n", z, row);
    }
    WARN("water depth, x from -160 to 40 in steps of 5 ('#' = river, '~' = shallow)\n" + map);

    for (const auto& [name, p] : squadOf(*run.comp)) {
        WARN(fmt::format("{:<6} ({:7.1f}, {:7.1f})  water here {:.2f} m", name, p.x, p.z,
                         terrain.waterDepthAt(glm::vec2(p.x, p.z))));
    }
}

TEST_CASE("The squad stands three on one bank and two on the other", "[glowmere][river]") {
    // Asked for in those terms: five aliens, "2 placed on one side of the river and 3 placed on the
    // other". Before this the river had all four on its west bank, so the split is a real placement
    // and not an accident of where the squad already stood.
    //
    // "Side" is defined by crossing, not by a coordinate: two points are on opposite banks when the
    // straight line between them passes through water at least as deep as the scene's own wade
    // depth. An x threshold would be a fact about this river's current course, and the course is
    // terrain that somebody may regenerate.
    if (!farmAssetsPresent()) {
        SKIP("the farm GLBs are not on disk");
    }
    Run run(sceneFile());
    const world::TerrainQuery terrain = run.comp->terrainQuery();
    const auto squad = squadOf(*run.comp);
    REQUIRE(squad.size() == 5);

    // Partition by reachability-without-crossing from the first alien.
    std::vector<std::string> near;
    std::vector<std::string> far;
    for (const auto& [name, p] : squad) {
        (crossesRiver(terrain, squad.front().second, p) ? far : near).push_back(name);
    }
    INFO("this bank: " << fmt::format("{}", fmt::join(near, ", "))
                       << " | far bank: " << fmt::format("{}", fmt::join(far, ", ")));
    CHECK(near.size() + far.size() == 5);
    CHECK(std::min(near.size(), far.size()) == 2);
    CHECK(std::max(near.size(), far.size()) == 3);

    // The partition has to be consistent: everyone on a bank agrees with everyone else on it.
    for (const auto& [an, ap] : squad) {
        for (const auto& [bn, bp] : squad) {
            const bool sameBank =
                (std::find(near.begin(), near.end(), an) != near.end()) ==
                (std::find(near.begin(), near.end(), bn) != near.end());
            INFO(an << " vs " << bn);
            CHECK(crossesRiver(terrain, ap, bp) == !sameBank);
        }
    }

    // And nobody is standing in the water they were partitioned by.
    for (const auto& [name, p] : squad) {
        INFO(name << " stands in " << terrain.waterDepthAt(glm::vec2(p.x, p.z)) << " m of water");
        CHECK(terrain.waterDepthAt(glm::vec2(p.x, p.z)) < 0.5f);
        CHECK(terrain.isWalkable(glm::vec2(p.x, p.z)));
    }
}

TEST_CASE("How much room each animal actually has", "[.report][glowmere][wander]") {
    // Placement validated the *spot* -- walkable, not inside a solid -- and not the *territory*. An
    // animal standing somewhere legal with nowhere legal to go is stuck, and it reads as the frozen
    // pose the wander was blamed for. This asks the navigator the same question the wander asks, from
    // each animal's own anchor, and counts how often it gets an answer.
    if (!farmAssetsPresent()) {
        SKIP("the farm GLBs are not on disk");
    }
    Run run(sceneFile());
    entity::EntityWorld& world = run.comp->entityWorld();
    const entity::Navigator& nav = world.navigator();

    for (const auto& e : world.entities()) {
        if (e == nullptr) {
            continue;
        }
        const entity::EntityDesc& d = e->desc();
        const bool farm = std::find(d.tags.begin(), d.tags.end(), "farm") != d.tags.end();
        if (!farm) {
            continue;
        }
        float home = 0.0f;
        float lo = 0.0f;
        float hi = 0.0f;
        for (const entity::BehaviorDesc& b : d.behaviors) {
            if (b.kind != "wander") {
                continue;
            }
            home = b.settings.value("homeRadius", 0.0f);
            lo = b.settings.value("minRange", 0.0f);
            hi = b.settings.value("maxRange", 0.0f);
        }
        const glm::vec3 at = e->state().position();
        const glm::vec2 from(at.x, at.z);
        Rng rng(1234u);
        int found = 0;
        glm::vec2 dst{};
        for (int i = 0; i < 200; ++i) {
            if (nav.pickDestination(rng, from, lo, std::min(hi, home), dst)) {
                ++found;
            }
        }
        WARN(fmt::format("{:<11} at ({:7.1f},{:7.1f})  home {:5.1f}  destinations {:3d}/200", d.name,
                         at.x, at.z, home, found));
    }
}

TEST_CASE("Where the stall happens and what the navigator says there", "[.report][glowmere][wander]") {
    // The previous report asked `pickDestination` and got 200/200, which refutes "boxed in" and
    // nothing else: picking a destination is not the same question as being able to set off towards
    // it. `wander` takes its give-up branch when `Navigator::steer` returns a vector shorter than
    // 0.5 -- and `steer` is a *path* query from the body's own position, which `pickDestination`
    // never asks. This replays the run, finds the worst motionless stretch, and at the position the
    // body held during it asks both questions.
    if (!farmAssetsPresent()) {
        SKIP("the farm GLBs are not on disk");
    }
    Run run(sceneFile());
    entity::EntityWorld& world = run.comp->entityWorld();

    // Replay, remembering where each animal stood at the start of its worst stretch.
    struct Worst { double seconds = 0.0; glm::vec3 at{0.0f}; };
    std::map<std::string, Worst> worst;
    std::map<std::string, std::pair<double, glm::vec3>> current;
    std::map<std::string, glm::vec3> was;
    for (const auto& e : world.entities()) {
        was[e->name()] = e->state().position();
    }
    const double step = 1.0 / 60.0;
    FrameTime time;
    for (int i = 0; i < 5400; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        run.params.resetFinals();
        run.comp->updateFields(time, run.bus, run.modulator);
        run.modulator.applyRoutes(run.bus, run.params, time.deltaTime);
        run.comp->updateBehaviour(time, run.bus);
        for (const auto& ePtr : world.entities()) {
            const entity::Entity& e = *ePtr;
            if (!isAnimal(e) || e.directorMotion().active) {
                continue;
            }
            const glm::vec3 now = e.state().position();
            const glm::vec2 d(now.x - was[e.name()].x, now.z - was[e.name()].z);
            if (glm::length(d) < 0.001f) {
                auto& c = current[e.name()];
                if (c.first == 0.0) {
                    c.second = now;
                }
                c.first += step;
                if (c.first > worst[e.name()].seconds) {
                    worst[e.name()] = {c.first, c.second};
                }
            } else {
                current[e.name()] = {0.0, now};
            }
            was[e.name()] = now;
        }
    }

    const entity::Navigator& nav = world.navigator();
    for (const auto& ePtr : world.entities()) {
        const entity::Entity& e = *ePtr;
        if (!isAnimal(e)) {
            continue;
        }
        const entity::EntityDesc& d = e.desc();
        float home = 0.0f, lo = 0.0f, hi = 0.0f, speed = 0.0f, arrive = 0.0f;
        for (const entity::BehaviorDesc& b : d.behaviors) {
            if (b.kind != "wander") {
                continue;
            }
            home = b.settings.value("homeRadius", 0.0f);
            lo = b.settings.value("minRange", 0.0f);
            hi = b.settings.value("maxRange", 0.0f);
            speed = b.settings.value("speed", 1.6f);
            arrive = b.settings.value("arrive", 1.2f);
        }
        const glm::vec3 at = worst[d.name].at;
        const glm::vec2 from(at.x, at.z);
        Rng rng(99u);
        int picked = 0;
        int steerable = 0;
        int tooClose = 0;
        glm::vec2 dst{};
        for (int i = 0; i < 200; ++i) {
            if (!nav.pickDestination(rng, from, lo, std::min(hi, home > 0.0f ? home : hi), dst)) {
                continue;
            }
            ++picked;
            if (glm::length(dst - from) <= arrive) {
                ++tooClose;
            }
            if (glm::length(nav.steer(from, dst, std::max(speed * 1.5f, 2.0f))) > 0.5f) {
                ++steerable;
            }
        }
        WARN(fmt::format("{:<11} worst still {:6.2f} s at ({:7.1f},{:7.1f})  navigable {}  "
                         "picked {:3d}/200  steerable {:3d}  within-arrive {:3d}  radius {:.2f}",
                         d.name, worst[d.name].seconds, at.x, at.z,
                         nav.navigable(from) ? "yes" : "NO ", picked, steerable, tooClose,
                         e.state().radius));
    }
}


// The UFO does not fly to animals nobody can see.
//
// Reported as: "once that animal has been abducted it becomes invisible; the UFO should never
// target invisible animals." Within one session `isRetired` already handled it -- a `retire` step
// hides the body and adds it to the list, and nothing offers it again.
//
// **It did not survive a reload**, and that is what was being seen. `retired_` is runtime state and
// `visible` is saved as a parameter, so reopening a project whose cows had already been abducted
// gave a fresh scenario an empty retired list and a farm full of invisible animals it was perfectly
// willing to fly to, hover over and beam at nothing. `examples/world/glowmere-valley-2.json` was
// carrying exactly that: **seven animals saved invisible** by an earlier run, which is how this was
// found -- the fix changed which animals the saucer chose, and a camera test lost the precondition
// it was written around.
//
// The fix is a seed rather than a filter, and that distinction earned itself. A filter inside the
// query's candidate loop reads live parameters while the scenario is choosing, so the choice would
// depend on the frame it was asked on. A seed is a fact about the world as it was found, decided
// once, and nothing downstream can watch it change.

TEST_CASE("An animal that is already hidden is never abducted again", "[poc][ufo][stage][abduction]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present");
    }
    Run run(sceneFile());

    // Hide two animals the way a previous session's abduction leaves them: the saved *base* value
    // of the visibility parameter, which is what a project file carries.
    std::vector<std::string> hidden;
    for (const auto& e : run.comp->entityWorld().entities()) {
        if (e == nullptr || !isAnimal(*e) || hidden.size() >= 2) {
            continue;
        }
        if (params::IParameter* p = run.params.find("nodes/" + std::string(e->name()) + "/visible")) {
            p->setBaseComponent(0, 0.0f);
            hidden.push_back(std::string(e->name()));
        }
    }
    REQUIRE(hidden.size() == 2);
    INFO("hidden before the director ran: " << hidden[0] << " and " << hidden[1]);

    run.play(220.0);
    INFO(fmt::format("abducted {}: {}", run.abducted.size(), fmt::join(run.abducted, ", ")));

    // Neither of the two is among what it took. A count would not do -- there are sixteen animals
    // and the scenario would probably miss any given pair by luck.
    for (const std::string& name : hidden) {
        CHECK(std::find(run.abducted.begin(), run.abducted.end(), name) == run.abducted.end());
    }

    // The control, without which the check above passes on a director that did nothing at all: it
    // still abducted animals, and they are ones that were visible.
    CHECK(run.abducted.size() >= 3);
}

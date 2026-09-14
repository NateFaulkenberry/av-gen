// World navigation, measured rather than looked at (ADR-093, §47).
//
// §47 is explicit that compiling is not the acceptance test: "does the alien travel, stay grounded,
// avoid obstacles, feel alive". Three of those four are measurable, and this is where they are
// measured -- on the real project, through the real engine, over a real minute of its timeline.
//
// The reason this is a test and not a one-off probe is the failure this project keeps having. Seven
// times a system has been built, tested by hand once, and wired into nothing; the way a navigation
// system joins that list is by working on the afternoon it was written. A number in a test is the
// only kind of evidence that is still true next month.
//
// It was also written after a false measurement. The first attempt at establishing that the walker
// moved counted green pixels in a rendered frame and was fooled by the valley's own bioluminescent
// plants. Reading `Entity::locomotion().position` asks the thing itself.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "scene/composition.hpp"
#include "world/terrain_query.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace avgen;

// Whether this build had the optimiser on. Wall-clock ceilings are only meaningful when it did;
// see "navigation costs what it claims to" for the measurements that make the case.
#ifdef NDEBUG
constexpr bool kOptimised = true;
#else
constexpr bool kOptimised = false;
#endif
namespace fs = std::filesystem;

namespace {

constexpr double kStep = 1.0 / 60.0;

struct Walk {
    std::vector<glm::vec3> positions;
    std::vector<float> speeds;
    std::vector<int> activities;
    double seconds = 0.0;
    // Behaviour level of detail, accumulated over the run (§44). Not a diagnostic: "AI must not run
    // at full rate for a character 500 m away" is a requirement, and these are the only numbers
    // that say whether it holds.
    std::size_t fullUpdates = 0;
    std::size_t coarseUpdates = 0;
    std::size_t skippedUpdates = 0;

    [[nodiscard]] float travelled() const {
        float total = 0.0f;
        for (std::size_t i = 1; i < positions.size(); ++i) {
            const glm::vec3 d = positions[i] - positions[i - 1];
            total += glm::length(glm::vec2(d.x, d.z));
        }
        return total;
    }
    [[nodiscard]] float netDisplacement() const {
        if (positions.size() < 2) {
            return 0.0f;
        }
        const glm::vec3 d = positions.back() - positions.front();
        return glm::length(glm::vec2(d.x, d.z));
    }
    // The radius of the ground the walker actually covered. A character that paces a circle has a
    // large `travelled` and a small net displacement; this catches both by asking how much of the
    // world it visited.
    [[nodiscard]] float spread() const {
        if (positions.empty()) {
            return 0.0f;
        }
        glm::vec2 lo(positions.front().x, positions.front().z);
        glm::vec2 hi = lo;
        for (const glm::vec3& p : positions) {
            lo = glm::min(lo, glm::vec2(p.x, p.z));
            hi = glm::max(hi, glm::vec2(p.x, p.z));
        }
        return glm::length(hi - lo);
    }
    // How much of the time the character is under way. Asked of the reported speed rather than of
    // the frame-to-frame delta, because behaviour level of detail updates a distant entity every
    // 100 ms and a positional test would report a character striding across a valley as stationary
    // on five frames in six -- measuring the update rate rather than the character.
    [[nodiscard]] double movingFraction() const {
        if (speeds.empty()) {
            return 0.0;
        }
        const auto moving = static_cast<double>(
            std::count_if(speeds.begin(), speeds.end(), [](float v) { return v > 0.05f; }));
        return moving / static_cast<double>(speeds.size());
    }
    // The frame whose vertical second difference was worst, for the diagnostic when grounding
    // regresses: "jitter somewhere in a minute" is not something anyone can act on.
    [[nodiscard]] std::size_t worstJerkFrame() const {
        std::size_t worst = 0;
        float value = 0.0f;
        for (std::size_t i = 2; i < positions.size(); ++i) {
            const float jerk =
                std::abs(positions[i].y - 2.0f * positions[i - 1].y + positions[i - 2].y);
            if (jerk > value) {
                value = jerk;
                worst = i;
            }
        }
        return worst;
    }
};

// Runs the project's entity layer for `seconds` of its own timeline and records where the named
// entity was on every frame. Offline and deterministic: the frame times are generated here rather
// than taken from a clock, which is what makes two runs comparable at all.
Walk walkFor(app::Engine& engine, const std::string& name, double seconds) {
    Walk out;
    const auto* composition = engine.composition();
    REQUIRE(composition != nullptr);
    const entity::Entity* who = composition->entityWorld().find(name);
    REQUIRE(who != nullptr);
    const auto frames = static_cast<std::uint64_t>(seconds / kStep);
    for (std::uint64_t i = 0; i <= frames; ++i) {
        FrameTime time;
        time.renderTime = static_cast<double>(i) * kStep;
        time.deltaTime = i == 0 ? 0.0 : kStep;
        time.frameIndex = i;
        engine.update(time);
        out.positions.push_back(who->locomotion().position);
        out.speeds.push_back(who->locomotion().speed);
        out.activities.push_back(static_cast<int>(who->locomotion().activity));
        const entity::EntityWorld::Counts counts = composition->entityWorld().counts();
        out.fullUpdates += counts.full;
        out.coarseUpdates += counts.coarse;
        out.skippedUpdates += counts.skipped;
    }
    out.seconds = seconds;
    return out;
}

bool glowmereAvailable(const fs::path& root) {
    return fs::exists(root / "examples/world/glowmere-stylized.json") &&
           fs::exists(root / "assets/quaternius/glTF/CommonTree_1.gltf") &&
           fs::exists(root / "assets/imported/alien.gltf");
}

} // namespace

TEST_CASE("Glowmere's walker crosses its world, stays on the ground and keeps out of solids",
          "[integration][glowmere][navigation]") {
    const fs::path root = AVGEN_SOURCE_DIR;
    if (!glowmereAvailable(root)) {
        SKIP("Glowmere's assets are not installed");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(root / "examples/world/glowmere-stylized.json"));
    REQUIRE(engine.composition() != nullptr);
    const scene::Composition& composition = *engine.composition();

    // The world it is walking in. All three of these were absent before ADR-093, and a walker with
    // none of them is the straight-line rejection sampler this replaced.
    const entity::Navigator& nav = composition.entityWorld().navigator();
    REQUIRE(nav.valid());
    REQUIRE(nav.obstacles() != nullptr);
    // Per-instance solids: an actual trunk at an actual spot, not "trees grow around here".
    INFO("obstacles: " << nav.obstacles()->size());
    CHECK(nav.obstacles()->size() > 500);
    CHECK(nav.obstacles()->built());
    REQUIRE(nav.grid() != nullptr);
    REQUIRE(nav.grid()->valid());
    // A graph that called almost nothing walkable would produce a walker that cannot plan, and it
    // would do so silently -- which is how the undergrowth bug survived as long as it did.
    const entity::NavGridStats& grid = nav.grid()->stats();
    INFO("grid " << grid.width << "x" << grid.height << " cells, " << grid.walkable << " walkable");
    CHECK(grid.walkable > grid.cells / 4);
    // §3's seam, joined up (ADR-090). The terrain query surface deliberately cannot answer
    // `isOccupied` from the canopy model -- that is statistical and would turn "is something
    // standing here" into "does something grow nearby" -- so it names a one-method interface and
    // expects navigation to fill it. This is the assertion that it was actually filled: an obstacle
    // set that exists but was never published through the seam is the seventh system this project
    // built and wired into nothing.
    const world::TerrainQuery terrain = composition.terrainQuery();
    REQUIRE(terrain.valid());
    REQUIRE(terrain.hasObstacles());
    {
        // Pick a real trunk and ask the shared surface about it. Both halves must agree, because
        // they are meant to be one answer reached two ways.
        const spatial::NavigationObstacle& solid = nav.obstacles()->obstacles()[0];
        INFO("obstacle 0 at " << solid.center.x << "," << solid.center.y << " r=" << solid.radius);
        CHECK(terrain.isOccupied(solid.center, 0.3f));
        CHECK(terrain.isOccupied(solid.center, 0.3f) ==
              nav.obstacles()->isOccupied(solid.center.x, solid.center.y, 0.3f));
        CHECK(terrain.at(solid.center).canopy >= 0.0f);
        // And `penetration` gives a distance rather than the interface's coarse default, because a
        // steering behaviour that only knows "blocked" has nothing to steer by.
        CHECK(terrain.obstacles->penetration(solid.center, 0.3f) > 0.0f);
        const glm::vec2 away = solid.center + glm::vec2(solid.radius + 40.0f, 0.0f);
        CHECK(terrain.obstacles->penetration(away, 0.3f) < 0.0f);
    }

    // Somewhere to go (§6). Landmarks, glowing patches, shoreline, high ground.
    INFO("interest points: " << composition.entityWorld().interestPoints().size());
    // Landmarks, luminous patches, shoreline and high ground. The floor is low on purpose -- a
    // different world has different amounts of each -- but it is not zero, because an empty registry
    // sends `explore` down its fallback path and looks exactly like the old random-annulus wander.
    CHECK(composition.entityWorld().interestPoints().size() >= 8);
    {
        // And all four kinds are actually represented. Vistas in particular were nearly absent
        // until the extraction stopped asking for a strict local maximum: Glowmere produced two.
        std::size_t kinds = 0;
        for (const entity::InterestKind kind :
             {entity::InterestKind::Landmark, entity::InterestKind::Glow,
              entity::InterestKind::Water, entity::InterestKind::Vista}) {
            const auto points = composition.entityWorld().interestPoints();
            if (std::any_of(points.begin(), points.end(),
                            [&](const entity::InterestPoint& p) { return p.kind == kind; })) {
                ++kinds;
            }
        }
        INFO("interest kinds represented: " << kinds << " of 4");
        CHECK(kinds >= 3);
    }

    const Walk walk = walkFor(engine, "wanderer", 60.0);

    // ---- does it travel -------------------------------------------------------------------------
    INFO("travelled " << walk.travelled() << " m, net " << walk.netDisplacement() << " m, spread "
                      << walk.spread() << " m, moving on " << walk.movingFraction() * 100.0 << "% of frames");
    CHECK(walk.travelled() > 90.0f);       // metres in a minute: a walk, not a shuffle
    CHECK(walk.movingFraction() > 0.35);
    // The one that separates travelling from pacing. The old behaviour was leashed to a
    // sixteen-metre home radius and covered ground without ever going anywhere.
    CHECK(walk.spread() > 45.0f);

    // ---- does it stay grounded ------------------------------------------------------------------
    //
    // Two questions, and they pull against each other. *Is the body on the surface* is answered by
    // the residual, which the ground follower clamps into a stated band. *Is it jittering* cannot
    // be answered by the residual at all -- snapping the body straight onto the ground gives a
    // residual of exactly zero and is the jitteriest thing available, because it inherits every
    // wrinkle of a noise function at whatever speed the character is moving.
    //
    // So jitter is measured as vertical acceleration, and it is measured *against the alternative*:
    // the same path, snapped. That makes the test self-calibrating. A bare threshold would have to
    // be re-tuned whenever the terrain or the walk speed changed; a comparison stays meaningful
    // because both sides move together.
    //
    // Sampled only on the frames the entity actually ran. Behaviour level of detail updates it
    // every 100 ms out at the far end of the valley, and differencing a held value against frame
    // numbers measures the update rate rather than the character.
    float worstFloat = 0.0f;
    float worstSink = 0.0f;
    std::vector<float> bodyY;
    std::vector<float> snapY;
    for (std::size_t i = 0; i < walk.positions.size(); ++i) {
        const glm::vec3 p = walk.positions[i];
        const float ground = nav.groundHeight(glm::vec2(p.x, p.z));
        // Penetration measured against the body's footprint rather than against the point under its
        // origin: a body resting on a footprint sits a little below the sample at its own centre
        // whenever the ground under the rest of it is lower, and that is not the mesh in the ground.
        float lowest = ground;
        for (int k = 0; k < 8; ++k) {
            const float angle = static_cast<float>(k) * 0.7853982f;
            lowest = std::min(lowest, nav.groundHeight(glm::vec2(p.x, p.z) +
                                                       glm::vec2(std::cos(angle), std::sin(angle)) * 0.55f));
        }
        worstFloat = std::max(worstFloat, p.y - ground);
        worstSink = std::max(worstSink, lowest - p.y);
        const bool moved = i == 0 || glm::length(glm::vec2(p.x - walk.positions[i - 1].x,
                                                           p.z - walk.positions[i - 1].z)) > 1e-4f;
        if (moved) {
            bodyY.push_back(p.y);
            snapY.push_back(ground);
        }
    }
    const auto verticalAcceleration = [](const std::vector<float>& series) {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 2; i < series.size(); ++i) {
            const double d = static_cast<double>(series[i]) - 2.0 * series[i - 1] + series[i - 2];
            sum += d * d;
            ++count;
        }
        return count == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(count));
    };
    const double smoothed = verticalAcceleration(bodyY);
    const double snapped = verticalAcceleration(snapY);
    INFO("worst float " << worstFloat << " m, worst sink " << worstSink
                        << " m; vertical acceleration " << smoothed << " vs " << snapped
                        << " snapped, over " << bodyY.size() << " updated frames");
    // Both measured against the raw sample under the body's origin. The follower's ceiling is
    // stated against the *filtered* surface, so on a slope the figure here is that ceiling plus the
    // few centimetres by which the footprint mean sits below its centre -- which is the body
    // resting on its own footprint rather than the body leaving the ground.
    CHECK(worstFloat < 0.35f);  // never hanging above the surface
    CHECK(worstSink < 0.005f);  // and never below everything its own footprint covers
    // Not a *reduction* here, and deliberately so. Out at the far end of the valley this character
    // is on the coarse behaviour tier and updates every 100 ms, which at four metres a second is a
    // 400 mm step -- far wider than the wrinkles a footprint filter removes, so almost all of the
    // vertical motion left in this series is the hill itself, and no grounding scheme should or
    // could flatten that. What *is* forbidden here is overshoot: a filter that rang, or a clamp the
    // body bounced between, would make this worse than snapping. The reduction the ground follower
    // actually claims is measured at full update rate in test_navigation_system.cpp, where the
    // wrinkles are resolved and it cuts vertical acceleration by more than half.
    REQUIRE(snapped > 0.0);
    CHECK(smoothed <= snapped * 1.05);

    // ---- does it avoid obstacles ----------------------------------------------------------------
    // The assertion the whole obstacle representation exists for. Before it, a walker's route was
    // checked against a statistical canopy that could not say where a trunk was, and it walked
    // through trees.
    std::size_t inside = 0;
    for (const glm::vec3& p : walk.positions) {
        spatial::ObstacleHit hit;
        if (nav.obstacles()->blocker(glm::vec2(p.x, p.z), nav.filter(p.y), hit)) {
            ++inside;
        }
    }
    INFO("frames inside a solid: " << inside << " of " << walk.positions.size());
    CHECK(inside == 0);

    // ---- and the rest of what §1 asks it to stay out of --------------------------------------
    // Water and the world's edge. Both were nominally handled before -- `sample` has always
    // rejected a submerged point and a point outside the boundary margin -- but nothing walked a
    // character for a minute and checked. Water in particular is newly real: ADR-090's terrain
    // generates rivers and lakes where there used to be none, so "avoid water" stopped being a
    // theoretical constraint some time after the rule for it was written.
    std::size_t wet = 0;
    std::size_t outside = 0;
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    for (const glm::vec3& p : walk.positions) {
        const glm::vec2 flat(p.x, p.z);
        const entity::NavSample s = nav.sample(flat);
        if (s.reject == entity::NavReject::Submerged) {
            ++wet;
        }
        if (flat.x < lo.x || flat.x > hi.x || flat.y < lo.y || flat.y > hi.y) {
            ++outside;
        }
    }
    INFO("frames in water: " << wet << ", frames outside the world: " << outside);
    CHECK(wet == 0);
    CHECK(outside == 0);

    // ---- does it do more than walk --------------------------------------------------------------
    // §6's loop, seen from outside: a character that only ever walked would report one activity.
    std::vector<int> seen = walk.activities;
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    INFO("distinct activities: " << seen.size());
    CHECK(seen.size() >= 3);

    // ---- what it cost (§44) ---------------------------------------------------------------------
    // The requirement is that a distant character does not think at full rate. The aggregate counts
    // cover every entity in the scene -- the craft sits inside its own full-detail radius the whole
    // time -- so the walker's own rate is measured by how often it actually moved, which is the
    // thing the requirement is about.
    std::size_t ran = 0;
    for (std::size_t i = 1; i < walk.positions.size(); ++i) {
        if (walk.positions[i] != walk.positions[i - 1]) {
            ++ran;
        }
    }
    INFO("entity updates: " << walk.fullUpdates << " full, " << walk.coarseUpdates << " coarse, "
                            << walk.skippedUpdates << " skipped; the walker itself ran on " << ran
                            << " of " << walk.positions.size() << " frames");
    CHECK(walk.skippedUpdates + walk.coarseUpdates > 1000);
    // Out at the far end of the valley it thinks at a tenth of the frame rate, not at all of it.
    CHECK(ran < walk.positions.size() / 2);
}

TEST_CASE("The same world and the same seed produce the same walk", "[integration][glowmere][navigation]") {
    const fs::path root = AVGEN_SOURCE_DIR;
    if (!glowmereAvailable(root)) {
        SKIP("Glowmere's assets are not installed");
    }
    // Determinism is the hard constraint the whole project rests on: an offline render of a music
    // video has to reproduce exactly, and a character with a mind of its own is the easiest place
    // for a wall clock or an unseeded generator to get in. Two independent loads, compared frame by
    // frame, is the only check that actually forbids one.
    app::Engine first(app::EngineMode::Offline);
    REQUIRE(first.loadProject(root / "examples/world/glowmere-stylized.json"));
    const Walk a = walkFor(first, "wanderer", 20.0);

    app::Engine second(app::EngineMode::Offline);
    REQUIRE(second.loadProject(root / "examples/world/glowmere-stylized.json"));
    const Walk b = walkFor(second, "wanderer", 20.0);

    REQUIRE(a.positions.size() == b.positions.size());
    for (std::size_t i = 0; i < a.positions.size(); ++i) {
        INFO("frame " << i);
        REQUIRE(a.positions[i].x == b.positions[i].x);
        REQUIRE(a.positions[i].y == b.positions[i].y);
        REQUIRE(a.positions[i].z == b.positions[i].z);
    }
}

TEST_CASE("seeking to a time puts the walker where that time would have put it",
          "[integration][glowmere][navigation]") {
    const fs::path root = AVGEN_SOURCE_DIR;
    if (!glowmereAvailable(root)) {
        SKIP("Glowmere's assets are not installed");
    }
    // The gap the sequencer reported: `EntityWorld` integrated whatever dt arrived, `reset()`
    // existed and nothing called it, and a seek left every character exactly where the playhead had
    // walked it to. Scrubbing back to the same second twice gave two different frames.
    //
    // What is asserted is the property that was missing, not a stronger one: *the same seek time
    // always produces the same state, however the playhead got there*. Matching a played-through
    // timeline exactly is a different and much harder claim -- a character's position depends on
    // its whole history of routes and avoidances, and the audio analysis is not replayed -- and it
    // is not needed for the thing seeking is for. An offline render plays from zero and is exact.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(root / "examples/world/glowmere-stylized.json"));
    const entity::Entity* who = engine.composition()->entityWorld().find("wanderer");
    REQUIRE(who != nullptr);

    engine.seekSeconds(30.0);
    const glm::vec3 first = who->locomotion().position;
    // A seek that did nothing would pass this by accident, so establish that it moved the character
    // somewhere at all before asking whether it is repeatable.
    CHECK(glm::length(first) > 0.0f);

    // Walk the playhead somewhere else entirely, then come back the long way round.
    for (std::uint64_t i = 1; i <= 120; ++i) {
        FrameTime time;
        time.renderTime = 30.0 + static_cast<double>(i) * kStep;
        time.deltaTime = kStep;
        time.frameIndex = i;
        engine.update(time);
    }
    engine.seekSeconds(4.0);
    engine.seekSeconds(30.0);
    const glm::vec3 again = who->locomotion().position;
    INFO("first " << first.x << "," << first.y << "," << first.z << " vs again " << again.x << ","
                  << again.y << "," << again.z);
    CHECK(first.x == again.x);
    CHECK(first.y == again.y);
    CHECK(first.z == again.z);
}

TEST_CASE("navigation costs what it claims to", "[integration][glowmere][navigation][performance]") {
    const fs::path root = AVGEN_SOURCE_DIR;
    if (!glowmereAvailable(root)) {
        SKIP("Glowmere's assets are not installed");
    }
    // Numbers, not "performance is good". Each of these is a cost the navigation stack added, and
    // each has a ceiling generous enough not to fail on a loaded machine and tight enough to catch
    // a regression of the kind that matters -- an accidental linear scan, a per-query allocation.
    //
    // The measurements come in two kinds, and they are asserted differently on purpose.
    //
    // *Wall clock* means nothing without optimisation. The same three measurements on this machine:
    // the grid builds in 161 ms release and 5,080 ms debug, `isOccupied` costs 25 ns release and
    // 544 ns debug, a route plans in 1.1 ms release and 13.8 ms debug -- 12x to 31x apart. A ceiling
    // that holds in both is so loose it catches nothing, so these are checked only in an optimised
    // build, and `kOptimised` says so rather than the test quietly passing for the wrong reason.
    // (The nanosecond ceiling was the one to watch: 544 against a limit of 900 was luck, not room.)
    //
    // *Counts* are identical in both builds -- 107 of 120 routes found, 9,492 cells expanded each,
    // to the unit -- because they are properties of the algorithm and not of the code generator. So
    // they are asserted unconditionally, and they are the better test: "the search stopped being
    // bounded" is what the ceiling below was really trying to catch, and a count catches it exactly
    // where a millisecond catches it through a proxy that a faster machine hides.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(root / "examples/world/glowmere-stylized.json"));
    const entity::Navigator& nav = engine.composition()->entityWorld().navigator();
    REQUIRE(nav.obstacles() != nullptr);
    REQUIRE(nav.grid() != nullptr);

    const entity::NavGridStats& grid = nav.grid()->stats();
    INFO("grid built in " << grid.buildMs << " ms for " << grid.cells << " cells");
    CHECK(grid.cells > 0);
    // One-time, at scene load, next to a 50 ms terrain build and a second of glTF decoding.
    if (kOptimised) {
        CHECK(grid.buildMs < 900.0);
    }

    SECTION("the obstacle index is an index, not a scan") {
        const auto begin = std::chrono::steady_clock::now();
        std::size_t hits = 0;
        for (int i = 0; i < 200000; ++i) {
            const float x = -280.0f + static_cast<float>(i % 560);
            const float z = -280.0f + static_cast<float>((i * 7) % 560);
            hits += nav.obstacles()->isOccupied(x, z, 0.45f) ? 1 : 0;
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin)
                              .count() / 200000.0;
        INFO(nav.obstacles()->size() << " obstacles, " << ns << " ns per isOccupied, " << hits << " hits");
        // A linear scan over 2,000-odd obstacles is microseconds, not hundreds of nanoseconds.
        if (kOptimised) {
            CHECK(ns < 900.0);
        }
    }

    SECTION("a route across the world is planned in about a millisecond") {
        std::vector<glm::vec2> path;
        const glm::vec2 lo = nav.worldMin();
        const glm::vec2 hi = nav.worldMax();
        const auto begin = std::chrono::steady_clock::now();
        std::size_t routed = 0;
        std::size_t expansions = 0;
        constexpr int kQueries = 120;
        for (int i = 0; i < kQueries; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kQueries);
            const glm::vec2 from = glm::mix(lo, hi, glm::vec2(t, 1.0f - t));
            const glm::vec2 to = glm::mix(lo, hi, glm::vec2(1.0f - t, t));
            if (nav.findPath(from, to, path)) {
                ++routed;
                expansions += nav.grid()->lastExpansions();
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                              .count() / kQueries;
        INFO(routed << " of " << kQueries << " corner-to-corner routes found, " << ms
                    << " ms each, " << (routed > 0 ? expansions / routed : 0) << " cells expanded");
        // Corner to corner is the worst case this world has, and a character asks for one every few
        // seconds. Anything in this range is free.
        if (kOptimised) {
            CHECK(ms < 45.0);
        }
        // The bounded-search checks, which is what the ceiling above was a proxy for. Both hold in
        // any build.
        //
        // A* with a consistent heuristic and a closed set expands each cell at most once, so a route
        // that expands more cells than the grid contains has started re-expanding them -- the
        // specific way this search would stop being bounded, and the one a wall-clock ceiling only
        // notices once it is slow enough to trip a limit calibrated on somebody else's machine.
        REQUIRE(routed > 0);
        CHECK(expansions / routed < nav.grid()->stats().cells);
        // And the grid stays passable. 107 of 120 corner-to-corner routes are findable here; the
        // rest start or end inside an obstacle, which is a legitimate refusal. A grid that silently
        // became impassable would still satisfy every ceiling above by doing no work at all.
        CHECK(routed >= 90);
    }
}

// ADR-193. `Composition::rebuild()` discards the obstacle field and builds a fresh one -- "a stale
// solid is a character walking round nothing, and a missing one is a character walking through a
// tree", as its own comment puts it. The entity layer never heard about it.
//
// `EntityWorld::setNavigator` takes a `Navigator` **by value**, and a `Navigator` holds
// `shared_ptr`s to the obstacle field and to the baked `NavGrid`. It is installed once, from
// `installEntities()`, which `rebuild()` does not call. So after a terrain edit or a hero moving,
// the walkers are pathing against the previous world while `TerrainQuery` -- rebuilt per call --
// sees the new one. The bridge was kept in step; the characters were not.
//
// The probe is a hero appearing where a walker could previously stand.
TEST_CASE("the navigator follows the world when it is rebuilt",
          "[integration][navigation][obstacles]") {
    const fs::path root = AVGEN_SOURCE_DIR;
    if (!glowmereAvailable(root)) {
        SKIP("Glowmere's assets are not installed");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(root / "examples/world/glowmere-stylized.json").has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);

    FrameTime time;
    time.renderTime = 0.0;
    engine.update(time);

    // Somewhere the world says a body can stand. Chosen from the navigator rather than assumed,
    // so the test does not depend on Glowmere's layout.
    const entity::Navigator& nav = composition->entityWorld().navigator();
    REQUIRE(nav.grid() != nullptr);
    const std::uint32_t blockedBefore = nav.grid()->stats().blocked;
    glm::vec2 spot{0.0f, 0.0f};
    bool found = false;
    for (int i = 0; i < 400 && !found; ++i) {
        const float a = static_cast<float>(i) * 0.61f;
        const float r = 20.0f + static_cast<float>(i) * 0.6f;
        const glm::vec2 candidate{std::cos(a) * r, std::sin(a) * r};
        if (nav.sample(candidate).navigable) {
            spot = candidate;
            found = true;
        }
    }
    REQUIRE(found);

    // A big authored solid, exactly there. Heroes are the obstacles a composition contributes for
    // "the large authored solids -- the elder, the monument, the arch".
    std::vector<world::HeroPoint> heroes = composition->heroes();
    world::HeroPoint blocker;
    blocker.name = "test-blocker";
    blocker.position = glm::vec3(spot.x, nav.groundHeight(spot) + 6.0f, spot.y);
    blocker.radius = 12.0f;
    blocker.height = 12.0f;
    blocker.importance = 0.5f;
    blocker.preferredCameraDistance = 30.0f;
    blocker.activationRadius = 120.0f;
    heroes.push_back(blocker);
    REQUIRE(composition->setHeroes(std::move(heroes)).has_value());

    time.renderTime = 1.0 / 60.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 1;
    engine.update(time);

    // `TerrainQuery` agreeing proves nothing here, and an earlier version of this test used it as
    // a control and was wrong to: it is rebuilt on every call and reads `heroes_` directly, so it
    // sees a hero whether or not anything rebuilt. The control has to be something only a *rebuild*
    // can produce.

    // And so must the navigator the walkers actually use. Before ADR-193 this failed: the entity
    // world was still holding the obstacle field from load.
    const entity::Navigator& after = composition->entityWorld().navigator();
    INFO("spot " << spot.x << ", " << spot.y);

    // The grid was re-baked, not merely the clearance field re-read. A twelve-metre solid takes
    // cells out of the walkable set, and the count is the one number that can only move if the
    // *grid* -- the thing `EntityWorld` was holding a stale shared_ptr to -- was built again.
    REQUIRE(after.grid() != nullptr);
    INFO("blocked cells " << blockedBefore << " -> " << after.grid()->stats().blocked);
    CHECK(after.grid()->stats().blocked > blockedBefore);

    // And the walker's own question answers correctly.
    CHECK_FALSE(after.sample(spot).navigable);
}

// ADR-194. The wade band is a scene key, a field on `NavSettings` and a term in the grid's cost --
// three things that are only worth anything if the one a scene can set reaches the other two. The
// unit tests pin the rule on a world built to have a ford in it; this pins the *wiring*, on the
// world that actually has a river through it.
TEST_CASE("a scene that declares a wade band gets a walkable ford out of it",
          "[integration][glowmere][navigation][water]") {
    const fs::path root = AVGEN_SOURCE_DIR;
    if (!glowmereAvailable(root)) {
        SKIP("Glowmere's assets are not installed");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(root / "examples/world/glowmere-stylized.json").has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);

    FrameTime time;
    time.renderTime = 0.0;
    engine.update(time);

    // The world as it was authored: nobody wades, and the river is a wall.
    REQUIRE(composition->navWadeDepth() == 0.0f);
    const entity::Navigator& dry = composition->entityWorld().navigator();
    REQUIRE(dry.grid() != nullptr);
    REQUIRE(dry.settings().wadeDepth == 0.0f);
    const std::size_t walkableBefore = dry.grid()->stats().walkable;
    const std::size_t waterCells = dry.grid()->stats().water;
    // Glowmere has water in it, or nothing below is being measured.
    REQUIRE(waterCells > 0);
    // And not one cell of it records a depth, because this walker has no band to record it against.
    std::size_t recordedBefore = 0;
    for (const entity::NavCell& c : dry.grid()->cells()) {
        recordedBefore += c.wade > 0 ? 1u : 0u;
    }
    CHECK(recordedBefore == 0);

    composition->setNavWadeDepth(0.9f);
    time.renderTime = 1.0 / 60.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 1;
    engine.update(time);

    const entity::Navigator& wading = composition->entityWorld().navigator();
    REQUIRE(wading.grid() != nullptr);
    // The key reached the walker.
    CHECK(wading.settings().wadeDepth == 0.9f);
    // The key reached the graph, and the graph was re-baked rather than re-read: a cell that is
    // both walkable and wet is a thing the old rule could not produce.
    std::size_t wadeable = 0;
    std::size_t recorded = 0;
    for (std::size_t i = 0; i < wading.grid()->cells().size(); ++i) {
        const entity::NavCell& c = wading.grid()->cells()[i];
        recorded += c.wade > 0 ? 1u : 0u;
        if ((c.flags & entity::NavWalkable) != 0 && (c.flags & entity::NavWater) != 0) {
            ++wadeable;
        }
    }
    INFO("walkable " << walkableBefore << " -> " << wading.grid()->stats().walkable << ", "
                     << waterCells << " water cells, " << wadeable << " of them now walkable, "
                     << recorded << " carrying a depth");
    CHECK(wading.grid()->stats().walkable > walkableBefore);
    CHECK(wadeable > 0);
    CHECK(recorded > 0);
}

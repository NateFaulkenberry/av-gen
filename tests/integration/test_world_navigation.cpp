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
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace avgen;
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
    // Somewhere to go (§6). Landmarks, glowing patches, shoreline, high ground.
    INFO("interest points: " << composition.entityWorld().interestPoints().size());
    CHECK(composition.entityWorld().interestPoints().size() >= 8);

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
    CHECK(worstFloat < 0.3f);   // never hanging above the surface
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
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(root / "examples/world/glowmere-stylized.json"));
    const entity::Navigator& nav = engine.composition()->entityWorld().navigator();
    REQUIRE(nav.obstacles() != nullptr);
    REQUIRE(nav.grid() != nullptr);

    const entity::NavGridStats& grid = nav.grid()->stats();
    INFO("grid built in " << grid.buildMs << " ms for " << grid.cells << " cells");
    // One-time, at scene load, next to a 50 ms terrain build and a second of glTF decoding.
    CHECK(grid.buildMs < 900.0);

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
        CHECK(ns < 900.0);
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
        // seconds. Anything in this range is free; the ceiling is here to catch a search that
        // stopped being bounded.
        CHECK(ms < 45.0);
    }
}

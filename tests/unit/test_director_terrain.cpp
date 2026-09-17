// Does the Auto-director's terrain clearance actually reach the real project?
//
// ADR-080 built `world::clearPath` and `camera_director.cpp` calls it, so on paper a directed camera
// cannot end up inside a hillside. The report was that it does anyway. This file asks the question
// of the project the report came from, and asks it with a *different instrument* than the one that
// did the clearing: `ClearanceField::minimumHeight` decides where the floor is, so measuring the
// result with `ClearanceField` would only prove that it agrees with itself. `TerrainQuery::surfaceAt`
// is the function the rest of the engine uses to ask how high the ground is, and it is what a
// rendered frame effectively answers to.
//
// The control is the point of the file (ADR-182): the same bake is measured before and after the
// clearance pass. If the uncleared path shows no violations either, this probe has proved nothing
// about clearance and the real cause is elsewhere.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "world/terrain_query.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

struct Violation {
    glm::vec3 position{0.0f};
    float surface = 0.0f;
    [[nodiscard]] float depth() const { return surface - position.y; }
};

// Every camera/position key in a baked track array, in bake order.
std::vector<glm::vec3> cameraPathOf(const nlohmann::json& baked) {
    std::vector<glm::vec3> path;
    if (!baked.is_array()) {
        return path;
    }
    for (const auto& track : baked) {
        if (track.value("target", std::string()) != "camera/position") {
            continue;
        }
        for (const auto& key : track.at("keys")) {
            const auto& v = key.at("value");
            path.emplace_back(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
        }
    }
    return path;
}

// How many of `path` sit at or below the surface, and how deep the worst one is. `margin` is the
// headroom a camera needs to not be visibly in the ground: a lens exactly on a surface still shows
// it filling the frame, which is the same reason `ClearanceField::cameraRadius` exists.
std::vector<Violation> below(const world::TerrainQuery& ground, const std::vector<glm::vec3>& path,
                             float margin) {
    std::vector<Violation> out;
    for (const glm::vec3& p : path) {
        const float surface = ground.surfaceAt(glm::vec2(p.x, p.z)) + margin;
        if (p.y < surface) {
            out.push_back({.position = p, .surface = surface});
        }
    }
    return out;
}

float worstDepth(const std::vector<Violation>& v) {
    float worst = 0.0f;
    for (const Violation& x : v) {
        worst = std::max(worst, x.depth());
    }
    return worst;
}

std::filesystem::path projectPath(const char* name) {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / name;
}

} // namespace

// The measurement. Reports both arms rather than asserting one, because what this needs to
// establish first is whether there is anything to fix at all.
TEST_CASE("how far into the ground a directed camera on the real project actually goes",
          "[.probe][director][terrain]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    for (const char* name : {"glowmere-valley-2.json", "glowmere-valley-2-multicam.json"}) {
        const std::filesystem::path project = projectPath(name);
        if (!std::filesystem::exists(project)) {
            continue;
        }
        app::Engine engine(app::EngineMode::Offline);
        auto loaded = engine.loadProject(project);
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        if (engine.track() == nullptr) {
            continue; // the project's audio is not available here
        }
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        const world::TerrainQuery ground = comp->terrainQuery();
        if (!ground.valid()) {
            WARN(std::string(name) + ": no terrain query -- clearance has nothing to measure against");
            continue;
        }

        auto structure = app::structureOfTrack(*engine.track());
        REQUIRE(structure.has_value());

        for (const auto mode : {app::DirectorMode::ContinuousShot, app::DirectorMode::EditedSequence}) {
            app::AutoDirectorSettings take;
            take.mode = mode;
            const auto seq = app::directHeroes(comp->heroes(), *structure, take);
            if (!seq) {
                continue;
            }

            // The control: the bake as the shot geometry produced it, before clearance ran.
            const std::vector<glm::vec3> raw = cameraPathOf(seq->toTimelineTracks());

            // The arm: what actually lands on the timeline, which is the bake after `clearPath`.
            REQUIRE(app::installSequence(engine, *seq, take).has_value());
            std::vector<glm::vec3> installed;
            for (const params::Track& t : engine.timeline().tracks()) {
                if (t.target != "camera/position") {
                    continue;
                }
                for (const auto& k : t.keys) {
                    installed.emplace_back(k.value[0], k.value[1], k.value[2]);
                }
            }

            // The gap the key-wise measurement cannot see. Keys are cleared individually and the
            // track between them is a straight line, so a path clear at every key can still cut
            // through anything convex in between -- and this cut peaks in the tens of m/s, which is
            // metres of unmeasured ground per key interval. Sampled off the installed track through
            // the timeline's own evaluator, so this is the curve the camera actually follows.
            std::size_t sampled = 0;
            std::size_t sunk = 0;
            float worstBetween = 0.0f;
            double worstAt = 0.0;
            for (const params::Track& t : engine.timeline().tracks()) {
                if (t.target != "camera/position" || t.keys.size() < 2) {
                    continue;
                }
                const double t0 = t.firstKeyTime();
                const double t1 = t.lastKeyTime();
                const int steps = 4000;
                for (int i = 0; i <= steps; ++i) {
                    const double time = std::lerp(t0, t1, static_cast<double>(i) / steps);
                    const params::KeyValue v = t.evaluate(time);
                    const float floorY = ground.surfaceAt(glm::vec2(v[0], v[2])) + 1.2f;
                    ++sampled;
                    if (v[1] < floorY) {
                        ++sunk;
                        if (floorY - v[1] > worstBetween) {
                            worstBetween = floorY - v[1];
                            worstAt = time;
                        }
                    }
                }
            }
            WARN(std::string(name) << " mode=" << static_cast<int>(mode)
                 << " | BETWEEN KEYS: " << sunk << "/" << sampled
                 << " samples below surface+1.2, worst " << worstBetween
                 << " m at t=" << worstAt << "s");

            for (const float margin : {0.0f, 1.2f}) {
                const auto rawBad = below(ground, raw, margin);
                const auto instBad = below(ground, installed, margin);
                INFO(std::string(name) << " mode=" << static_cast<int>(mode) << " margin=" << margin
                     << "\n  uncleared: " << rawBad.size() << "/" << raw.size()
                     << " keys below surface, worst " << worstDepth(rawBad) << " m"
                     << "\n  installed: " << instBad.size() << "/" << installed.size()
                     << " keys below surface, worst " << worstDepth(instBad) << " m");
                CHECK(true); // reporting probe; the INFO is the result
                WARN(std::string(name) << " mode=" << static_cast<int>(mode)
                     << " margin=" << margin
                     << " | uncleared " << rawBad.size() << "/" << raw.size()
                     << " worst " << worstDepth(rawBad)
                     << " m | installed " << instBad.size() << "/" << installed.size()
                     << " worst " << worstDepth(instBad) << " m");
            }
        }
    }
#endif
}

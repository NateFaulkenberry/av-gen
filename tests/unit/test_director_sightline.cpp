// Can the directed camera actually see the hero it is framing?
//
// ADR-080 keeps the camera *out of* the scenery: `world::clearPath` pushes the eye sideways out of a
// hero it is inside and up out of the ground and the canopy. Nothing ever asked the other question
// -- whether something stands *between* the eye and the hero it is pointed at. A camera perfectly
// clear of the ground, at the authored distance and elevation, framing a hero on the far side of a
// ridge passed every check this repository had.
//
// This file is the reproduction and the verification, and it is written to the same rule the terrain
// probe next door was: **a different instrument than the one doing the fixing**.
// `world::heroSightline` decides, so measuring with `world::heroSightline` would only prove it agrees
// with itself. Here the march is written out by hand against `TerrainQuery::surfaceAt` -- the
// function the rest of the engine asks how high the ground is, and the one a rendered frame answers
// to -- and against the raw `HeroPoint` capsules, unpadded.
//
// **The control (ADR-182) is `maxSightlineCorrection = 0`**: the same bake, through the same
// installation, with the pass measuring and moving nothing. If the control arm shows no obstructed
// keys either, this probe has proved nothing and the cause is elsewhere.
//
// **What the first version of this file got wrong, because it is the finding.** It filtered shots by
// `lookMode() == Subject`, copying the rule `installSequence` uses to decide which shots follow their
// hero (ADR-158). That rule is right for aim-follow and wrong here: on the shipped Glowmere cut
// **20 of 23 shots are handoffs**, and a handoff *does* hold a subject -- the first one before the
// swing and the second after it. The filter examined 3 shots and declared the film almost clean.
// `Shot::heldSubjectAt` is the question asked properly.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "world/hero.hpp"
#include "world/terrain_query.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::filesystem::path projectPath(const char* name) {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / name;
}

// What the hand-written instrument reports about one eye and one subject.
struct Reading {
    bool blockedByGround = false;
    float worstDepth = 0.0f;   // metres the ridge stands above the sightline at its worst
    std::string blockedBy;     // the hero in the way, if one is
    // How much room the tightest sightline had, in metres. The number that matters more than the
    // count: "no key is blocked" and "every key clears the ground by fifteen metres" are different
    // findings, and only the second one says the film is not on a knife edge. A count of zero that
    // came from a margin of 0.05 m is a count that the next terrain seed will not reproduce.
    float tightest = 1.0e9f;
    [[nodiscard]] bool blocked() const { return blockedByGround || !blockedBy.empty(); }
};

// Nine rays across the subject's silhouette, marched at one metre. Deliberately the same *sampling*
// as the thing under test -- a measurement at a different resolution measures a different question --
// and deliberately not the same *code*: this asks `TerrainQuery::surfaceAt` and the raw capsules,
// where `heroSightline` asks `WorldMap::sample` and the `ClearanceField`.
Reading read(const world::TerrainQuery& ground, std::span<const world::HeroPoint> heroes,
             glm::vec3 eye, glm::vec3 base, float radius, float height, const std::string& subject) {
    Reading out;
    const glm::vec3 centre = base + glm::vec3(0.0f, height * 0.5f, 0.0f);
    const glm::vec2 flat(centre.x - eye.x, centre.z - eye.z);
    const float span = glm::length(flat);
    if (span < 1e-3f) {
        return out;
    }
    const glm::vec2 right = glm::vec2(-flat.y, flat.x) / span;
    for (const float h : {0.2f, 0.5f, 0.8f}) {
        for (const float lateral : {-0.70710678f, 0.0f, 0.70710678f}) {
            const glm::vec3 aim = base + glm::vec3(0.0f, height * h, 0.0f)
                                + glm::vec3(right.x, 0.0f, right.y) * (lateral * radius);
            const glm::vec3 ray = aim - eye;
            const float length = glm::length(ray);
            const float reach = length - radius;   // the subject's own body is not an obstruction
            if (reach <= 1.0f) {
                continue;
            }
            const int steps = std::max(static_cast<int>(reach), 1);
            for (int i = 1; i <= steps; ++i) {
                const float s = (static_cast<float>(i) / static_cast<float>(steps)) * (reach / length);
                const glm::vec3 p = eye + ray * s;
                const float surface = ground.surfaceAt(glm::vec2(p.x, p.z));
                out.tightest = std::min(out.tightest, p.y - surface);
                if (p.y < surface) {
                    out.blockedByGround = true;
                    out.worstDepth = std::max(out.worstDepth, surface - p.y);
                }
                for (const world::HeroPoint& hero : heroes) {
                    if (hero.name == subject) {
                        continue;
                    }
                    const float above = p.y - hero.position.y;
                    if (above < 0.0f || above > hero.height) {
                        continue;
                    }
                    if (glm::length(glm::vec2(p.x - hero.position.x, p.z - hero.position.z))
                        < hero.radius) {
                        out.blockedBy = hero.name;
                    }
                }
            }
        }
    }
    return out;
}

struct Tally {
    std::size_t holding = 0;    // installed keys whose shot was holding a hero
    std::size_t blocked = 0;    // ...of which the hero could not be seen
    std::size_t byGround = 0;
    std::size_t byHero = 0;
    float worstDepth = 0.0f;
    std::string worst;
    float tightest = 1.0e9f;   // the least headroom any sightline in the film had, in metres
};

// Reads the *installed* timeline, which is the path the camera actually follows, and pairs each key
// with the subject the sequence says that key is holding. Not the shot geometry: what the film does
// is what came out the far side of `installSequence`.
Tally tallyInstalled(app::Engine& engine, const app::Sequence& seq,
                     const world::TerrainQuery& ground,
                     std::span<const world::HeroPoint> heroes) {
    Tally t;
    const std::vector<const app::FocalTarget*> held = seq.heldSubjectPerKey();
    std::vector<glm::vec3> path;
    for (const params::Track& track : engine.timeline().tracks()) {
        if (track.target != "camera/position") {
            continue;
        }
        for (const auto& k : track.keys) {
            path.emplace_back(k.value[0], k.value[1], k.value[2]);
        }
    }
    for (std::size_t i = 0; i < path.size() && i < held.size(); ++i) {
        if (held[i] == nullptr) {
            continue;
        }
        ++t.holding;
        glm::vec3 base = held[i]->position;
        float radius = held[i]->radius;
        float height = radius * 2.0f;
        for (const world::HeroPoint& hero : heroes) {
            if (hero.name == held[i]->name) {
                base = hero.position;
                radius = hero.radius;
                height = hero.height;
                break;
            }
        }
        const Reading r = read(ground, heroes, path[i], base, radius, height, held[i]->name);
        t.tightest = std::min(t.tightest, r.tightest);
        if (!r.blocked()) {
            continue;
        }
        ++t.blocked;
        t.byGround += r.blockedByGround ? 1 : 0;
        t.byHero += r.blockedBy.empty() ? 0 : 1;
        if (r.worstDepth > t.worstDepth) {
            t.worstDepth = r.worstDepth;
            t.worst = held[i]->name;
        }
        if (t.worst.empty() && !r.blockedBy.empty()) {
            t.worst = held[i]->name + " behind " + r.blockedBy;
        }
    }
    return t;
}

struct Arms {
    Tally raw;       // the bake as the shot geometry produced it, before any clearance at all
    Tally control;   // maxSightlineCorrection = 0: the pass measures and moves nothing
    Tally armed;     // the shipped default
    bool valid = false;
};

// The third arm, and the one that says whether ADR-080's canopy lift was already doing this job by
// accident. `clearPath` raises every key to `ground + canopy * 1.12 + 1.2`, which on a wooded map is
// ten-odd metres of altitude the shot never asked for -- and an eye ten metres up looking down at a
// hero forty metres away clears most ground between them by construction. Measuring only the
// installed path cannot tell "there was nothing to fix" from "something else fixed it".
Tally tallyBaked(const nlohmann::json& baked, const app::Sequence& seq,
                 const world::TerrainQuery& ground, std::span<const world::HeroPoint> heroes) {
    Tally t;
    const std::vector<const app::FocalTarget*> held = seq.heldSubjectPerKey();
    std::vector<glm::vec3> path;
    for (const auto& track : baked) {
        if (track.value("target", std::string()) != "camera/position") {
            continue;
        }
        for (const auto& k : track.at("keys")) {
            const auto& v = k.at("value");
            path.emplace_back(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
        }
    }
    for (std::size_t i = 0; i < path.size() && i < held.size(); ++i) {
        if (held[i] == nullptr) {
            continue;
        }
        ++t.holding;
        glm::vec3 base = held[i]->position;
        float radius = held[i]->radius;
        float height = radius * 2.0f;
        for (const world::HeroPoint& hero : heroes) {
            if (hero.name == held[i]->name) {
                base = hero.position;
                radius = hero.radius;
                height = hero.height;
                break;
            }
        }
        const Reading r = read(ground, heroes, path[i], base, radius, height, held[i]->name);
        t.tightest = std::min(t.tightest, r.tightest);
        if (!r.blocked()) {
            continue;
        }
        ++t.blocked;
        t.byGround += r.blockedByGround ? 1 : 0;
        t.byHero += r.blockedBy.empty() ? 0 : 1;
        if (r.worstDepth > t.worstDepth) {
            t.worstDepth = r.worstDepth;
            t.worst = held[i]->name;
        }
    }
    return t;
}

Arms runBoth(const char* name, app::DirectorMode mode) {
    Arms arms;
    const std::filesystem::path project = projectPath(name);
    if (!std::filesystem::exists(project)) {
        return arms;
    }
    for (int pass = 0; pass < 2; ++pass) {
        app::Engine engine(app::EngineMode::Offline);
        if (!engine.loadProject(project) || engine.track() == nullptr) {
            return arms;
        }
        scene::Composition* comp = engine.composition();
        if (comp == nullptr || !comp->terrainQuery().valid()) {
            return arms;
        }
        auto structure = app::structureOfTrack(*engine.track());
        if (!structure) {
            return arms;
        }
        app::AutoDirectorSettings take;
        take.mode = mode;
        take.maxSightlineCorrection = pass == 0 ? 0.0f : 0.25f;
        const auto seq = app::directHeroes(comp->heroes(), *structure, take);
        if (!seq) {
            return arms;
        }
        if (pass == 0) {
            arms.raw = tallyBaked(seq->toTimelineTracks(), *seq, comp->terrainQuery(), comp->heroes());
        }
        if (!app::installSequence(engine, *seq, take)) {
            return arms;
        }
        Tally t = tallyInstalled(engine, *seq, comp->terrainQuery(), comp->heroes());
        (pass == 0 ? arms.control : arms.armed) = t;
    }
    arms.valid = true;
    return arms;
}

const char* kProjects[] = {"glowmere-valley-2.json", "glowmere-valley-2-multicam.json",
                           "glowmere-valley-2-song.json", "glowmere-atmospherics.json",
                           "grove.json", "world.json"};

} // namespace

TEST_CASE("how many directed keys frame a hero something is standing in front of",
          "[.probe][director][sightline]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    for (const char* name : kProjects) {
        for (const auto mode : {app::DirectorMode::ContinuousShot, app::DirectorMode::EditedSequence}) {
            const Arms arms = runBoth(name, mode);
            if (!arms.valid) {
                continue;
            }
            WARN(std::string(name) << " mode=" << app::directorModeName(mode)
                 << "\n  raw     (no clearance):  " << arms.raw.blocked << "/" << arms.raw.holding
                 << " keys cannot see their hero (" << arms.raw.byGround << " ground, "
                 << arms.raw.byHero << " hero), worst ridge " << arms.raw.worstDepth
                 << " m, tightest clearance " << arms.raw.tightest << " m [" << arms.raw.worst << "]"
                 << "\n  control (no correction): " << arms.control.blocked << "/"
                 << arms.control.holding << " keys cannot see their hero ("
                 << arms.control.byGround << " ground, " << arms.control.byHero
                 << " hero), worst ridge " << arms.control.worstDepth << " m, tightest clearance "
                 << arms.control.tightest << " m [" << arms.control.worst << "]"
                 << "\n  armed   (0.25):          " << arms.armed.blocked << "/"
                 << arms.armed.holding << " keys cannot see their hero ("
                 << arms.armed.byGround << " ground, " << arms.armed.byHero << " hero), worst ridge "
                 << arms.armed.worstDepth << " m, tightest clearance " << arms.armed.tightest
                 << " m [" << arms.armed.worst << "]");
            CHECK(true);   // reporting probe; the WARN is the result
        }
    }
#endif
}

// ---- the regression --------------------------------------------------------------------------
//
// The probe above reports; this asserts. Both halves are needed and they are not the same test: a
// number that moved is evidence, and an invariant that holds is a guarantee.

TEST_CASE("the sightline pass leaves fewer blind keys than it found on the real project",
          "[director][sightline][glowmere]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    bool ranAny = false;
    for (const char* name : kProjects) {
        for (const auto mode : {app::DirectorMode::ContinuousShot, app::DirectorMode::EditedSequence}) {
            const Arms arms = runBoth(name, mode);
            if (!arms.valid) {
                continue;
            }
            ranAny = true;
            INFO(std::string(name) << " mode=" << app::directorModeName(mode)
                 << ": control " << arms.control.blocked << "/" << arms.control.holding
                 << ", armed " << arms.armed.blocked << "/" << arms.armed.holding);
            // The same keys are examined either way -- the correction moves cameras, never the cut.
            CHECK(arms.armed.holding == arms.control.holding);
            // And the pass never makes it worse. "No worse" rather than "zero" on purpose: a
            // correction costing more than the shot is refused by design (see `clearSightlines`), so
            // zero is not the promise, and an assertion demanding it would be an assertion demanding
            // the bound be removed.
            CHECK(arms.armed.blocked <= arms.control.blocked);
        }
    }
    if (!ranAny) {
        SKIP("the Glowmere projects or their audio are not available here");
    }
#endif
}

// **The reproduction, as a regression.**
//
// Found by the probe above and not by anybody looking: on `glowmere-atmospherics` in continuous
// mode, four of the 185 baked keys that hold a hero were framing `spire-cap` from behind
// `elder-2-cap` -- one sixteen-metre hero standing in front of another. It was there before
// `clearPath` ran and still there after, because nothing in ADR-080 has ever asked what is *between*
// the camera and its subject; the camera was correctly outside both heroes the whole time.
//
// The two arms are the same bake through the same installation, and the *only* difference is the
// correction budget. Measured, as everywhere in this file, by a hand-written march against
// `TerrainQuery::surfaceAt` and the raw capsules rather than by the query that does the fixing.
TEST_CASE("the hero standing in front of another hero on glowmere-atmospherics",
          "[director][sightline][glowmere]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const Arms arms = runBoth("glowmere-atmospherics.json", app::DirectorMode::ContinuousShot);
    if (!arms.valid) {
        SKIP("glowmere-atmospherics or its audio is not available here");
    }
    INFO("control " << arms.control.blocked << "/" << arms.control.holding << " blind ["
         << arms.control.worst << "], armed " << arms.armed.blocked << "/" << arms.armed.holding);
    // The bug, still reproducible on demand. If this ever stops firing the arm below has stopped
    // proving anything, and this file should be told so rather than quietly passing.
    CHECK(arms.control.blocked > 0);
    CHECK(arms.control.byHero > 0);
    // And the fix.
    CHECK(arms.armed.blocked == 0);
#endif
}

// Two questions about the camera and the saucer, both answered by measurement (ADR-217).
//
// 1. **Is the camera actually pointed at the UFO during a shot cut for it, or does it rotate away?**
//    Asked because nobody had checked. ADR-158's aim-follow adds the hero's movement since the cut
//    to the baked aim, and Glowmere's saucer is a hero, a node, an entity and a staging actor all at
//    once -- so the chain *should* resolve. "Should" is not a measurement, and a saucer crossing two
//    hundred metres in seven seconds is the hardest case that mechanism has ever been given.
//
//    The measurement is an angle, and it is taken against a counterfactual so it cannot be vacuous
//    (ADR-182): the same frames, with the follow table cleared, are what "rotating away" looks like.
//    A test that only reported the followed angle would pass just as happily if the follow did
//    nothing and the saucer happened to stay still.
//
// 2. **Does the new hold keep the camera on the saucer while an abduction is running?** Two cuts of
//    the same film differing in exactly one setting, and the difference measured off the camera
//    rather than off anything the director reports about itself.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "world/effects.hpp"
#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "params/timeline.hpp"
#include "scene/composition.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::filesystem::path glowmereProject() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.json";
}

// Degrees between two directions from one eye.
float angleBetween(const glm::vec3& eye, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 da = a - eye;
    const glm::vec3 db = b - eye;
    const float la = glm::length(da);
    const float lb = glm::length(db);
    if (la < 1e-4f || lb < 1e-4f) {
        return 0.0f;
    }
    const float c = std::clamp(glm::dot(da, db) / (la * lb), -1.0f, 1.0f);
    return std::acos(c) * 57.29577951308232f;
}

const world::HeroPoint* heroNamed(const scene::Composition& comp, std::string_view name) {
    for (const world::HeroPoint& h : comp.heroes()) {
        if (h.name == name) {
            return &h;
        }
    }
    return nullptr;
}

// A cut of the shipped project whose first shot on `visitor` is found, or the seed search giving up.
// Deterministic: the seeds are tried in order and the first that casts the saucer wins.
struct Cut {
    bool found = false;
    std::uint32_t seed = 1;
    double start = 0.0;
    double end = 0.0;
};

Cut firstVisitorShot(app::Engine& engine, scene::Composition& comp,
                     const app::AutoDirectorSettings& base) {
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        app::AutoDirectorSettings s = base;
        s.seed = seed;
        if (!app::directEngine(engine, comp.heroes(), s).has_value()) {
            continue;
        }
        for (const scene::AimFollow& f : comp.aimFollow()) {
            // Early enough that the abduction scenario -- six cycles of roughly twenty seconds -- is
            // still running when the shot ends, which is the case the hold exists for.
            if (f.hero == "visitor" && f.endSeconds < 110.0) {
                return Cut{.found = true, .seed = seed, .start = f.startSeconds, .end = f.endSeconds};
            }
        }
    }
    return Cut{};
}

} // namespace

TEST_CASE("The directed camera really is pointed at the saucer while it flies", "[director][ufo][aim]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("Glowmere Valley 2 is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(glowmereProject());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    if (engine.track() == nullptr) {
        SKIP("the project's audio is not available here");
    }
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    REQUIRE(heroNamed(*comp, "visitor") != nullptr);
    // The scenario has to be running, or "the saucer flies" is not a thing this test can observe.
    REQUIRE(!comp->staging().scenarios.empty());

    app::AutoDirectorSettings take;
    const Cut cut = firstVisitorShot(engine, *comp, take);
    INFO("seed " << cut.seed << ", visitor shot " << cut.start << ".." << cut.end << " s");
    REQUIRE(cut.found);

    // Play to the end of that shot, recording the aim error against the saucer every frame the
    // playhead is inside it -- and, on the same frames, what the aim would have been with no follow.
    FixedStepClock clock(60.0);
    const int steps = static_cast<int>(std::min(cut.end + 1.0, engine.durationSeconds()) * 60.0);
    engine.seekSeconds(0.0);
    const auto windowIt =
        std::find_if(comp->aimFollow().begin(), comp->aimFollow().end(),
                     [&](const scene::AimFollow& f) {
                         return f.hero == "visitor" && f.startSeconds == cut.start;
                     });
    REQUIRE(windowIt != comp->aimFollow().end());
    const scene::AimFollow window = *windowIt;

    // What is measured is the *miss vector* -- how far the aim lands from the saucer -- against how
    // far the follow had to compensate. "Rotating away" would be a miss that grows with the
    // saucer's travel; tracking is a miss that stays the framing offset the shot was baked with,
    // however far the craft has flown since the cut.
    //
    // Sampled a quarter of a second inside each end of the window. `applyDirectedAim` selects its
    // window on the engine's own render time and this loop counts frames, and the two disagree by a
    // frame or two at a boundary -- which is a frame with no follow at all on it, and on a hero that
    // has flown a hundred metres that one frame would be the maximum of everything below.
    //
    // Degrees are reported too, because they are what a viewer sees, and they are *not* the thing to
    // assert on: the framing offset is preserved in world units, so the same correct aim is a small
    // angle when the saucer is far away and a large one when it passes close to the eye.
    std::vector<float> misses;
    std::vector<float> bareMisses;
    std::vector<float> angles;
    float worstFollowOffset = 0.0f; // how far the saucer had moved since the cut
    // Does the hero table actually track the craft? This is the link the rest of the measurement
    // depends on: the aim is measured against the *hero*, so "the aim is on the hero" only means
    // "the aim is on the UFO" if the hero is on the UFO.
    //
    // Against `Entity::visualPosition()`, which is unambiguously where the saucer is drawn. Two
    // earlier spellings of this probe measured something else and are worth naming: `nodeBounds`
    // on a `procedural` node returns the instance extent memoised at *rebuild* time, and the node's
    // own `position` parameter is the body's transform rather than the place the hero was authored
    // at. What is asserted is that the gap does not *grow* -- the hero is allowed to sit somewhere
    // other than the origin of the thing it names, and Glowmere's does, on purpose -- and, below,
    // that the probe can *see* a gap when there is one. Two wrong spellings is enough reason not to
    // trust a third on its word.
    const entity::Entity* craft = comp->entityWorld().find("visitor");
    REQUIRE(craft != nullptr);
    float heroToCraftFirst = -1.0f;
    float worstHeroToNode = 0.0f;
    // A frame where the followed and unfollowed aims are the same point is a frame the follow did
    // not run on -- `applyDirectedAim` found no active window, or a different one. Counting them is
    // what separates "the follow is partial" from "this loop's frame numbering disagrees with the
    // engine's about two frames at the edges".
    int unfollowedFrames = 0;
    for (int i = 0; i < steps; ++i) {
        engine.update(engine.tick(clock));
        const double t = static_cast<double>(i) / 60.0;
        if (t < cut.start + 0.25 || t >= cut.end - 0.25) {
            continue;
        }
        const world::HeroPoint* saucer = heroNamed(*comp, "visitor");
        REQUIRE(saucer != nullptr);
        const float gap = glm::length(craft->visualPosition() - saucer->position);
        if (heroToCraftFirst < 0.0f) {
            heroToCraftFirst = gap;
        }
        worstHeroToNode = std::max(worstHeroToNode, std::fabs(gap - heroToCraftFirst));
        const scene::Camera& cam = comp->scene().camera;
        const glm::vec3 followed = saucer->position - window.heroAtCut;
        worstFollowOffset = std::max(worstFollowOffset, glm::length(followed));
        misses.push_back(glm::length(cam.target - saucer->position));
        // The counterfactual: `applyDirectedAim` adds exactly `followed`, so subtracting it is the
        // baked aim, which is what the camera would be pointed at if the follow did nothing.
        const float bare = glm::length((cam.target - followed) - saucer->position);
        bareMisses.push_back(bare);
        if (std::fabs(bare - misses.back()) < 0.01f) {
            ++unfollowedFrames;
        }
        angles.push_back(angleBetween(cam.position, cam.target, saucer->position));
    }
    REQUIRE(misses.size() > 30);
    const auto quantile = [](std::vector<float> v, double q) {
        std::sort(v.begin(), v.end());
        return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
    };
    // ADR-185: a shot carries its aim across the cut, swinging from where the previous shot was
    // looking into its own framing over `swingWindow` (half the shot, by default). During that swing
    // the aim is *supposed* to be somewhere other than the subject, so the head of the window is
    // reported separately rather than allowed to set the maximum for the whole shot.
    const std::vector<float> settled(misses.begin() + static_cast<long>(misses.size() * 3 / 5),
                                     misses.end());
    const std::vector<float> settledAngles(angles.begin() + static_cast<long>(angles.size() * 3 / 5),
                                           angles.end());
    const float medMiss = quantile(misses, 0.5);
    const float maxMiss = quantile(misses, 1.0);
    const float settledMax = quantile(settled, 1.0);
    const float settledMaxAngle = quantile(settledAngles, 1.0);
    const float medBare = quantile(bareMisses, 0.5);
    const float medAngle = quantile(angles, 0.5);
    const float maxAngle = quantile(angles, 1.0);
    // Printed rather than only INFO'd: the question this test exists to answer is "what is the
    // number", and an INFO is invisible unless something fails.
    fmt::print("\n[aim] shot {:.1f}..{:.1f} s on 'visitor', {} frames sampled. The saucer was up to "
               "{:.1f} m from where the shot was cut for it. Aim miss: median {:.2f} m, max {:.2f} m; "
               "unfollowed median {:.1f} m. Angle off the saucer: median {:.1f} deg, max {:.1f} deg. "
               "Past the aim swing: worst miss {:.2f} m ({:.1f} deg). "
               "Hero sits {:.2f} m from the craft and drifts {:.3f} m ({:.2f}% of the flight) over "
               "the window. Frames the follow did not run on: {}.\n",
               cut.start, cut.end, misses.size(), worstFollowOffset, medMiss, maxMiss, medBare,
               medAngle, maxAngle, settledMax, settledMaxAngle, heroToCraftFirst, worstHeroToNode,
               100.0f * worstHeroToNode / std::max(worstFollowOffset, 1e-3f), unfollowedFrames);
    // The probe has to be able to fail. If the saucer had never left the place the shot was cut for,
    // followed and unfollowed would be the same number and the comparison would prove nothing
    // (ADR-182).
    REQUIRE(worstFollowOffset > 20.0f);
    // The hero table is what the aim follows, so it has to be where the saucer is.
    // The hero rides the craft. What it sits at is an authored anchor offset and is not this test's
    // business -- a hero is allowed to sit somewhere other than the origin of the thing it names,
    // and Glowmere's saucer hero is authored 1.45 m under its node on purpose. What *is* this
    // test's business is that the offset does not grow as the craft flies.
    //
    // Bounded relative to the flight rather than at an absolute metre, because the quantity it has
    // to be small compared to is the distance travelled: `syncHeroesToNodes` accumulates per-frame
    // deltas, so whatever it loses it loses in proportion to how far the thing went. Measured, 1.08 m
    // over 122.6 m -- under 1% -- and an order of magnitude below the framing offset the aim already
    // carries, so it cannot be what the aim error is made of.
    CHECK(worstHeroToNode < 0.02f * worstFollowOffset);
    // The aim tracks the craft rather than the place it used to be. The three bounds this used to
    // state were all fractions of `worstFollowOffset`, and that model is wrong in a way that only a
    // second film could expose: **what is left over is the shot's FRAMING offset, which is a fixed
    // quantity** -- set by the subject's radius and the shot's framing -- and not a fraction of how
    // far the subject happened to fly. The original cut flew the saucer 122.6 m, so a ~6 m framing
    // offset was 5% and sat comfortably under a 20% bound; a re-baked cut flies it 90.5 m in a
    // wider framing, and the same *kind* of offset is 20.6%. Nothing regressed. The bound was
    // measuring the film.
    //
    // So the assertions are on what the follow actually promises, which is film-independent:
    //
    //   1. it removes most of the staleness the shot would otherwise accumulate, and
    //   2. the miss does not GROW as the craft flies -- a follow that has stopped following shows
    //      up as drift against travel, whatever the framing offset underneath it is.
    //
    // (2) is the real contract and the one the old bounds could not state: a test that only caps
    // the miss passes a follow that is uniformly 20 m off and fails one that is perfectly centred
    // on a subject framed wide.
    CHECK(medBare > 2.5f * medMiss);
    // Drift: the settled tail against the settled head. Both are after ADR-185's swing window, so
    // neither carries the cut's own transition, and the difference is what the follow let slip
    // while the craft crossed `worstFollowOffset` metres.
    const std::vector<float> settledHead(settled.begin(), settled.begin() + static_cast<long>(settled.size() / 2));
    const std::vector<float> settledTail(settled.begin() + static_cast<long>(settled.size() / 2), settled.end());
    const float drift = std::fabs(quantile(settledTail, 0.5) - quantile(settledHead, 0.5));
    INFO("aim drift across the settled window: " << drift << " m over " << worstFollowOffset << " m of flight");
    CHECK(drift < 0.1f * worstFollowOffset);
    // And the aim is on the craft rather than a hundred metres of stale aim behind it. Stated
    // against the *subject's own size* -- the thing a framing offset is actually proportional to --
    // rather than against the distance it travelled.
    // Read from the cut itself rather than written down here. A literal would be this film's
    // visitor radius, which is the exact mistake the bounds above were just rewritten to stop
    // making.
    const auto spanIt = std::find_if(engine.shotSpans().begin(), engine.shotSpans().end(),
                                     [&](const world::ShotSpan& sp) {
                                         return sp.subject == "visitor" && sp.start <= cut.start + 1e-3 &&
                                                sp.end >= cut.end - 1e-3;
                                     });
    REQUIRE(spanIt != engine.shotSpans().end());
    const float subjectRadius = std::max(spanIt->subjectRadius, 1.0f);
    INFO("settled worst miss " << settledMax << " m against a subject radius of " << subjectRadius << " m");
    CHECK(settledMax < 6.0f * subjectRadius);

    // ---- and the control arm, because a probe that cannot fail proves nothing (ADR-182) ----
    //
    // Everything above rests on the hero being where the saucer is, and the number that says so is
    // a *small* number -- which is exactly the shape of reading that a broken probe also produces.
    // So: move the hero a known distance and check the measurement notices. Without this, a probe
    // that had silently stopped reading either position would report a rock-steady zero drift and
    // look like the strongest evidence in the file.
    const glm::vec3 shove(25.0f, 0.0f, 0.0f);
    const float before = glm::length(craft->visualPosition() - heroNamed(*comp, "visitor")->position);
    std::vector<world::HeroPoint> moved = comp->heroes();
    for (world::HeroPoint& h : moved) {
        if (h.name == "visitor") {
            h.position += shove;
        }
    }
    REQUIRE(comp->setHeroes(std::move(moved)).has_value());
    engine.update(engine.tick(clock));
    const float after = glm::length(craft->visualPosition() - heroNamed(*comp, "visitor")->position);
    fmt::print("[aim] control arm: hero shoved {:.1f} m, measured gap {:.2f} m -> {:.2f} m.\n",
               glm::length(shove), before, after);
    // One frame of the saucer's own motion is centimetres, so nearly all of the change is the shove.
    CHECK(std::fabs(after - before) > 0.5f * glm::length(shove));
#endif
}

TEST_CASE("The camera can be told to stay with a scenario's actor until it is done",
          "[director][ufo][hold]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("Glowmere Valley 2 is not present");
    }

    // One playthrough, to just past the end of the first shot on the saucer, reporting how far the
    // camera's aim was from the saucer on each frame after that shot ended and whether the hold was
    // on. Everything else about the two arms is identical.
    struct Arm {
        // How far the aim landed from the saucer, over the frames after the shot ended while the
        // abduction was still running. Metres rather than degrees for the assertion, for the reason
        // the aim test gives; degrees are reported.
        float worstMissAfter = 0.0f;
        float worstAngleAfter = 0.0f;
        int heldFrames = 0;
        int engagedFrames = 0;
        int frames = 0;
    };

    const auto play = [&](const std::string& holdScenario) {
        app::Engine engine(app::EngineMode::Offline);
        auto loaded = engine.loadProject(glowmereProject());
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        if (engine.track() == nullptr) {
            return Arm{};
        }
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        app::AutoDirectorSettings base;
        const Cut cut = firstVisitorShot(engine, *comp, base);
        REQUIRE(cut.found);
        // Re-cut with the hold setting and the *same* seed, so the film is the same film.
        app::AutoDirectorSettings s = base;
        s.seed = cut.seed;
        s.holdScenario = holdScenario;
        REQUIRE(app::directEngine(engine, comp->heroes(), s).has_value());

        Arm arm;
        FixedStepClock clock(60.0);
        const double until = std::min(cut.end + 8.0, engine.durationSeconds());
        const int steps = static_cast<int>(until * 60.0);
        engine.seekSeconds(0.0);
        for (int i = 0; i < steps; ++i) {
            engine.update(engine.tick(clock));
            const double t = static_cast<double>(i) / 60.0;
            if (t < cut.end) {
                continue;
            }
            ++arm.frames;
            if (comp->aimHeld()) {
                ++arm.heldFrames;
            }
            if (!comp->director().binding("abduction", "target").empty()) {
                ++arm.engagedFrames;
            } else {
                continue; // the abduction finished; the hold has no claim past that and nor has this
            }
            const world::HeroPoint* saucer = heroNamed(*comp, "visitor");
            REQUIRE(saucer != nullptr);
            const scene::Camera& cam = comp->scene().camera;
            arm.worstMissAfter =
                std::max(arm.worstMissAfter, glm::length(cam.target - saucer->position));
            arm.worstAngleAfter =
                std::max(arm.worstAngleAfter, angleBetween(cam.position, cam.target, saucer->position));
        }
        return arm;
    };

    const Arm off = play("");
    if (off.frames == 0) {
        SKIP("the project's audio is not available here");
    }
    const Arm on = play("abduction");

    const auto report = [](const char* name, const Arm& a) {
        fmt::print("[hold] {}: {} frames after the shot, {} with an abduction running, {} held; "
                   "worst aim miss {:.1f} m ({:.1f} deg off the saucer)\n",
                   name, a.frames, a.engagedFrames, a.heldFrames, a.worstMissAfter,
                   a.worstAngleAfter);
    };
    report("holdScenario=\"\"         ", off);
    report("holdScenario=\"abduction\"", on);

    // The arms have to differ in something the camera did, or the setting reached nothing (ADR-182).
    REQUIRE(off.engagedFrames > 30);
    // Off is off: the default setting must not hold a single frame.
    CHECK(off.heldFrames == 0);
    CHECK(on.heldFrames > 0);
    // And the hold does what it says: once the cut has moved on, the held camera is still pointed at
    // the saucer and the unheld one has gone somewhere else entirely. A held aim's miss is *exactly*
    // the framing offset the shot was frozen with -- the eye and the aim take the same delta -- so
    // the two arms are not close.
    CHECK(on.worstMissAfter < off.worstMissAfter);
    CHECK(on.worstMissAfter < 0.25f * off.worstMissAfter);
#endif
}

TEST_CASE("The hold refuses what it cannot mean, and round-trips", "[director][hold][settings]") {
    app::AutoDirectorSettings s;
    CHECK(s.holdScenario.empty()); // off by default, or it would change every existing project
    CHECK(s.validate().has_value());

    s.holdScenario = "abduction";
    CHECK(s.validate().has_value());
    s.holdRole.clear();
    CHECK_FALSE(s.validate().has_value()); // a scenario with no role to watch holds for ever
    s.holdRole = "target";
    s.holdReleaseSeconds = -1.0;
    CHECK_FALSE(s.validate().has_value());
    s.holdReleaseSeconds = 0.75;
    REQUIRE(s.validate().has_value());

    const auto back = app::AutoDirectorSettings::fromJson(s.toJson());
    REQUIRE(back.has_value());
    CHECK(*back == s);
    // A project written before the hold existed reads as "off", not as a scenario called "".
    const auto older = app::AutoDirectorSettings::fromJson(nlohmann::json{{"dwell", 3}});
    REQUIRE(older.has_value());
    CHECK(older->holdScenario.empty());
    CHECK(older->holdRole == "target");
    CHECK(older->dwellShots == 3);
}

// The hold has to survive a save and a reload without being re-directed, or an offline render --
// which reloads the project before drawing it -- would be a different film from the window that
// asked for it. Derived from `autoDirector` on load rather than stored a second time, so the two
// cannot disagree.
TEST_CASE("A saved hold is installed again when the project is loaded", "[director][hold][project]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("Glowmere Valley 2 is not present");
    }
    const std::filesystem::path out =
        std::filesystem::temp_directory_path() / "avgen-hold-roundtrip.json";

    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(glowmereProject()).has_value());
        REQUIRE(engine.composition() != nullptr);
        // What the hold is on load is whatever `autoDirector` declares -- which is the invariant
        // this test is actually about, and which used to be stated as "empty".
        //
        // That was true when the shipped project carried no director settings, and ADR-225 made it
        // false: the project now persists `holdScenario`, so loading it installs a hold before
        // anything is directed. Asserting emptiness here was asserting that the project had no
        // settings, which is a fact about the file rather than about the round trip, and it broke
        // the moment somebody saved a hold into it from the application.
        const bool declared = !engine.autoDirector().holdScenario.empty();
        INFO("project declares holdScenario '" << engine.autoDirector().holdScenario << "'");
        CHECK(engine.composition()->aimHeldHero().empty() != declared);
        engine.autoDirector().holdScenario = "abduction";
        REQUIRE(engine.saveProject(out).has_value());
    }
    {
        app::Engine engine(app::EngineMode::Offline);
        auto loaded = engine.loadProject(out);
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        REQUIRE(engine.composition() != nullptr);
        CHECK(engine.autoDirector().holdScenario == "abduction");
        // Resolved scenario -> actor -> body entity -> node, with nothing directed: the hold is
        // installed by the load itself.
        CHECK(engine.composition()->aimHeldHero() == "visitor");
        CHECK(engine.composition()->aimHold().role == "target");
    }
    std::error_code ec;
    std::filesystem::remove(out, ec);
#endif
}

// Is the animal in frame while the hold is driving the camera? (the framing half of ADR-217)
//
// Reported as "you cant always see the animal as its being abducted". The mechanism is visible in
// `applyAimHold` without running anything: it translates the eye AND the target by the *craft's*
// delta, so the saucer keeps its screen position and size exactly, and the body hanging a hover
// height beneath it appears nowhere in that arithmetic.
//
// This probe has to run where the hold is LIVE, which means a loaded project, a directed cut and a
// `holdScenario`. A first version of it was built on the abduction-alignment harness instead --
// which never directs a camera at all, so `applyDirectedAim` returned early, `applyAimHold` never
// executed, and it measured the scene's static authored camera. The numbers looked plausible and
// were about something else entirely; the fix under test could not move them by a single frame,
// which is the only reason it was caught.
TEST_CASE("probe: is the abducted animal in frame while the hold drives", "[.probe][hold][framing]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("Glowmere Valley 2 is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(glowmereProject()).has_value());
    if (engine.track() == nullptr) {
        SKIP("the project's audio is not available here");
    }
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    app::AutoDirectorSettings base;
    const Cut cut = firstVisitorShot(engine, *comp, base);
    REQUIRE(cut.found);
    app::AutoDirectorSettings s = base;
    s.seed = cut.seed;
    s.holdScenario = "abduction";
    s.holdRole = "target";
    REQUIRE(app::directEngine(engine, comp->heroes(), s).has_value());

    int held = 0, fully = 0, partly = 0, off = 0;
    int craftFrames = 0, craftFully = 0, craftOff = 0;
    float smallest = 1e9f, worstOut = 0.0f;
    FixedStepClock clock(60.0);
    engine.seekSeconds(0.0);
    const int steps = static_cast<int>(std::min(240.0, engine.durationSeconds()) * 60.0);
    for (int i = 0; i < steps; ++i) {
        engine.update(engine.tick(clock));
        // Only the frames the hold is actually driving: everywhere else the camera is the cut's and
        // the animal's framing is not this feature's business.
        if (!comp->aimHeld()) {
            continue;
        }
        const std::string_view target = comp->director().binding("abduction", "target");
        if (target.empty()) {
            continue;
        }
        // The LIFT, not the whole engagement. A role binds at `acquire` and stays bound through
        // approach, beam, abduct and depart; during the approach the craft is still flying toward an
        // animal that has no reason to be in shot yet, and counting those frames answers a question
        // nobody asked. "as its being abducted" is the `abduct` beat.
        if (comp->director().beat("abduction") != "abduct") {
            continue;
        }
        const std::vector<glm::vec3> corners = comp->nodeCorners(std::string(target));
        if (corners.empty()) {
            continue;
        }
        ++held;
        const scene::Camera& cam = comp->scene().camera;
        const glm::mat4 viewProj = cam.projection(16.0f / 9.0f) * cam.view();
        int in = 0;
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const glm::vec3& p : corners) {
            const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
            if (clip.w <= 1e-4f) { // behind the eye
                worstOut = std::max(worstOut, 2.0f);
                continue;
            }
            const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
            lo = glm::min(lo, ndc);
            hi = glm::max(hi, ndc);
            if (std::fabs(ndc.x) <= 1.0f && std::fabs(ndc.y) <= 1.0f) {
                ++in;
            } else {
                worstOut = std::max({worstOut, std::fabs(ndc.x) - 1.0f, std::fabs(ndc.y) - 1.0f});
            }
        }
        // The CRAFT, as well as the animal. "if the ufo is moving somewhere and we are focused on it
        // it should be in the shot, it currently is not" -- and nothing was asserting it. A framing
        // rule that puts the animal on screen by pushing the saucer off it has traded one report for
        // another.
        {
            // Not `nodeCorners`: the visitor is a PROCEDURAL node, whose bounds are the instance
            // extent memoised at rebuild time (ADR-217), so it comes back empty here and the whole
            // craft measurement silently reported nothing. The hero carries the craft's own radius,
            // which is what the director frames against anyway.
            std::vector<glm::vec3> craftCorners;
            if (const world::HeroPoint* h = heroNamed(*comp, "visitor")) {
                const entity::Entity* body = comp->entityWorld().find("visitor");
                const glm::vec3 c = body != nullptr ? body->visualPosition() : h->position;
                const float r = std::max(h->radius, 0.5f);
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sy = -1; sy <= 1; sy += 2) {
                        for (int sz = -1; sz <= 1; sz += 2) {
                            craftCorners.push_back(c + glm::vec3(sx, sy, sz) * r);
                        }
                    }
                }
            }
            if (!craftCorners.empty()) {
                int craftIn = 0;
                for (const glm::vec3& p : craftCorners) {
                    const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
                    if (clip.w <= 1e-4f) {
                        continue;
                    }
                    const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
                    if (std::fabs(ndc.x) <= 1.0f && std::fabs(ndc.y) <= 1.0f) {
                        ++craftIn;
                    }
                }
                ++craftFrames;
                if (craftIn == static_cast<int>(craftCorners.size())) {
                    ++craftFully;
                } else if (craftIn == 0) {
                    ++craftOff;
                }
            }
        }
        // How far the eye is from the pair, and how far apart the pair is. If the distance tracks a
        // separation that collapses as the animal rises, the camera dollies in with it -- which is
        // "the camera is the one being abducted".
        {
            const entity::Entity* prey = comp->entityWorld().find(std::string(target));
            const world::HeroPoint* craft = heroNamed(*comp, "visitor");
            if (prey != nullptr && craft != nullptr) {
                const float apart = glm::length(craft->position - prey->visualPosition());
                const glm::vec3 mid = (craft->position + prey->visualPosition()) * 0.5f;
                const float eye = glm::length(cam.position - mid);
                if (held % 30 == 1) {
                    fmt::print("[dolly] frame {:5d}  pair apart {:7.2f} m   eye to pair {:8.2f} m\n",
                               held, apart, eye);
                }
            }
        }
        if (in == static_cast<int>(corners.size())) {
            ++fully;
            smallest = std::min(smallest, std::max(hi.x - lo.x, hi.y - lo.y));
        } else if (in > 0) {
            ++partly;
        } else {
            ++off;
        }
    }
    fmt::print("[hold-framing] {} held frames with a bound target: fully in {}, partly {}, off {}\n",
               held, fully, partly, off);
    if (craftFrames > 0) {
        fmt::print("[hold-framing] the CRAFT over the same frames: fully in {} ({:.0f}%), entirely off {} ({:.0f}%)\n",
                   craftFully, 100.0 * craftFully / craftFrames, craftOff, 100.0 * craftOff / craftFrames);
    }
    if (held > 0) {
        fmt::print("[hold-framing] fully in {:.0f}%, smallest NDC span {:.3f}, worst {:.2f} outside\n",
                   100.0 * fully / held, smallest > 1e8f ? -1.0f : smallest, worstOut);
    }
    // The probe must be able to see a bound target at all, or every number above is a zero that
    // means "this never ran" rather than "this never failed".
    CHECK(held > 0);
#endif
}

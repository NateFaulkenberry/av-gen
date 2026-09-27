// ADR-907: a wander that walks on rather than back, keeps off ground its author called too steep,
// and eases into and out of its stops.
//
// The GV3 character audit traced the animals' "walk -> stop -> turn 180 -> walk back" to two rules
// in `wander`: every destination was a uniformly random direction, and a body past its home radius
// had its next destination drawn around home -- a return trip by construction. It also found them
// standing and walking on 20-28 degree flanks, facing into the hill, because nothing but the
// world's 63-degree cliff rule said where a body could go. Each arm below runs a real body through
// the real behaviour for long enough to make the claim statistical, reads the answer off the
// ADR-910 character quality recorder, and has a control arm that must read the opposite -- the
// knob moved back to what the old behaviour did, or out of the way -- so the arm cannot pass by
// measuring nothing (ADR-182).
//
//   forward      600 s on open ground: no stop is walked out of more than 150 degrees from the
//                way it was walked into  |  control: `headingSpread` 180, the whole circle, reverses
//   home         a home radius holds a body near its anchor without a reversal  |  control: an
//                unleashed body walks away
//   slope        on a ridge world, `maxSlope` keeps every standing body off steep ground and every
//                walk off it too, and its default (12 degrees) does with no key in the file  |
//                control: `maxSlope` 0 (the world's cliff rule only) stands the same body on the flank
//   eased        with an authored gait the measured acceleration never beats the gait's own
//                numbers and the legs never walk on the spot  |  control: an absurd gait shows the
//                numbers are what is being read

#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "entity/gait.hpp"
#include "entity/navigation.hpp"
#include "support/cast_world.hpp"
#include "world/camera_clearance.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace avgen;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

constexpr float kDegrees = 57.2957795f;

// A wanderer with the settings a grazing animal has, over whatever it is given on top.
CastMember grazer(nlohmann::json extra = nlohmann::json::object()) {
    nlohmann::json w = {{"speed", 1.4}, {"turnRate", 95.0}, {"minRange", 4.0}, {"maxRange", 12.0},
                        {"pauseMin", 0.5}, {"pauseMax", 2.0}, {"homeRadius", 25.0}};
    for (const auto& [k, v] : extra.items()) {
        w[k] = v;
    }
    CastMember m;
    m.name = "grazer";
    m.seed = 424242;
    m.behaviors.push_back(behavior("wander", w));
    return m;
}

// A flat world with one ridge down x = 15: flat ground to the west of x = -10, a flank climbing to
// 18 m at the crest. Everything a slope limit has to tell apart, and nothing else.
struct RidgeBed {
    world::WorldMap map;
    world::Ecology ecology;
    world::ClearanceField field;
    entity::Navigator nav;

    RidgeBed() {
        map.size = glm::vec2(400.0f, 400.0f);
        world::Feature ridge;
        ridge.name = "ridge";
        ridge.kind = world::FeatureKind::Ridge;
        ridge.path = {glm::vec3(15.0f, 0.0f, -200.0f), glm::vec3(15.0f, 0.0f, 200.0f)};
        ridge.width = 25.0f;
        ridge.amplitude = 18.0f;
        ridge.smoothing = 0;
        map.features.push_back(ridge);
        map.prepare();
        field.map = &map;
        field.ecology = &ecology;
        field.cameraRadius = 0.6f;
        field.groundClearance = 0.0f;
        nav = entity::Navigator(&map, field);
    }
    RidgeBed(const RidgeBed&) = delete;
    RidgeBed& operator=(const RidgeBed&) = delete;

    [[nodiscard]] float slopeAt(glm::vec2 p) const {
        return std::acos(std::clamp(nav.groundNormal(p, 1.0f).y, -1.0f, 1.0f)) * kDegrees;
    }
};

} // namespace

TEST_CASE("a wanderer walks on from its stops rather than back the way it came",
          "[wander][adr907]") {
    // Twelve hundred seconds on open ground: well over a hundred stops (a leg of 4-12 m at 1.4 m/s
    // and a pause of up to two seconds is about eight seconds), which is enough that "none of them
    // was a reversal" is a statement about the rule rather than about luck.
    CastWorld forward({grazer()});
    forward.play(1200.0);
    const entity::CharacterQuality q = forward.quality("grazer");
    WARN(fmt::format("forward cone: {} stops, {} measured, {} over 90 deg, {} reversals, {:.0f} m travelled",
                     q.behaviour.stops, q.behaviour.measuredStops, q.behaviour.turnsOver90,
                     q.behaviour.reversals, q.motion.travelMetres));
    REQUIRE(q.behaviour.measuredStops > 100);
    CHECK(q.behaviour.reversals == 0);
    CHECK(q.motion.travelMetres > 800.0);

    // The whole circle -- the old uniform draw's directions -- with the same turning. Half its
    // destinations lie behind it, and it turns more than 90 degrees out of a stop many times more
    // often. (It rarely *reverses* by the recorder's measure, and that is ADR-908 at work: from
    // rest it pivots only until the way is within 90 degrees and walks the rest of the turn on a
    // curve, so the first metre and a half out of a stop is never straight back.)
    CastWorld circle({grazer({{"headingSpread", 180.0}})});
    circle.play(1200.0);
    const entity::CharacterQuality c = circle.quality("grazer");
    WARN(fmt::format("whole circle: {} stops, {} measured, {} over 90 deg, {} reversals",
                     c.behaviour.stops, c.behaviour.measuredStops, c.behaviour.turnsOver90,
                     c.behaviour.reversals));
    REQUIRE(c.behaviour.measuredStops > 100);
    CHECK(c.behaviour.turnsOver90 > q.behaviour.turnsOver90 * 4);

    // The owner's pattern in its pure form, which is the control the recorder has to see: the whole
    // circle *and* a body that pivots on the spot and walks straight (a turn rate no walk-through
    // turn can keep up with). Walk, stop, turn round, walk back -- and it is counted.
    const auto pivotAndGo = [](double spread) {
        CastWorld w({grazer({{"headingSpread", spread}, {"turnRate", 1440.0}})});
        w.play(1200.0);
        return w.quality("grazer");
    };
    const entity::CharacterQuality uniform = pivotAndGo(180.0);
    WARN(fmt::format("whole circle, pivot and go: {} measured stops, {} reversals", uniform.behaviour.measuredStops,
                     uniform.behaviour.reversals));
    REQUIRE(uniform.behaviour.measuredStops > 100);
    CHECK(uniform.behaviour.reversals > 10);
    // And the cone alone removes it, even from a body that pivots and goes: the destinations are
    // what reversed it, and the cone is what changed them.
    const entity::CharacterQuality coned = pivotAndGo(60.0);
    WARN(fmt::format("forward cone, pivot and go: {} measured stops, {} reversals", coned.behaviour.measuredStops,
                     coned.behaviour.reversals));
    REQUIRE(coned.behaviour.measuredStops > 100);
    CHECK(coned.behaviour.reversals == 0);
}

TEST_CASE("a home radius keeps a wanderer near home by leaning, not by turning it round",
          "[wander][adr907]") {
    // A tight territory for the range it walks: the lean toward home is doing work on most legs.
    const auto run = [](float home) {
        CastWorld w({grazer({{"homeRadius", home}, {"maxRange", 14.0}})});
        float furthest = 0.0f;
        w.play(1200.0, [&] {
            const glm::vec3 p = w.body("grazer").state().position();
            furthest = std::max(furthest, glm::length(glm::vec2(p.x, p.z)));
        });
        return std::pair(furthest, w.quality("grazer"));
    };
    const auto [held, leashed] = run(12.0f);
    WARN(fmt::format("home 12 m: furthest {:.1f} m from home, {} reversals in {} measured stops",
                     held, leashed.behaviour.reversals, leashed.behaviour.measuredStops));
    // Never further out than a leg can carry it past the edge of its territory: the leash refuses
    // any destination that would take it further out than it already is.
    CHECK(held < 12.0f + 14.0f);
    REQUIRE(leashed.behaviour.measuredStops > 100);
    CHECK(leashed.behaviour.reversals == 0);

    // The control: no home at all. A forward-biased walk with no leash leaves, which is the point
    // of the leash, and is what shows `homeRadius` is what held the first body.
    const auto [gone, unleashed] = run(0.0f);
    WARN(fmt::format("no home: furthest {:.1f} m", gone));
    CHECK(gone > 100.0f);
    (void)unleashed;
}

TEST_CASE("a slope limit keeps a wanderer, standing and walking, off ground steeper than it",
          "[wander][adr907][slope]") {
    RidgeBed bed;
    // The instrument first: the bed has the ground the arm needs, flat where the body starts and
    // steep within its reach, or the control arm below proves nothing.
    REQUIRE(bed.slopeAt({-15.0f, 0.0f}) < 2.0f);
    float steepest = 0.0f;
    for (float x = -10.0f; x <= 15.0f; x += 0.5f) {
        steepest = std::max(steepest, bed.slopeAt({x, 0.0f}));
    }
    INFO("the ridge's flank reaches " << steepest << " degrees");
    REQUIRE(steepest > 25.0f);

    constexpr float kLimit = 10.0f;
    // A negative limit leaves the key out of the file: the default's arm.
    const auto run = [&](float limit, float steep = kLimit + 2.0f) {
        nlohmann::json extra = {{"homeRadius", 30.0}, {"maxRange", 16.0}};
        if (limit >= 0.0f) {
            extra["maxSlope"] = limit;
        }
        CastMember m = grazer(extra);
        m.at = glm::vec3(-12.0f, 0.0f, 0.0f);
        // `ground` so the body reports the surface it stands on to the recorder.
        m.behaviors.push_back(behavior("ground", {{"slopeAlign", 0.55}}));
        entity::CharacterQualityThresholds th;
        th.steepDegrees = steep; // steeper than the limit, with room for the footprint
        CastWorld w({m}, &bed.nav, th);
        float walkedOn = 0.0f;
        w.play(600.0, [&] {
            const entity::Entity& e = w.body("grazer");
            if (e.state().speed > 0.3f) {
                const glm::vec3 p = e.state().position();
                walkedOn = std::max(walkedOn, bed.slopeAt({p.x, p.z}));
            }
        });
        return std::pair(walkedOn, w.quality("grazer"));
    };
    const auto [walked, limited] = run(kLimit);
    WARN(fmt::format("maxSlope {:.0f}: walked on up to {:.1f} deg, stood {:.0f} s ({:.1f} s on steep, "
                     "{:.1f} s facing uphill), steepest standing {:.1f} deg",
                     kLimit, walked, limited.ground.stillSeconds, limited.ground.steepSeconds,
                     limited.ground.facingUphillSeconds, limited.ground.slopeMaxDegrees));
    REQUIRE(limited.ground.stillSeconds > 60.0); // it stood, and stood on something it reported
    CHECK(limited.ground.steepSeconds == 0.0);
    CHECK(limited.ground.facingUphillSeconds == 0.0);
    // Walking too: the straight line to every destination was checked, so the walk between two
    // gentle places does not cross the flank. A degree of margin for sampling the line every 2 m.
    CHECK(walked < kLimit + 3.0f);

    // The control: `maxSlope` 0, the world's cliff rule and nothing more. The same body on the same
    // bed stands and walks on the flank.
    const auto [walkedFree, free] = run(0.0f);
    WARN(fmt::format("maxSlope 0 (the world's cliff rule only): walked on up to {:.1f} deg, {:.1f} s "
                     "standing on steep ground, {:.1f} s of it facing uphill",
                     walkedFree, free.ground.steepSeconds, free.ground.facingUphillSeconds));
    CHECK(free.ground.steepSeconds > 5.0);
    CHECK(walkedFree > kLimit + 10.0f);

    // And the default. A wanderer whose file says nothing about slope keeps off ground steeper than
    // 12 degrees -- the line the analyzer calls steep -- standing and walking: the owner's "animals
    // should avoid walking into steep slopes" (brief §11) as what a wanderer does unless told
    // otherwise, not as a knob a scene has to know to turn. Measured as the first arm is: standing
    // ground against the limit plus two degrees for the footprint, and walks against the limit plus
    // three for sampling the line every 2 m.
    const auto [walkedDefault, byDefault] = run(-1.0f, 14.0f);
    WARN(fmt::format("maxSlope absent (the default, 12): walked on up to {:.1f} deg, {:.1f} s standing "
                     "on ground over 14 deg, {:.1f} s facing uphill",
                     walkedDefault, byDefault.ground.steepSeconds, byDefault.ground.facingUphillSeconds));
    REQUIRE(byDefault.ground.stillSeconds > 60.0);
    CHECK(byDefault.ground.steepSeconds == 0.0);
    CHECK(walkedDefault < 12.0f + 3.0f);
}

TEST_CASE("a wanderer put on a flank walks down or along it, never up it",
          "[wander][adr907][slope]") {
    // The case the first slope fallback got wrong, found on GV2-multicam's four flank-anchored
    // animals: from a flank every straight walk starts on steep ground, so no destination passes
    // the slope rule, and ranking the rest by their walk's steepest point ranked them all the same
    // -- the pick fell behind the body as often as ahead, and the animals paced between a few spots.
    // Now the rest are ranked by the ground the body would stand on, ahead first, and never steeper
    // than where it stands while anything else is on offer.
    RidgeBed bed;
    // Start part-way up the west flank, facing along it (north), where the slope is a hillside.
    float startX = 0.0f;
    for (float x = -8.0f; x <= 12.0f; x += 0.5f) {
        const float s = bed.slopeAt({x, 0.0f});
        if (s > 15.0f && s < 25.0f) {
            startX = x;
            break;
        }
    }
    const float startSlope = bed.slopeAt({startX, 0.0f});
    INFO("starting at x = " << startX << " on a " << startSlope << "-degree flank");
    REQUIRE(startSlope > 15.0f);

    struct Run {
        entity::CharacterQuality q;
        float steepestStood = 0.0f;  // the steepest ground it came to a stop on
        float highestClimb = 0.0f;   // metres above its starting ground it ever stood
        double onFlatFrom = -1.0;    // when it first stood on ground under 6 degrees
    };
    const auto run = [&](float home, float limit) {
        CastMember m = grazer({{"homeRadius", home}, {"maxRange", 12.0}, {"maxSlope", limit}});
        m.at = glm::vec3(startX, 0.0f, 0.0f);
        m.behaviors.push_back(behavior("ground", {{"slopeAlign", 0.55}}));
        CastWorld w({m}, &bed.nav);
        Run out;
        const float startHeight = bed.nav.groundHeight({startX, 0.0f});
        w.play(300.0, [&] {
            const entity::Entity& e = w.body("grazer");
            if (e.state().speed < 0.1f) {
                const glm::vec3 p = e.state().position();
                const float s = bed.slopeAt({p.x, p.z});
                out.steepestStood = std::max(out.steepestStood, s);
                out.highestClimb = std::max(out.highestClimb, bed.nav.groundHeight({p.x, p.z}) - startHeight);
                if (out.onFlatFrom < 0.0 && s < 6.0f) {
                    out.onFlatFrom = w.time();
                }
            }
        });
        out.q = w.quality("grazer");
        return out;
    };

    // Flat ground within its territory: it walks down to it, and from there the slope rule holds.
    const Run down = run(40.0f, 12.0f);
    WARN(fmt::format("flank start, flat within reach: first stood on the flat at {:.1f} s; steepest stop {:.1f} "
                     "deg; climbed at most {:.2f} m; {} stops, {} reversals, {} A->B->A",
                     down.onFlatFrom, down.steepestStood, down.highestClimb, down.q.behaviour.stops,
                     down.q.behaviour.reversals, down.q.behaviour.revisits));
    REQUIRE(down.onFlatFrom >= 0.0);
    CHECK(down.onFlatFrom < 60.0);
    CHECK(down.steepestStood < startSlope + 1.0f);
    CHECK(down.highestClimb < 0.5f);
    CHECK(down.q.behaviour.reversals == 0);

    // Leashed to the flank, with nothing gentle in its territory: it grazes along the hill and never
    // climbs it. (It does walk back and forth: a 6 m leash on a contour is a strip about 12 m long,
    // and a strip has two directions. That is the scene's to fix -- home an animal on the flat, as
    // GV3's recommendations do -- and not something a destination rule can.)
    const Run along = run(6.0f, 12.0f);
    WARN(fmt::format("leashed to the flank: steepest stop {:.1f} deg (started on {:.1f}); climbed at most {:.2f} "
                     "m; {} stops, {} reversals, {} A->B->A, {:.0f} m travelled",
                     along.steepestStood, startSlope, along.highestClimb, along.q.behaviour.stops,
                     along.q.behaviour.reversals, along.q.behaviour.revisits, along.q.motion.travelMetres));
    REQUIRE(along.q.behaviour.stops > 10);
    CHECK(along.steepestStood < startSlope + 1.5f);
    CHECK(along.highestClimb < 1.0f);

    // The control: `maxSlope` 0 -- the world's cliff rule and nothing else. The same body from the
    // same spot wanders up the flank as readily as down it.
    const Run free = run(40.0f, 0.0f);
    WARN(fmt::format("maxSlope 0: steepest stop {:.1f} deg, climbed {:.2f} m", free.steepestStood,
                     free.highestClimb));
    CHECK(free.highestClimb > 2.0f);
}

TEST_CASE("a wanderer eases into and out of its stops at its own gait's rates",
          "[wander][adr907][gait]") {
    const auto run = [](float accel, float decel) {
        CastMember m = grazer();
        m.gait.walkSpeed = 1.4f;
        m.gait.accel = accel;
        m.gait.decel = decel;
        m.gait.accelAuthored = true; // what a scene naming either key sets (ADR-620)
        CastWorld w({m});
        w.play(300.0);
        return w.quality("grazer");
    };
    constexpr float kAccel = 2.2f;
    constexpr float kDecel = 3.0f;
    const entity::CharacterQuality eased = run(kAccel, kDecel);
    WARN(fmt::format("gait {:.1f}/{:.1f}: largest measured acceleration {:.2f} m/s^2, {:.2f} s walking on "
                     "the spot, {} stops",
                     kAccel, kDecel, eased.motion.largestAccel, eased.behaviour.stuckSeconds,
                     eased.behaviour.stops));
    REQUIRE(eased.behaviour.stops > 25);
    // The body's own ground speed never changes faster than the gait allows. The margin is the
    // part of an acceleration that is turning rather than speeding up: a body on a curve of radius
    // R at speed v accelerates at v^2 / R sideways, which the recorder measures too.
    CHECK(eased.motion.largestAccel < static_cast<double>(kDecel) + 1.5);
    // And the legs never walk on the spot: the published speed and the ground covered agree. Before
    // ADR-907 wander stopped the body dead while the ADR-620 limiter ramped the published speed
    // down, and the legs went on walking for about 0.4 s after every stop.
    CHECK(eased.behaviour.stuckSeconds == 0.0);

    // The control: the same body with a gait that permits anything. What limited the first body
    // was the gait's numbers, read through the behaviour, and not something else in the walk.
    const entity::CharacterQuality snapped = run(1000.0f, 1000.0f);
    WARN(fmt::format("gait 1000/1000: largest measured acceleration {:.1f} m/s^2", snapped.motion.largestAccel));
    CHECK(snapped.motion.largestAccel > 20.0);
}

TEST_CASE("a body that stopped underneath a reaction starts its next walk from standing",
          "[gait][adr907]") {
    // An observe pose at the end of an errand, or a two-second hold in a beam, stands the body still
    // while the activity shown is something other than a gait. Before ADR-907 the gait remembered
    // the walk through it, and the next walk -- ramping up from nothing, now that behaviours ease --
    // read "Idle" on its first frame, changed the gait, and `minDwell` then refused the walk for a
    // quarter of a second while the body accelerated: legs idle, ground moving.
    entity::GaitSettings settings; // moveEnter 0.15, minDwell 0.25
    constexpr double kDt = 1.0 / 60.0;
    const auto startAfterHold = [&](float speedDuringHold) {
        entity::Gait gait;
        for (int i = 0; i < 60; ++i) {
            (void)gait.select(settings, entity::Activity::Walk, 1.5f, 0.0f, kDt);
        }
        for (int i = 0; i < 120; ++i) {
            (void)gait.select(settings, entity::Activity::React, speedDuringHold, 0.0f, kDt);
        }
        // Then a walk ramping up at 6 m/s^2, the default gait's own accel: the first frame at
        // 0.1 m/s, below the move band, and 1.0 m/s ten frames later. Count the frames the body is
        // above the band -- visibly travelling -- while the gait still shows it standing.
        int standingWhileMoving = 0;
        for (int i = 1; i <= 20; ++i) {
            const float speed = std::min(1.5f, 0.1f * static_cast<float>(i));
            const entity::Activity shown = gait.select(settings, entity::Activity::Walk, speed, 0.0f, kDt);
            if (speed > settings.moveEnter && shown != entity::Activity::Walk) {
                ++standingWhileMoving;
            }
        }
        return standingWhileMoving;
    };
    // Held still: the walk shows the frame the body crosses the band.
    CHECK(startAfterHold(0.0f) == 0);
    // The control that pins what changed: a flinch that keeps walking (the speed never left the band)
    // keeps its walk through the reaction and back out of it, as ADR-091 has always required -- the
    // existing "a reaction does not lose the gait it interrupted" arm, from the other side.
    CHECK(startAfterHold(1.5f) == 0);
    entity::Gait flinch;
    for (int i = 0; i < 60; ++i) {
        (void)flinch.select(settings, entity::Activity::Walk, 1.5f, 0.0f, kDt);
    }
    const std::uint32_t before = flinch.changes();
    for (int i = 0; i < 30; ++i) {
        (void)flinch.select(settings, entity::Activity::React, 1.5f, 0.0f, kDt);
    }
    CHECK(flinch.current() == entity::Activity::Walk);
    CHECK(flinch.changes() == before);
    // And a hold that stood the body still is remembered as standing, once.
    entity::Gait held;
    for (int i = 0; i < 60; ++i) {
        (void)held.select(settings, entity::Activity::Walk, 1.5f, 0.0f, kDt);
    }
    const std::uint32_t heldBefore = held.changes();
    for (int i = 0; i < 30; ++i) {
        (void)held.select(settings, entity::Activity::React, 0.0f, 0.0f, kDt);
    }
    CHECK(held.current() == entity::Activity::Idle);
    CHECK(held.changes() == heldBefore + 1);
}

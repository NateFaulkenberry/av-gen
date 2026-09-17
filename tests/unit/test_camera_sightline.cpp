// Line of sight from a camera to the hero it is framing -- the half of ADR-080 that was never built.
//
// `clearPath` keeps the eye out of the ground, the canopy and the hero. Nothing asked whether
// anything stood *between* them. These are the geometric properties of the query that answers it,
// checked on a real generated terrain rather than on a plane: a plane has no ridge to hide behind,
// and a fixture that cannot express the failure cannot test the fix (ADR-182).
//
// **The shape of the fixtures is the thing to get right here.** The Visibility Lab's predecessor bug
// survived every test in this repository because every fixture was a centred, axis-aligned box, for
// which the right rule and the wrong rule were bit-identical. So every camera below stands at a real
// bearing and a real elevation off the world's axes, and every arm is paired with a control that the
// same code must answer differently.

#include "world/camera_clearance.hpp"
#include "world/world_composer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <optional>
#include <vector>

using namespace avgen;

namespace {

world::WorldMap testMap() {
    world::WorldRecipe recipe;
    recipe.world = "sightline";
    recipe.seed = 20260917u;
    recipe.extent = 600.0f;
    return world::terrainFor(recipe);
}

// Somewhere on this map a hero could stand and be hidden by the ground from an eye a fixed distance
// away on a fixed bearing. Found by search rather than typed in, because a hard-coded pair of
// coordinates is a fixture that stops meaning anything the first time the noise is reseeded -- and
// returning nothing is a legitimate answer this file checks for rather than asserting through.
struct Ridge {
    glm::vec3 hero{0.0f};
    glm::vec3 eye{0.0f};
    float depth = 0.0f;   // metres the ground stands above the straight line between them
};

std::optional<Ridge> findRidge(const world::WorldMap& map, float distance, float eyeHeight) {
    std::optional<Ridge> best;
    // A real bearing and a real elevation. 0.9 rad is 51.6 degrees -- off every axis of the world and
    // off the diagonal too, so a rule that happens to hold for an axis-aligned camera is not one
    // this fixture will confirm.
    const glm::vec2 bearing(std::cos(0.9f), std::sin(0.9f));
    for (int j = 2; j < 34; ++j) {
        for (int i = 2; i < 34; ++i) {
            const glm::vec2 p = map.min() + map.size * glm::vec2(i / 36.0f, j / 36.0f);
            const float heroGround = map.sample(p, 0.5f).height;
            const glm::vec2 e = p + bearing * distance;
            const float eyeGround = map.sample(e, 0.5f).height;
            const glm::vec3 hero(p.x, heroGround, p.y);
            const glm::vec3 eye(e.x, eyeGround + eyeHeight, e.y);
            // The centre of the hero, which is what a sightline is about.
            const glm::vec3 aim = hero + glm::vec3(0.0f, 4.0f, 0.0f);
            float depth = 0.0f;
            for (int k = 1; k < 60; ++k) {
                const float s = static_cast<float>(k) / 60.0f;
                const glm::vec3 q = eye + (aim - eye) * s;
                const float ground = map.sample(glm::vec2(q.x, q.z), 0.5f).height;
                depth = std::max(depth, ground - q.y);
            }
            if (depth > 0.0f && (!best || depth > best->depth)) {
                best = Ridge{.hero = hero, .eye = eye, .depth = depth};
            }
        }
    }
    return best;
}

world::SubjectCapsule subjectAt(glm::vec3 base) {
    world::SubjectCapsule s;
    s.name = "elder";
    s.position = base;
    s.radius = 3.0f;
    s.height = 8.0f;
    return s;
}

} // namespace

TEST_CASE("a hero behind a ridge is not visible, and one in the open is", "[camera][sightline]") {
    const world::WorldMap map = testMap();
    world::ClearanceField field;
    field.map = &map;

    const auto ridge = findRidge(map, 70.0f, 2.0f);
    if (!ridge) {
        SKIP("this terrain has no ridge at 70 m on that bearing");
    }
    INFO("ridge stands " << ridge->depth << " m above the straight line, hero at " << ridge->hero.x
         << ", " << ridge->hero.z);

    const world::SubjectCapsule subject = subjectAt(ridge->hero);

    // The arm.
    const world::Sightline hidden = world::heroSightline(field, ridge->eye, subject);
    CHECK(hidden.visible < 1.0f);
    CHECK(hidden.requiredLift > 0.0f);

    // **The control, and it is the whole point of the file.** The same query, the same terrain, the
    // same subject -- from an eye raised until it can see over. If this also reported "blocked", the
    // arm above would be measuring nothing but the function's willingness to say no.
    const glm::vec3 raised = ridge->eye + glm::vec3(0.0f, hidden.requiredLift, 0.0f);
    const world::Sightline seen = world::heroSightline(field, raised, subject);
    INFO("lifted " << hidden.requiredLift << " m; visible " << hidden.visible << " -> "
         << seen.visible);
    CHECK(seen.visible > hidden.visible);
}

TEST_CASE("the lift the sightline asks for is the lift that clears it", "[camera][sightline]") {
    const world::WorldMap map = testMap();
    world::ClearanceField field;
    field.map = &map;
    const auto ridge = findRidge(map, 70.0f, 2.0f);
    if (!ridge) {
        SKIP("this terrain has no ridge at 70 m on that bearing");
    }
    const world::SubjectCapsule subject = subjectAt(ridge->hero);

    // The derivation, as an assertion. A ray to a *fixed* point rises by (1 - s) of whatever the eye
    // rises by, so the lift that clears an obstruction standing h above the ray at s is h / (1 - s).
    // That is an equality, not an approximation, and the test that matters is that applying it
    // actually clears the view rather than getting close to it.
    const world::Sightline before = world::heroSightline(field, ridge->eye, subject);
    REQUIRE(before.visible < 1.0f);
    const world::Sightline after =
        world::heroSightline(field, ridge->eye + glm::vec3(0.0f, before.requiredLift, 0.0f), subject);
    INFO("required " << before.requiredLift << " m, visible " << before.visible << " -> "
         << after.visible);
    CHECK(after.clear());

    // And **less than it asked for is not enough**, which is the control that separates "the number
    // is sufficient" from "the number is arbitrary and large". Half a lift that genuinely had to be
    // that big leaves the view blocked.
    const world::Sightline half =
        world::heroSightline(field, ridge->eye + glm::vec3(0.0f, before.requiredLift * 0.5f, 0.0f), subject);
    INFO("half the lift leaves visible at " << half.visible);
    CHECK(half.visible < 1.0f);
}

TEST_CASE("a hero standing in front of another hero blocks it, and the push clears it",
          "[camera][sightline]") {
    const world::WorldMap map = testMap();
    // Flat-ish ground is not available on a generated map, so the ground is taken out of the answer
    // by putting both the eye and the subject well above whatever is under them. What is being
    // isolated here is the *hero* term, and an arm that could also be firing on terrain is not an
    // isolated arm.
    const glm::vec2 centre = map.min() + map.size * 0.5f;
    const float ground = map.sample(centre, 0.5f).height;

    const glm::vec3 heroBase(centre.x, ground + 60.0f, centre.y);
    const world::SubjectCapsule subject = subjectAt(heroBase);

    // The eye on a real bearing, level with the subject's middle.
    const glm::vec2 bearing(std::cos(2.3f), std::sin(2.3f));
    const glm::vec3 eye(centre.x + bearing.x * 60.0f, ground + 64.0f, centre.y + bearing.y * 60.0f);

    // A blocker exactly halfway, dead on the line.
    world::HeroPoint blocker;
    blocker.name = "visitor";
    blocker.position = (eye + (heroBase + glm::vec3(0.0f, 4.0f, 0.0f))) * 0.5f
                     - glm::vec3(0.0f, 6.0f, 0.0f);
    blocker.radius = 6.0f;
    blocker.height = 12.0f;
    std::vector<world::HeroPoint> heroes{blocker};

    world::ClearanceField field;
    field.map = &map;
    field.heroes = heroes;

    const world::Sightline blocked = world::heroSightline(field, eye, subject);
    INFO("visible " << blocked.visible << ", blocker '" << blocked.blocker << "'");
    CHECK(blocked.visible < 1.0f);
    CHECK(blocked.blocker == "visitor");

    // The control: **the same geometry with the blocker moved aside**. Not "no heroes at all" --
    // that arm would also pass if the query simply refused to look at heroes whose name it did not
    // recognise. The blocker is still there, still the same size, just not in the way.
    std::vector<world::HeroPoint> aside{blocker};
    aside[0].position.x += 40.0f;
    field.heroes = aside;
    const world::Sightline clear = world::heroSightline(field, eye, subject);
    INFO("with the blocker moved aside, visible " << clear.visible);
    CHECK(clear.clear());

    // And the correction the blocked reading asked for restores the view.
    field.heroes = heroes;
    const glm::vec3 moved = eye + glm::vec3(blocked.requiredPush.x, blocked.requiredLift,
                                            blocked.requiredPush.y);
    const world::Sightline after = world::heroSightline(field, moved, subject);
    INFO("moved by lift " << blocked.requiredLift << " push "
         << glm::length(blocked.requiredPush) << "; visible " << after.visible);
    CHECK(after.visible > blocked.visible);
}

TEST_CASE("the subject never blocks itself", "[camera][sightline]") {
    const world::WorldMap map = testMap();
    const glm::vec2 centre = map.min() + map.size * 0.5f;
    const float ground = map.sample(centre, 0.5f).height;
    const glm::vec3 base(centre.x, ground + 40.0f, centre.y);

    world::HeroPoint self;
    self.name = "elder";
    self.position = base;
    self.radius = 3.0f;
    self.height = 8.0f;
    std::vector<world::HeroPoint> heroes{self};

    world::ClearanceField field;
    field.map = &map;
    field.heroes = heroes;

    // A hero is in the hero list *and* is the subject. Without the name match the query reports the
    // subject as standing in front of itself at every sample, which would make every shot in every
    // film obstructed and is the obvious way to write this wrong.
    const glm::vec2 bearing(std::cos(1.7f), std::sin(1.7f));
    const glm::vec3 eye(centre.x + bearing.x * 40.0f, ground + 46.0f, centre.y + bearing.y * 40.0f);
    const world::Sightline s = world::heroSightline(field, eye, subjectAt(base));
    INFO("visible " << s.visible << ", blocker '" << s.blocker << "'");
    CHECK(s.clear());
}

TEST_CASE("clearSightlines moves only the keys that hold a hero and cannot see it",
          "[camera][sightline]") {
    const world::WorldMap map = testMap();
    const auto ridge = findRidge(map, 70.0f, 2.0f);
    if (!ridge) {
        SKIP("this terrain has no ridge at 70 m on that bearing");
    }
    world::ClearanceField field;
    field.map = &map;
    const world::SubjectCapsule subject = subjectAt(ridge->hero);

    // Three keys at the same blocked eye. The middle one holds the hero; the outer two hold nothing,
    // the way a handoff's swing and a Passage do. Only the middle one may move, and the smoothing
    // must not drag its neighbours up with it -- that is the property a positional smoother gets
    // wrong and the reason `clearPath` smooths offsets rather than positions.
    std::vector<glm::vec3> path{ridge->eye, ridge->eye, ridge->eye};
    const std::vector<glm::vec3> was = path;
    std::vector<world::SightlineTarget> targets(3);
    targets[1].holds = true;
    targets[1].subject = subject;

    const world::SightlineResult r = world::clearSightlines(field, path, targets, 10.0f, 0);
    INFO("examined " << r.examined << " obstructed " << r.obstructed << " corrected " << r.corrected
         << " refused " << r.refused);
    CHECK(r.examined == 1);
    CHECK(r.obstructed == 1);
    CHECK(r.corrected == 1);
    CHECK(path[1].y > was[1].y);
    CHECK_THAT(path[0].y, Catch::Matchers::WithinAbs(was[0].y, 1e-4));
    CHECK_THAT(path[2].y, Catch::Matchers::WithinAbs(was[2].y, 1e-4));
}

TEST_CASE("a correction larger than the shot is refused rather than applied",
          "[camera][sightline]") {
    const world::WorldMap map = testMap();
    const auto ridge = findRidge(map, 70.0f, 2.0f);
    if (!ridge) {
        SKIP("this terrain has no ridge at 70 m on that bearing");
    }
    world::ClearanceField field;
    field.map = &map;

    std::vector<glm::vec3> path{ridge->eye};
    std::vector<world::SightlineTarget> targets(1);
    targets[0].holds = true;
    targets[0].subject = subjectAt(ridge->hero);

    // The bound as a measurement, not as a setting: with no budget at all the pass is a pure
    // instrument -- it still finds the obstruction and still says how far it would have had to move
    // -- and the path comes back byte for byte. That is the arm the lab's control case runs, and an
    // implementation that "helpfully" moved the key anyway would fail here rather than quietly
    // making every control arm a duplicate of its treatment.
    const std::vector<glm::vec3> was = path;
    const world::SightlineResult none = world::clearSightlines(field, path, targets, 0.0f, 0);
    CHECK(none.obstructed == 1);
    CHECK(none.corrected == 0);
    CHECK(none.refused == 1);
    CHECK(none.worstRefused > 0.0f);
    CHECK_THAT(path[0].y, Catch::Matchers::WithinAbs(was[0].y, 1e-6));

    // And with a budget large enough it is applied, so the refusal above is the bound doing its job
    // rather than the pass being unable to correct anything.
    const world::SightlineResult wide = world::clearSightlines(field, path, targets, 10.0f, 0);
    CHECK(wide.corrected == 1);
    CHECK(path[0].y > was[0].y);
}

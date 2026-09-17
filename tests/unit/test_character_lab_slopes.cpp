// Character Animation Lab -- zone E, slopes, and §24's rotation test.
//
// The contract, read off `entity/grounding.{hpp,cpp}` rather than assumed: a walker does not become
// part of the terrain. `GroundSettings::slopeAlign` (0.55 by default) is the fraction of the way
// from upright towards the surface normal that the body leans, clamped to `maxTilt` (34 degrees).
// `GroundFollower::update` resolves that lean into the body's own frame -- `wantPitch` from the
// normal's component along the heading, `wantRoll` from its component along the body's right -- and
// the caller adds those to `motion.rotation.x` and `.z`, which are *Euler degrees on the node*.
//
// That last step is where a local/world mistake would live, and it is exactly what §24 asks about.
// Grounding's pitch and roll are body-frame quantities. The node's Euler triple is turned into a
// quaternion by `scene::quatFromEulerDegrees` -- one fixed composition order for everybody -- and
// the yaw that would have to be applied *first* for a body-frame tilt to mean anything shares that
// triple with them.
//
// The invariant with teeth is not "the pitch is 12 degrees". It is geometric and heading-free:
//
//     A body's lean is defined by the ground under it. The terrain normal does not know which way
//     the body is facing, so the body's own up-axis, in world space, must come out the same
//     whichever way it faces.
//
// If the Euler composition applies a body-frame tilt in world axes, that invariant breaks as soon
// as the yaw is not zero, and the symptom is a character that leans correctly walking north and
// leans sideways walking east -- "posing incorrectly on slopes", reported from Glowmere.
//
// ADR-182: the control arm is the flat-ground case. On flat ground the lean is zero for every
// heading, so every heading trivially agrees; if the slope arm also agreed for a *trivial* reason
// -- a tilt that was never applied, a normal that came back vertical -- the control could not tell
// the difference. So the slope arm additionally requires that the body actually be tilted.

#include "entity/grounding.hpp"
#include "entity/navigation.hpp"
#include "scene/composition.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <string>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace avgen;

namespace {

constexpr float kDegrees = 57.29577951f;

struct Bed {
    world::WorldMap map;
    world::Ecology ecology;
    world::ClearanceField clearance;
};

// A world with real relief, built once per case so the navigator can borrow stable pointers.
void makeBed(Bed& bed) {
    bed.map.size = glm::vec2(400.0f, 400.0f);
    bed.map.seaLevel = 4.0f;
    bed.map.layers = {{.frequency = 0.004f, .amplitude = 22.0f}, {.frequency = 0.02f, .amplitude = 6.0f}};
    bed.map.prepare();
    bed.clearance.map = &bed.map;
    bed.clearance.ecology = &bed.ecology;
}

// The world-space up-axis of a body standing at `p` facing `yaw`, as the renderer would build it.
//
// This follows the production path exactly: grounding produces body-frame pitch/roll, `applyOffsets`
// puts them in the node's Euler triple beside the yaw, and `quatFromEulerDegrees` -- the one the
// composition uses for every node -- turns the triple into the rotation the mesh is drawn with.
glm::vec3 bodyUpAt(const entity::Navigator& nav, glm::vec2 p, float yaw, const entity::GroundSettings& gs) {
    entity::GroundFollower follower;
    // Settle the follower: its height and tilt are smoothed, so a single call reports the start of a
    // ramp rather than the steady state. 2 s at 60 Hz is well past the 240 ms slope smoothing.
    entity::GroundResult ground{};
    for (int i = 0; i < 120; ++i) {
        ground = follower.update(nav, p, yaw, 0.0f, 1.0 / 60.0, gs);
    }
    const glm::vec3 euler(ground.pitch, yaw * kDegrees, ground.roll);
    const glm::quat q = scene::quatFromEulerDegrees(euler);
    return glm::normalize(q * glm::vec3(0.0f, 1.0f, 0.0f));
}

// Finds somewhere on the map whose slope is close to `degrees`.
bool findSlope(const world::WorldMap& map, float degrees, float tolerance, glm::vec2& out) {
    const float want = std::cos(degrees / kDegrees);
    float best = 1e9f;
    bool found = false;
    for (float x = -180.0f; x <= 180.0f; x += 3.0f) {
        for (float z = -180.0f; z <= 180.0f; z += 3.0f) {
            const glm::vec2 p(x, z);
            const float got = map.normal(p, 0.5f).y;
            const float err = std::fabs(got - want);
            if (err < best) {
                best = err;
                out = p;
                found = true;
            }
        }
    }
    return found && best < tolerance;
}

} // namespace

// Tagged `[!shouldfail]`: this invariant is CORRECT and the engine does NOT satisfy it.
//
// Catch2 runs the case, expects it to fail, and keeps the suite green -- and shouts if it ever
// starts passing, which is precisely the signal wanted the day the rotation representation is
// fixed. It is not tagged off or deleted, because a defect nobody measures is a defect nobody
// remembers. See ADR-260 for why there is no fix inside the current representation.
TEST_CASE("a body's lean on a slope is the same whichever way it faces",
          "[unit][charlab][slopes][rotation][!shouldfail]") {
    Bed bed;
    makeBed(bed);
    entity::Navigator nav(&bed.map, bed.clearance);
    REQUIRE(nav.valid());
    entity::GroundSettings gs; // shipped defaults: slopeAlign 0.55, maxTilt 34 degrees

    // §24's four headings, plus two off-axis ones. The axis-aligned four can hide an Euler-order
    // mistake whose error happens to vanish at multiples of 90 degrees; 37 and 214 degrees cannot.
    const std::vector<float> yawsDegrees = {0.0f, 90.0f, 180.0f, 270.0f, 37.0f, 214.0f};

    // ---- the control: flat ground -------------------------------------------------------------
    // Somewhere as close to level as this map has. Every heading must agree here, and must agree on
    // "upright" specifically -- which is what stops the slope arm below passing for a trivial reason.
    {
        glm::vec2 flat(0.0f);
        REQUIRE(findSlope(bed.map, 0.0f, 0.02f, flat));
        std::vector<glm::vec3> ups;
        for (const float yaw : yawsDegrees) {
            ups.push_back(bodyUpAt(nav, flat, yaw / kDegrees, gs));
        }
        for (const glm::vec3& up : ups) {
            INFO(fmt::format("flat up ({:.4f}, {:.4f}, {:.4f})", up.x, up.y, up.z));
            CHECK(up.y > 0.999f); // upright, as flat ground demands
        }
    }

    // ---- the arm: real slopes -------------------------------------------------------------------
    for (const float degrees : {10.0f, 20.0f, 30.0f, 40.0f}) {
        glm::vec2 p(0.0f);
        if (!findSlope(bed.map, degrees, 0.03f, p)) {
            WARN(fmt::format("no {:.0f} degree slope on this map; skipped", degrees));
            continue;
        }
        const glm::vec3 normal = bed.map.normal(p, 0.5f);
        const float actual = std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * kDegrees;

        std::vector<glm::vec3> ups;
        for (const float yaw : yawsDegrees) {
            ups.push_back(bodyUpAt(nav, p, yaw / kDegrees, gs));
        }

        // The body must actually be leaning. Without this the agreement below would be satisfied by
        // a body that stayed bolt upright on a 40 degree hill, which is a different bug wearing the
        // same green tick (ADR-182).
        float worstTiltFromVertical = 0.0f;
        for (const glm::vec3& up : ups) {
            worstTiltFromVertical =
                std::max(worstTiltFromVertical, std::acos(std::clamp(up.y, -1.0f, 1.0f)) * kDegrees);
        }
        INFO(fmt::format("slope {:.1f} degrees at ({:.0f},{:.0f}); worst lean {:.2f} degrees", actual,
                         p.x, p.y, worstTiltFromVertical));
        CHECK(worstTiltFromVertical > actual * 0.25f);

        // The invariant. The ground under this point has one normal; the lean it produces is a
        // property of that normal and of `slopeAlign`, not of the body's heading.
        float worstDisagreement = 0.0f;
        std::string perHeading;
        for (std::size_t i = 0; i < ups.size(); ++i) {
            const float angle =
                std::acos(std::clamp(glm::dot(ups[0], ups[i]), -1.0f, 1.0f)) * kDegrees;
            worstDisagreement = std::max(worstDisagreement, angle);
            perHeading += fmt::format("\n    yaw {:6.1f}: up ({:+.4f},{:+.4f},{:+.4f})  lean {:5.2f}  "
                                      "differs from yaw 0 by {:5.2f} degrees",
                                      yawsDegrees[i], ups[i].x, ups[i].y, ups[i].z,
                                      std::acos(std::clamp(ups[i].y, -1.0f, 1.0f)) * kDegrees, angle);
        }
        INFO(perHeading);
        INFO(fmt::format("worst disagreement between headings: {:.3f} degrees", worstDisagreement));
        CHECK(worstDisagreement < 1.0f);
    }
}


TEST_CASE("the node Euler triple gimbal-locks a walking body at due east and due west",
          "[unit][charlab][slopes][rotation]") {
    // Why the case above cannot be fixed where the tilt is computed, rather than merely is not.
    //
    // `scene::quatFromEulerDegrees` is `glm::quat(vec3)`, which composes **Rz(roll) . Ry(yaw) .
    // Rx(pitch)** -- measured against `glm::eulerAngleZYX`, exact to float. ADR-240 relies on that
    // order and writes `eulerDegrees` as its exact inverse, so it is a scene-wide representation
    // contract and not a stray call.
    //
    // Yaw is the **middle** angle of that composition, and a middle angle at +/-90 degrees is
    // gimbal lock. Working the composition through on the body's up-axis:
    //
    //     Rz(r) . Ry(y) . Rx(p) . (0,1,0)
    //         = ( sin p sin y cos r - cos p sin r,
    //             sin p sin y sin r + cos p cos r,
    //             sin p cos y )
    //
    // The z component is `sin(pitch) * cos(yaw)`. At yaw = +/-90 degrees `cos(yaw)` is zero, so **no
    // value of pitch can tilt the body along z at all** -- and roll, the only channel left, acts in
    // world axes. There is no (pitch, roll) that produces the wanted lean: the equation has no
    // solution, not merely an inconvenient one. That is what makes this architectural. A body
    // walking due east or due west is not an edge case; it is a quarter of all headings.
    //
    // This case asserts the lock itself, so the proof above is measured rather than argued.
    Bed bed;
    makeBed(bed);
    entity::Navigator nav(&bed.map, bed.clearance);
    REQUIRE(nav.valid());
    entity::GroundSettings gs;

    glm::vec2 p(0.0f);
    REQUIRE(findSlope(bed.map, 10.0f, 0.03f, p));

    // Due north: the pitch channel has full authority over z, and uses it.
    const glm::vec3 north = bodyUpAt(nav, p, 0.0f, gs);
    CHECK(std::fabs(north.z) > 0.02f);

    // Due east and due west: cos(yaw) is zero and the z lean is annihilated exactly, whatever the
    // ground under the body is doing.
    for (const float yaw : {90.0f, 270.0f}) {
        const glm::vec3 up = bodyUpAt(nav, p, yaw / kDegrees, gs);
        INFO(fmt::format("yaw {:.0f}: up ({:+.6f}, {:+.6f}, {:+.6f})", yaw, up.x, up.y, up.z));
        CHECK(std::fabs(up.z) < 1e-6f);
    }
}

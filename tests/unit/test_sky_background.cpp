// ADR-348: which background the scene pass draws, and the coupling that used to make that one
// choice with "what lights the scene".
//
// Before this, the skybox was always whatever the IBL had been built from. A scene lit by an HDRI
// therefore had the HDRI behind it, with no way to ask for anything else -- so a day/night cycle's
// `zenithColor`, `horizonColor` and `groundColor` were computed, tested, and then had no effect on
// any frame. The feature was not broken; it was unreachable, which is worse, because everything
// still looked plausible.
//
// The predicate is extracted rather than tested through a render because inline in a render pass
// the only way to find out what it does is to render, and a render is exactly what was not
// catching this.

#include "scene/scene_types.hpp"

#include <catch2/catch_test_macros.hpp>

using avgen::scene::Environment;
using avgen::scene::SkyBackground;
using avgen::scene::skyBackgroundFor;

namespace {

// A scene lit by an equirectangular map: `haveIbl` true, `iblFromSky` false.
constexpr bool kFromMap = false;
constexpr bool kFromSky = true;
constexpr bool kHaveIbl = true;

} // namespace

TEST_CASE("An HDRI can light the scene while the procedural sky stands behind it", "[sky][environment]") {
    Environment env;
    env.sky.enabled = true;

    // THE ARM. This is the configuration that had no expressible form before ADR-348.
    env.proceduralSkyBackground = true;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromMap) == SkyBackground::Analytic);

    // THE CONTROL, and the one that fails on the old coupling. With the flag off and everything
    // else identical, the very same environment must still draw the map's cube -- which is what
    // the old code did unconditionally. If this ever returns Analytic the change has stopped being
    // opt-in and every world with an HDRI has silently lost its sky.
    env.proceduralSkyBackground = false;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromMap) == SkyBackground::IblCube);

    // And the arm is genuinely reachable only via the flag: nothing else in a default environment
    // turns it on.
    CHECK(skyBackgroundFor(Environment{}, kHaveIbl, kFromMap) == SkyBackground::IblCube);
}

TEST_CASE("The decoupling changes nothing for a scene without an HDRI", "[sky][environment]") {
    Environment env;
    env.sky.enabled = true;

    // ADR-036's behaviour, unchanged: a procedural sky lights the scene without standing behind it
    // unless the scene asks.
    env.sky.showBackground = false;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromSky) == SkyBackground::FlatColour);
    env.sky.showBackground = true;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromSky) == SkyBackground::IblCube);

    // With the sky already the IBL there is nothing to decouple, so the flag must NOT divert to
    // the analytic path: the cube is what the lighting was built from, so sampling it is both
    // cheaper and guaranteed to agree with the shading. A flag that quietly changed this would
    // make every existing procedural-sky world take a different code path for no benefit.
    env.proceduralSkyBackground = true;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromSky) == SkyBackground::IblCube);
    env.sky.showBackground = false;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromSky) == SkyBackground::FlatColour);
}

TEST_CASE("Nothing draws a sky when there is none to draw", "[sky][environment]") {
    Environment env;
    env.sky.enabled = true;
    env.proceduralSkyBackground = true;

    // No IBL at all: the flat background colour, whatever the flags say. This is the case that was
    // painting the sky for a whole round of renders on the ocean world while every number looked
    // plausible, because the scene carried `skybox: false` inherited from an earlier pass.
    CHECK(skyBackgroundFor(env, /*haveIbl=*/false, kFromMap) == SkyBackground::FlatColour);

    env.showSkybox = false;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromMap) == SkyBackground::FlatColour);
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromSky) == SkyBackground::FlatColour);

    // `sky.enabled` false means there is no analytic sky to evaluate, so the flag cannot select it
    // -- asking for a sky that does not exist must fall through to the map, not to black.
    env.showSkybox = true;
    env.sky.enabled = false;
    CHECK(skyBackgroundFor(env, kHaveIbl, kFromMap) == SkyBackground::IblCube);
}

TEST_CASE("The three backgrounds are exclusive and cover every combination", "[sky][environment]") {
    // The exhaustive sweep. Sixteen combinations of the four inputs, each landing on exactly one
    // answer -- which is the property that makes the enum safe to switch on, and the thing an
    // inline boolean expression could never be checked for.
    int analytic = 0;
    int cube = 0;
    int flat = 0;
    for (const bool haveIbl : {false, true}) {
        for (const bool fromSky : {false, true}) {
            for (const bool procedural : {false, true}) {
                for (const bool showBackground : {false, true}) {
                    Environment env;
                    env.sky.enabled = true;
                    env.proceduralSkyBackground = procedural;
                    env.sky.showBackground = showBackground;
                    switch (skyBackgroundFor(env, haveIbl, fromSky)) {
                    case SkyBackground::Analytic: ++analytic; break;
                    case SkyBackground::IblCube: ++cube; break;
                    case SkyBackground::FlatColour: ++flat; break;
                    }
                }
            }
        }
    }
    CHECK(analytic + cube + flat == 16);
    // The control on the sweep itself: all three answers must actually occur. A predicate that
    // returned one constant would satisfy the sum above and nothing else in this file's arms.
    CHECK(analytic > 0);
    CHECK(cube > 0);
    CHECK(flat > 0);
    // Analytic is reachable only with an IBL that is not the sky and the flag on: 2 of 16.
    CHECK(analytic == 2);
}

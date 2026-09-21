// What the artist actually gets when they press "Add fog bank" (ADR-562, ADR-579).
//
// Every existing fog test builds its bank and then APPLIES A NAMED STYLE, or authors the field
// outright from an example scene. None of them asks the one question the user asked: press the
// button, look at the scene, is anything there? That gap is ADR-401's shape -- a suite green about
// a path nobody walks -- and it is why "the fog bank does nothing" could ship past a full suite.
//
// The chain under test is the shipped one, end to end and with no copy of it here: the registry's
// factory is what `world_effects_panel.cpp` calls on the button, and `buildAtmosphericFrame` is
// what the engine calls every frame. `VolumeRenderer::enabled(scene)` marches when `mediumCount > 0`,
// and the march gates on lane 0's `w` -- the radius, where 0 means off (`core/vortex.hpp:46`).
// So "visible" here means: a medium was seated, its gate is open, and it has optical depth.
//
// How it fails: set `VortexField::radius`'s default back to 0 and drop the style's assignment, or
// give the factory a fade-in with no envelope at t=0, and the case names which of the three it was.

#include "core/vortex.hpp"
#include "world/atmospherics.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>

using namespace avgen;

namespace {

// Exactly the call the Add button makes: `append(makeAtmosphericEffect(schema->kind, ...))`.
world::AtmosphericEffect addedFromPanel() {
    const world::EffectSchema* s = world::effectSchema(world::AtmosphereKind::VolumetricFog);
    REQUIRE(s != nullptr);
    REQUIRE(s->factory != nullptr); // no factory means no button at all
    return s->factory("Fog Bank");
}

world::AtmosphericFrame frameAt(const world::AtmosphericEffect& e, double seconds) {
    world::AtmosphericContext ctx;
    ctx.seconds = seconds;
    world::AtmosphericFrame f{};
    world::buildAtmosphericFrame(std::span(&e, 1), ctx, f);
    return f;
}

} // namespace

TEST_CASE("a fog bank added from the panel is visible without touching anything", "[fog][panel]") {
    const world::AtmosphericEffect e = addedFromPanel();

    // 1. The effect itself is live. An effect added switched off is a button that does nothing.
    CHECK(e.enabled);

    // 2. The gate is open. `radius` defaults to 0 and 0 is off, so the factory must set it; if it
    //    does not, every later number is packed into a slot the march skips.
    CHECK(e.vortex.field.radius > 0.0f);

    // 3. There is something to see through. Extinction per metre, ADR-374.
    CHECK(e.vortex.density > 0.0f);

    // 4. A slot is seated, and nothing was dropped on the way.
    const world::AtmosphericFrame f = frameAt(e, 0.0);
    CHECK(f.mediumCount > 0);
    CHECK(f.mediaDropped == 0);
}

TEST_CASE("a fog bank is visible at the moment it is added, not three seconds later", "[fog][panel]") {
    // This is the case that reproduced the bug. With the factory's former `timing.fadeIn = 3.0`
    // the measured curve was:
    //
    //     t=0      mediumCount=0   radius=0      density=0
    //     t=0.001  mediumCount=0   radius=0      density=0
    //     t=0.25   mediumCount=1   radius=1800   density=2.24e-06
    //     t=1      mediumCount=1   radius=1800   density=2.95e-05
    //     t=3      mediumCount=1   radius=1800   density=1.14e-04   <- fully faded in
    //
    // Note the first column and not the last: a fade does not make a new bank FAINT at the origin,
    // it makes it ABSENT, because an envelope at or below 1e-4 is dropped before a slot is taken
    // (`atmospherics.cpp:736`). The density at the end of the fade was always fine -- 1.14e-04 per
    // metre across a 1800 m bank is an optical depth around 0.4, a haze you can plainly see. The
    // defect was a three-second hole at frame 0, which is where a scene opens.
    const world::AtmosphericEffect e = addedFromPanel();

    const world::AtmosphericFrame at0 = frameAt(e, 0.0);
    const world::AtmosphericFrame at3 = frameAt(e, 3.0);

    // A seat is taken whatever the clock says -- the envelope scales the picture, it does not
    // decide whether a medium exists. If this fails, the fade is removing the slot rather than
    // dimming it, and the two are different defects.
    CHECK(at0.mediumCount == at3.mediumCount);

    // Lane 1's `w` is DENSITY with the envelope folded in (the lane map in
    // `volumetric_fog_effect.cpp`). At a full fade-in it must be non-zero; that is the thing the
    // march integrates, and a zero here is an invisible bank however open the gate is.
    REQUIRE(at3.mediumCount > 0);
    CHECK(at3.media[0].lane[1].w > 0.0f);

    // And the moment of the press. A zero here is not a bug in the fade -- it is the fade working
    // exactly as written -- but it IS the whole of "I added it and saw no effect", so it is stated
    // rather than left to be rediscovered.
    REQUIRE(at0.mediumCount > 0);
    INFO("density at t=0 (the moment of the press): " << at0.media[0].lane[1].w);
    INFO("density at t=3 (the fade-in's end):       " << at3.media[0].lane[1].w);
    CHECK(at0.media[0].lane[1].w > 0.0f);
}

// The auto-exposure meter across a discontinuity (ADR-398).
//
// `scene::PostSettings::exposureReset` has carried the comment "re-seed the meter (scene change,
// timeline seek)" since it was written. Only the first half was true: the one-shot is raised by
// `Engine::resetCameraState`, and that function had exactly one caller -- the composition install.
// `seekSeconds` reset the modulators, the sources, the music detector, the cue state, the director
// and every entity, and left the meter on its pre-seek reading. Scrub from a night interior to a
// noon exterior and the first frames of the new shot were exposed for the old one, then walked out
// of it over the meter's adaptation time.
//
// ADR-385, which is the reason this file exists rather than a fix on its own: the comment was the
// only thing asserting the behaviour, and the comment was wrong.
//
// No device is needed. The meter's re-seed is a flag on a settings struct, and a rule that needs a
// GPU to check is a rule nobody checks.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/timeline.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

namespace {

// No audio source, so no device is opened.
struct Meter {
    app::Engine engine{app::EngineMode::Live};
    FixedStepClock clock{60.0};

    Meter() {
        // A length from something other than a wav, so no device is opened and the transport still
        // has somewhere to seek to. A transport that clamped a seek back to where the playhead
        // already was would make every assertion below vacuous, which is why the tests check that
        // the playhead moved rather than trusting the call.
        params::Track track;
        track.target = "orb/scale";
        track.addKey({.time = 0.0, .value = {1.0f}});
        track.addKey({.time = 30.0, .value = {2.0f}});
        engine.timeline().enabled = true;
        engine.timeline().addTrack(track);
        engine.rebind();
        engine.refreshTransport();
    }

    void step() {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
    }
    [[nodiscard]] bool reset() const { return engine.post().exposureReset; }
};

} // namespace

TEST_CASE("a timeline seek re-seeds the exposure meter", "[engine][exposure][determinism]") {
    Meter m;
    REQUIRE(m.engine.durationSeconds() > 15.0);

    // Frame one carries the reset the engine is constructed with: nothing has been metered yet, so
    // there is nothing to adapt from.
    m.step();
    CHECK(m.reset());

    // ...and it is a ONE-SHOT. This is the control, and it is the assertion that stops the test
    // passing against an implementation that simply leaves the flag raised forever -- which would
    // re-seed the meter on every frame and remove auto-exposure from the engine entirely.
    m.step();
    CHECK_FALSE(m.reset());

    // The defect. A seek is a discontinuity: the frames you came from are not this frame's past.
    const double before = m.engine.positionSeconds();
    m.engine.seekSeconds(12.0);
    REQUIRE(m.engine.positionSeconds() > before + 1.0); // the playhead really moved
    m.step();
    CHECK(m.reset());

    // Still a one-shot on the far side, or the seek has traded one defect for the other.
    m.step();
    CHECK_FALSE(m.reset());
}

TEST_CASE("re-rendering the same second is not a seek", "[engine][exposure][determinism]") {
    // The distinction `core/pre_roll.hpp` exists to carry, applied here: a JUMP drops the state
    // that was a function of the frames you came from; a REPEAT has to reproduce. A seek to where
    // the playhead already is moves nothing, and re-seeding the meter for it would make a frame
    // depend on whether somebody clicked the playhead rather than on the second it names.
    //
    // This is the assertion that would fail if `resetCameraState` were moved into the per-frame
    // update instead of into the seek.
    Meter m;
    m.step();
    m.step();
    REQUIRE_FALSE(m.reset());

    const double where = m.engine.positionSeconds();
    m.step();
    CHECK_FALSE(m.reset());
    CHECK(m.engine.positionSeconds() >= where);
}

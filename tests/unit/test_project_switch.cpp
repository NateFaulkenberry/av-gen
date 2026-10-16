// ADR-1390: the rule for what the editor's overlays are when a project opens. The GPU half -- that no project's
// picture depends on the one before it -- is tests/rendering/test_project_switch_gpu.cpp; this is the rule itself.

#include "app/project_switch.hpp"
#include "labs/lab.hpp"
#include "labs/overlays.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

TEST_CASE("A lab's overlays last as long as the lab's project, and no longer", "[switch][labs]") {
    rendering::DebugViewOptions session;
    session.entityBounds = true; // `--debug-draw entityBounds`: the person's choice for the whole run
    app::ProjectSwitch sw(session);
    sw.sync(1);
    CHECK_FALSE(sw.observe(1).has_value()); // the same project: nothing to do

    // Engineering Labs > Lighting: its overlays, for its fixture.
    sw.expectOverlays(labs::overlaysFor(labs::LabId::Lighting));
    CHECK(sw.expecting());
    const auto lab = sw.observe(2);
    REQUIRE(lab.has_value());
    CHECK(lab->lights);
    CHECK(lab->worldAxes);
    CHECK_FALSE(sw.expecting());
    CHECK_FALSE(sw.observe(2).has_value());

    // File > Examples > anything: the session's overlays again -- the Lighting Lab's axes and light arrows were what
    // the owner saw drawn through Astral Forge.
    const auto next = sw.observe(3);
    REQUIRE(next.has_value());
    CHECK_FALSE(next->lights);
    CHECK_FALSE(next->worldAxes);
    CHECK(next->entityBounds);
}

TEST_CASE("A lab whose fixture did not open leaves no overlays for the next project", "[switch][labs]") {
    app::ProjectSwitch sw;
    sw.sync(5);
    sw.expectOverlays(labs::overlaysFor(labs::LabId::Animation));
    sw.cancelExpected(); // the open failed: the generation never moved
    const auto next = sw.observe(6);
    REQUIRE(next.has_value());
    CHECK_FALSE(next->skeletons);
}

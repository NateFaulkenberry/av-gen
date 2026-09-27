// UI reach for ADR-907 to 909: every new control a character's walk, turn, pause and pace answer to
// is a registered parameter, named for what the viewer sees, and found in both places an artist
// looks -- the Parameters panel and the World panel Inspector for the character they clicked.
//
// The owner's rule: anything visible in the picture must be findable in the UI under a name for what
// the viewer sees, and a control that exists but that nobody can connect to what they are looking at
// is a defect. This does each panel's own arithmetic on the registered set, as the emission stream's
// reach test does (test_emission_lanes.cpp), rather than asserting that a path was registered:
//
//   Parameters panel   group `entity` (on the Intermediate layer, which the editor opens on), then
//                      `parameterSubGroup` as the section heading, then the label as the row
//   Inspector          a click on a character selects the node it drives (`EntityDesc::driven`), and
//                      the character's section lists `entityInspectorPrefix(name)`: heading and row
//                      from `inspectorPlace` and `inspectorRowLabel`, as world_panel.cpp draws them
//
// And one arm the registry alone cannot give: the gait's turn knobs are read back from their
// parameters every step (`Entity::refreshGait`), a path of their own, so a slider moved in either
// panel must move the body -- measured on a trajectory, against the same body left alone.

#include "entity/entity.hpp"
#include "support/cast_world.hpp"
#include "ui/ui_logic.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace avgen;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

// A grazing animal and a deciding alien with every ADR-907 to 909 knob authored, so each registers.
std::vector<CastMember> cast() {
    CastMember cow;
    cow.name = "cow";
    cow.gait.walkSpeed = 1.4f; // an authored gait: the turn knobs register only for a body that walks
    cow.behaviors.push_back(behavior("wander", {{"speed", 1.4}, {"maxSlope", 12.0}, {"turnRadius", 2.0}}));

    CastMember alien;
    alien.name = "rook";
    alien.at = glm::vec3(30.0f, 0.0f, 0.0f);
    alien.gait.walkSpeed = 2.0f;
    alien.gait.turnRadius = 1.5f;
    alien.behaviors.push_back(behavior(
        "decide",
        {{"maxStillSeconds", 8.0},
         {"considerers",
          {{{"kind", "holdPost"}, {"name", "post"}, {"tolerance", 6.0}, {"duration", 3.0}},
           {{"kind", "interest"}, {"name", "graze"}, {"speedRange", {0.7, 1.3}}},
           {{"kind", "react"}, {"name", "beam"}, {"urgentSpeed", 1.6}}}}}));
    return {cow, alien};
}

struct Reach {
    const char* entity;
    const char* path;
    const char* label;   // the row in both panels
    const char* section; // the Parameters panel's heading under `entity`
    const char* heading; // the Inspector's heading in the character's section
    const char* row;     // ...and its row there
};

} // namespace

TEST_CASE("UI reach: every walk, turn, pause and pace control is named and found where an artist looks",
          "[ui][entity][adr907][adr908][adr909]") {
    CastWorld w(cast());
    const std::vector<Reach> reach{
        // ADR-907: where a wanderer goes and how it arrives.
        {"cow", "entity/cow/wander/headingSpread", "how far off straight ahead it wanders (deg)", "cow/wander",
         "wander", "how far off straight ahead it wanders (deg)"},
        {"cow", "entity/cow/wander/maxSlope", "steepest ground it walks on (deg, 0 = any)", "cow/wander", "wander",
         "steepest ground it walks on (deg, 0 = any)"},
        {"cow", "entity/cow/wander/arrival", "slows down over the last (m)", "cow/wander", "wander",
         "slows down over the last (m)"},
        {"cow", "entity/cow/wander/arrive", "counts as arrived within (m)", "cow/wander", "wander",
         "counts as arrived within (m)"},
        {"cow", "entity/cow/wander/pauseMax", "longest pause between walks (s)", "cow/wander", "wander",
         "longest pause between walks (s)"},
        {"cow", "entity/cow/wander/speed", "walking speed (metres a second)", "cow/wander", "wander",
         "walking speed (metres a second)"},
        // ADR-908: how it turns.
        {"cow", "entity/cow/wander/turnRadius", "turn radius (m, 0 = from speed and turn rate)", "cow/wander",
         "wander", "turn radius (m, 0 = from speed and turn rate)"},
        {"cow", "entity/cow/wander/turnRate", "turn rate (degrees a second)", "cow/wander", "wander",
         "turn rate (degrees a second)"},
        {"rook", "entity/rook/gait/turnRate", "turn rate on errands (degrees a second, 0 = the default 140)",
         "rook/gait", "gait", "turn rate on errands (degrees a second, 0 = the default 140)"},
        {"rook", "entity/rook/gait/turnRadius", "turn radius on errands (m, 0 = turns on the spot)", "rook/gait",
         "gait", "turn radius on errands (m, 0 = turns on the spot)"},
        {"rook", "entity/rook/gait/pivotRadius", "turning on the spot, its feet circle at (m)", "rook/gait", "gait",
         "turning on the spot, its feet circle at (m)"},
        // ADR-909: how long it stands, where it will not go back to, and at what pace.
        {"rook", "entity/rook/decide/maxStillSeconds", "longest it stands still (s, 0 = no limit)", "rook/decide",
         "decide", "longest it stands still (s, 0 = no limit)"},
        {"rook", "entity/rook/decide/loopSeconds", "won't walk back to where it just was for (s)", "rook/decide",
         "decide", "won't walk back to where it just was for (s)"},
        {"rook", "entity/rook/decide/loopRadius", "counts as back where it was within (m)", "rook/decide",
         "decide", "counts as back where it was within (m)"},
        {"rook", "entity/rook/decide/loopPenalty", "a walk straight back is worth (x its score, 0 = never)",
         "rook/decide", "decide", "a walk straight back is worth (x its score, 0 = never)"},
        {"rook", "entity/rook/decide/post/duration", "stays at its post for (s, 0 = until something better)",
         "rook/decide/post", "decide", "post/stays at its post for (s, 0 = until something better)"},
        {"rook", "entity/rook/decide/graze/paceFrom", "slowest pace (x walk speed, 0 = walk)", "rook/decide/graze",
         "decide", "graze/slowest pace (x walk speed, 0 = walk)"},
        {"rook", "entity/rook/decide/graze/paceTo", "fastest pace (x walk speed, 0 = walk)", "rook/decide/graze",
         "decide", "graze/fastest pace (x walk speed, 0 = walk)"},
        {"rook", "entity/rook/decide/beam/paceFrom", "slowest pace (x walk speed, 0 = walk)", "rook/decide/beam",
         "decide", "beam/slowest pace (x walk speed, 0 = walk)"},
        {"rook", "entity/rook/decide/beam/urgentSpeed", "hurries at full alarm (x its pace)", "rook/decide/beam",
         "decide", "beam/hurries at full alarm (x its pace)"},
    };
    for (const Reach& r : reach) {
        INFO(r.path);
        params::IParameter* p = w.params.find(r.path);
        REQUIRE(p != nullptr);
        CHECK(p->flags().exposed);
        CHECK(p->flags().modulatable);
        CHECK(p->flags().serialized);
        // The Parameters panel: the `entity` group, the middle of the path as its heading, the label.
        CHECK(p->group() == "entity");
        CHECK(p->label() == r.label);
        CHECK(ui::parameterSubGroup(p->path(), p->group()) == r.section);
        // ...listed on the layer the editor opens on, and not only on Advanced.
        CHECK(ui::layerShowsPath(ui::AuthoringLayer::Intermediate, p->path()));
        // The World panel Inspector: a click on the character selects the node it drives, and the
        // character's own section lists this under its heading and row.
        const entity::Entity& body = w.body(r.entity);
        CHECK(body.desc().driven() == r.entity);
        const std::string prefix = ui::entityInspectorPrefix(body.name());
        REQUIRE(p->path().rfind(prefix, 0) == 0);
        const ui::InspectorPlace place = ui::inspectorPlace(p->path(), prefix);
        CHECK(place.heading == r.heading);
        CHECK(ui::inspectorRowLabel(p->path(), place.cut, p->label()) == r.row);
    }
    // The controls that stay where they were: a node's own transform is still the Properties
    // section's plain row, and the layer filter still filters.
    const ui::InspectorPlace transform = ui::inspectorPlace("nodes/cow/position", "nodes/cow/");
    CHECK(transform.heading.empty());
    CHECK(ui::inspectorRowLabel("nodes/cow/position", transform.cut, "position") == "position");
    CHECK_FALSE(ui::layerShowsPath(ui::AuthoringLayer::Beginner, "entity/cow/wander/maxSlope"));
    CHECK_FALSE(ui::layerShowsPath(ui::AuthoringLayer::Intermediate, "entityish/x"));
}

TEST_CASE("the gait's turn knobs, moved as parameters, move the body",
          "[entity][gait][adr908]") {
    // A body walking north at pace is told to walk back south, as in the ADR-908 half-circle arm.
    // The file authors no turn radius; the only difference between the two runs is the parameter,
    // set the way a slider, a track or a route sets it. `refreshGait` reads it back every step.
    const auto run = [](float radiusParameter) {
        CastMember m;
        m.name = "walker";
        m.gait.walkSpeed = 1.5f;
        m.gait.accel = 3.0f;
        m.gait.decel = 4.0f;
        CastWorld w({m});
        auto* radius = w.params.findAs<float>("entity/walker/gait/turnRadius");
        REQUIRE(radius != nullptr);
        CHECK(radius->value() == 0.0f); // the file's value: none
        if (radiusParameter > 0.0f) {
            radius->setBase(radiusParameter);
        }
        entity::Entity& e = w.body("walker");
        entity::ActionDesc north;
        north.kind = entity::ActionKind::Move;
        north.target.kind = entity::TargetKind::Point;
        north.target.point = glm::vec3(0.0f, 0.0f, 40.0f);
        e.actions().push(north, entity::Authority::Routine);
        w.play(5.0);
        entity::ActionDesc south = north;
        south.target.point = glm::vec3(0.0f, 0.0f, -10.0f);
        e.actions().override({south}, entity::Authority::Routine, w.time());
        float swing = 0.0f;
        w.play(30.0, [&] { swing = std::max(swing, std::abs(e.state().position().x)); });
        return std::pair(swing, w.quality("walker"));
    };
    const auto [curved, curvedQ] = run(2.5f);
    const auto [pivoted, pivotedQ] = run(0.0f);
    WARN(fmt::format("gait/turnRadius parameter 2.5: swung {:.2f} m, {:.0f} deg turned standing; "
                     "left at 0: swung {:.2f} m, {:.0f} deg turned standing",
                     curved, curvedQ.motion.pivotYawDegrees, pivoted, pivotedQ.motion.pivotYawDegrees));
    CHECK(curved > 4.0f);                          // the half circle the parameter asked for
    CHECK(curvedQ.motion.pivotYawDegrees < 5.0);
    CHECK(pivoted < 0.5f);                         // the control: the file's pivot and go
    CHECK(pivotedQ.motion.pivotYawDegrees > 90.0);
}

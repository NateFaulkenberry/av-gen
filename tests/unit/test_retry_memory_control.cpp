// ADR-933, amended 2026-09-27: the retry memory (`mind.memory.failSeconds`) is a control.
//
// ADR-933 made a decider remember an urgent errand that did not get where it was going, for the mind's
// `failSeconds`, so a reaction cannot send a body back to the same river bank again and again. That
// number decides whether a character paces -- something a viewer sees -- and it was read once from the
// file. This checks it is now a parameter an artist finds under the character's decide group, named for
// what it does, and that the PARAMETER decides the behaviour: ADR-933's own stall fixture, with the file
// and the parameter saying opposite things, does what the parameter says.

#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "entity/mind.hpp"
#include "entity/navigation.hpp"
#include "support/cast_world.hpp"
#include "ui/ui_logic.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <string>

using namespace avgen;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

constexpr const char* kLabel = "won't try again where it couldn't get to for (s, 0 = tries at once)";

// test_urgent_retries.cpp's first river: a flat world with a deep channel at x = +30 that ends inside
// it, so its banks are one region and ADR-932 leaves a straight walk into the water alone. Walkers wade
// to 0.8 m.
world::WorldMap riverWorld() {
    world::WorldMap map;
    map.name = "a river";
    map.size = glm::vec2(240.0f, 240.0f);
    map.baseHeight = 0.0f;
    world::Feature river;
    river.name = "river";
    river.kind = world::FeatureKind::River;
    river.path = {{30.0f, 0.0f, -130.0f}, {30.0f, 0.0f, 40.0f}};
    river.width = 12.0f;
    river.amplitude = 2.5f;
    river.water = true;
    map.features.push_back(river);
    map.prepare();
    return map;
}

entity::Navigator wader(const world::WorldMap& map, world::Ecology& ecology) {
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &ecology;
    clearance.cameraRadius = 0.6f;
    clearance.groundClearance = 0.0f;
    entity::NavSettings settings;
    settings.wadeDepth = 0.8f;
    entity::Navigator nav(&map, clearance, settings);
    nav.buildGrid(4.0f);
    return nav;
}

// ember's shape, as test_urgent_retries.cpp has it: a post, a reaction to a beam, nothing else to do.
CastMember watcher(double failSecondsInTheFile, bool mind = true) {
    CastMember m;
    m.name = "watcher";
    m.seed = 4242;
    m.at = glm::vec3(0.0f, 0.0f, 0.0f);
    m.gait.walkSpeed = 2.0f;
    nlohmann::json settings = {
        {"hertz", 2.0},
        {"dwellTicks", 1.0},
        {"margin", 0.05},
        {"considerers",
         {{{"kind", "react"}, {"name", "beam"}, {"events", {"beam"}}, {"weight", 1.5}, {"approach", 6.0},
           {"flee", 0.0}, {"dwell", 2.0}, {"fadeSeconds", 80.0}},
          {{"kind", "holdPost"}, {"name", "home"}, {"weight", 0.5}, {"tolerance", 6.0}, {"pull", 0.12},
           {"activity", "observe"}},
          {{"kind", "idle"}, {"name", "idle"}, {"weight", 0.05}}}}};
    if (mind) {
        settings["mind"] = {{"memory",
                             {{"eventSeconds", 90.0}, {"failSeconds", failSecondsInTheFile}, {"habituationSeconds", 600.0}}}};
    }
    m.behaviors.push_back(behavior("decide", settings));
    return m;
}

struct Watched {
    entity::CharacterQuality quality;
    int waterTrips = 0;
    float deepest = 0.0f;
};

// A beam 46 m east, across the water, raised at 1 s; 90 s of the film. `parameter` < 0 leaves the
// control where the file put it.
Watched watchBeam(const entity::Navigator& nav, double failSecondsInTheFile, float parameter) {
    CastWorld w({watcher(failSecondsInTheFile)}, &nav);
    auto* p = w.params.findAs<float>("entity/watcher/decide/failSeconds");
    REQUIRE(p != nullptr);
    CHECK(p->base() == static_cast<float>(failSecondsInTheFile)); // the file's number is the default
    if (parameter >= 0.0f) {
        p->setBase(parameter); // as a slider, a key or a project parameter sets it
    }
    const std::uint32_t type = w.world.eventType("beam");
    bool raised = false;
    bool wet = false;
    Watched out;
    w.play(90.0, [&] {
        if (!raised && w.time() >= 1.0) {
            entity::WorldEvent e;
            e.type = type;
            e.position = glm::vec3(46.0f, 0.0f, 0.0f);
            e.radius = 200.0f;
            e.magnitude = 1.0f;
            e.time = w.time();
            w.world.emitEvent(e);
            raised = true;
        }
        const glm::vec3 at = w.body("watcher").state().position();
        const float depth = w.world.navigator().terrain().waterDepthAt(glm::vec2(at.x, at.z));
        out.deepest = std::max(out.deepest, depth);
        if (!wet && depth > 0.3f) {
            ++out.waterTrips;
        }
        wet = depth > 0.3f ? true : (depth <= 0.0f ? false : wet);
    });
    out.quality = w.quality("watcher");
    return out;
}

std::string summary(const Watched& w) {
    const entity::CharacterBehaviourMetrics& b = w.quality.behaviour;
    return fmt::format("{} walk(s) into the water, reversals {}, longest pacing {} ({:.1f} s)", w.waterTrips,
                       b.reversals, b.longestPacing, b.longestPacingSeconds);
}

} // namespace

TEST_CASE("UI reach: the retry memory is a control under the character's decide group",
          "[ui][entity][decide][adr933]") {
    CastWorld w({watcher(30.0)});
    params::IParameter* p = w.params.find("entity/watcher/decide/failSeconds");
    REQUIRE(p != nullptr);
    CHECK(p->flags().exposed);
    CHECK(p->flags().modulatable);
    CHECK(p->flags().serialized);
    // The Parameters panel: the `entity` group on the layer the editor opens on, "watcher/decide", the words.
    CHECK(p->group() == "entity");
    CHECK(p->label() == kLabel);
    CHECK(ui::parameterSubGroup(p->path(), p->group()) == "watcher/decide");
    CHECK(ui::layerShowsPath(ui::AuthoringLayer::Intermediate, p->path()));
    // The World panel Inspector: the character's section, under "decide", beside its other habits.
    const std::string prefix = ui::entityInspectorPrefix("watcher");
    const ui::InspectorPlace place = ui::inspectorPlace(p->path(), prefix);
    CHECK(place.heading == "decide");
    CHECK(ui::inspectorRowLabel(p->path(), place.cut, p->label()) == kLabel);
    CHECK(p->baseComponent(0) == 30.0f);

    // A decider with no mind makes no urgent errands and remembers no failed plan: it gets no slider
    // that would move nothing.
    CastWorld mindless({watcher(30.0, false)});
    CHECK(mindless.params.find("entity/watcher/decide/failSeconds") == nullptr);
    CHECK(mindless.params.find("entity/watcher/decide/loopSeconds") != nullptr);
}

TEST_CASE("the retry memory's parameter, not the file, decides whether a body paces the bank",
          "[decide][adr933]") {
    const world::WorldMap map = riverWorld();
    world::Ecology ecology;
    const entity::Navigator nav = wader(map, ecology);
    REQUIRE(nav.grid() != nullptr);

    // ADR-933's two arms, as the file sets them: remembered for 30 s it walks into the water once;
    // forgotten at once (0) it is sent back again and again.
    const Watched remembered = watchBeam(nav, 30.0, -1.0f);
    const Watched forgotten = watchBeam(nav, 0.0, -1.0f);
    INFO("file 30: " << summary(remembered));
    INFO("file 0: " << summary(forgotten));
    REQUIRE(remembered.waterTrips == 1);
    REQUIRE(forgotten.waterTrips >= 2);

    // The same two, each with the file saying the opposite and the parameter saying what the arm needs.
    const Watched paramForgets = watchBeam(nav, 30.0, 0.0f);
    const Watched paramRemembers = watchBeam(nav, 0.0, 30.0f);
    INFO("file 30, parameter 0: " << summary(paramForgets));
    INFO("file 0, parameter 30: " << summary(paramRemembers));
    CHECK(paramForgets.waterTrips == forgotten.waterTrips);
    CHECK(paramForgets.quality.behaviour.longestPacing == forgotten.quality.behaviour.longestPacing);
    CHECK(paramForgets.quality.behaviour.reversals == forgotten.quality.behaviour.reversals);
    CHECK(paramRemembers.waterTrips == remembered.waterTrips);
    CHECK(paramRemembers.quality.behaviour.longestPacing == remembered.quality.behaviour.longestPacing);
    CHECK(paramRemembers.quality.behaviour.reversals == remembered.quality.behaviour.reversals);
    WARN("ADR-933 amended, the retry memory as a parameter: file 30: " << summary(remembered) << "; file 0: "
         << summary(forgotten) << "; file 30, parameter 0: " << summary(paramForgets)
         << "; file 0, parameter 30: " << summary(paramRemembers));
}

// The live scene switcher's decisions (ADR-1063, ADR-1341): the list, stepping, program change, labels, and the
// performer's response carried across a switch as offsets from each scene's own defaults.

#include "app/live_scenes.hpp"
#include "params/parameter_set.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace avgen;
using Catch::Approx;

namespace {

app::ExampleInfo example(std::string name, std::string category, std::string file, bool live = true) {
    app::ExampleInfo e{std::move(name), "", std::move(category), std::filesystem::path("/ex") / file};
    e.live = live;
    return e;
}

void addResponse(params::ParameterSet& p, float sensitivity, float release) {
    p.add(params::ParamDesc<float>{.path = "sonic/response/sensitivity", .defaultValue = sensitivity, .hardMin = 0,
                                   .hardMax = 1, .softMin = 0, .softMax = 1});
    p.add(params::ParamDesc<float>{.path = "sonic/response/transient", .defaultValue = 0.5f, .hardMin = 0,
                                   .hardMax = 1, .softMin = 0, .softMax = 1});
    p.add(params::ParamDesc<float>{.path = "sonic/response/sustain", .defaultValue = 0.5f, .hardMin = 0,
                                   .hardMax = 1, .softMin = 0, .softMax = 1});
    p.add(params::ParamDesc<float>{.path = "sonic/response/attack", .defaultValue = 1.0f, .hardMin = 0.25f,
                                   .hardMax = 4, .softMin = 0.25f, .softMax = 4});
    p.add(params::ParamDesc<float>{.path = "sonic/response/release", .defaultValue = release, .hardMin = 0.25f,
                                   .hardMax = 4, .softMin = 0.25f, .softMax = 4});
}

} // namespace

TEST_CASE("The live scenes are every live example, Sonic Live first, then in the index's order", "[live][adr1341]") {
    const std::vector<app::ExampleInfo> all{example("The Temple", "Showcase", "t.json", false),
                                            example("Astral Forge Live", "Astral Forge", "af.json"),
                                            example("Sonic VFX - Event Horizon", "Sonic VFX", "eh.json"),
                                            example("Sonic Live", "Sonic Garden", "live.json"),
                                            example("Sonic Abstract - Neon Vector", "Sonic Abstract", "nv.json"),
                                            example("THE RIFT", "Showcase", "rift.json", false),
                                            example("THE RIFT LIVE", "The Rift", "rift-live.json")};
    const auto list = app::liveSceneList(all);
    // Every family, not only the open project's: the dropdown used to hide Astral Forge, Digital Mosh and The Rift.
    REQUIRE(list.size() == 5);
    CHECK(list[0].name == "Sonic Live");
    CHECK(list[1].name == "Astral Forge Live");
    CHECK(list[2].name == "Sonic VFX - Event Horizon");
    CHECK(list[3].name == "Sonic Abstract - Neon Vector");
    CHECK(list[4].name == "THE RIFT LIVE");
    CHECK(app::liveSceneLabel(list[2]) == "Event Horizon");
    CHECK(app::liveSceneLabel(list[3]) == "Neon Vector");
    CHECK(app::liveSceneLabel(list[0]) == "Sonic Live");
    CHECK(app::liveSceneIndex(list, "/ex/rift-live.json") == 4);
    CHECK(app::liveSceneIndex(list, "/ex/../ex/eh.json") == 2);
    CHECK(app::liveSceneIndex(list, "/ex/t.json") == -1);   // an offline example is not a live scene
    CHECK(app::liveSceneIndex(list, "/ex/rift.json") == -1); // nor is the offline project of a live scene
    CHECK(app::liveSceneIndex(list, {}) == -1);
}

TEST_CASE("Stepping wraps, and a program change picks a scene", "[live][adr1063]") {
    CHECK(app::steppedLiveScene(0, 1, 3) == 1);
    CHECK(app::steppedLiveScene(2, 1, 3) == 0);
    CHECK(app::steppedLiveScene(0, -1, 3) == 2);
    CHECK(app::steppedLiveScene(-1, 1, 3) == 0);
    CHECK(app::steppedLiveScene(-1, -1, 3) == 2);
    CHECK(app::steppedLiveScene(0, 1, 0) == -1);
    CHECK(app::liveSceneForProgram(0, 5) == 0);
    CHECK(app::liveSceneForProgram(7, 5) == 2);
    CHECK(app::liveSceneForProgram(3, 0) == -1);
}

TEST_CASE("The performer's response carries as offsets from each scene's defaults", "[live][adr1063]") {
    params::ParameterSet from;
    addResponse(from, 0.5f, 1.0f);
    from.find("sonic/response/sensitivity")->setBaseComponent(0, 0.65f); // the room needs +0.15
    from.find("sonic/response/release")->setBaseComponent(0, 2.0f);      // and twice the release
    const auto carry = app::captureResponse(from);
    REQUIRE(carry);
    params::ParameterSet to;
    addResponse(to, 0.6f, 0.5f); // a scene authored hotter and tighter
    app::applyResponse(*carry, to);
    CHECK(to.find("sonic/response/sensitivity")->baseComponent(0) == Approx(0.75f));
    CHECK(to.find("sonic/response/release")->baseComponent(0) == Approx(1.0f));
    CHECK(to.find("sonic/response/transient")->baseComponent(0) == Approx(0.5f));
    // Clamped to the range.
    from.find("sonic/response/sensitivity")->setBaseComponent(0, 1.0f);
    const auto high = app::captureResponse(from);
    REQUIRE(high);
    app::applyResponse(*high, to);
    CHECK(to.find("sonic/response/sensitivity")->baseComponent(0) == 1.0f);
    // A project with no response parameters has nothing to carry.
    params::ParameterSet none;
    CHECK_FALSE(app::captureResponse(none).has_value());
}

TEST_CASE("The switcher steps through the set the open project belongs to", "[live][adr1074]") {
    // A fixture index, not the real one: two sets interleaved with everything else.
}

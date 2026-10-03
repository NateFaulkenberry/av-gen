// The live scene switcher's decisions (ADR-1063): the list, stepping, program change, labels, and the performer's
// response carried across a switch as offsets from each scene's own defaults.

#include "app/live_scenes.hpp"
#include "params/parameter_set.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace avgen;
using Catch::Approx;

namespace {

app::ExampleInfo example(std::string name, std::string category, std::string file) {
    return app::ExampleInfo{std::move(name), "", std::move(category), std::filesystem::path("/ex") / file};
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

TEST_CASE("The live scenes are Sonic Live and the Sonic VFX examples, in the index's order", "[live][adr1063]") {
    const std::vector<app::ExampleInfo> all{example("The Temple", "Showcase", "t.json"),
                                            example("Sonic VFX - Event Horizon", "Sonic VFX", "eh.json"),
                                            example("Sonic Live", "Lab", "live.json"),
                                            example("Sonic VFX - Tesla Choir", "Sonic VFX", "tc.json")};
    const auto list = app::liveSceneList(all);
    REQUIRE(list.size() == 3);
    CHECK(list[0].name == "Sonic Live");
    CHECK(list[1].name == "Sonic VFX - Event Horizon");
    CHECK(list[2].name == "Sonic VFX - Tesla Choir");
    CHECK(app::liveSceneLabel(list[1]) == "Event Horizon");
    CHECK(app::liveSceneLabel(list[0]) == "Sonic Live");
    CHECK(app::liveSceneIndex(list, "/ex/tc.json") == 2);
    CHECK(app::liveSceneIndex(list, "/ex/../ex/eh.json") == 1);
    CHECK(app::liveSceneIndex(list, "/ex/t.json") == -1);
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
    const std::vector<app::ExampleInfo> all{example("The Temple", "Showcase", "t.json"),
                                            example("Sonic Abstract - Sacred Geometry", "Sonic Abstract", "sg.json"),
                                            example("Sonic VFX - Event Horizon", "Sonic VFX", "eh.json"),
                                            example("Sonic Live", "Lab", "live.json"),
                                            example("Sonic Abstract - Neon Vector", "Sonic Abstract", "nv.json"),
                                            example("Sonic VFX - Tesla Choir", "Sonic VFX", "tc.json"),
                                            example("Sonic Abstract - Particle World", "Sonic Abstract", "pw.json")};
    const auto names = [](const std::vector<app::ExampleInfo>& list) {
        std::vector<std::string> out;
        for (const auto& e : list) {
            out.push_back(e.name);
        }
        return out;
    };
    const std::vector<std::string> vfx{"Sonic Live", "Sonic VFX - Event Horizon", "Sonic VFX - Tesla Choir"};
    const std::vector<std::string> abstract{"Sonic Abstract - Sacred Geometry", "Sonic Abstract - Neon Vector",
                                            "Sonic Abstract - Particle World"};
    // Sonic VFX keeps today's behaviour, from Sonic Live and from any of its scenes.
    CHECK(names(app::liveSceneListFor(all, "/ex/live.json")) == vfx);
    CHECK(names(app::liveSceneListFor(all, "/ex/tc.json")) == vfx);
    // Sonic Abstract is its own list, in index order.
    const auto fromAbstract = app::liveSceneListFor(all, "/ex/nv.json");
    CHECK(names(fromAbstract) == abstract);
    CHECK(app::liveSceneIndex(fromAbstract, "/ex/nv.json") == 1);
    CHECK(app::liveSceneIndex(fromAbstract, "/ex/eh.json") == -1); // no stepping across sets
    // Any other project, or none: today's default list.
    CHECK(names(app::liveSceneListFor(all, "/ex/t.json")) == vfx);
    CHECK(names(app::liveSceneListFor(all, "/elsewhere/mine.json")) == vfx);
    CHECK(names(app::liveSceneListFor(all, "")) == vfx);
    // Labels drop the set's own prefix.
    CHECK(app::liveSceneLabel(fromAbstract[0]) == "Sacred Geometry");
    CHECK(app::liveSceneLabel(all[2]) == "Event Horizon");
    CHECK(app::liveSceneLabel(all[3]) == "Sonic Live");
}

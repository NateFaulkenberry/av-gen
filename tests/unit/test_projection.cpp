// ADR-1026: the Live panel's projection -- which display, fullscreen, the window, the scaling, the state machine,
// the per-machine settings, and that the projection output never enters the project's outputs block.
#include "app/output_manager.hpp"
#include "app/projection.hpp"
#include "app/settings.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;
using app::Projection;
using app::ProjectionDisplay;

namespace {

ProjectionDisplay laptop() {
    return {.index = 0, .name = "Built-in Retina Display", .width = 1512, .height = 982, .primary = true};
}

ProjectionDisplay projector() {
    return {.index = 1, .name = "EPSON PJ", .width = 1920, .height = 1080, .primary = false};
}

Projection::Observed seen(bool windowOpen, std::vector<ProjectionDisplay> displays = {}) {
    Projection::Observed o;
    o.windowOpen = windowOpen;
    o.displays = std::move(displays);
    return o;
}

} // namespace

TEST_CASE("projection picks the remembered display, else a non-primary one, else the primary", "[projection][app]") {
    const std::vector<ProjectionDisplay> both{laptop(), projector()};
    const std::vector<ProjectionDisplay> one{laptop()};

    // Automatic: the projector, not the laptop, even though the laptop is listed first.
    auto c = app::chooseProjectionDisplay(both, "");
    CHECK(c.index == 1);
    CHECK(c.name == "EPSON PJ");
    CHECK_FALSE(c.primary);
    CHECK_FALSE(c.fellBack);

    // Remembered by name, even when it is the primary.
    c = app::chooseProjectionDisplay(both, "Built-in Retina Display");
    CHECK(c.index == 0);
    CHECK(c.primary);
    CHECK_FALSE(c.fellBack);

    // The remembered projector is unplugged: the automatic choice, and it says it fell back.
    c = app::chooseProjectionDisplay(one, "EPSON PJ");
    CHECK(c.index == 0);
    CHECK(c.primary);
    CHECK(c.fellBack);

    // One display, nothing remembered: the primary, no fallback to report.
    c = app::chooseProjectionDisplay(one, "");
    CHECK(c.index == 0);
    CHECK_FALSE(c.fellBack);

    // Indices follow the list, not the name's old position.
    const std::vector<ProjectionDisplay> reordered{
        {.index = 0, .name = "EPSON PJ", .width = 1920, .height = 1080, .primary = false},
        {.index = 1, .name = "Built-in Retina Display", .width = 1512, .height = 982, .primary = true}};
    CHECK(app::chooseProjectionDisplay(reordered, "EPSON PJ").index == 0);

    // No video subsystem: the platform's default display.
    c = app::chooseProjectionDisplay({}, "EPSON PJ");
    CHECK(c.index == -1);
}

TEST_CASE("projection fullscreen defaults on for another display and off for this one", "[projection][app]") {
    app::AppSettings::Projection settings;
    const std::vector<ProjectionDisplay> both{laptop(), projector()};
    const std::vector<ProjectionDisplay> one{laptop()};

    auto desc = app::makeProjectionOutput(settings, both);
    CHECK(desc.name == app::kProjectionOutputName);
    CHECK(desc.display == 1);
    CHECK(desc.fullscreen);
    CHECK(desc.borderless);      // nothing but the picture
    CHECK(desc.width == 1920);   // automatic on another display: its own size
    CHECK(desc.height == 1080);
    CHECK(desc.mapping.isIdentity());
    CHECK(desc.validate().has_value());

    desc = app::makeProjectionOutput(settings, one);
    CHECK(desc.display == 0);
    CHECK_FALSE(desc.fullscreen); // would cover the editor
    CHECK_FALSE(desc.borderless); // a window that can be moved and closed
    CHECK(desc.width == 756);     // half this screen, so the editor stays usable beside it
    CHECK(desc.height == 491);

    // The person's own choice wins either way, and a windowed size is honoured.
    settings.fullscreen = true;
    CHECK(app::makeProjectionOutput(settings, one).fullscreen);
    settings.fullscreen = false;
    settings.windowWidth = 1280;
    settings.windowHeight = 720;
    desc = app::makeProjectionOutput(settings, both);
    CHECK_FALSE(desc.fullscreen);
    CHECK(desc.width == 1280);
    CHECK(desc.height == 720);

    // No display list: a valid window on the default display.
    desc = app::makeProjectionOutput(app::AppSettings::Projection{}, {});
    CHECK(desc.display == -1);
    CHECK(desc.validate().has_value());
}

TEST_CASE("projection scaling fits, fills or stretches the frame onto the window", "[projection][app]") {
    using app::ProjectionScaling;
    // Same shape: the identity, so the mapper takes its plain-copy path.
    CHECK(app::projectionMapping(ProjectionScaling::Fit, 1920, 1080, 3840, 2160).isIdentity());
    CHECK(app::projectionMapping(ProjectionScaling::Fill, 1920, 1080, 1280, 720).isIdentity());
    CHECK(app::projectionMapping(ProjectionScaling::Stretch, 1000, 1000, 1920, 1080).isIdentity());

    // A square canvas on a 16:9 projector, Fit: full height, centred, bars left and right.
    auto m = app::projectionMapping(ProjectionScaling::Fit, 1000, 1000, 1920, 1080);
    CHECK(m.corners[0].x == Catch::Approx(0.5 - 0.5 * 1080.0 / 1920.0));
    CHECK(m.corners[1].x == Catch::Approx(0.5 + 0.5 * 1080.0 / 1920.0));
    CHECK(m.corners[0].y == 0.0f);
    CHECK(m.corners[2].y == 1.0f);
    CHECK(m.validate().has_value());

    // A wide canvas on a 16:9 window, Fit: full width, bars above and below.
    m = app::projectionMapping(ProjectionScaling::Fit, 3000, 1000, 1920, 1080);
    CHECK(m.corners[0].x == 0.0f);
    CHECK(m.corners[1].x == 1.0f);
    const double h = (1920.0 / 1080.0) / 3.0;
    CHECK(m.corners[0].y == Catch::Approx(0.5 - 0.5 * h));
    CHECK(m.corners[3].y == Catch::Approx(0.5 + 0.5 * h));
    CHECK(m.validate().has_value());

    // Fill: the same square canvas is cropped to 16:9 from its centre, corners untouched.
    m = app::projectionMapping(ProjectionScaling::Fill, 1000, 1000, 1920, 1080);
    CHECK(m.crop.w == 1.0f);
    CHECK(m.crop.h == Catch::Approx(1080.0 / 1920.0));
    CHECK(m.crop.y == Catch::Approx(0.5 - 0.5 * 1080.0 / 1920.0));
    CHECK(m.corners[0] == glm::vec2(0.0f, 0.0f));
    CHECK(m.validate().has_value());

    // A minimised window or an empty canvas: the identity, never a degenerate quad.
    CHECK(app::projectionMapping(ProjectionScaling::Fit, 0, 0, 1920, 1080).isIdentity());
    CHECK(app::projectionMapping(ProjectionScaling::Fit, 1920, 1080, 0, 0).isIdentity());
}

// ADR-1088: Start projects the open project, whatever it is -- there is no "is this a live project" input to the
// decision any more, and so no path that swaps another project in.
TEST_CASE("projection start opens the window at once on the open project", "[projection][app]") {
    Projection p;
    CHECK_FALSE(p.active());
    REQUIRE(p.start() == Projection::Action::OpenWindow);
    CHECK(p.state() == Projection::State::Running);
    CHECK(p.active());
    CHECK(p.start() == Projection::Action::None); // a second press while running does nothing
    // Until the host reports the open, a closed window is not a reason to stop.
    CHECK(p.update(seen(false)) == Projection::Action::None);
    p.opened(true, "EPSON PJ", {});
    CHECK(p.displayName() == "EPSON PJ");
    CHECK(p.update(seen(true, {laptop(), projector()})) == Projection::Action::None);
    CHECK(p.state() == Projection::State::Running);
    // Opening another project does not stop a running projection: the window shows what renders.
    CHECK(p.update(seen(true, {laptop(), projector()})) == Projection::Action::None);
    CHECK(p.active());

    // Stop closes it.
    CHECK(p.stop() == Projection::Action::CloseWindow);
    CHECK_FALSE(p.active());
    CHECK(p.message().empty());
    CHECK(p.stop() == Projection::Action::None);
}

TEST_CASE("projection ends cleanly on every way out", "[projection][app]") {
    SECTION("the window does not open") {
        Projection p;
        REQUIRE(p.start() == Projection::Action::OpenWindow);
        p.opened(false, {}, "SDL_CreateWindow failed");
        CHECK_FALSE(p.active());
        CHECK(p.message().find("SDL_CreateWindow failed") != std::string::npos);
    }
    SECTION("the person closes the window (close button or Esc)") {
        Projection p;
        REQUIRE(p.start() == Projection::Action::OpenWindow);
        p.opened(true, "EPSON PJ", {});
        CHECK(p.update(seen(false, {laptop(), projector()})) == Projection::Action::CloseWindow);
        CHECK_FALSE(p.active());
        CHECK_FALSE(p.message().empty());
    }
    SECTION("the projector is unplugged") {
        Projection p;
        REQUIRE(p.start() == Projection::Action::OpenWindow);
        p.opened(true, "EPSON PJ", {});
        CHECK(p.update(seen(true, {laptop()})) == Projection::Action::CloseWindow);
        CHECK_FALSE(p.active());
        CHECK(p.message().find("EPSON PJ") != std::string::npos);
    }
    SECTION("an empty display list (no video subsystem) is not an unplug") {
        Projection p;
        REQUIRE(p.start() == Projection::Action::OpenWindow);
        p.opened(true, "EPSON PJ", {});
        CHECK(p.update(seen(true, {})) == Projection::Action::None);
        CHECK(p.active());
    }
    SECTION("Stop before the window reported its open: nothing is left running") {
        Projection p;
        REQUIRE(p.start() == Projection::Action::OpenWindow);
        CHECK(p.stop() == Projection::Action::CloseWindow);
        CHECK(p.update(seen(false)) == Projection::Action::None);
        CHECK_FALSE(p.active());
    }
}

TEST_CASE("projection settings are this machine's and round-trip", "[projection][app][settings]") {
    app::AppSettings settings;
    CHECK(settings.projection.display.empty());
    CHECK_FALSE(settings.projection.fullscreen.has_value());
    // Unset fullscreen is not written, so it stays "automatic" after a save.
    CHECK_FALSE(settings.toJson().at("projection").contains("fullscreen"));

    settings.projection.display = "EPSON PJ";
    settings.projection.fullscreen = false;
    settings.projection.windowWidth = 1280;
    settings.projection.windowHeight = 720;
    settings.projection.scaling = app::ProjectionScaling::Fill;
    const auto loaded = app::AppSettings::fromJson(settings.toJson());
    REQUIRE(loaded.has_value());
    CHECK(loaded->projection.display == "EPSON PJ");
    REQUIRE(loaded->projection.fullscreen.has_value());
    CHECK_FALSE(*loaded->projection.fullscreen);
    CHECK(loaded->projection.windowWidth == 1280);
    CHECK(loaded->projection.windowHeight == 720);
    CHECK(loaded->projection.scaling == app::ProjectionScaling::Fill);

    // Lenient: nonsense falls back to the defaults rather than refusing the settings file.
    auto doc = settings.toJson();
    doc["projection"] = nlohmann::json{{"windowWidth", -5}, {"windowHeight", 720}, {"scaling", "zoom"}, {"fullscreen", 3}};
    const auto lenient = app::AppSettings::fromJson(doc);
    REQUIRE(lenient.has_value());
    CHECK(lenient->projection.windowWidth == 0);
    CHECK(lenient->projection.windowHeight == 0);
    CHECK(lenient->projection.scaling == app::ProjectionScaling::Fit);
    CHECK_FALSE(lenient->projection.fullscreen.has_value());
}

TEST_CASE("the projection output never enters the project and survives a project load", "[projection][outputs]") {
    app::OutputManager manager;
    app::OutputDesc projectOutput;
    projectOutput.name = "left";
    REQUIRE(manager.add(projectOutput).has_value());
    auto added = manager.add(app::makeProjectionOutput(app::AppSettings::Projection{}, {laptop(), projector()}));
    REQUIRE(added.has_value());
    (*added)->projection = true;

    // Saved: only the project's output.
    const auto j = manager.toJson();
    REQUIRE(j.size() == 1);
    CHECK(j[0]["name"] == "left");

    // A project load replaces the project's outputs and keeps the projection.
    REQUIRE(manager.fromJson(nlohmann::json::parse(R"([{"name": "right"}])")).has_value());
    REQUIRE(manager.outputs().size() == 2);
    CHECK(manager.find("left") == nullptr);
    CHECK(manager.find("right") != nullptr);
    REQUIRE(manager.find(app::kProjectionOutputName) != nullptr);
    CHECK(manager.find(app::kProjectionOutputName)->projection);

    // A project that names an output like the projection is refused, and nothing changes.
    nlohmann::json clash = nlohmann::json::array();
    clash.push_back(nlohmann::json{{"name", app::kProjectionOutputName}});
    CHECK_FALSE(manager.fromJson(clash).has_value());
    CHECK(manager.outputs().size() == 2);
}

// ADR-1089: a slow output is presented to less often, never allowed to hold the loop every frame; a healthy one is
// untouched; a recovered one comes back.
TEST_CASE("a slow output is paced down and a recovered one comes back", "[projection][outputs][live-quality]") {
    app::PresentPacer pacer;
    // Healthy: every frame presents, whatever the count.
    for (int i = 0; i < 100; ++i) {
        REQUIRE(pacer.due());
        pacer.acquired(0.05);
    }
    CHECK(pacer.interval() == 1);
    CHECK(pacer.skipped() == 0);

    // The measured case: every acquire blocks 14.8 ms. Count the frames that pay a blocked acquire.
    int blocked = 0;
    for (int i = 0; i < 800; ++i) {
        if (pacer.due()) {
            pacer.acquired(14.8);
            ++blocked;
        }
    }
    CHECK(pacer.interval() == app::PresentPacer::kMaxInterval);
    // At most one blocked acquire in eight frames once paced (plus the few on the way down).
    CHECK(blocked <= 800 / static_cast<int>(app::PresentPacer::kMaxInterval) + 4);
    CHECK(pacer.skipped() >= 690u);

    // The display recovers: fast acquires walk the interval back to every frame.
    for (int i = 0; i < 2000 && pacer.interval() > 1; ++i) {
        if (pacer.due()) {
            pacer.acquired(0.05);
        }
    }
    CHECK(pacer.interval() == 1);
    // One slow acquire among fast ones costs a step, not the projection.
    pacer.acquired(20.0);
    CHECK(pacer.interval() == 2);
}

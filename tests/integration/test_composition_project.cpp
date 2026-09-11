// The 2D composition inside a project (ADR-081): that it saves, that it loads, that its layer
// properties reach the timeline, and that a project written before it existed still opens.
//
// This file exists because three systems in this engine were carried faithfully in memory and
// dropped at the last step for want of serialisation, and because two more bound their timeline
// tracks to nothing and said nothing about it. Those are the two failures under test here.

#include "app/engine.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <algorithm>
#include <fstream>

using namespace avgen;
namespace fs = std::filesystem;
using Catch::Approx;

namespace {

// A scratch folder of this process's own, and everything inside it named relatively: path
// resolution is under test elsewhere in this suite, and an absolute path hides it.
struct Scratch {
    fs::path dir;
    fs::path previous;
    explicit Scratch(const char* name) {
        dir = testsupport::processTempDir() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        previous = fs::current_path();
        fs::current_path(dir);
    }
    ~Scratch() {
        fs::current_path(previous);
        fs::remove_all(dir);
    }
};

void authorComposition(app::Engine& engine) {
    auto& line = engine.addTextLayer("we were never here", 4.0, 9.0);
    line.name = "line one";
    line.position = glm::vec2(0.5f, 0.22f);
    line.size = 0.085f;
    line.color = glm::vec4(0.92f, 0.97f, 1.0f, 1.0f);
    line.setTracking(0.04f);
    line.outlineWidth = 0.015f;
    line.pushAuthored();

    auto& border = engine.addShapeLayer(comp::ShapeKind::Rectangle);
    border.name = "frame";
    border.size = glm::vec2(1.66f, 0.92f);
    border.color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    border.strokeWidth = 0.003f;
    border.pushAuthored();
}

} // namespace

TEST_CASE("a composition survives a project save and load", "[composition][project]") {
    Scratch scratch("composition_project");
    const fs::path file = "session.json"; // relative on purpose

    std::string saved;
    {
        app::Engine engine(app::EngineMode::Offline);
        authorComposition(engine);
        REQUIRE(engine.layers().size() == 2);
        REQUIRE(engine.saveProject(file).has_value());
        std::ifstream in(file);
        saved.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(saved.find("\"composition\"") != std::string::npos);

    app::Engine loaded(app::EngineMode::Offline);
    REQUIRE(loaded.loadProject(file).has_value());
    REQUIRE(loaded.layers().size() == 2);

    const auto* text = dynamic_cast<const comp::TextLayer*>(loaded.layers().layers()[0].get());
    REQUIRE(text != nullptr);
    CHECK(text->name == "line one");
    CHECK(text->text() == "we were never here");
    CHECK(text->startTime == Approx(4.0));
    CHECK(text->endTime == Approx(9.0));
    CHECK(text->size == Approx(0.085f));
    CHECK(text->tracking() == Approx(0.04f));
    CHECK(text->outlineWidth == Approx(0.015f));

    const auto* shape = dynamic_cast<const comp::ShapeLayer*>(loaded.layers().layers()[1].get());
    REQUIRE(shape != nullptr);
    CHECK(shape->strokeWidth == Approx(0.003f));

    // And the parameters came back with them, which is what makes them animatable at all.
    for (const std::string& path : loaded.layers().parameterPaths()) {
        INFO(path);
        CHECK(loaded.params().find(path) != nullptr);
    }
    const auto* opacity = loaded.params().find(text->parameterPath("opacity"));
    REQUIRE(opacity != nullptr);
    CHECK(opacity->baseComponent(0) == Approx(1.0f));
}

TEST_CASE("a project with no composition loads exactly as it always did", "[composition][project]") {
    Scratch scratch("composition_legacy");
    // A project document written before the composition system existed: current envelope, every
    // other block present, and no "composition" key anywhere.
    const fs::path file = "legacy.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.saveProject(file).has_value());
    }
    nlohmann::json doc;
    {
        std::ifstream in(file);
        doc = nlohmann::json::parse(in);
    }
    REQUIRE_FALSE(doc.contains("composition"));

    app::Engine loaded(app::EngineMode::Offline);
    REQUIRE(loaded.loadProject(file).has_value());
    CHECK(loaded.layers().empty());
    CHECK(loaded.projectWarnings().empty());
}

TEST_CASE("layer properties keyframe on the project's own timeline", "[composition][project][timeline]") {
    Scratch scratch("composition_timeline");
    const fs::path file = "keyed.json";
    std::string opacityPath;
    std::string positionPath;
    {
        app::Engine engine(app::EngineMode::Offline);
        auto& line = engine.addTextLayer("entrance", 0.0, 0.0);
        opacityPath = line.parameterPath("opacity");
        positionPath = line.parameterPath("position");

        // A fade and an entrance, recorded the way the editor records them: move the playhead,
        // set the value, key it.
        line.opacity = 0.0f;
        line.pushAuthored();
        REQUIRE(engine.timeline().recordKey(engine.params(), opacityPath, -1, 10.0) != nullptr);
        line.opacity = 1.0f;
        line.pushAuthored();
        REQUIRE(engine.timeline().recordKey(engine.params(), opacityPath, -1, 11.0) != nullptr);

        line.position = glm::vec2(-0.5f, 0.2f);
        line.pushAuthored();
        REQUIRE(engine.timeline().recordKey(engine.params(), positionPath, -1, 10.0) != nullptr);
        line.position = glm::vec2(0.5f, 0.2f);
        line.pushAuthored();
        REQUIRE(engine.timeline().recordKey(engine.params(), positionPath, -1, 12.0) != nullptr);

        REQUIRE(engine.saveProject(file).has_value());
    }

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(file).has_value());
    // The trap this whole test exists for: every track bound to something real.
    CHECK(engine.timeline().unboundTargets().empty());
    REQUIRE(engine.timeline().tracks().size() == 2);
    for (const params::Track& track : engine.timeline().tracks()) {
        INFO(track.target);
        CHECK(track.param != nullptr);
    }

    // And the layer actually moves when the clock does.
    comp::Layer* layer = engine.layers().at(0);
    REQUIRE(layer != nullptr);
    const comp::Frame frame{1920, 1080};
    const auto stateAt = [&](double seconds) {
        engine.params().resetFinals();
        engine.timeline().apply(params::TimelineClock{seconds, 0.0});
        engine.layers().build(frame, seconds);
        return std::pair{layer->resolvedOpacity(), layer->resolvedPosition()};
    };
    {
        const auto [opacity, position] = stateAt(9.5);
        CHECK(opacity == Approx(0.0f).margin(1e-4f));
        CHECK(position.x == Approx(-0.5f).margin(1e-4f));
    }
    {
        const auto [opacity, position] = stateAt(10.5);
        CHECK(opacity == Approx(0.5f).margin(1e-3f));
        CHECK(position.x == Approx(-0.25f).margin(1e-3f));
    }
    {
        const auto [opacity, position] = stateAt(13.0);
        CHECK(opacity == Approx(1.0f).margin(1e-4f));
        CHECK(position.x == Approx(0.5f).margin(1e-4f));
    }
}

TEST_CASE("deleting a layer takes its parameters and its tracks with it",
          "[composition][project][timeline]") {
    app::Engine engine(app::EngineMode::Offline);
    auto& a = engine.addTextLayer("keep");
    auto& b = engine.addTextLayer("delete");
    const std::string keptPath = a.parameterPath("opacity");
    const std::string goingPath = b.parameterPath("opacity");
    const std::uint32_t goingId = b.id;
    REQUIRE(engine.timeline().recordKey(engine.params(), keptPath, -1, 1.0) != nullptr);
    REQUIRE(engine.timeline().recordKey(engine.params(), goingPath, -1, 1.0) != nullptr);
    REQUIRE(engine.timeline().tracks().size() == 2);

    REQUIRE(engine.removeLayer(goingId));
    CHECK(engine.layers().size() == 1);
    CHECK(engine.params().find(goingPath) == nullptr);
    CHECK(engine.params().find(keptPath) != nullptr);
    // No track left aimed at a layer that no longer exists.
    REQUIRE(engine.timeline().tracks().size() == 1);
    CHECK(engine.timeline().tracks()[0].target == keptPath);
    CHECK(engine.timeline().unboundTargets().empty());
}

TEST_CASE("a new project clears the composition and its parameters", "[composition][project]") {
    app::Engine engine(app::EngineMode::Offline);
    const auto& layer = engine.addTextLayer("gone");
    const std::string path = layer.parameterPath("opacity");
    REQUIRE(engine.params().find(path) != nullptr);
    engine.newProject();
    CHECK(engine.layers().empty());
    CHECK(engine.params().find(path) == nullptr);
}

TEST_CASE("a layer property is a modulation target like any other", "[composition][project][modulation]") {
    // Nothing text-specific exists for audio reactivity: a layer property is a parameter, and the
    // route system that already drives the scene drives it unchanged.
    app::Engine engine(app::EngineMode::Offline);
    auto& line = engine.addTextLayer("bass");
    params::ModRoute route;
    route.source = "audio.bass";
    route.target = line.parameterPath("scale");
    route.amount = 1.0f;
    engine.modulator().addRoute(route);
    engine.rebind();
    const auto& routes = engine.modulator().routes();
    REQUIRE(routes.size() >= 1);
    const auto it = std::ranges::find_if(routes, [&](const params::ModRoute& r) { return r.target == route.target; });
    REQUIRE(it != routes.end());
    CHECK(it->targetParam != nullptr);
}

TEST_CASE("the authoring loop: add, type, place, key, play", "[composition][project][authoring]") {
    // The sequence the Composition panel performs, through the same engine calls it makes. The
    // panel's pixels cannot be tested here; the loop underneath them can, and it is the loop the
    // whole feature is judged by.
    app::Engine engine(app::EngineMode::Offline);
    const comp::Frame frame{1920, 1080};

    // 1. "+ Add Layer -> Text" at the playhead.
    comp::TextLayer& text = engine.addTextLayer("Your words here", 0.0);
    CHECK(engine.layers().size() == 1);
    CHECK(engine.layers().build(frame, 0.0).drawnLayers == 1);

    // 2. Type.
    text.setText("hold the light");
    // 3. Drag it into the lower third.
    text.position = glm::vec2(0.5f, 0.2f);
    text.size = 0.07f;
    text.pushAuthored();
    const std::uint64_t afterTyping = engine.layers().vertexVersion();
    engine.layers().build(frame, 0.0);

    // 4. Choose a face. The one on this machine, whatever it is called: the point is that picking
    //    a family re-shapes and the layer says what it got.
    comp::FontDesc font = text.font();
    if (!comp::fontBackend().families().empty()) {
        font.family = comp::fontBackend().families().front();
        font.postScriptName.clear();
        text.setFont(font);
    }
    engine.layers().build(frame, 0.0);
    CHECK(engine.layers().vertexVersion() > afterTyping); // a new face re-shapes; a new size does not

    // 5. Move the playhead, set the value, press the key dot. Twice.
    const std::string path = text.parameterPath("opacity");
    text.opacity = 0.0f;
    text.pushAuthored();
    REQUIRE(engine.timeline().recordKey(engine.params(), path, -1, 8.0) != nullptr);
    text.opacity = 1.0f;
    text.pushAuthored();
    REQUIRE(engine.timeline().recordKey(engine.params(), path, -1, 9.0) != nullptr);
    CHECK(engine.timeline().isAutomated(path)); // the dot lights up

    // 6. Play. The engine's own update drives it: signals, timeline, modulation, then the layers.
    FixedStepClock clock(60.0);
    clock.restartAt(0.0);
    std::vector<float> opacityOverTime;
    for (int f = 0; f <= 600; ++f) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        engine.layers().build(frame, engine.timelineClock().seconds);
        opacityOverTime.push_back(text.resolvedOpacity());
    }
    CHECK(opacityOverTime[0] == Approx(0.0f).margin(1e-4f));      // t = 0
    CHECK(opacityOverTime[8 * 60] == Approx(0.0f).margin(1e-3f)); // t = 8, the first key
    CHECK(opacityOverTime[9 * 60] == Approx(1.0f).margin(1e-3f)); // t = 9, the second
    CHECK(opacityOverTime[10 * 60] == Approx(1.0f).margin(1e-4f));
    // It really is a fade and not a jump.
    CHECK(opacityOverTime[8 * 60 + 30] > 0.3f);
    CHECK(opacityOverTime[8 * 60 + 30] < 0.7f);

    // 7. Duplicate it for the second line, which is what an author does next.
    comp::Layer* second = engine.duplicateLayer(text.id);
    REQUIRE(second != nullptr);
    CHECK(engine.layers().indexOf(second->id) == 1);
    CHECK(engine.params().find(second->parameterPath("opacity")) != nullptr);
    // The copy has its own parameters, so keying one does not move the other.
    CHECK(second->parameterPath("opacity") != path);
}

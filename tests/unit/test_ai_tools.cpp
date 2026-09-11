// The AV Gen Tool API (ADR-094) against a real engine.
//
// Every test here drives `app::Engine` -- the same object the application runs -- through the same
// registry the AI panel uses. Nothing is mocked: §54 forbids fake engine APIs, and a tool suite
// that passed against a stand-in would prove only that the stand-in works.
//
// The tests that matter most are the ones about *silence*: this repository has repeatedly shipped
// features that were correct except that they did nothing, and three of those failure modes are
// reachable from this API. Each has a test that fails if the guard is removed.

#include "ai/capabilities.hpp"
#include "ai/engine_tools.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "ai/transaction.hpp"
#include "app/engine.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace {

struct Fixture {
    app::Engine engine{app::EngineMode::Offline};
    ai::ToolRegistry registry;
    ai::ToolContext ctx{engine};

    Fixture() { ai::registerEngineTools(registry); }

    ai::ToolResult call(const std::string& name, json args = json::object()) {
        return registry.invoke(name, args, ctx);
    }
};

// The helix example: a composition with procedural nodes, a free camera and fog. Small, ships in
// the repository, and loads without an external asset.
std::filesystem::path helixScene() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "helix" / "helix.scene.json";
}

} // namespace

TEST_CASE("The tool registry is populated and every tool is classified", "[ai][tools]") {
    ai::ToolRegistry registry;
    ai::registerEngineTools(registry);

    REQUIRE(registry.size() > 20);
    for (const ai::Tool* tool : registry.all()) {
        INFO(tool->definition.name);
        CHECK(!tool->definition.name.empty());
        CHECK(tool->definition.name.find('.') != std::string::npos); // the §40 namespace vocabulary
        CHECK(!tool->definition.description.empty());
        CHECK(!tool->definition.title.empty());
        CHECK(tool->definition.inputSchema.is_object());
        CHECK(tool->definition.inputSchema.value("type", std::string{}) == "object");
        // Annotated one way or the other; ToolRegistry::add enforces it, and this is the assertion
        // that the enforcement is still there.
        CHECK((tool->definition.annotations.readOnly ||
               tool->definition.annotations.mutatesProject ||
               tool->definition.annotations.mutatesSession));
        // A session mutation must not claim to be undoable: the snapshot domain is project state,
        // and promising a rollback for the transport position would be a promise nothing keeps.
        if (tool->definition.annotations.mutatesSession &&
            !tool->definition.annotations.mutatesProject) {
            CHECK_FALSE(tool->definition.annotations.undoable);
        }
        // Anything that mutates must be inside the snapshot domain, or a rollback would leave it
        // behind. This is the invariant that keeps transactions honest as tools are added.
        if (tool->definition.annotations.mutatesProject) {
            CHECK(tool->definition.annotations.undoable);
        }
    }
    // The namespaces §39 asks for.
    const auto domains = registry.domains();
    for (const char* expected : {"audio", "camera", "capability", "environment", "material",
                                 "modulation", "parameter", "performance", "project", "scene",
                                 "sequencer", "signal"}) {
        INFO(expected);
        CHECK(std::find(domains.begin(), domains.end(), expected) != domains.end());
    }
}

TEST_CASE("Adding a tool with no classification is refused", "[ai][tools]") {
    ai::ToolRegistry registry;
    ai::Tool tool;
    tool.definition.name = "bad.tool";
    tool.definition.inputSchema = ai::schema::object(json::object());
    tool.execute = [](const json&, ai::ToolContext&) { return ai::ToolResult::ok({}); };
    CHECK_THROWS(registry.add(tool));

    tool.definition.annotations.readOnly = true;
    tool.definition.annotations.destructive = true;
    CHECK_THROWS(registry.add(tool));
}

TEST_CASE("Arguments are validated against the tool's schema before it runs", "[ai][tools]") {
    Fixture f;

    SECTION("a missing required field is a recoverable error, not a crash") {
        const auto result = f.call("parameter.set", json{{"value", 1.0}});
        REQUIRE_FALSE(result.success);
        REQUIRE(result.error.has_value());
        CHECK(result.error->code == ai::ToolErrorCode::InvalidArguments);
        CHECK(result.error->message.find("path") != std::string::npos);
        CHECK(!result.error->recovery.empty());
    }
    SECTION("a wrong type is reported with both types") {
        const auto result = f.call("parameter.set", json{{"path", 7}, {"value", 1.0}});
        REQUIRE_FALSE(result.success);
        CHECK(result.error->code == ai::ToolErrorCode::InvalidArguments);
    }
    SECTION("an unknown field is refused rather than ignored") {
        const auto result =
            f.call("parameter.set", json{{"path", "orb/scale"}, {"value", 1.0}, {"colour", "red"}});
        REQUIRE_FALSE(result.success);
        CHECK(result.error->message.find("colour") != std::string::npos);
    }
    SECTION("a value outside a documented range is OUT_OF_RANGE, not INVALID_ARGUMENTS") {
        const auto result = f.call("camera.set", json{{"fovDegrees", 400.0}});
        REQUIRE_FALSE(result.success);
        CHECK(result.error->code == ai::ToolErrorCode::OutOfRange);
    }
    SECTION("an enum lists the legal values in the recovery text") {
        const auto result = f.call("camera.set", json{{"mode", "cinematic"}});
        REQUIRE_FALSE(result.success);
        CHECK(result.error->recovery.find("orbit") != std::string::npos);
    }
    SECTION("an unknown tool name suggests how to find the real ones") {
        const auto result = f.call("scene.teleport_everything");
        REQUIRE_FALSE(result.success);
        CHECK(result.error->code == ai::ToolErrorCode::NotFound);
        CHECK(result.error->recovery.find("capability.list_tools") != std::string::npos);
    }
}

TEST_CASE("Inspection tools read the real engine", "[ai][tools]") {
    Fixture f;

    SECTION("project.get_state counts what is actually there") {
        const auto result = f.call("project.get_state");
        REQUIRE(result.success);
        CHECK(result.value.at("parameterCount").get<std::size_t>() == f.engine.params().size());
        CHECK(result.value.at("signals").get<std::size_t>() == f.engine.signals().size());
        CHECK_FALSE(result.value.at("hasAudio").get<bool>());
    }
    SECTION("parameter.search finds a parameter the orb scene really registers") {
        const auto result = f.call("parameter.search", json{{"query", "emissive"}});
        REQUIRE(result.success);
        REQUIRE(result.value.at("matched").get<int>() > 0);
        bool found = false;
        for (const auto& p : result.value.at("parameters")) {
            if (p.at("path") == "orb/emissive") {
                found = true;
            }
        }
        CHECK(found);
    }
    SECTION("parameter.get carries the range a set will be clamped to") {
        const auto result = f.call("parameter.get", json{{"paths", json::array({"orb/scale"})}});
        REQUIRE(result.success);
        const auto& p = result.value.at("parameters").at(0);
        CHECK(p.at("path") == "orb/scale");
        CHECK_THAT(p.at("hardMin").get<double>(), WithinAbs(0.05, 1e-6));
        CHECK_THAT(p.at("hardMax").get<double>(), WithinAbs(8.0, 1e-6));
    }
    SECTION("parameter.get reports what it could not find without failing the whole call") {
        const auto result = f.call(
            "parameter.get", json{{"paths", json::array({"orb/scale", "orb/doesNotExist"})}});
        REQUIRE(result.success);
        CHECK(result.value.at("parameters").size() == 1);
        CHECK(result.value.at("notFound").at(0) == "orb/doesNotExist");
    }
    SECTION("signal.list returns the live bus") {
        const auto result = f.call("signal.list", json{{"query", "bass"}});
        REQUIRE(result.success);
        CHECK(result.value.at("matched").get<int>() >= 1);
    }
    SECTION("performance.get_stats is honest about having no renderer") {
        const auto result = f.call("performance.get_stats");
        REQUIRE_FALSE(result.success);
        CHECK(result.error->code == ai::ToolErrorCode::Unavailable);
        CHECK(result.error->message.find("no renderer") != std::string::npos);
    }
    SECTION("performance.get_stats reports real numbers when a source is installed") {
        f.ctx.setPerformanceSource([] {
            ai::PerformanceSnapshot s;
            s.available = true;
            s.fps = 120.0;
            s.drawCalls = 42;
            s.gpuFrameMs = 3.5;
            s.gpuPasses.push_back({"shadow", 1.25});
            return s;
        });
        const auto result = f.call("performance.get_stats");
        REQUIRE(result.success);
        CHECK(result.value.at("drawCalls").get<int>() == 42);
        CHECK(result.value.at("gpuPasses").at(0).at("label") == "shadow");
    }
}

TEST_CASE("parameter.set writes the engine and reports what actually landed", "[ai][tools]") {
    Fixture f;
    auto* scale = f.engine.params().find("orb/scale");
    REQUIRE(scale != nullptr);

    SECTION("an ordinary set changes the engine") {
        const auto result = f.call("parameter.set", json{{"path", "orb/scale"}, {"value", 2.5}});
        REQUIRE(result.success);
        CHECK_THAT(static_cast<double>(scale->baseComponent(0)), WithinAbs(2.5, 1e-5));
        CHECK_THAT(static_cast<double>(result.value.at("value").get<double>()), WithinAbs(2.5, 1e-5));
        CHECK_THAT(static_cast<double>(result.value.at("previous").get<double>()), WithinAbs(1.0, 1e-5));
        CHECK_FALSE(result.value.contains("clamped"));
    }
    SECTION("clamping is reported rather than silently applied") {
        const auto result = f.call("parameter.set", json{{"path", "orb/scale"}, {"value", 500.0}});
        REQUIRE(result.success);
        CHECK(result.value.at("clamped").get<bool>());
        CHECK_THAT(static_cast<double>(result.value.at("value").get<double>()), WithinAbs(8.0, 1e-5));
        // The reported value is what the engine holds, not what was asked for.
        CHECK_THAT(static_cast<double>(scale->baseComponent(0)), WithinAbs(8.0, 1e-5));
    }
    SECTION("a vector takes an array and rejects the wrong length") {
        const auto ok = f.call("parameter.set",
                               json{{"path", "orb/baseColor"}, {"value", json::array({0.1, 0.2, 0.3})}});
        REQUIRE(ok.success);
        const auto bad = f.call("parameter.set",
                                json{{"path", "orb/baseColor"}, {"value", json::array({0.1, 0.2})}});
        REQUIRE_FALSE(bad.success);
        CHECK(bad.error->recovery.find("3 component") != std::string::npos);
    }
    SECTION("an unknown path says how to find the right one") {
        const auto result = f.call("parameter.set", json{{"path", "orb/glow"}, {"value", 1.0}});
        REQUIRE_FALSE(result.success);
        CHECK(result.error->code == ai::ToolErrorCode::NotFound);
        CHECK(result.error->recovery.find("parameter.search") != std::string::npos);
    }
}

// ---- the three silent-failure guards ----------------------------------------------------------

TEST_CASE("A set that a Replace-mode track will overwrite is reported, not silently lost",
          "[ai][tools][regression]") {
    Fixture f;
    // Automation on orb/scale in Replace mode: the base value is rewritten every frame, so setting
    // it changes nothing a viewer would ever see. That is exactly the class of failure this API
    // exists to make visible.
    params::Track track;
    track.target = "orb/scale";
    track.mode = params::TrackMode::Replace;
    track.addKey(params::Key{.time = 0.0, .value = {3.0f, 0, 0, 0}});
    f.engine.timeline().addTrack(track);
    REQUIRE(f.engine.timeline().bind(f.engine.params()).has_value());

    const auto result = f.call("parameter.set", json{{"path", "orb/scale"}, {"value", 2.0}});
    REQUIRE(result.success);
    REQUIRE(result.value.contains("overridden"));
    CHECK(result.value.at("overridden").at(0).get<std::string>().find("replace") !=
          std::string::npos);
}

TEST_CASE("A route that replaces a parameter is reported on a set", "[ai][tools][regression]") {
    Fixture f;
    params::ModRoute route;
    route.source = "audio.bass";
    route.target = "orb/emissive";
    route.op = params::ModOp::Replace;
    f.engine.modulator().addRoute(route);
    f.engine.rebind();

    const auto result = f.call("parameter.set", json{{"path", "orb/emissive"}, {"value", 4.0}});
    REQUIRE(result.success);
    REQUIRE(result.value.contains("overridden"));
}

TEST_CASE("sequencer.add_keyframe refuses a target that does not exist",
          "[ai][tools][regression]") {
    Fixture f;
    // params::Timeline::bind skips unknown targets and says nothing. A tool that let one through
    // would create automation that animates nothing -- the failure that has cost this project two
    // features already.
    const auto result = f.call("sequencer.add_keyframe",
                               json{{"target", "orb/nonexistent"}, {"time", 1.0}, {"value", 2.0}});
    REQUIRE_FALSE(result.success);
    CHECK(result.error->code == ai::ToolErrorCode::NotFound);
    CHECK(result.error->message.find("animate nothing") != std::string::npos);
    CHECK(f.engine.timeline().tracks().empty());
}

TEST_CASE("sequencer.add_keyframe creates a bound track on a real parameter", "[ai][tools]") {
    Fixture f;
    const auto first =
        f.call("sequencer.add_keyframe",
               json{{"target", "orb/scale"}, {"time", 0.0}, {"value", 1.0}, {"interp", "smooth"}});
    REQUIRE(first.success);
    CHECK(first.value.at("bound").get<bool>());

    const auto second = f.call("sequencer.add_keyframe",
                               json{{"target", "orb/scale"}, {"time", 4.0}, {"value", 3.0}});
    REQUIRE(second.success);
    CHECK(second.value.at("keys").get<int>() == 2);

    const params::Track* track = f.engine.timeline().findTrack("orb/scale", -1);
    REQUIRE(track != nullptr);
    REQUIRE(track->param != nullptr);
    REQUIRE(track->keys.size() == 2);
    CHECK_THAT(static_cast<double>(track->evaluate(4.0)[0]), WithinAbs(3.0, 1e-4));
    CHECK_THAT(static_cast<double>(track->evaluate(0.0)[0]), WithinAbs(1.0, 1e-4));
    // And the unbound list stays empty, which is what sequencer.get_state reports on.
    CHECK(f.engine.timeline().unboundTargets().empty());
}

TEST_CASE("sequencer.get_state surfaces tracks that animate nothing", "[ai][tools][regression]") {
    Fixture f;
    params::Track orphan;
    orphan.target = "a/parameter/this/scene/does/not/have";
    orphan.addKey(params::Key{.time = 0.0, .value = {1.0f, 0, 0, 0}});
    f.engine.timeline().addTrack(orphan);
    (void)f.engine.timeline().bind(f.engine.params());

    const auto result = f.call("sequencer.get_state");
    REQUIRE(result.success);
    REQUIRE(result.value.contains("unboundTargets"));
    CHECK(result.value.at("unboundTargets").at(0) == orphan.target);
    CHECK(result.value.contains("unboundWarning"));
}

// ---- modulation ---------------------------------------------------------------------------------

TEST_CASE("modulation.create builds a route that is actually bound", "[ai][tools]") {
    Fixture f;
    const std::size_t baseline = f.engine.modulator().routes().size();
    const auto result = f.call("modulation.create", json{{"source", "audio.bass"},
                                                         {"target", "orb/emissive"},
                                                         {"amount", 3.0},
                                                         {"op", "add"},
                                                         {"attackMs", 5.0},
                                                         {"decayMs", 120.0}});
    REQUIRE(result.success);
    CHECK(result.value.at("bound").get<bool>());
    // The engine installs default post-processing routes at construction, so the new one is the
    // last, not the only one.
    REQUIRE(f.engine.modulator().routes().size() == baseline + 1);
    const params::ModRoute& route = f.engine.modulator().routes().back();
    CHECK(route.source == "audio.bass");
    CHECK(route.target == "orb/emissive");
    CHECK_THAT(static_cast<double>(route.amount), WithinAbs(3.0, 1e-5));
    CHECK(route.targetParam != nullptr);
    CHECK(route.sourceId != signals::kInvalidSignal);
    CHECK_THAT(static_cast<double>(route.chain.decayMs), WithinAbs(120.0, 1e-5));
}

TEST_CASE("modulation.create refuses a signal or parameter that does not exist", "[ai][tools]") {
    Fixture f;
    const std::size_t baseline = f.engine.modulator().routes().size();
    const auto badSource = f.call("modulation.create",
                                  json{{"source", "audio.subsonic"}, {"target", "orb/emissive"}});
    REQUIRE_FALSE(badSource.success);
    CHECK(badSource.error->recovery.find("signal.list") != std::string::npos);

    const auto badTarget =
        f.call("modulation.create", json{{"source", "audio.bass"}, {"target", "orb/glow"}});
    REQUIRE_FALSE(badTarget.success);
    CHECK(badTarget.error->code == ai::ToolErrorCode::NotFound);
    CHECK(f.engine.modulator().routes().size() == baseline);
}

TEST_CASE("An event signal can be given an envelope so it pulses rather than flashes",
          "[ai][tools]") {
    Fixture f;
    const auto result = f.call("modulation.create", json{{"source", "beat.pulse"},
                                                         {"target", "orb/impulse"},
                                                         {"amount", 1.0},
                                                         {"envelope", "linearfall"},
                                                         {"envelopeFallPerSecond", 3.0}});
    REQUIRE(result.success);
    const params::ModRoute& route = f.engine.modulator().routes().back();
    CHECK(route.chain.envelope == params::EnvelopeMode::LinearFall);
    CHECK_THAT(static_cast<double>(route.chain.envelopeFallPerSecond), WithinAbs(3.0, 1e-5));
}

TEST_CASE("modulation.remove refuses a route owned by a graph or an entity", "[ai][tools]") {
    Fixture f;
    params::ModRoute route;
    route.source = "audio.bass";
    route.target = "orb/emissive";
    route.fromEntity = true;
    f.engine.modulator().addRoute(route);
    f.engine.rebind();
    const std::size_t count = f.engine.modulator().routes().size();
    const auto index = static_cast<int>(count - 1);

    const auto result = f.call("modulation.remove", json{{"index", index}});
    REQUIRE_FALSE(result.success);
    CHECK(result.error->code == ai::ToolErrorCode::Conflict);
    CHECK(f.engine.modulator().routes().size() == count);
}

// ---- camera -----------------------------------------------------------------------------------

TEST_CASE("The camera tools work on a real composition", "[ai][tools]") {
    Fixture f;
    REQUIRE(f.engine.loadComposition(helixScene()).has_value());

    SECTION("camera.get reports the mode, because the mode decides whether a pose does anything") {
        const auto result = f.call("camera.get");
        REQUIRE(result.success);
        CHECK(result.value.at("mode").is_number());
        CHECK(result.value.contains("modeNames"));
        CHECK(result.value.at("effective").contains("position"));
    }
    SECTION("setting a pose in orbit mode switches to free and says so") {
        auto* mode = f.engine.params().find("camera/mode");
        REQUIRE(mode != nullptr);
        mode->setBaseComponent(0, 0.0f); // orbit

        const auto result =
            f.call("camera.set", json{{"position", json::array({1.0, 2.0, 3.0})},
                                      {"target", json::array({0.0, 0.0, 0.0})}});
        REQUIRE(result.success);
        CHECK(result.value.at("modeChangedTo") == "free");
        CHECK_THAT(static_cast<double>(mode->baseComponent(0)), WithinAbs(1.0, 1e-5));
        const params::IParameter* position = f.engine.params().find("camera/position");
        REQUIRE(position != nullptr);
        CHECK_THAT(static_cast<double>(position->baseComponent(0)), WithinAbs(1.0, 1e-4));
        CHECK_THAT(static_cast<double>(position->baseComponent(2)), WithinAbs(3.0, 1e-4));
    }
    SECTION("camera.frame_node moves the camera and explains what it measured") {
        const auto nodes = f.call("scene.find_nodes");
        REQUIRE(nodes.success);
        REQUIRE(nodes.value.at("matched").get<int>() > 0);
        const std::string name = nodes.value.at("nodes").at(0).at("name").get<std::string>();

        const auto result = f.call("camera.frame_node", json{{"name", name}, {"margin", 1.5}});
        REQUIRE(result.success);
        CHECK(result.value.at("subject") == name);
        CHECK(result.value.at("distance").get<double>() > 0.0);
        CHECK(!result.value.at("basis").get<std::string>().empty());
    }
    SECTION("camera.frame_node refuses a node that is not there") {
        const auto result = f.call("camera.frame_node", json{{"name", "no-such-node"}});
        REQUIRE_FALSE(result.success);
        CHECK(result.error->recovery.find("scene.find_nodes") != std::string::npos);
    }
}

TEST_CASE("Scene tools read and move real composition nodes", "[ai][tools]") {
    Fixture f;
    REQUIRE(f.engine.loadComposition(helixScene()).has_value());

    const auto summary = f.call("scene.get_summary");
    REQUIRE(summary.success);
    REQUIRE(summary.value.at("nodes").size() > 0);
    const std::string name = summary.value.at("nodes").at(0).at("name").get<std::string>();

    SECTION("scene.get_node lists the parameters that drive it") {
        const auto result = f.call("scene.get_node", json{{"name", name}});
        REQUIRE(result.success);
        CHECK(result.value.at("name") == name);
        REQUIRE(result.value.at("parameters").size() > 0);
    }
    SECTION("scene.set_node_transform writes the node's position parameter") {
        const auto result = f.call("scene.set_node_transform",
                                   json{{"name", name}, {"position", json::array({5.0, 6.0, 7.0})}});
        REQUIRE(result.success);
        const params::IParameter* position =
            f.engine.params().find("nodes/" + name + "/position");
        REQUIRE(position != nullptr);
        CHECK_THAT(static_cast<double>(position->baseComponent(1)), WithinAbs(6.0, 1e-4));
    }
    SECTION("scene.set_node_transform with nothing to write says so instead of succeeding") {
        const auto result = f.call("scene.set_node_transform", json{{"name", name}});
        REQUIRE_FALSE(result.success);
        CHECK(result.error->code == ai::ToolErrorCode::InvalidArguments);
    }
    SECTION("scene.set_node_visible hides a node") {
        const auto result =
            f.call("scene.set_node_visible", json{{"name", name}, {"visible", false}});
        REQUIRE(result.success);
        const params::IParameter* visible = f.engine.params().find("nodes/" + name + "/visible");
        REQUIRE(visible != nullptr);
        CHECK(visible->baseComponent(0) < 0.5F);
    }
}

TEST_CASE("environment.set applies several atmosphere values in one call", "[ai][tools]") {
    Fixture f;
    REQUIRE(f.engine.loadComposition(helixScene()).has_value());

    const auto result = f.call(
        "environment.set",
        json{{"values", json{{"scene/fogDensity", 0.12}, {"scene/brightness", 1.4}}}});
    REQUIRE(result.success);
    CHECK(result.value.at("applied").size() == 2);
    const params::IParameter* fog = f.engine.params().find("scene/fogDensity");
    REQUIRE(fog != nullptr);
    CHECK_THAT(static_cast<double>(fog->baseComponent(0)), WithinAbs(0.12, 1e-4));

    SECTION("a path this scene does not have is named, not ignored") {
        const auto mixed = f.call(
            "environment.set",
            json{{"values", json{{"scene/fogDensity", 0.2}, {"env/sky/aurora", 1.0}}}});
        REQUIRE(mixed.success);
        CHECK(mixed.value.at("notFound").at(0) == "env/sky/aurora");
    }
    SECTION("all-unknown fails rather than reporting an empty success") {
        const auto none =
            f.call("environment.set", json{{"values", json{{"env/sky/aurora", 1.0}}}});
        REQUIRE_FALSE(none.success);
        CHECK(none.error->code == ai::ToolErrorCode::NotFound);
    }
}

TEST_CASE("lighting.list says why individual lights cannot be edited", "[ai][tools]") {
    Fixture f;
    REQUIRE(f.engine.loadComposition(helixScene()).has_value());
    const auto result = f.call("lighting.list");
    REQUIRE(result.success);
    // §54: where an operation cannot be safely exposed, document the limitation rather than paper
    // over it. The note is part of the contract, not decoration.
    REQUIRE(result.value.contains("note"));
    CHECK(result.value.at("note").get<std::string>().find("rebuild") != std::string::npos);
}

TEST_CASE("The capability registry reports what is missing and why", "[ai][tools]") {
    Fixture f;
    const auto result = f.call("capability.list");
    REQUIRE(result.success);

    bool sawUnavailableWithReason = false;
    bool sawTerrain = false;
    for (const auto& domain : result.value.at("domains")) {
        if (!domain.value("available", true)) {
            REQUIRE(domain.contains("unavailableReason"));
            CHECK(domain.at("unavailableReason").get<std::string>().size() > 40);
            sawUnavailableWithReason = true;
        }
        if (domain.at("id") == "terrain") {
            sawTerrain = true;
            CHECK_FALSE(domain.at("available").get<bool>());
        }
    }
    CHECK(sawUnavailableWithReason);
    CHECK(sawTerrain);
    CHECK(result.value.at("thisSession").at("composition").get<bool>() == false);

    SECTION("and each available domain's operations come from the tool registry itself") {
        for (const auto& domain : result.value.at("domains")) {
            if (domain.at("id") != "parameter") {
                continue;
            }
            REQUIRE(domain.contains("operations"));
            bool sawSet = false;
            for (const auto& op : domain.at("operations")) {
                if (op.at("name") == "parameter.set") {
                    sawSet = true;
                    CHECK(op.at("mutatesProject").get<bool>());
                    CHECK(op.at("reversible").get<bool>());
                }
            }
            CHECK(sawSet);
        }
    }
}

// ---- transactions ---------------------------------------------------------------------------------

TEST_CASE("A snapshot round-trips the parameter domain", "[ai][transaction]") {
    app::Engine engine(app::EngineMode::Offline);
    ai::SnapshotStore store;

    auto* scale = engine.params().find("orb/scale");
    REQUIRE(scale != nullptr);
    scale->setBaseComponent(0, 1.5f);
    const std::string id = store.capture(engine, "before").id;

    const std::size_t baseline = engine.modulator().routes().size();

    scale->setBaseComponent(0, 4.0f);
    params::ModRoute route;
    route.source = "audio.bass";
    route.target = "orb/emissive";
    engine.modulator().addRoute(route);
    engine.rebind();
    REQUIRE(engine.modulator().routes().size() == baseline + 1);

    REQUIRE(store.restore(engine, id).has_value());
    // The pointer is looked up again: loadProject replaces values, and a stale pointer would be
    // the exact bug the rebind exists to prevent.
    CHECK_THAT(static_cast<double>(engine.params().find("orb/scale")->baseComponent(0)), WithinAbs(1.5, 1e-5));
    CHECK(engine.modulator().routes().size() == baseline);
}

TEST_CASE("A snapshot restores the timeline too", "[ai][transaction]") {
    app::Engine engine(app::EngineMode::Offline);
    ai::SnapshotStore store;
    const std::string id = store.capture(engine, "empty").id;

    params::Track track;
    track.target = "orb/scale";
    track.addKey(params::Key{.time = 1.0, .value = {2.0f, 0, 0, 0}});
    engine.timeline().addTrack(track);
    REQUIRE(engine.timeline().tracks().size() == 1);

    REQUIRE(store.restore(engine, id).has_value());
    CHECK(engine.timeline().tracks().empty());
}

TEST_CASE("A restored route is bound, not merely present", "[ai][transaction][regression]") {
    // ADR-019 shipped a loadProject that destroyed the routes it had just installed. A restore
    // that leaves routes unbound is the same failure with a different name.
    app::Engine engine(app::EngineMode::Offline);
    ai::SnapshotStore store;
    params::ModRoute route;
    route.source = "audio.bass";
    route.target = "orb/emissive";
    route.amount = 2.0f;
    const std::size_t baseline = engine.modulator().routes().size();
    engine.modulator().addRoute(route);
    engine.rebind();
    const std::string id = store.capture(engine, "with route").id;

    engine.modulator().clearRoutes();
    engine.rebind();
    REQUIRE(engine.modulator().routes().empty());

    REQUIRE(store.restore(engine, id).has_value());
    REQUIRE(engine.modulator().routes().size() == baseline + 1);
    const auto& routes = engine.modulator().routes();
    const auto restored = std::find_if(routes.begin(), routes.end(), [](const params::ModRoute& r) {
        return r.source == "audio.bass" && r.target == "orb/emissive";
    });
    REQUIRE(restored != routes.end());
    CHECK(restored->targetParam != nullptr);
    CHECK(restored->sourceId != signals::kInvalidSignal);
}

TEST_CASE("A transaction rolls back every change together", "[ai][transaction]") {
    app::Engine engine(app::EngineMode::Offline);
    ai::SnapshotStore store;
    ai::SnapshotTransactionSink sink(engine, store);

    const auto before = static_cast<double>(engine.params().find("orb/scale")->baseComponent(0));
    {
        ai::Transaction transaction(sink, "make it bigger");
        engine.params().find("orb/scale")->setBaseComponent(0, 6.0f);
        engine.params().find("orb/emissive")->setBaseComponent(0, 12.0f);
        transaction.rollback();
    }
    CHECK_THAT(static_cast<double>(engine.params().find("orb/scale")->baseComponent(0)), WithinAbs(before, 1e-5));

    SECTION("and leaves them alone when committed") {
        {
            ai::Transaction transaction(sink, "keep it");
            engine.params().find("orb/scale")->setBaseComponent(0, 6.0f);
            transaction.commit();
        }
        CHECK_THAT(static_cast<double>(engine.params().find("orb/scale")->baseComponent(0)), WithinAbs(6.0, 1e-5));
    }
    SECTION("an un-committed transaction rolls back when it goes out of scope") {
        {
            ai::Transaction transaction(sink, "abandoned");
            engine.params().find("orb/scale")->setBaseComponent(0, 7.0f);
        }
        CHECK_THAT(static_cast<double>(engine.params().find("orb/scale")->baseComponent(0)), WithinAbs(before, 1e-5));
    }
}

TEST_CASE("changedParameters names exactly what moved", "[ai][transaction]") {
    app::Engine engine(app::EngineMode::Offline);
    const nlohmann::json before = ai::SnapshotStore::captureDocument(engine);
    engine.params().find("orb/scale")->setBaseComponent(0, 3.0f);
    engine.params().find("orb/emissive")->setBaseComponent(0, 2.0f);
    const nlohmann::json after = ai::SnapshotStore::captureDocument(engine);

    const auto changed = ai::SnapshotStore::changedParameters(before, after);
    REQUIRE(changed.size() == 2);
    CHECK(changed[0] == "orb/emissive");
    CHECK(changed[1] == "orb/scale");
}

TEST_CASE("project.create_snapshot and restore_snapshot work through the tool API", "[ai][tools]") {
    Fixture f;
    const auto created = f.call("project.create_snapshot", json{{"label", "before the pass"}});
    REQUIRE(created.success);
    const std::string id = created.value.at("id").get<std::string>();

    REQUIRE(f.call("parameter.set", json{{"path", "orb/scale"}, {"value", 5.0}}).success);
    const auto restored = f.call("project.restore_snapshot", json{{"id", id}});
    REQUIRE(restored.success);
    CHECK(restored.value.at("parametersChanged").get<int>() == 1);
    CHECK_THAT(static_cast<double>(f.engine.params().find("orb/scale")->baseComponent(0)), WithinAbs(1.0, 1e-5));

    SECTION("an unknown id is a recoverable error") {
        const auto bad = f.call("project.restore_snapshot", json{{"id", "snap-999"}});
        REQUIRE_FALSE(bad.success);
        CHECK(bad.error->recovery.find("project.list_snapshots") != std::string::npos);
    }
}

TEST_CASE("A tool's one-line summary agrees with its structured result",
          "[ai][tools][regression]") {
    // Caught by running the thing: several summaries read the result object in the same expression
    // that moved it into `ToolResult::ok`, which is unsequenced -- so `environment.set` said "0
    // environment value(s) set" while having set four of them, and `scene.get_summary` said "0
    // node(s)" about a scene with nodes. A tool whose report disagrees with what it did is the
    // precise failure this whole API exists to prevent, committed inside the API.
    Fixture f;
    REQUIRE(f.engine.loadComposition(helixScene()).has_value());

    const auto summary = f.call("scene.get_summary");
    REQUIRE(summary.success);
    CHECK(summary.summary.find(std::to_string(summary.value.at("nodes").size()) + " node") !=
          std::string::npos);
    CHECK_FALSE(summary.summary.starts_with("0 node"));

    const auto capability = f.call("capability.list");
    REQUIRE(capability.success);
    CHECK(capability.summary.find(std::to_string(capability.value.at("domains").size()) +
                                  " domain") != std::string::npos);

    const auto env = f.call(
        "environment.set",
        json{{"values", json{{"scene/fogDensity", 0.2}, {"scene/brightness", 1.1}}}});
    REQUIRE(env.success);
    CHECK(env.value.at("applied").size() == 2);
    CHECK(env.summary.find("2 environment value") != std::string::npos);

    const auto tools = f.call("capability.list_tools");
    REQUIRE(tools.success);
    CHECK(tools.summary.find(std::to_string(tools.value.at("tools").size()) + " tool") !=
          std::string::npos);

    const auto groups = f.call("parameter.list_groups");
    REQUIRE(groups.success);
    CHECK(groups.summary.find(std::to_string(groups.value.at("groups").size()) + " group") !=
          std::string::npos);

    const auto search = f.call("parameter.search", json{{"query", "fog"}, {"limit", 3}});
    REQUIRE(search.success);
    CHECK(search.value.at("returned").get<std::size_t>() ==
          search.value.at("parameters").size());

    const auto routes = f.call("modulation.list");
    REQUIRE(routes.success);
    CHECK(routes.summary.find(std::to_string(routes.value.at("routes").size()) + " route") !=
          std::string::npos);
}

TEST_CASE("Tool results are compact and carry the error shape the spec asks for", "[ai][tools]") {
    Fixture f;
    const auto ok = f.call("parameter.set", json{{"path", "orb/scale"}, {"value", 2.0}});
    const json okJson = ok.toJson();
    CHECK(okJson.at("success").get<bool>());
    CHECK(okJson.contains("result"));
    CHECK_FALSE(okJson.contains("error"));

    const auto bad = f.call("parameter.set", json{{"path", "nope"}, {"value", 2.0}});
    const json badJson = bad.toJson();
    CHECK_FALSE(badJson.at("success").get<bool>());
    REQUIRE(badJson.contains("error"));
    CHECK(badJson.at("error").at("code") == "NOT_FOUND");
    CHECK(badJson.at("error").contains("message"));
    CHECK(badJson.at("error").contains("recovery"));
}

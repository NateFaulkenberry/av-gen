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
#include "scene/composition.hpp"
#include "support/synth.hpp"
#include "audio/audio_file.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <unistd.h>

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

// ---- the piece, as opposed to the tracks it bakes into -------------------------------------------

TEST_CASE("sequence.get_state reports the music video, not the timeline", "[ai][tools][sequence]") {
    // Night Shift is the repository's worked music video, so this asserts against a real piece
    // rather than a fixture: five shots cutting between two scenes, a character with animation
    // cues, lyric overlays, and the song's own sections.
    const std::filesystem::path project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "city" / "night-shift.json";
    if (!std::filesystem::exists(project)) {
        SKIP("night-shift.json is not present in this checkout");
    }
    Fixture f;
    const auto loaded = f.engine.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());

    const auto result = f.call("sequence.get_state");
    REQUIRE(result.success);
    const json& out = result.value;

    // The thing the assistant could not previously answer at all: what shots are in this video.
    REQUIRE(out.contains("shots"));
    CHECK(out["shotCount"].get<std::size_t>() == 5);
    REQUIRE(out["shots"].size() == 5);
    CHECK(out["shots"][0]["name"].get<std::string>() == "First Light");
    CHECK(out["shots"][1]["name"].get<std::string>() == "The Walk");
    // A shot has to say when it cuts, or it cannot be placed against anything.
    CHECK(out["shots"][1]["startSeconds"].get<double>() > 0.0);
    CHECK(out["shots"][1]["endSeconds"].get<double>() >
          out["shots"][1]["startSeconds"].get<double>());
    // And which scene it is in, which is what makes "Crosstown" a different place from "The Walk".
    CHECK(out["shots"][1]["scene"].get<std::string>() == "plaza");
    CHECK(out["shots"][3]["scene"].get<std::string>() == "downtown");
    CHECK(out["scenes"].size() == 2);

    // The character, and the clips it plays. A shot that wants the hero running has to be able to
    // find out that it runs at all.
    CHECK(out["actorCount"].get<std::size_t>() == 1);
    REQUIRE(out["actors"].size() == 1);
    CHECK(out["actors"][0]["id"].get<std::string>() == "hero");
    CHECK(out["actors"][0]["clips"].size() >= 3);

    CHECK(out["overlayCount"].get<std::size_t>() > 0); // the lyrics

    // The song's landmarks. These are what "cut on the chorus" means, and they were invisible.
    REQUIRE(out.contains("sections"));
    CHECK(out["sections"].size() == 7);
    bool sawChorus = false;
    for (const json& section : out["sections"]) {
        sawChorus = sawChorus || section["name"].get<std::string>() == "CHORUS";
    }
    CHECK(sawChorus);

    // The audio it is cut to, by name and length rather than only as whatever the analyser hears at
    // this instant -- which is all `audio.get_analysis` can say.
    REQUIRE(out.contains("audio"));
    CHECK(out["audio"]["hasAudio"].get<bool>());
    CHECK(out["audio"]["file"].get<std::string>() == "night-shift.wav");
    CHECK(out["audio"]["durationSeconds"].get<double>() > 100.0);
}

TEST_CASE("sequence.get_state hands back beats only where they were asked for", "[ai][tools][sequence]") {
    const std::filesystem::path project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "city" / "night-shift.json";
    if (!std::filesystem::exists(project)) {
        SKIP("night-shift.json is not present in this checkout");
    }
    Fixture f;
    REQUIRE(f.engine.loadProject(project).has_value());

    // Without the argument, the summary and nothing more. A three-minute song has hundreds of
    // beats and sending them on every call would crowd out the rest of the answer.
    const auto plain = f.call("sequence.get_state");
    REQUIRE(plain.success);
    REQUIRE(plain.value.contains("audio"));
    CHECK_FALSE(plain.value["audio"].contains("beatsNear"));
    const auto beatCount = plain.value["audio"].value("beatCount", std::size_t{0});
    CHECK(beatCount > 0);

    // With it, the beats around one moment -- which is what "the downbeat before the chorus" needs.
    const auto near = f.call("sequence.get_state", json{{"beatsAround", 40.0}, {"beatWindow", 2.0}});
    REQUIRE(near.success);
    REQUIRE(near.value["audio"].contains("beatsNear"));
    const json& beats = near.value["audio"]["beatsNear"];
    CHECK(!beats.empty());
    CHECK(beats.size() < beatCount); // a window, not the whole song
    for (const json& beat : beats) {
        CHECK(beat.get<double>() >= 38.0);
        CHECK(beat.get<double>() <= 42.0);
    }
}

// ---- the project's own life cycle ---------------------------------------------------------------

TEST_CASE("An assistant can create a project, save it and open it again", "[ai][tools][project]") {
    // The round trip the bootstrap prompt calls its success criterion: a project that only exists
    // in the process that made it is not a project. Everything else the assistant can do edits a
    // session; this is the pair of verbs that makes one persist.
    const auto root = std::filesystem::temp_directory_path() /
                      ("avgen_ai_projects_" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    Fixture f;
    f.ctx.setProjectsRoot(root);

    // Nothing there yet, and the tool says so rather than inventing a listing.
    auto listed = f.call("project.list");
    REQUIRE(listed.success);
    CHECK(listed.value["projects"].empty());

    const auto created = f.call("project.create", json{{"name", "All You Got"}});
    INFO((created.error ? created.error->message : std::string{}));
    REQUIRE(created.success);
    CHECK(created.value["exists"].get<bool>());
    const auto file = std::filesystem::path(created.value["file"].get<std::string>());
    CHECK(std::filesystem::exists(file));           // on disk, not merely reported
    CHECK(f.engine.projectPath() == file);          // and the session is now that project

    // A value set after creation has to survive the save, or "saved" means nothing. `orb/scale`
    // rather than anything scene-shaped: a brand new project has the built-in orb and no
    // composition, which is exactly the state `project.create` leaves behind.
    REQUIRE(f.engine.params().find("orb/scale") != nullptr);
    f.engine.params().find("orb/scale")->setBaseComponent(0, 0.42f);
    const auto saved = f.call("project.save");
    REQUIRE(saved.success);
    CHECK(saved.value["exists"].get<bool>());
    CHECK(saved.value["bytes"].get<std::uint64_t>() > 0);

    listed = f.call("project.list");
    REQUIRE(listed.success);
    REQUIRE(listed.value["projects"].size() == 1);
    CHECK(listed.value["projects"][0].get<std::string>() == "All You Got");

    // Open it into a *different* engine, which is the only version of this test worth running: the
    // same engine would pass even if the file on disk were empty.
    Fixture other;
    other.ctx.setProjectsRoot(root);
    const auto opened = other.call("project.open", json{{"name", "All You Got"}});
    INFO((opened.error ? opened.error->message : std::string{}));
    REQUIRE(opened.success);
    CHECK(opened.value["parameters"].get<std::size_t>() > 0);
    const auto* scale = other.engine.params().find("orb/scale");
    REQUIRE(scale != nullptr);
    CHECK_THAT(scale->baseComponent(0), WithinAbs(0.42f, 1e-4));

    std::filesystem::remove_all(root);
}

TEST_CASE("The project tools refuse a name that would escape the projects folder", "[ai][tools][project]") {
    // A tool that took a path would let one bad argument write anywhere. These take a name, and a
    // name that is not a name is reported rather than quietly rewritten into something safe --
    // sanitising it would hide the mistake from the thing that made it.
    const auto root = std::filesystem::temp_directory_path() /
                      ("avgen_ai_projects_guard_" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    Fixture f;
    f.ctx.setProjectsRoot(root);

    for (const char* bad : {"../escape", "..", ".hidden", "a/b", "", "with\\backslash"}) {
        INFO("name: " << bad);
        const auto r = f.call("project.create", json{{"name", bad}});
        CHECK_FALSE(r.success);
        REQUIRE(r.error.has_value());
        CHECK(r.error->code == ai::ToolErrorCode::InvalidArguments);
    }
    // Nothing was created by any of them.
    std::size_t entries = 0;
    for (const auto& e : std::filesystem::directory_iterator(root)) { (void)e; ++entries; }
    CHECK(entries == 0);

    // And a real name still works, so the guard is a guard and not a wall.
    CHECK(f.call("project.create", json{{"name", "Night Shift"}}).success);
    // Creating it twice is a conflict, not a silent overwrite of somebody's work.
    const auto again = f.call("project.create", json{{"name", "Night Shift"}});
    CHECK_FALSE(again.success);
    REQUIRE(again.error.has_value());
    CHECK(again.error->code == ai::ToolErrorCode::Conflict);

    std::filesystem::remove_all(root);
}

TEST_CASE("Saving without a project says which tool to use instead", "[ai][tools][project]") {
    Fixture f; // no projects root, no project path
    const auto r = f.call("project.save");
    CHECK_FALSE(r.success);
    REQUIRE(r.error.has_value());
    CHECK(r.error->code == ai::ToolErrorCode::InvalidArguments);
    CHECK(r.error->recovery.find("project.create") != std::string::npos);

    // And without a root configured, the name-based tools refuse rather than guessing a location.
    const auto created = f.call("project.create", json{{"name", "Somewhere"}});
    CHECK_FALSE(created.success);
    REQUIRE(created.error.has_value());
    CHECK(created.error->code == ai::ToolErrorCode::Unavailable);
}

// ---- generating a world -------------------------------------------------------------------------

TEST_CASE("The assistant can find a recipe and generate the world it describes", "[ai][tools][world]") {
    const std::filesystem::path recipes = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "recipes";
    if (!std::filesystem::is_directory(recipes)) {
        SKIP("examples/recipes is not present in this checkout");
    }
    Fixture f;
    f.ctx.setContentRoots({std::filesystem::path(AVGEN_SOURCE_DIR) / "examples",
                           std::filesystem::path(AVGEN_SOURCE_DIR) / "assets"});

    const auto listed = f.call("world.list_recipes");
    REQUIRE(listed.success);
    REQUIRE(!listed.value["recipes"].empty());
    const std::string name = listed.value["recipes"][0]["name"].get<std::string>();

    const auto made = f.call("world.generate", json{{"recipe", name}});
    INFO((made.error ? made.error->message : std::string{}));
    REQUIRE(made.success);
    // Layers, not instances: composing produces the rule for each scatter and the instances only
    // exist once terrain is built from it. A tool that reported an instance count here would be
    // inventing a number this stage has not computed.
    CHECK(made.value["layers"].get<std::size_t>() > 0);
    CHECK(made.value["assetsConsidered"].get<std::size_t>() > 0);
    CHECK_FALSE(made.value.contains("warning"));
    REQUIRE(made.value.contains("layerDetail"));
    CHECK(made.value["layerDetail"][0].contains("category"));
    // It installed into the session, not merely composed in the air.
    REQUIRE(f.engine.composition() != nullptr);
}

TEST_CASE("A recipe outside the content roots cannot be reached", "[ai][tools][world]") {
    Fixture f; // no content roots at all
    const auto listed = f.call("world.list_recipes");
    REQUIRE(listed.success);
    CHECK(listed.value["recipes"].empty()); // nothing reachable, reported as nothing

    const auto made = f.call("world.generate", json{{"recipe", "glowmere-dense"}});
    CHECK_FALSE(made.success);
    REQUIRE(made.error.has_value());
    CHECK(made.error->code == ai::ToolErrorCode::NotFound);
}

TEST_CASE("Content resolution refuses a path that climbs out of its root", "[ai][tools][world]") {
    // The containment test resolves symlinks and `..` before comparing, because a prefix test on an
    // unresolved path is not a containment test: "<root>/../../etc/passwd" starts with the root.
    const auto root = std::filesystem::temp_directory_path() /
                      ("avgen_ai_content_" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "inside");
    std::ofstream(root / "inside" / "ok.txt") << "x";

    Fixture f;
    f.ctx.setContentRoots({root});
    CHECK(f.ctx.resolveContent(root / "inside" / "ok.txt").has_value());
    CHECK_FALSE(f.ctx.resolveContent(root / ".." / ".." / "etc" / "passwd").has_value());
    CHECK_FALSE(f.ctx.resolveContent("/etc/passwd").has_value());
    CHECK_FALSE(f.ctx.resolveContent(std::filesystem::path("/tmp")).has_value());

    std::filesystem::remove_all(root);
}

// ---- bringing media in --------------------------------------------------------------------------

TEST_CASE("The assistant can create a project and import a track into it", "[ai][tools][import]") {
    // The bootstrap prompt's first two sections, in order, as one test: make a project, find the
    // audio, import it, and have the project own the copy rather than the original.
    const auto root = std::filesystem::temp_directory_path() /
                      ("avgen_ai_import_" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::remove_all(root);
    const auto projects = root / "projects";
    const auto desktop = root / "desktop";
    std::filesystem::create_directories(projects);
    std::filesystem::create_directories(desktop);

    // A real decodable file, written the way the audio tests make fixtures.
    constexpr std::uint32_t rate = 48000;
    const auto tone = audio::AudioFile::fromInterleaved(
        testsupport::interleave(testsupport::sine(220.0f, rate, rate * 2, 0.4f), 2), 2, rate);
    const auto source = desktop / "test.wav";
    REQUIRE(tone.writeWav(source).has_value());

    Fixture f;
    f.ctx.setProjectsRoot(projects);
    f.ctx.setContentRoots({desktop});

    // Importing before there is a project says which tool to reach for, rather than writing the
    // copy somewhere arbitrary.
    auto early = f.call("asset.import_audio", json{{"file", source.string()}});
    CHECK_FALSE(early.success);
    REQUIRE(early.error.has_value());
    CHECK(early.error->recovery.find("project.create") != std::string::npos);

    REQUIRE(f.call("project.create", json{{"name", "All You Got"}}).success);

    const auto found = f.call("asset.list_importable", json{{"kind", "audio"}});
    REQUIRE(found.success);
    REQUIRE(found.value["files"].size() == 1);
    CHECK(found.value["files"][0]["name"].get<std::string>() == "test.wav");

    const auto imported = f.call("asset.import_audio", json{{"file", source.string()}});
    INFO((imported.error ? imported.error->message : std::string{}));
    REQUIRE(imported.success);
    CHECK_THAT(imported.value["durationSeconds"].get<double>(), WithinAbs(2.0, 0.05));
    CHECK(imported.value["sampleRate"].get<std::uint32_t>() == rate);
    CHECK(imported.value["channels"].get<std::uint32_t>() == 2);
    CHECK(f.engine.hasAudio());

    // The project owns a copy. This is the property the prompt asks for in so many words: the
    // project must survive the original moving.
    const auto copy = std::filesystem::path(imported.value["file"].get<std::string>());
    CHECK(std::filesystem::exists(copy));
    CHECK(copy.parent_path().parent_path() == projects / "All You Got");
    std::filesystem::remove(source);
    CHECK(std::filesystem::exists(copy)); // the original is gone and the project still has it

    std::filesystem::remove_all(root);
}

TEST_CASE("Importing refuses a file outside the readable folders", "[ai][tools][import]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("avgen_ai_import_guard_" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "allowed");
    std::filesystem::create_directories(root / "secret");
    std::ofstream(root / "secret" / "private.wav") << "not audio";

    Fixture f;
    f.ctx.setProjectsRoot(root / "projects");
    f.ctx.setContentRoots({root / "allowed"});
    REQUIRE(f.call("project.create", json{{"name", "Guarded"}}).success);

    for (const auto& bad : {root / "secret" / "private.wav",
                            root / "allowed" / ".." / "secret" / "private.wav"}) {
        INFO(bad.string());
        const auto r = f.call("asset.import_audio", json{{"file", bad.string()}});
        CHECK_FALSE(r.success);
        REQUIRE(r.error.has_value());
        CHECK(r.error->code == ai::ToolErrorCode::NotFound);
    }
    // And nothing was copied into the project by the attempts.
    CHECK_FALSE(std::filesystem::exists(root / "projects" / "Guarded" / "audio"));

    std::filesystem::remove_all(root);
}

// ---- authoring the piece ------------------------------------------------------------------------

TEST_CASE("The assistant can author shots, markers and a lyric placeholder", "[ai][tools][authoring]") {
    // §3, §5, §13 and §20 of the bootstrap prompt: the structure of a music video, made by the
    // assistant rather than read by it.
    Fixture f;
    const auto marker = f.call("sequence.add_marker",
                               json{{"time", 40.0}, {"name", "CHORUS"}, {"kind", "section"}});
    INFO((marker.error ? marker.error->message : std::string{}));
    REQUIRE(marker.success);
    CHECK(marker.value["markers"].get<std::size_t>() == 1);

    REQUIRE(f.call("sequence.add_shot", json{{"name", "Establish"}, {"start", 0.0}, {"duration", 12.0}}).success);
    const auto second = f.call("sequence.add_shot",
                               json{{"name", "The Walk"}, {"start", 12.0}, {"duration", 20.0}});
    REQUIRE(second.success);
    CHECK(second.value["shots"].get<std::size_t>() == 2);
    // The piece's duration follows its shots rather than being set by hand.
    CHECK(second.value["durationSeconds"].get<double>() >= 32.0);

    // A shot name is unique, and a second one says so rather than quietly making a duplicate that
    // is impossible to address afterwards.
    const auto dup = f.call("sequence.add_shot", json{{"name", "The Walk"}, {"start", 40.0}});
    CHECK_FALSE(dup.success);
    REQUIRE(dup.error.has_value());
    CHECK(dup.error->code == ai::ToolErrorCode::Conflict);

    // A scene slot that does not exist is caught here rather than becoming a shot that cuts to
    // nothing.
    const auto ghost = f.call("sequence.add_shot", json{{"name", "Nowhere"}, {"scene", "atlantis"}});
    CHECK_FALSE(ghost.success);
    REQUIRE(ghost.error.has_value());
    CHECK(ghost.error->code == ai::ToolErrorCode::NotFound);

    const auto lyric = f.call("sequence.add_overlay",
                              json{{"text", "[LYRICS PLACEHOLDER]"}, {"start", 4.0}, {"end", 8.0}});
    INFO((lyric.error ? lyric.error->message : std::string{}));
    REQUIRE(lyric.success);
    CHECK(lyric.value["overlays"].get<std::size_t>() == 1);
    // An overlay that ends before it starts is refused, not silently swapped.
    CHECK_FALSE(f.call("sequence.add_overlay",
                       json{{"text", "backwards"}, {"start", 9.0}, {"end", 2.0}}).success);

    // Everything lands in the piece the read tool reports, which is the check that these two halves
    // are talking about the same object.
    const auto state = f.call("sequence.get_state");
    REQUIRE(state.success);
    CHECK(state.value["shotCount"].get<std::size_t>() == 2);
    CHECK(state.value["overlayCount"].get<std::size_t>() == 1);
    REQUIRE(state.value["sections"].size() == 1);
    CHECK(state.value["sections"][0]["name"].get<std::string>() == "CHORUS");
}

TEST_CASE("Re-installing a sequence replaces its tracks instead of stacking them", "[ai][tools][authoring]") {
    // The property that makes a bake-per-edit the right shape: five edits must not leave five
    // copies of the same track behind. If this ever regressed, every assistant edit would double
    // the timeline.
    Fixture f;
    REQUIRE(f.call("sequence.add_shot", json{{"name", "One"}, {"start", 0.0}, {"duration", 5.0}}).success);
    const std::size_t afterFirst = f.engine.timeline().tracks().size();
    for (int i = 0; i < 4; ++i) {
        REQUIRE(f.call("sequence.add_marker",
                       json{{"time", static_cast<double>(i)}, {"name", "M" + std::to_string(i)}})
                    .success);
    }
    CHECK(f.engine.timeline().tracks().size() == afterFirst);
    CHECK(f.engine.sequence().markers.size() == 4);
}

// ---- entities and the fields that govern them ---------------------------------------------------

TEST_CASE("The assistant can put a music field into a scene", "[ai][tools][field]") {
    // §18 of the bootstrap brief -- "proximity influence prototype" -- against an engine that has
    // the finished version of it (ADR-097) and, until now, no way to be asked for one.
    Fixture f;
    REQUIRE(f.engine.loadFile(helixScene()).has_value());
    REQUIRE(f.engine.composition() != nullptr);

    const auto before = f.call("field.list");
    REQUIRE(before.success);
    CHECK(before.value["fields"].empty());

    const auto made = f.call("field.create", json{{"name", "headphones"},
                                                  {"radius", 14.0},
                                                  {"strength", 1.5},
                                                  {"falloff", "smooth"}});
    INFO((made.error ? made.error->message : std::string{}));
    REQUIRE(made.success);
    CHECK_THAT(made.value["reach"].get<double>(), WithinAbs(14.0, 1e-3));
    // A field that follows nothing says so, rather than leaving an author to wonder why it never
    // moves.
    CHECK(made.value.contains("note"));

    const auto after = f.call("field.list");
    REQUIRE(after.success);
    REQUIRE(after.value["fields"].size() == 1);
    CHECK(after.value["fields"][0]["name"].get<std::string>() == "headphones");
    // The install reports what each field resolved to, which is how an author learns whether it is
    // scrub-exact (ADR-091) without rendering the same frame twice.
    CHECK(!after.value["resolution"].empty());

    // A second field of the same name is a conflict, not a silent duplicate.
    const auto dup = f.call("field.create", json{{"name", "headphones"}});
    CHECK_FALSE(dup.success);
    REQUIRE(dup.error.has_value());
    CHECK(dup.error->code == ai::ToolErrorCode::Conflict);

    // A falloff that is not one is refused with the list of the ones that are.
    const auto bad = f.call("field.create", json{{"name", "other"}, {"falloff", "exponential"}});
    CHECK_FALSE(bad.success);
    REQUIRE(bad.error.has_value());
    CHECK(bad.error->recovery.find("smooth") != std::string::npos);
}

TEST_CASE("Entity and field tools say so when there is no composition", "[ai][tools][field]") {
    Fixture f; // the built-in orb, no composition
    for (const char* tool : {"entity.list", "field.list"}) {
        INFO(tool);
        const auto r = f.call(tool);
        CHECK_FALSE(r.success);
        REQUIRE(r.error.has_value());
        CHECK(r.error->code == ai::ToolErrorCode::Unavailable);
    }
}

// ---- looking at the frame -----------------------------------------------------------------------

TEST_CASE("The assistant can check whether a shot frames its subject", "[ai][tools][probe]") {
    // The prompt asks the assistant to verify its own work and it has no eyes: every provider
    // declares vision false. This is the frame described as numbers instead.
    Fixture f;
    REQUIRE(f.engine.loadFile(helixScene()).has_value());
    REQUIRE(f.engine.composition() != nullptr);
    // The first node that actually has geometry: a composition may open with lights or empties,
    // and "frame that" is not a meaningful request about a node with no bounds.
    std::string node;
    for (const auto& n : f.engine.composition()->nodes()) {
        if (f.engine.composition()->nodeBounds(n->name).valid) {
            node = n->name;
            break;
        }
    }
    REQUIRE(!node.empty());

    // Frame it deliberately, then ask. `camera.frame_node` is the tool an assistant would have used.
    REQUIRE(f.call("camera.frame_node", json{{"name", node}}).success);
    const auto framed = f.call("render.probe", json{{"node", node}});
    INFO((framed.error ? framed.error->message : std::string{}));
    REQUIRE(framed.success);
    CHECK(framed.value["node"]["onScreen"].get<bool>());
    // It fills a sensible part of the height rather than being a speck or overflowing entirely.
    const double height = framed.value["node"]["heightFraction"].get<double>();
    CHECK(height > 0.05);
    CHECK(framed.value["node"]["frame"]["left"].get<double>() < 1.0);

    // Now point the camera away from everything and ask again. This is the case worth having: a
    // camera aimed at nothing must report nothing, not look fine.
    REQUIRE(f.call("camera.set", json{{"position", {0.0, 5000.0, 0.0}},
                                      {"target", {0.0, 6000.0, 0.0}}}).success);
    // The probe reads the camera a frame produced, the same as camera.get, so let one happen. In a
    // live session frames run between tool calls; in a test nothing does unless it is asked for.
    f.engine.update(FrameTime{0.0, 1.0 / 60.0, 0});
    const auto empty = f.call("render.probe");
    REQUIRE(empty.success);
    CHECK(empty.value["visible"].empty());
    CHECK(empty.value["offScreen"].get<std::size_t>() > 0);
    CHECK(empty.value.contains("warning"));

    // A node that does not exist is a named error, not an empty answer that reads as "not visible".
    const auto ghost = f.call("render.probe", json{{"node", "no-such-node"}});
    CHECK_FALSE(ghost.success);
    REQUIRE(ghost.error.has_value());
    CHECK(ghost.error->code == ai::ToolErrorCode::NotFound);
}

TEST_CASE("The frame probe uses the whole bounding box, not the centre", "[ai][tools][probe]") {
    // A building whose centre is behind the camera can still fill the frame. Testing the centre
    // alone would call it invisible, which is the bug this guards -- so the camera is put *inside*
    // the scene's own bounds and something must still be in frame.
    Fixture f;
    REQUIRE(f.engine.loadFile(helixScene()).has_value());
    scene::WorldBounds all;
    for (const auto& n : f.engine.composition()->nodes()) {
        const scene::WorldBounds b = f.engine.composition()->nodeBounds(n->name);
        if (b.valid) {
            all = b;
            break;
        }
    }
    REQUIRE(all.valid);

    REQUIRE(f.call("camera.set", json{{"position", {all.centre().x, all.centre().y, all.centre().z}},
                                      {"target", {all.max.x + 10.0, all.centre().y, all.centre().z}}})
                .success);
    f.engine.update(FrameTime{0.0, 1.0 / 60.0, 0});
    const auto probe = f.call("render.probe");
    REQUIRE(probe.success);
    // Something is either in frame or explicitly partly behind; what must not happen is a silent
    // "nothing here" for a camera standing in the middle of the scene.
    const bool anythingSeen = !probe.value["visible"].empty();
    INFO("visible " << probe.value["visible"].size() << ", offScreen "
                    << probe.value["offScreen"].get<std::size_t>());
    CHECK(anythingSeen);
}

// ---- making and unmaking objects ----------------------------------------------------------------

TEST_CASE("The assistant can put an object in the scene and take it out", "[ai][tools][node]") {
    Fixture f;
    REQUIRE(f.engine.loadFile(helixScene()).has_value());
    REQUIRE(f.engine.composition() != nullptr);
    f.ctx.setContentRoots({std::filesystem::path(AVGEN_SOURCE_DIR) / "assets"});
    const std::size_t before = f.engine.composition()->nodes().size();

    // A group: an empty transform, which is how a scene gets tidied.
    const auto group = f.call("scene.create_node", json{{"name", "block-a"}, {"kind", "group"},
                                                        {"position", {10.0, 0.0, -4.0}}});
    INFO((group.error ? group.error->message : std::string{}));
    REQUIRE(group.success);
    CHECK(f.engine.composition()->nodes().size() == before + 1);
    REQUIRE(f.engine.composition()->findNode("block-a") != nullptr);

    // A name already in use is a conflict: names address nodes everywhere else in this API, so two
    // of them would make one unreachable.
    const auto dup = f.call("scene.create_node", json{{"name", "block-a"}, {"kind", "group"}});
    CHECK_FALSE(dup.success);
    REQUIRE(dup.error.has_value());
    CHECK(dup.error->code == ai::ToolErrorCode::Conflict);

    // An asset outside the readable folders is refused here rather than becoming a node that
    // renders nothing.
    const auto outside = f.call("scene.create_node",
                                json{{"name", "smuggled"}, {"asset", "/etc/passwd"}});
    CHECK_FALSE(outside.success);
    REQUIRE(outside.error.has_value());
    CHECK(outside.error->code == ai::ToolErrorCode::NotFound);

    // Parenting, and the cycle it must refuse.
    REQUIRE(f.call("scene.create_node", json{{"name", "child"}, {"kind", "group"},
                                             {"parent", "block-a"}}).success);
    CHECK(f.engine.composition()->findNode("child")->parent == "block-a");
    const auto loop = f.call("scene.set_parent", json{{"name", "block-a"}, {"parent", "child"}});
    CHECK_FALSE(loop.success);
    REQUIRE(loop.error.has_value());
    CHECK(loop.error->code == ai::ToolErrorCode::Conflict);
    CHECK_FALSE(f.call("scene.set_parent", json{{"name", "block-a"}, {"parent", "block-a"}}).success);

    // Moving to the root works and reports what it was.
    const auto moved = f.call("scene.set_parent", json{{"name", "child"}, {"parent", ""}});
    REQUIRE(moved.success);
    CHECK(moved.value["previousParent"].get<std::string>() == "block-a");
    CHECK(f.engine.composition()->findNode("child")->parent.empty());

    // Deleting a group takes its children with it, and says how many went.
    REQUIRE(f.call("scene.set_parent", json{{"name", "child"}, {"parent", "block-a"}}).success);
    const auto removed = f.call("scene.delete_node", json{{"name", "block-a"}});
    REQUIRE(removed.success);
    CHECK(removed.value["removed"].get<std::size_t>() == 2);
    CHECK(f.engine.composition()->findNode("child") == nullptr);
    CHECK(f.engine.composition()->nodes().size() == before);
}

TEST_CASE("A created node is inside the transaction that made it", "[ai][tools][node]") {
    // The reason item 3 waited. A snapshot used to cover the parameter domain only, so rolling a
    // task back would have restored its numbers and left the object it made standing in the scene.
    // The snapshot now captures the composition too, and this is what says so.
    Fixture f;
    REQUIRE(f.engine.loadFile(helixScene()).has_value());
    const std::size_t before = f.engine.composition()->nodes().size();

    ai::SnapshotStore store;
    const std::string id = store.capture(f.engine, "before the assistant ran").id;

    REQUIRE(f.call("scene.create_node", json{{"name", "regrettable"}, {"kind", "group"}}).success);
    REQUIRE(f.engine.composition()->findNode("regrettable") != nullptr);
    // ...and a parameter change alongside it, so the test proves the two halves roll back together
    // rather than one of them happening to work.
    REQUIRE(f.call("parameter.set", json{{"path", "scene/fogDensity"}, {"value", 0.77}}).success);

    REQUIRE(store.restore(f.engine, id).has_value());
    CHECK(f.engine.composition()->findNode("regrettable") == nullptr);
    CHECK(f.engine.composition()->nodes().size() == before);
    const auto* fog = f.engine.params().find("scene/fogDensity");
    REQUIRE(fog != nullptr);
    CHECK(fog->baseComponent(0) < 0.7f); // back to whatever the scene said, not 0.77
}

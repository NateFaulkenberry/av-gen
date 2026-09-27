// ADR-902: `avgen --project <file> --audit-routes <out.json>` on a project written to disk, with one
// item for each kind of verdict the report has to tell apart, and the schema an external evaluator
// ingests held key by key.

#include "app/engine.hpp"
#include "app/route_audit_cli.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

namespace {

std::filesystem::path projectDir() {
    const auto dir = testsupport::processTempDir() / "route_audit_cli";
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& path, const json& j) {
    std::ofstream out(path);
    out << j.dump(2);
    REQUIRE(out.good());
}

json sphere(const std::string& name, json material) {
    return json{{"name", name},
                {"kind", "procedural"},
                {"procedural",
                 {{"source", {{"kind", "sphere"}, {"radius", 1.0}, {"segments", 8}, {"rings", 4}}},
                  {"distribution", {{"kind", "single"}}},
                  {"material", std::move(material)}}}};
}

std::filesystem::path writeProject() {
    const auto dir = projectDir();
    const json scene = {
        {"format", "avgen-scene"},
        {"version", 1},
        {"name", "audit"},
        {"materialPrograms",
         json::array({json{{"name", "glowing"},
                           {"ops", json::array({json{{"kind", "constant"}, {"dst", 1}, {"constant", {0.2, 1.0, 0.5, 1.0}}}})},
                           {"emission", 1}},
                      json{{"name", "dull"},
                           {"ops", json::array({json{{"kind", "constant"}, {"dst", 1}, {"constant", {0.3, 0.3, 0.3, 1.0}}}})},
                           {"baseColor", 1}}})},
        {"nodes", json::array({sphere("lamp", {{"program", "glowing"}}), sphere("rock", {{"program", "dull"}})})}};
    write(dir / "audit.scene.json", scene);

    const auto route = [](const char* source, const char* target, json chain = json::object()) {
        return json{{"source", source}, {"target", target}, {"component", -1}, {"amount", 1.0},
                    {"op", "add"},      {"polarity", "unipolar"}, {"enabled", true}, {"chain", std::move(chain)}};
    };
    const json project = {
        {"format", "avgen-project"},
        {"version", 4},
        {"assets", {{"scene", {{"kind", "composition"}, {"path", "audit.scene.json"}}}}},
        {"render", {{"fps", 30.0}}},
        {"routes",
         json::array({
             route("timeline.hits", "post/halation/intensity",                                          // 0: gated out
                   {{"attackMs", 20.0}, {"decayMs", 300.0}, {"threshold", "gate"}, {"thresholdLevel", 0.5}}),
             route("timeline.hits", "post/grade/contrast", {{"attackMs", 10.0}, {"decayMs", 300.0}}),   // 1: live
             route("lfo.wob", "nodes/rock/emissiveBoost"),                                              // 2: dead node
             route("lfo.wob", "nodes/lamp/emissiveBoost"),                                              // 3: live (program-lit)
             route("lfo.wob", "material/dull/emissionIntensity"),                                       // 4: no emission
             route("lfo.wob", "orb/nowhere"),                                                           // 5: unknown target
         })},
        // No audio, so nothing here reads audio.* or beat.* (those are silent in this project, which is
        // its own rule): an LFO for the continuous routes and a scored event timeline for the events.
        {"sources",
         json::array({json{{"kind", "lfo"}, {"name", "wob"}, {"settings", {{"shape", "sine"}}}},
                      json{{"kind", "timeline"},
                           {"name", "hits"},
                           {"settings",
                            {{"mode", "event"},
                             {"loopLength", 0.0},
                             // Soft hits: every one at 0.3, below route 0's gate at 0.5.
                             {"keys", json::array({json{{"time", 1.0}, {"value", 0.3}, {"interp", "step"}},
                                                   json{{"time", 2.0}, {"value", 0.3}, {"interp", "step"}}})}}}}})},
        {"timeline",
         {{"enabled", true},
          {"tracks",
           json::array({json{{"target", "material/glowing/emissionIntensity"},
                             {"component", -1},
                             {"timeBase", "seconds"},
                             {"mode", "replace"},
                             {"keys", json::array({json{{"time", 0.0}, {"value", {1.0}}, {"interp", "linear"}},
                                                   json{{"time", 5.0}, {"value", {3.0}}, {"interp", "linear"}}})}},
                        json{{"target", "material/dull/emissionIntensity"},
                             {"component", -1},
                             {"timeBase", "seconds"},
                             {"mode", "replace"},
                             {"keys", json::array({json{{"time", 0.0}, {"value", {1.0}}, {"interp", "linear"}}})}}})}}}};
    write(dir / "audit.json", project);
    return dir / "audit.json";
}

const json& entry(const json& list, const std::string& key, const std::string& value) {
    for (const json& e : list) {
        if (e.value(key, std::string()) == value) {
            return e;
        }
    }
    FAIL("no entry with " << key << " = " << value);
    static const json none;
    return none;
}

bool hasRule(const json& e, const std::string& rule) {
    for (const json& f : e["findings"]) {
        if (f["rule"] == rule) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("The route audit (--audit-routes) reports every route and track with a verdict and a reason", "[liveness][cli][adr902]") {
    const auto project = writeProject();
    auto report = app::auditProjectFile(project);
    REQUIRE(report.has_value());
    const json& r = *report;

    // The envelope.
    CHECK(r["format"] == "avgen-route-audit");
    CHECK(r["version"] == 1);
    CHECK(r["tier"] == "configured");
    CHECK(r["frameRate"] == 30.0); // the project's own render rate
    CHECK(r["hasAudio"] == false);
    for (const char* key : {"project", "durationSeconds", "verdicts", "rules", "summary", "routes", "tracks",
                            "effectDefaultRoutes", "effects"}) {
        CHECK(r.contains(key));
    }
    std::set<std::string> ids;
    for (const json& rule : r["rules"]) {
        ids.insert(rule["id"].get<std::string>());
        CHECK(rule.contains("appliesTo"));
        CHECK(rule.contains("verdicts"));
        CHECK(rule.contains("summary"));
    }
    CHECK(ids.contains("event-swallowed"));
    CHECK(ids.contains("node-emits-nothing"));

    // Every entry carries the common fields.
    for (const char* list : {"routes", "tracks"}) {
        for (const json& e : r[list]) {
            for (const char* key : {"index", "verdict", "reason", "findings"}) {
                INFO(list << " " << e.dump());
                CHECK(e.contains(key));
            }
        }
    }

    const json& routes = r["routes"];
    const json& swallowed = entry(routes, "target", "post/halation/intensity");
    CHECK(swallowed["verdict"] == "dead");
    CHECK(hasRule(swallowed, "event-swallowed"));
    CHECK(swallowed["sourceIsEvent"] == true);
    CHECK(swallowed["eventPassThrough"].get<double>() < 0.1);
    CHECK(swallowed["typicalEventStrength"].get<double>() == 0.3);
    CHECK(swallowed["origin"] == "project");

    const json& pulse = entry(routes, "target", "post/grade/contrast");
    CHECK(pulse["verdict"] == "live");
    CHECK(pulse["findings"].empty());
    CHECK(pulse["eventPassThrough"].get<double>() > 0.9);

    CHECK(entry(routes, "target", "nodes/rock/emissiveBoost")["verdict"] == "dead");
    CHECK(hasRule(entry(routes, "target", "nodes/rock/emissiveBoost"), "node-emits-nothing"));
    CHECK(entry(routes, "target", "nodes/lamp/emissiveBoost")["verdict"] == "live");
    CHECK(hasRule(entry(routes, "target", "material/dull/emissionIntensity"), "program-has-no-emission"));
    CHECK(hasRule(entry(routes, "target", "orb/nowhere"), "unknown-target"));

    const json& tracks = r["tracks"];
    CHECK(entry(tracks, "target", "material/glowing/emissionIntensity")["verdict"] == "live");
    const json& deadArc = entry(tracks, "target", "material/dull/emissionIntensity");
    CHECK(deadArc["verdict"] == "dead");
    CHECK_THAT(deadArc["reason"].get<std::string>(), ContainsSubstring("dull"));

    // The summary counts what the lists say.
    int dead = 0;
    for (const json& e : routes) {
        dead += e["verdict"] == "dead" ? 1 : 0;
    }
    CHECK(r["summary"]["routes"]["dead"] == dead);
    CHECK(r["summary"]["routes"]["total"] == routes.size());
    CHECK(r["summary"]["findingsByRule"]["node-emits-nothing"] == 1);
}

TEST_CASE("The route audit (--audit-routes): a project load logs and warns about its dead items", "[liveness][cli][adr902]") {
    const auto project = writeProject();
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    REQUIRE(engine.loadProject(project).has_value());
    const auto& warnings = engine.projectWarnings();
    const auto mentions = [&](const std::string& needle) {
        return std::any_of(warnings.begin(), warnings.end(),
                           [&](const std::string& w) { return w.find(needle) != std::string::npos; });
    };
    CHECK(mentions("[node-emits-nothing]"));
    CHECK(mentions("[program-has-no-emission]"));
    CHECK(mentions("[event-swallowed]"));
    // What the binds report already (an unresolved target) is not reported a second time, and a live
    // route is not reported at all.
    CHECK_FALSE(mentions("[unknown-target]"));
    CHECK_FALSE(mentions("post/grade/contrast"));
}

TEST_CASE("The route audit (--audit-routes) writes the file and exits by the documented codes", "[liveness][cli][adr902]") {
    const auto project = writeProject();
    const auto out = projectDir() / "audit.report.json";
    std::filesystem::remove(out);
    REQUIRE(app::runRouteAuditCommand(project, out, 0.0) == 0);
    std::ifstream in(out);
    const json written = json::parse(in, nullptr, false);
    REQUIRE_FALSE(written.is_discarded());
    CHECK(written["format"] == "avgen-route-audit");
    // --fps overrides the project's rate.
    REQUIRE(app::runRouteAuditCommand(project, out, 120.0) == 0);
    std::ifstream again(out);
    CHECK(json::parse(again)["frameRate"] == 120.0);
    CHECK(app::runRouteAuditCommand({}, out, 0.0) == 2);
    CHECK(app::runRouteAuditCommand(projectDir() / "no-such-project.json", out, 0.0) == 3);
}

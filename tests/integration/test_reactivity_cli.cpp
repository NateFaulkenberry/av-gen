// ADR-927: `avgen --project <file> --propose-reactivity <out>` -- the Director's default proposal as a
// document a generator reads, edits and installs, headless and with no GPU.
//
//   * the document names every item with its route (as a project writes it, stamped with the item),
//     its reason, and the liveness verdict the project would get with the proposal installed;
//   * the project file is not touched;
//   * `install` is enough: a project with its routes, sources, parameters and plan appended -- by
//     plain JSON edits, as GV3's Python generator makes them -- loads with every planned route live,
//     the Director panel reading every item as made, and a route edited before installing reading
//     as the person's.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "app/reactivity_cli.hpp"
#include "support/gltf_fixture.hpp"
#include "support/reactivity_fixture.hpp"
#include "support/temp_dir.hpp"
#include "ui/director_panel_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& path, const json& j) {
    std::ofstream out(path);
    out << j.dump(2);
    REQUIRE(out.good());
}

json readJson(const fs::path& path) {
    std::ifstream in(path);
    return json::parse(in);
}

// The glade as files: a scene, and a project naming it, its audio and its sections.
fs::path writeGladeProject(const fs::path& dir) {
    fs::create_directories(dir);
    const fs::path glb = testsupport::writeTriangleGlb("reactivity_cli");
    write(dir / "glade.scene.json", json::parse(testsupport::gladeSceneJson(glb, glb)));
    seq::Sequence piece;
    piece.sectionTimeline = testsupport::gladeSections();
    const json project = {{"format", "avgen-project"},
                          {"version", 4},
                          {"assets",
                           {{"scene", {{"kind", "composition"}, {"path", "glade.scene.json"}}},
                            {"audio", {{"path", testsupport::gladeWav().string()}}}}},
                          {"render", {{"fps", 60.0}}},
                          {"sequence", piece.toJson()},
                          {"routes", json::array()},
                          {"sources", json::array()}};
    write(dir / "glade.json", project);
    return dir / "glade.json";
}

} // namespace

TEST_CASE("The reactivity proposal of a project on disk: every item, its reason, its verdict, and what to install",
          "[directing][reactivity][cli][adr927]") {
    const fs::path dir = testsupport::processTempDir() / "reactivity_cli";
    const fs::path project = writeGladeProject(dir);
    const std::string before = readJson(project).dump();

    auto doc = app::proposeReactivityForProject(project);
    INFO((doc ? std::string() : doc.error().message));
    REQUIRE(doc);
    CHECK((*doc)["format"] == "avgen-reactivity-proposal");
    CHECK((*doc)["version"] == 1);
    CHECK((*doc)["hasAudio"] == true);
    CHECK(readJson(project).dump() == before); // the file is not touched

    const json& items = (*doc)["items"];
    REQUIRE(items.size() >= 15);
    std::size_t live = 0;
    for (const json& item : items) {
        INFO(item.dump().substr(0, 400));
        CHECK_FALSE(item.value("reason", std::string()).empty());
        CHECK(item["route"]["planItem"] == "reactivity/" + item["key"].get<std::string>());
        CHECK(item["route"].contains("chain"));
        CHECK(item["route"]["chain"].contains("delayMs"));
        live += item["verdict"] == "live" ? 1 : 0;
    }
    CHECK(live == items.size()); // every proposed route live under the liveness audit
    const json& summary = (*doc)["summary"];
    CHECK(summary["audit"]["live"] == items.size());
    CHECK(summary["audit"]["dead"] == 0);
    CHECK(summary["validation"]["errors"] == 0);
    for (const char* level : {"micro", "meso", "macro"}) {
        CHECK(summary["byLevel"].value(level, 0) > 0);
    }
    CHECK((*doc)["catalog"]["targets"].size() > items.size());
    CHECK((*doc)["install"]["routes"].size() == items.size());
    CHECK((*doc)["install"]["sources"].size() == (*doc)["sources"].size());
    CHECK((*doc)["install"]["parameters"].contains("sources/two-bar-breath/beatsPerCycle"));
    CHECK((*doc)["install"]["directingPlan"]["id"] == "reactivity");

    // Exit codes: a document written, no project, a project that will not load.
    const fs::path out = dir / "proposal.json";
    CHECK(app::runProposeReactivityCommand(project, out, 0.0) == 0);
    CHECK(readJson(out)["format"] == "avgen-reactivity-proposal");
    CHECK(app::runProposeReactivityCommand({}, out, 0.0) == 2);
    CHECK(app::runProposeReactivityCommand(dir / "missing.json", out, 0.0) == 3);
}

TEST_CASE("Installing the proposal by editing the project's JSON gives a project whose planned routes are all live",
          "[directing][reactivity][cli][adr927]") {
    const fs::path dir = testsupport::processTempDir() / "reactivity_install";
    const fs::path project = writeGladeProject(dir);
    auto doc = app::proposeReactivityForProject(project);
    REQUIRE(doc);
    const json& install = (*doc)["install"];

    // What a generator does: append the routes and sources, merge the parameters, add the plan. It
    // also edits one route before installing -- the fungi's kick turned down -- which the Director
    // must then read as the person's.
    json p = readJson(project);
    for (json route : install["routes"]) {
        if (route["planItem"] == "reactivity/kick.fungi") {
            route["amount"] = 0.2;
        }
        p["routes"].push_back(route);
    }
    for (const json& source : install["sources"]) {
        p["sources"].push_back(source);
    }
    p["parameters"] = install["parameters"];
    p["directingPlans"] = json::array({install["directingPlan"]});
    const fs::path installed = dir / "glade-installed.json";
    write(installed, p);

    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(installed);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded);
    const scene::RouteAudit audit = engine.auditRoutes();
    std::size_t planned = 0;
    for (const scene::AuditEntry& entry : audit.routes) {
        const params::ModRoute& r = engine.modulator().routes()[entry.index];
        if (r.planItem.empty()) {
            continue;
        }
        ++planned;
        INFO(r.planItem << ": " << scene::auditReason(entry));
        CHECK(entry.verdict == params::liveness::Verdict::Live);
    }
    CHECK(planned == install["routes"].size());
    CHECK(engine.params().find("sources/two-bar-breath/beatsPerCycle")->baseComponent(0) == 8.0f);
    REQUIRE(engine.directingPlans().size() == 1);
    const auto rows = ui::reactivityRows(engine.directingPlans().front(), engine.modulator().routes());
    REQUIRE(rows.size() == install["routes"].size());
    for (const ui::ReactivityRow& row : rows) {
        INFO(row.key);
        CHECK(row.state == (row.key == "kick.fungi" ? "edited by hand" : "as made"));
    }
    // And the Director, asked again, keeps the person's edit.
    const directing::SceneFacts facts = app::sceneFactsFor(engine);
    const directing::Compilation again =
        directing::compilePlan(engine.directingPlans().front(), facts); // the stored plan, re-proposed as is
    CHECK(std::any_of(again.validation.issues.begin(), again.validation.issues.end(), [](const directing::Issue& i) {
        return i.code == directing::IssueCode::HandEdited && i.item == "kick.fungi";
    }));
}

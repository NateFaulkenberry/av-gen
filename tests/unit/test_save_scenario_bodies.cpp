// A save must not photograph the run (ADR-264).
//
// A project's `parameters` are applied *over* its scene at load, and a staging scenario owns the
// visibility and the transform of every body it moves -- it hides an animal once it has abducted it
// and shows the beam only while it fires. So the value such a node holds at the instant somebody
// presses save is a picture of where that run happened to be, and saving it means the next load
// opens with an invisible goat and a beam that never switches off.
//
// This is not hypothetical and it is not rare: four consecutive saves of Glowmere re-introduced it
// while the previous fix was being written, each one re-breaking a scene the commit before had
// repaired. That is what moved the repair out of a cleanup script and into the save.

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "stage/staging.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;

namespace {

// Every `nodes/<body>/<field>` key in a saved document, for the fields a scenario drives.
std::vector<std::string> bodyOverrides(const nlohmann::json& doc) {
    std::vector<std::string> out;
    if (!doc.contains("parameters") || !doc["parameters"].is_object()) {
        return out;
    }
    for (const auto& [path, value] : doc["parameters"].items()) {
        if (path.rfind("nodes/", 0) != 0) {
            continue;
        }
        for (const char* field : {"/visible", "/position", "/rotation", "/scale"}) {
            if (path.size() > std::strlen(field) &&
                path.compare(path.size() - std::strlen(field), std::strlen(field), field) == 0) {
                out.push_back(path);
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("saving a project does not persist the bodies its scenario drives",
          "[unit][project][staging][save]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    if (!std::filesystem::exists(project)) {
        SKIP("Glowmere Valley 2 multicam is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    const scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    REQUIRE_FALSE(comp->staging().empty());

    // Which nodes the scenario owns, asked of the same function the save asks.
    const std::vector<entity::EntityDesc>& descs = comp->entities();
    std::vector<std::string> names;
    for (const entity::EntityDesc& d : descs) {
        names.push_back(d.name);
    }
    const auto nodeOf = [&](const std::string& n) {
        for (const entity::EntityDesc& d : descs) {
            if (d.name == n) { return d.node.empty() ? d.name : d.node; }
        }
        return n;
    };
    const auto tagsOf = [&](const std::string& n) {
        for (const entity::EntityDesc& d : descs) {
            if (d.name == n) { return d.tags; }
        }
        return std::vector<std::string>{};
    };
    const std::set<std::string> owned = stage::scenarioOwnedNodes(comp->staging(), nodeOf, tagsOf, names);

    // The control, and the reason this test can fail at all: the scenario must actually own
    // something. An empty set would make every assertion below vacuously true.
    INFO("scenario owns " << owned.size() << " node(s)");
    REQUIRE(owned.size() > 1);

    const std::filesystem::path out =
        std::filesystem::temp_directory_path() / "avgen_save_scenario_bodies.json";
    REQUIRE(engine.saveProject(out).has_value());
    std::ifstream in(out);
    REQUIRE(in.good());
    nlohmann::json saved;
    in >> saved;

    std::vector<std::string> leaked;
    for (const std::string& path : bodyOverrides(saved)) {
        const std::size_t first = path.find('/');
        const std::size_t second = path.find('/', first + 1);
        const std::string body = path.substr(first + 1, second - first - 1);
        if (owned.count(body) != 0) {
            leaked.push_back(path);
        }
    }
    std::string names_;
    for (const std::string& p : leaked) {
        names_ += (names_.empty() ? "" : ", ") + p;
    }
    INFO("a save kept " << leaked.size() << " override(s) of scenario-driven bodies: " << names_);
    CHECK(leaked.empty());
    std::filesystem::remove(out);
#endif
}

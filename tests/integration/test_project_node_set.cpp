// The measurement that named ADR-320, on the project it was taken on.
//
// `examples/world/glowmere-valley-2-multicam.json` is the owner's own film. Load it, delete an
// object, save, reload: **80 nodes, 79, then 80 again.** The object came back, so it was in every
// frame of every export -- and an offline render builds its own `Engine` and loads the project
// document, so nothing about the window it was deleted in ever reached a deliverable.
//
// `test_node_project_round_trip.cpp` is the regression test: a three-node fixture, fast, and it
// covers the cases a real film has no instance of. This is the field arm, and it is worth its two
// project loads for one reason: the synthetic fixture has no terrain, no ecology, no graph and no
// nested scene, and the thing that would break this fix quietly is a node the composition holds
// that the scene file does not list. If any such node existed here, an untouched save would record
// it as an addition -- so the first assertion is that an untouched save of an eighty-node world
// writes no record at all.

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "ui/world_edit.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }

std::size_t nodeCount(app::Engine& engine) {
    REQUIRE(engine.composition() != nullptr);
    return engine.composition()->nodes().size();
}

// A leaf: nothing is parented to it, so the delete is one node rather than a subtree and the
// arithmetic below is a fact about the record rather than about `withDescendants`.
std::string aLeaf(app::Engine& engine) {
    const auto& nodes = engine.composition()->nodes();
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const std::string& name = (*it)->name;
        const bool isParent = std::any_of(nodes.begin(), nodes.end(), [&](const auto& other) {
            return other->parent == name;
        });
        if (!isParent) {
            return name;
        }
    }
    return {};
}

} // namespace

TEST_CASE("an object deleted from the owner's own film stays deleted", "[nodes][project][world]") {
    const fs::path project = repoRoot() / "examples/world/glowmere-valley-2-multicam.json";
    REQUIRE(fs::is_regular_file(project));

    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    const std::size_t authored = nodeCount(engine);
    INFO("the film has " << authored << " node(s)");
    CHECK(authored > 50); // it was 80 when this was written; the claim is "a real world", not a count

    // Saved beside the project, not into a temp directory: `saveProject` writes its asset
    // references relative to the folder it is saving into, and a copy somewhere else would be
    // exercising path rebasing rather than the node record. Removed at the end either way.
    const fs::path copy = project.parent_path() /
                          (".avgen_node_set_probe_" + std::to_string(static_cast<long long>(::getpid())) + ".json");
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code ec;
            fs::remove(p, ec);
        }
    } cleanup{copy};

    // The control, and on a world this size it is the one that matters: an untouched save of a
    // project whose composition came from a file writes no record. If the composition held a node
    // the scene file does not list -- an ecology layer, a graph's output, anything generated at
    // load -- this would record it as an addition and the next load would have two of it.
    REQUIRE(engine.saveProject(copy).has_value());
    {
        std::ifstream in(copy);
        const json doc = json::parse(in, nullptr, false);
        REQUIRE_FALSE(doc.is_discarded());
        if (doc.contains("sceneNodes")) {
            UNSCOPED_INFO("recorded: " << doc["sceneNodes"].dump().substr(0, 400));
        }
        CHECK_FALSE(doc.contains("sceneNodes"));
    }

    const std::string doomed = aLeaf(engine);
    REQUIRE_FALSE(doomed.empty());
    const std::vector<std::string> names{doomed};
    const ui::EditCommand removal = ui::deleteNodes(engine, names);
    REQUIRE(removal.removed.size() == 1);
    REQUIRE(nodeCount(engine) == authored - 1);
    REQUIRE(engine.saveProject(copy).has_value());

    app::Engine fresh(app::EngineMode::Offline);
    auto reloaded = fresh.loadProject(copy);
    INFO((reloaded.has_value() ? std::string() : reloaded.error().message));
    REQUIRE(reloaded.has_value());
    INFO("deleted '" << doomed << "': " << authored << " -> " << (authored - 1) << " -> "
                     << nodeCount(fresh));
    CHECK(nodeCount(fresh) == authored - 1);
    CHECK(fresh.composition()->findNode(doomed) == nullptr);
}

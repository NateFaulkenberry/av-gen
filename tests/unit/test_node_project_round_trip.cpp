// Removing an object did not survive a save, so the object was in every render (ADR-320).
//
// The family's fourth instance, and the one the owner reported first: "my changes to hero
// selection, removal of objects from the scene, etc" were not reaching the output. A project whose
// scene came from a file saves that scene **by reference** -- `assets.scene.path` plus a hash of
// bytes already on disk -- and nothing but a "Save Scene As..." dialog writes a scene file. So an
// edit that lands in the `Composition` rather than in a parameter lives in the window the person is
// looking at and in no document any render reads. World effects were the first (ADR-207),
// atmospheric effects the second (ADR-230), heroes the third (ADR-276). This is the node set.
//
// Measured on the owner's own project before the fix, `glowmere-valley-2-multicam.json`:
// **80 nodes, 79 after the delete, 80 again after a save and a reload.**
//
// What makes it harder than its three siblings: a removal is a *negative* fact. The other three are
// lists the project can hold a copy of. There is no deleted node left to carry anything, so the
// only thing that can record the deletion is its absence, measured against the file -- which is why
// the record is a difference and not a copy. The arms below are chosen around exactly that:
//
//   A  the control       an untouched project writes no `sceneNodes` key at all
//   B  removal           delete, save, reload -> gone, and the survivor is the right one
//   C  addition          add, save, reload -> there, with its own numbers
//   D  add then delete   **no key at all** -- a record that is a copy of the live list would write
//                        eighty nodes here and still pass B and C
//   E  reparenting       deleting a parent moves its child to the grandparent in the session, and
//                        the reload has to agree; a splice that only dropped the entry would leave
//                        the child naming a parent that is gone
//   F  ADR-271's rule    the scene file is byte-for-byte untouched by any of it
//   G  the pure function on documents alone, including the direction that must fail safe: a scene
//      document this build cannot read produces **no record**, because "I could not read the file"
//      is not evidence that eighty nodes were added.

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "ui/world_edit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

json docOf(const fs::path& p) {
    std::ifstream in(p);
    json doc = json::parse(in, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    return doc;
}

// A three-node composition saved to a real scene file, so the project that follows saves the scene
// **by reference** -- which is the entire mechanism under test. An inlined composition carries its
// own node list through `Composition::toJson` and was never broken.
//
// "group" is a parent and "leaf" hangs off it, because the reparenting arm needs a hierarchy and a
// fixture that only had siblings would let a splice that merely dropped the entry pass everything.
struct Fixture {
    fs::path dir;
    fs::path glb;
    fs::path scenePath;
    fs::path projectPath;
    app::Engine engine{app::EngineMode::Offline};

    Fixture() {
        dir = fs::temp_directory_path() /
              ("avgen_node_round_trip_" + std::to_string(static_cast<long long>(::getpid())));
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto tmp = testsupport::writeTriangleGlb("node_round_trip");
        glb = dir / "tri.glb";
        fs::copy_file(tmp, glb, fs::copy_options::overwrite_existing);
        fs::remove(tmp);
        scenePath = dir / "world.scene.json";
        projectPath = dir / "world.json";

        engine.newComposition();
        add("monument", glm::vec3(4.0f, 0.0f, -7.0f), "");
        add("group", glm::vec3(0.0f, 0.0f, 0.0f), "");
        add("leaf", glm::vec3(1.0f, 2.0f, 3.0f), "group");
        REQUIRE(engine.saveComposition(scenePath).has_value());
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void add(const std::string& name, glm::vec3 position, const std::string& parent) {
        scene::CompositionNode node;
        node.name = name;
        node.kind = scene::NodeKind::Gltf;
        node.asset = glb.generic_string();
        node.transform.position = position;
        node.parent = parent;
        REQUIRE(engine.addNode(std::move(node)).has_value());
    }

    [[nodiscard]] std::size_t nodes() const {
        const auto* comp = const_cast<app::Engine&>(engine).composition();
        return comp == nullptr ? 0 : comp->nodes().size();
    }
};

// The offline render's own path: a fresh `Engine` that loads the *project document*. This is what
// makes the defect matter -- `startRenderFromUi` saves the project for you and renders that file,
// so anything the save dropped was invisible to every frame anybody exported.
struct Reloaded {
    app::Engine engine{app::EngineMode::Offline};
    explicit Reloaded(const fs::path& project) {
        auto loaded = engine.loadProject(project);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        REQUIRE(engine.composition() != nullptr);
    }
    [[nodiscard]] std::vector<std::string> names() {
        std::vector<std::string> out;
        for (const auto& n : engine.composition()->nodes()) {
            out.push_back(n->name);
        }
        return out;
    }
    [[nodiscard]] const scene::CompositionNode* find(const std::string& name) {
        return engine.composition()->findNode(name);
    }
};

bool contains(const std::vector<std::string>& v, const std::string& n) {
    return std::find(v.begin(), v.end(), n) != v.end();
}

} // namespace

TEST_CASE("a removed object stays removed through the save an offline render reloads",
          "[nodes][project]") {
    Fixture f;
    const std::string sceneBefore = bytesOf(f.scenePath);

    // ---- arm A, first, because it is the control and it has to hold before and after ----
    // A project whose session removed and added nothing writes no `sceneNodes` key at all. Without
    // this, arm B would pass on a save that wrote the whole node list into every project in the
    // repository -- which would also make the shared scene file dead for each of them.
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());
    CHECK_FALSE(docOf(f.projectPath).contains("sceneNodes"));
    {
        Reloaded back(f.projectPath);
        CHECK(back.names().size() == 3);
    }

    // ---- arm B: the report ----
    const std::vector<std::string> doomed{"monument"};
    const ui::EditCommand removal = ui::deleteNodes(f.engine, doomed);
    REQUIRE(removal.removed.size() == 1);
    REQUIRE(f.nodes() == 2);
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());

    {
        const json doc = docOf(f.projectPath);
        REQUIRE(doc.contains("sceneNodes"));
        REQUIRE(doc["sceneNodes"].contains("removed"));
        CHECK(doc["sceneNodes"]["removed"] == json::array({"monument"}));
        // A removal is not an addition wearing a different hat: nothing is added here, and a record
        // that wrote the live list would have two entries under `added` instead.
        CHECK_FALSE(doc["sceneNodes"].contains("added"));
    }

    // The measurement that names the defect: two in the session, two reloaded. It was two and
    // three -- the object came back, so it was in every frame.
    {
        Reloaded back(f.projectPath);
        const auto names = back.names();
        INFO("session " << f.nodes() << " node(s), reloaded " << names.size());
        REQUIRE(names.size() == 2);
        CHECK_FALSE(contains(names, "monument"));
        CHECK(contains(names, "group"));
        CHECK(contains(names, "leaf"));
    }

    // ---- arm F: ADR-271's rule ----
    // The scene file is not touched. That is the whole argument for putting the deletion in the
    // project: nothing but a file dialog writes a scene file, and a scene is shared between
    // projects, so an editor that wrote scenes would delete an object out of a project the user
    // did not open. Byte-for-byte, because a re-serialisation that happened to be equivalent would
    // still re-fingerprint the file.
    CHECK(bytesOf(f.scenePath) == sceneBefore);
}

TEST_CASE("an object added in the editor survives the same save", "[nodes][project]") {
    // The other half of what the owner asked about -- "removal of objects from the scene, etc".
    // Measured the same way and broken the same way: an added node lived in the composition the
    // window was drawing and in no document any render reads.
    Fixture f;
    const std::string sceneBefore = bytesOf(f.scenePath);

    scene::CompositionNode fresh;
    fresh.name = "beacon";
    fresh.kind = scene::NodeKind::Gltf;
    fresh.asset = f.glb.generic_string();
    fresh.transform.position = glm::vec3(-9.0f, 1.5f, 2.0f);
    std::vector<scene::CompositionNode> batch;
    batch.push_back(std::move(fresh));
    std::vector<std::string> created;
    const ui::EditCommand placed = ui::placeNodes(f.engine, std::move(batch), "Place", &created);
    REQUIRE(created.size() == 1);
    REQUIRE(f.nodes() == 4);
    REQUIRE(f.engine.saveProject(f.projectPath).has_value());

    {
        const json doc = docOf(f.projectPath);
        REQUIRE(doc.contains("sceneNodes"));
        REQUIRE(doc["sceneNodes"].contains("added"));
        REQUIRE(doc["sceneNodes"]["added"].size() == 1);
        CHECK(doc["sceneNodes"]["added"][0]["name"] == created.front());
        CHECK_FALSE(doc["sceneNodes"].contains("removed"));
    }

    Reloaded back(f.projectPath);
    const auto names = back.names();
    INFO("session " << f.nodes() << " node(s), reloaded " << names.size());
    REQUIRE(names.size() == 4);
    const scene::CompositionNode* node = back.find(created.front());
    REQUIRE(node != nullptr);
    // Not merely present: the same object. A round trip that kept the name and lost the numbers is
    // ADR-230's shape -- ninety-four parameter values kept and the aurora lost.
    CHECK(node->kind == scene::NodeKind::Gltf);
    CHECK_THAT(node->transform.position.x, Catch::Matchers::WithinAbs(-9.0, 1e-4));
    CHECK_THAT(node->transform.position.y, Catch::Matchers::WithinAbs(1.5, 1e-4));
    CHECK_THAT(node->transform.position.z, Catch::Matchers::WithinAbs(2.0, 1e-4));
    CHECK(bytesOf(f.scenePath) == sceneBefore);
}

TEST_CASE("an object added and then deleted leaves no record at all", "[nodes][project]") {
    // Arm D, and it is the arm that says what the record *is*. A project that held a copy of the
    // live node list would pass the removal and addition cases above and write the whole scene
    // here; this asserts the difference, which is the thing that keeps a shared scene file alive
    // for the project that references it.
    Fixture f;
    scene::CompositionNode temp;
    temp.name = "scratch";
    temp.kind = scene::NodeKind::Gltf;
    temp.asset = f.glb.generic_string();
    std::vector<scene::CompositionNode> batch;
    batch.push_back(std::move(temp));
    std::vector<std::string> created;
    static_cast<void>(ui::placeNodes(f.engine, std::move(batch), "Place", &created));
    REQUIRE(created.size() == 1);
    REQUIRE(f.nodes() == 4);
    static_cast<void>(ui::deleteNodes(f.engine, created));
    REQUIRE(f.nodes() == 3);

    REQUIRE(f.engine.saveProject(f.projectPath).has_value());
    CHECK_FALSE(docOf(f.projectPath).contains("sceneNodes"));
    Reloaded back(f.projectPath);
    CHECK(back.names().size() == 3);
    CHECK_FALSE(contains(back.names(), created.front()));
}

TEST_CASE("deleting a parent reloads its child where the session put it", "[nodes][project]") {
    // Arm E. `Composition::detachNode` reparents a child to the grandparent so it keeps its local
    // transform; a reload that merely dropped the entry would leave the child naming a parent that
    // is gone. The editor deletes whole subtrees, so this is reached through `removeNode` -- which
    // is what the object list's own remove button calls.
    Fixture f;
    f.engine.removeNode("group");
    REQUIRE(f.nodes() == 2);
    const scene::CompositionNode* live = f.engine.composition()->findNode("leaf");
    REQUIRE(live != nullptr);
    const std::string parentInSession = live->parent;
    CHECK(parentInSession.empty()); // "group" had no parent, so the grandparent is the root

    REQUIRE(f.engine.saveProject(f.projectPath).has_value());
    Reloaded back(f.projectPath);
    REQUIRE(back.names().size() == 2);
    const scene::CompositionNode* reloaded = back.find("leaf");
    REQUIRE(reloaded != nullptr);
    CHECK(reloaded->parent == parentInSession);
    // And it is still where it was, which is the thing the reparenting is for.
    CHECK_THAT(reloaded->transform.position.y, Catch::Matchers::WithinAbs(2.0, 1e-4));
}

TEST_CASE("the node-edit record is a difference between two documents", "[nodes][project][json]") {
    // Arm G: the decision on its own, with no engine anywhere near it, so the cases that are
    // awkward to stage through a UI can be asserted directly (ADR-278's argument for making
    // `unknownKeys` a pure function, applied again).
    const json sceneDoc{{"nodes", json::array({json{{"name", "a"}}, json{{"name", "b"}}})}};

    SECTION("no difference is no record") {
        CHECK(scene::nodeEditsAgainst(sceneDoc["nodes"], sceneDoc).is_null());
    }
    SECTION("a removal is named") {
        const json live = json::array({json{{"name", "a"}}});
        const json edits = scene::nodeEditsAgainst(live, sceneDoc);
        REQUIRE(edits.is_object());
        CHECK(edits["removed"] == json::array({"b"}));
        CHECK_FALSE(edits.contains("added"));
    }
    SECTION("a node the scene never had is carried whole") {
        json live = sceneDoc["nodes"];
        live.push_back(json{{"name", "c"}, {"kind", "orb"}});
        const json edits = scene::nodeEditsAgainst(live, sceneDoc);
        REQUIRE(edits.is_object());
        CHECK(edits["added"] == json::array({json{{"name", "c"}, {"kind", "orb"}}}));
    }
    SECTION("a node that moved is not an edit") {
        // The difference is taken **by name**, deliberately: where a node is already lives in the
        // project's `parameters` block (ADR-271), and a second copy of it here would be a second
        // answer to one question.
        const json live = json::array({json{{"name", "a"}, {"position", json::array({5, 5, 5})}},
                                       json{{"name", "b"}}});
        CHECK(scene::nodeEditsAgainst(live, sceneDoc).is_null());
    }
    SECTION("a scene document this build cannot read produces no record") {
        // The safe direction, and the only one worth choosing between. Recording the difference
        // against nothing would write every node in the world into the project as an addition --
        // an outcome worse than the defect, and one that would look exactly like a working fix
        // from the addition arm.
        CHECK(scene::nodeEditsAgainst(sceneDoc["nodes"], json()).is_null());
        CHECK(scene::nodeEditsAgainst(sceneDoc["nodes"], json("not a document")).is_null());
    }

    SECTION("applying it reproduces detachNode's reparenting") {
        json doc{{"nodes", json::array({json{{"name", "g"}, {"parent", "root"}},
                                       json{{"name", "child"}, {"parent", "g"}},
                                       json{{"name", "root"}}})}};
        scene::applyNodeEdits(doc, json{{"removed", json::array({"g"})}});
        REQUIRE(doc["nodes"].size() == 2);
        CHECK(doc["nodes"][0]["name"] == "child");
        CHECK(doc["nodes"][0]["parent"] == "root");
    }
    SECTION("a removal at the top drops the child's parent key rather than leaving it dangling") {
        json doc{{"nodes", json::array({json{{"name", "g"}}, json{{"name", "child"}, {"parent", "g"}}})}};
        scene::applyNodeEdits(doc, json{{"removed", json::array({"g"})}});
        REQUIRE(doc["nodes"].size() == 1);
        CHECK_FALSE(doc["nodes"][0].contains("parent"));
    }
    SECTION("an empty record changes nothing") {
        json doc = sceneDoc;
        scene::applyNodeEdits(doc, json());
        scene::applyNodeEdits(doc, json::object());
        CHECK(doc == sceneDoc);
    }
    SECTION("additions land after removals, so a reused name is the session's") {
        json doc = sceneDoc;
        scene::applyNodeEdits(doc, json{{"removed", json::array({"a"})},
                                        {"added", json::array({json{{"name", "a"}, {"kind", "orb"}}})}});
        REQUIRE(doc["nodes"].size() == 2);
        CHECK(doc["nodes"][1]["name"] == "a");
        CHECK(doc["nodes"][1]["kind"] == "orb");
    }
}

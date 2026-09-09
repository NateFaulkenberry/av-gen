// The procedural graph through a composition (ADR-028): a graph in a scene file emits nodes and
// parameters, re-evaluation replaces what it installed, and hand-made nodes survive.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "graph/graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
fs::path write(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << body;
    return p;
}
// primitive -> radial distribution -> output/procedural named "colonnade"
const char* kGraph = R"({
  "format": "avgen-graph", "version": 1, "name": "colonnade",
  "nodes": [
    { "name": "prim", "type": "generators/primitive", "params": { "kind": "cylinder", "radius": 0.5, "height": 6 } },
    { "name": "ring", "type": "distributions/radial", "params": { "count": 24, "radius": 12 } },
    { "name": "out", "type": "output/procedural", "params": { "name": "colonnade" } }
  ],
  "links": [ { "from": "prim.mesh", "to": "out.source" }, { "from": "ring.points", "to": "out.points" },
             { "from": "ring.spec", "to": "out.spec" } ]
})";
} // namespace

TEST_CASE("A scene file's graph emits nodes with ordinary parameters", "[integration][graph]") {
    const auto dir = fs::temp_directory_path() / "avgen_graph_engine";
    fs::remove_all(dir);
    write(dir / "world.graph.json", kGraph);
    const auto scene = write(dir / "world.json", R"({"format":"avgen-scene","version":1,"name":"g",
        "graph": "world.graph.json",
        "nodes": [ { "name": "handmade", "kind": "orb", "position": [0, 3, 0] } ] })");
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadComposition(scene);
    const std::string loadError = loaded.has_value() ? std::string("ok") : loaded.error().message;
    INFO(loadError);
    REQUIRE(loaded.has_value());
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    auto* comp = engine.composition();
    REQUIRE(comp != nullptr);
    REQUIRE(comp->graph() != nullptr);
    CHECK(comp->graph()->nodes.size() == 3);
    // The emitted object became a node with the usual parameter prefix.
    REQUIRE(comp->findNode("colonnade") != nullptr);
    REQUIRE(comp->findNode("handmade") != nullptr);
    REQUIRE(engine.params().find("procedural/colonnade/distribution/radius") != nullptr);
    REQUIRE(engine.scene().procedurals.size() == 1);
    CHECK(engine.scene().procedurals[0].instances.size() == 24);

    // Re-evaluating with a changed parameter replaces the installed node, not the hand-made one.
    graph::Graph g = *comp->graph();
    graph::Node* ring = g.findNode("ring");
    REQUIRE(ring != nullptr);
    ring->params["count"] = 40;
    REQUIRE(comp->setGraph(std::move(g)).has_value());
    engine.update(engine.tick(clock));
    CHECK(engine.scene().procedurals.size() == 1);
    CHECK(engine.scene().procedurals[0].instances.size() == 40);
    CHECK(comp->findNode("handmade") != nullptr);
    CHECK(comp->nodeCount() == 2);

    // Saving writes the graph, not its emitted nodes.
    const auto out = dir / "saved.json";
    REQUIRE(comp->saveFile(out).has_value());
    std::ifstream in(out);
    const nlohmann::json saved = nlohmann::json::parse(in);
    REQUIRE(saved.contains("graph"));
    REQUIRE(saved.at("nodes").size() == 1);
    CHECK(saved.at("nodes")[0].at("name") == "handmade");

    // Clearing the graph removes what it installed.
    comp->clearGraph();
    engine.update(engine.tick(clock));
    CHECK(comp->findNode("colonnade") == nullptr);
    CHECK(comp->findNode("handmade") != nullptr);
    CHECK(engine.scene().procedurals.empty());
    fs::remove_all(dir);
}
